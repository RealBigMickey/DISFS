import asyncio
from time import time
import aioconsole
import os
from discord import File
from quart import Quart, request, abort, jsonify
from _config import TOKEN, NOTIFICATIONS_ID, DATABASE_URL
from discord_api import get_client
import asyncpg
import tempfile


import sys
sys.path.append(os.path.abspath(os.path.join(os.path.dirname(__file__), '..')))
from dummySQL import seed_dummy_data


app = Quart(__name__)
discord_client = get_client(NOTIFICATIONS_ID)
POOL: asyncpg.Pool  # Similiar to declaring the type of variable POOL

# FIFO queue for uploads
upload_queue: asyncio.Queue = asyncio.Queue()


CURRENT_USER_ID = None
CURRENT_USERNAME = ""


async def startup():
    global POOL

    POOL = await asyncpg.create_pool(DATABASE_URL)

    schema = open("schema.sql").read()
    async with POOL.acquire() as conn:
        await conn.execute(schema)
    
        await conn.execute(
            """
            INSERT INTO users (username)
            VALUES ($1)
            ON CONFLICT (username) DO NOTHING;
            """,
            "William"
        )

    await seed_dummy_data(POOL)
    asyncio.create_task(discord_client.start(TOKEN))
    asyncio.create_task(admin_console())
        

def require_login():
    if CURRENT_USER_ID is None:
        abort(401, "Not logged in")


# Usage: GET /login?user=John, Returns: { "user_id": 42, "username": "John" }
@app.route("/login", methods=["GET"])
async def login():
    user = request.args.get("user", "").strip()
    if not user:
        return "", 404

    async with POOL.acquire() as conn:
        row = await conn.fetchrow(
            "SELECT id FROM users WHERE username=$1", user
        )
    if row is None:
        return "", 404
    
    return f"{row['id']}:{user}\n", 200, {"Content-Type": "text/plain"}



async def validate_user():
    """
    Extract & verify user_id from query.
    Returns int user_id or aborts.
    """
    uid_str = request.args.get("user_id")
    if not uid_str:
        abort(400, "Missing user_id")
    try:
        uid = int(uid_str)
    except:
        abort(400, "Invalid user_id")
    async with POOL.acquire() as conn:
        row = await conn.fetchrow("SELECT 1 FROM users WHERE id=$1", uid)
    if not row:
        abort(404, "User not found")
    return uid



@app.route("/upload", methods=["GET"])
async def upload():
    """
    POST /upload?user_id=22&path=foo/bar.txt&chunk=0
    form-file field 'file'
    """
    user_id = await validate_user()
    path = request.args.get("path", "").lstrip("/")
    chunk = int(request.args.get("chunk", 0))
    f = (await request.files)["file"]
    
    tmp = tempfile.NamedTemporaryFile(delete=False)
    await f.save(tmp.name)

    asyncio.create_task(
        dispatch_upload(user_id, path, chunk, tmp.name)
    )
    return "", 202


async def dispatch_upload(user_id, path, chunk, tmp_name):
    await discord_client.wait_until_ready()

    ch = discord_client.get_channel(NOTIFICATIONS_ID)
    msg = await ch.send(file=File(tmp_name, filename=f"{path}.chunk{chunk}"))

    async with POOL.acquire() as conn:
        await conn.execute(
            """
            INSERT INTO uploads(user_id, path, chunk, message_id)
            VALUES($1,$2,$3,$4)
            ON CONFLICT(user_id,path,chunk)
              DO UPDATE SET message_id = EXCLUDED.message_id;
            """,
            user_id, path, chunk, msg.id
        )
    os.unlink(tmp_name)



