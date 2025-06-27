import asyncio
import time
import aioconsole
import os
from discord import File
from quart import Quart, request, abort, jsonify, Response
from server._config import TOKEN, NOTIFICATIONS_ID, DATABASE_URL, VAULT_IDS
from server.discord_api import get_client
import asyncpg
import tempfile

from server.app_utils import validate_user, dispatch_upload, admin_console, create_closure


import sys
sys.path.append(os.path.abspath(os.path.join(os.path.dirname(__file__), '..')))
from dummySQL import seed_dummy_data, seed_foo_txt


# Note: abort() returned error messages are ignored for now

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
    await seed_foo_txt(POOL)
    asyncio.create_task(discord_client.start(TOKEN))
    asyncio.create_task(admin_console(discord_client, app))
    discord_client.channel_id = VAULT_IDS[0]
        

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
    
    return f"{row['id']}:{user}\n", 201, {"Content-Type": "text/plain"}




@app.route("/upload", methods=["POST"])
async def upload():
    """
    POST /upload?user_id=22&path=foo/bar.txt&chunk=0
    form-file field 'file'
    """
    user_id = await validate_user(POOL)

    file_path = request.args.get("path", "").lstrip("/")
    if not file_path:
        abort(400, "Missing path")

    try:
        chunk = int(request.args.get("chunk", ""))
    except ValueError:
        abort(400, "Invalid chunk index")
    
    data = await request.get_data()
    if not data:
        abort(400, "No data")

    tmp = tempfile.NamedTemporaryFile(delete=False)
    tmp.write(data)
    tmp.flush()
    tmp.close()

    chunk_size = os.path.getsize(tmp.name)

    try:
        await dispatch_upload(
            POOL, discord_client,
            user_id, file_path,
            chunk, chunk_size,
            tmp.name
        )
    except Exception:
        app.logger.exception("dispatch_upload failed")
        abort(500, "Upload failed")

    return "", 201