@app.route("/stat", methods=["GET"])
async def stat():
    user_id = await validate_user()
    raw_path = request.args.get("path", "").lstrip("/")
    if raw_path == "":
        now = int(time.time())
        return jsonify({"type": 2, "atime": now, "mtime": now,
                        "ctime": now, "crtime": now})
    
    parts = raw_path.split("/")
    async with POOL.acquire() as conn:
        parent_id = await conn.fetchval("""
            SELECT id FROM nodes
            WHERE user_id=$1 AND parent_id IS NULL
        """, user_id)

        node_id = None
        for item in parts:
            node_id = await conn.fetchval("""
                SELECT id FROM nodes
                WHERE user_id=$1 AND parent_id=$2 AND name=$3
            """, user_id, parent_id, item)
            if node_id is None:
                return "", 404
            else:
                parent_id = node_id
        
        node_row = await conn.fetchrow("""
            SELECT type, i_atime, i_mtime, i_ctime, i_crtime
            FROM nodes WHERE id=$1
        """, node_id)

        result = {
            "type": node_row["type"],
            "atime": node_row["i_atime"],
            "mtime": node_row["i_mtime"],
            "ctime": node_row["i_ctime"],
            "crtime": node_row["i_crtime"]
        }

        if node_row["type"] == 1:          # file → get size
            size = await conn.fetchval("""
                SELECT COALESCE(SUM(octet_length(message_id::text)),0)
                FROM file_chunks WHERE node_id=$1
            """, node_id)
            result["size"] = size

    return jsonify(result)

        



@app.route("/listdir", methods=["GET"])
async def listdir():
    """
    GET /listdir?user_id=22&path=foo/
    """
    user_id = await validate_user()
    dir_path = request.args.get("path", "/")
    if not dir_path.endswith("/"):
        dir_path += "/"
    dir_path = dir_path.lstrip("/")

    async with POOL.acquire() as conn:
        # 1) Resolve the parent directory node_id
        # Start at the user’s root node (parent_id IS NULL)
        parent_id = await conn.fetchval(
            "SELECT id FROM nodes WHERE user_id=$1 AND parent_id IS NULL",
            user_id
        )
        if parent_id is None:
            abort(404)

        if dir_path:
            for comp in dir_path.split("/"):
                if not comp:
                    continue
                nid = await conn.fetchval(
                    "SELECT id FROM nodes WHERE user_id=$1 AND parent_id=$2 AND name=$3",
                    user_id, parent_id, comp
                )
                if nid is None:
                    abort(404)
                parent_id = nid

        rows = await conn.fetch(
            """
            SELECT name, type, i_mtime
              FROM nodes
             WHERE user_id=$1
               AND parent_id=$2
             ORDER BY type DESC, name
            """,
            user_id, parent_id
        )

    result = []
    for r in rows:
        entry = {"name": r["name"], "type": r["type"], "mtime": r["i_mtime"]}
        result.append(entry)

    return jsonify(result)




@app.route("/download", methods=["GET"])
async def download():
    print("Download not implemented yet!");
    return "TBD\n", 200




@app.route("/ping", methods=["GET"])
async def ping():
    print("Recieved ping, returning pong!");
    return "pong\n", 200

@app.route("/dog_gif", methods=["POST"])
async def dog_gif_http():
    await discord_client.send_dog_gif()
    return "", 200



async def admin_console():
    await discord_client.wait_until_ready() 
    while True:
        cmd = await aioconsole.ainput("")
        cmd = cmd.strip().lower()
        match cmd:
            case "exit":
                print("Shutting down...")
                await app.shutdown()
            case "q":
                print("Shutting down...")
                await app.shutdown()
            case "status":
                print("Running... (Not implemented yet!)")  # TBD
            case "dog":
                await discord_client.send_dog_gif()
                print("Dog gif sent owo")
            case _:  # default case
                print(f"Unknown command: \"{cmd}\"")



# called after all code is complete
@app.after_serving
async def shutdown():
    await discord_client.close()
    print("Clean shutdown complete.")
    os._exit(0)

async def main():
    await startup()
    await app.run_task(host="0.0.0.0", port=5050),



if __name__ == "__main__":
    asyncio.run(main())