@app.route("/stat", methods=["GET"])
async def stat():
    user_id = await validate_user(POOL)
    raw_path = request.args.get("path", "").lstrip("/")
    if raw_path == "":
        now = int(time.time())
        return jsonify({"type": 2, "atime": now, "mtime": now,
                        "ctime": now, "crtime": now})
    
    parts = raw_path.split("/")
    async with POOL.acquire() as conn:
        parent_id = await conn.fetchval(
            """
            SELECT id FROM nodes
            WHERE user_id=$1 AND parent_id IS NULL
            """, user_id)

        node_id = None
        for item in parts:
            node_id = await conn.fetchval(
                """
                SELECT id FROM nodes
                WHERE user_id=$1 AND parent_id=$2 AND name=$3
                """, user_id, parent_id, item)
            if node_id is None:
                return "", 520  # 520 means not found
            else:
                parent_id = node_id

        node_row = await conn.fetchrow(
            """
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

        if node_row["type"] == 1:
            size = await conn.fetchval(
            """
            SELECT COALESCE(SUM(chunk_size), 0)
            FROM file_chunks WHERE node_id=$1
            """, node_id)
            result["size"] = size

    return jsonify(result)





@app.route("/listdir", methods=["GET"])
async def listdir():
    """
    GET /listdir?user_id=22&path=foo/
    """
    user_id = await validate_user(POOL)
    dir_path = request.args.get("path", "/")
    if not dir_path.endswith("/"):
        dir_path += "/"
    dir_path = dir_path.lstrip("/")

    async with POOL.acquire() as conn:
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


@app.route("/mkdir", methods=["POST"])
async def mkdir():
    user_id = await validate_user(POOL)
    dir_path = request.args.get("path", "").lstrip("/")

    if dir_path == "":
        return "", 404
    
    parts = dir_path.split("/")
    async with POOL.acquire() as conn, conn.transaction():
        parent_id = await conn.fetchval(
            "SELECT id FROM nodes WHERE user_id=$1 AND parent_id IS NULL",
            user_id
        )
        
        if parent_id is None:
            return 500
        
        now = int(time.time())
        for i, comp in enumerate(parts):
            node_id = await conn.fetchval(
            "SELECT id FROM nodes WHERE user_id=$1 AND parent_id=$2 AND name=$3",
            user_id,
            parent_id,
            comp
            )

            if node_id is None:
                if i != len(parts) - 1:
                    # Create missing intermediate directory
                    node_id = await conn.fetchval(
                        """
                        INSERT INTO nodes(user_id, name, parent_id, type, i_atime,
                                            i_mtime, i_ctime, i_crtime)
                        VALUES($1,$2,$3, 2 ,$4,$4,$4,$4)
                        RETURNING id
                        """,
                        user_id,
                        comp,
                        parent_id,
                        now
                    )
                    await create_closure(conn, node_id, parent_id)

                else:
                    # Create ACTUAL directory
                    node_id = await conn.fetchval(
                        """
                        INSERT INTO nodes(user_id, name, parent_id, type,
                                        i_atime, i_mtime, i_ctime, i_crtime)
                        VALUES($1,$2,$3, 2 ,$4,$4,$4,$4)
                        RETURNING id
                        """,
                        user_id,
                        comp,
                        parent_id,
                        now
                    )
                    await create_closure(conn, node_id, parent_id)
            parent_id = node_id
        
    return "", 201

@app.route("/download", methods=["GET"])
async def download():
    user_id = await validate_user(POOL)
    raw_path = request.args.get("path", "").lstrip("/")
    if not raw_path:
        abort(400)

    parts = raw_path.split("/")
    async with POOL.acquire() as conn:
        parent_id = await conn.fetchval(
            "SELECT id FROM nodes WHERE user_id=$1 AND parent_id IS NULL",
            user_id
        )
        node_id = None
        for comp in parts:
            node_id = await conn.fetchval(
                "SELECT id FROM nodes WHERE user_id=$1 AND parent_id=$2 AND name=$3",
                user_id, parent_id, comp
            )
            if node_id is None:
                abort(404)
            parent_id = node_id
        
        typ = await conn.fetchval("SELECT type FROM nodes WHERE id=$1", node_id)
        if typ != 1:
            abort(400)

        # Get chunks in order
        rows = await conn.fetch(
            """
            SELECT chunk_index, message_id
            FROM file_chunks
            WHERE node_id=$1
            ORDER BY chunk_index
            """,
            node_id
        )
        if not rows:
            abort(500, "no chunks")

    async def streamer():
        for r in rows:
            chunk = await discord_client.download_attachment(r["message_id"])
            yield chunk

    # stream the file over in waves of chunks
    return Response(streamer(), status=201, mimetype="application/octet-stream")



@app.route("/create", methods=["POST"])
async def create_file():
    user_id = await validate_user(POOL)
    raw_path = request.args.get("path", "").lstrip("/");

    if not raw_path:
        abort(400)
    
    parts = raw_path.split("/")
    file_name = parts.pop()
    now = int(time.time())

    async with POOL.acquire() as conn, conn.transaction():
        parent = await conn.fetchval(
            "SELECT id FROM nodes WHERE user_id=$1 AND parent_id IS NULL",
            user_id
        )

        for comp in parts:
            nid = await conn.fetchval(
                """
                SELECT id FROM nodes
                WHERE user_id=$1 AND parent_id=$2 AND name=$3
                """,
                user_id, parent, comp
            )
            if nid is None:
                nid = await conn.fetchval(
                    """
                    INSERT INTO nodes(user_id, name, parent_id, type,
                    i_atime, i_mtime, i_ctime, i_crtime)
                    VALUES($1,$2,$3, 2 ,$4,$4,$4,$4)
                    RETURNING id
                    """,
                    user_id, comp, parent, now
                )
                await create_closure(conn, nid, parent)
            parent = nid



        # just in case the file DOES exist
        check_exists = await conn.fetchval(
            """
            SELECT 1 FROM nodes
            WHERE user_id=$1 AND parent_id=$2 AND name=$3
            """,
            user_id, parent, file_name
        )
        if check_exists:
            abort(420)
        

        nid = await conn.fetchval(
            """
            INSERT INTO nodes(user_id, name, parent_id, type,
            i_atime, i_mtime, i_ctime, i_crtime)
            VALUES($1,$2,$3, 1 ,$4,$4,$4,$4)
            RETURNING id
            """,
            user_id, file_name, parent, now)
        await create_closure(conn, nid, parent)

    return "", 201


@app.route("/truncate", methods=["POST"])
async def truncate_file():
    user_id = await validate_user(POOL)
    raw_path = request.args.get("path", "").lstrip("/")
    size = request.args.get("size")
    if not raw_path or size is None:
        abort(400, "Missing path or size")
    size = int(size)

    async with POOL.acquire() as conn, conn.transaction():
        parent = await conn.fetchval(
            "SELECT id FROM nodes WHERE user_id=$1 AND parent_id IS NULL",
            user_id)
        
        parts = raw_path.split("/");
        for comp in parts:
            parent = await conn.fetchval(""
            "SELECT id FROM nodes WHERE user_id=$1 AND parent_id=$2 AND name=$3",
            user_id, parent, comp)
            if parent is None:
                abort(404)
        node_id = parent

        rows = await conn.fetch(
                "SELECT message_id FROM file_chunks WHERE node_id=$1",
                node_id)
        channel = discord_client.get_channel(discord_client.channel_id)

        for r in rows:
            try:
                msg = await channel.fetch_message(r["message_id"])
                await msg.delete()
            except Exception:
                app.logger.exception(f"failed to delete chunk msg: {msg.id}")
        
        await conn.execute("DELETE FROM file_chunks WHERE node_id=$1", node_id)

        await conn.execute("UPDATE nodes SET i_mtime=$1 WHERE id=$2",
                            int(time.time()), node_id)
        
    return "", 201


@app.route("/ping", methods=["GET"])
async def ping():
    print("Recieved ping, returning pong!");
    return "pong\n", 201

@app.route("/dog_gif", methods=["POST"])
async def dog_gif_http():
    await discord_client.send_dog_gif()
    return "", 201



# called after all code is complete
@app.after_serving
async def shutdown():
    await discord_client.close()
    print("Clean shutdown complete.")
    os._exit(0)
