import asyncio
import time
import os
from quart import Quart, request, jsonify, Response
from server._config import DATABASE_URL, TOKEN, NOTIFICATIONS_ID, DATABASE_URL, VAULT_IDS, FILE_CHUNK_TIMEOUT
from server.discord_api import get_client, delete_messages
import asyncpg
import tempfile
from asyncpg.exceptions import UniqueViolationError

from server.app_utils import validate_user, dispatch_upload, admin_console, create_closure, resolve_node, split_parent_and_name, node_info, is_descendant, get_parent_id, rewire_closure_for_move


import sys
sys.path.append(os.path.abspath(os.path.join(os.path.dirname(__file__), '..')))
from dummySQL import seed_foo_txt


# Note: http return error messages are ignored for now

app = Quart(__name__)
discord_client = get_client(NOTIFICATIONS_ID)
POOL: asyncpg.Pool  # Similiar to declaring the type of variable POOL

# FIFO queue for uploads
upload_queue: asyncio.Queue = asyncio.Queue()


CURRENT_USER_ID = None
CURRENT_USERNAME = ""


async def startup():
    global POOL

    # Create database if it doesn't exist
    try:
        base_url = DATABASE_URL.rsplit('/', 1)[0]
        admin_url = f"{base_url}/postgres"
        
        admin_pool = await asyncpg.create_pool(admin_url)
        async with admin_pool.acquire() as conn:
            # Check if database exists
            exists = await conn.fetchval(
                "SELECT 1 FROM pg_database WHERE datname = 'disfs_db'"
            )
            if not exists:
                await conn.execute("CREATE DATABASE disfs_db OWNER admin")
                app.logger.info("Created database disfs_db")
        await admin_pool.close()
    except Exception as e:
        app.logger.error(f"Failed to create database: {e}")

    # Actually connect to disfs_db
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

    await seed_foo_txt(POOL)  # For testing
    asyncio.create_task(discord_client.start(TOKEN))
    asyncio.create_task(admin_console(discord_client, app))
    discord_client.channel_id = VAULT_IDS[0]
        

def require_login():
    if CURRENT_USER_ID is None:
        return "Not logged in", 401

@app.route("/register", methods=["GET"])
async def register():
    username = request.args.get("user", "").strip()
    if not username:
        return "", 404

    async with POOL.acquire() as conn:
        await conn.execute(
            """
            INSERT INTO users (username)
            VALUES ($1)
            ON CONFLICT (username) DO NOTHING;
            """,
            username
        )

        row = await conn.fetchrow(
            "SELECT id FROM users WHERE username=$1", username
        )

        if row is None:
            return "", 404
        return f"{row['id']}:{username}\n", 201, {"Content-Type": "text/plain"}


# Usage: GET /login?user=John, Returns: { "user_id": 42, "username": "John" }
@app.route("/login", methods=["GET"])
async def login():
    username = request.args.get("user", "").strip()
    if not username:
        return "", 404

    async with POOL.acquire() as conn:
        row = await conn.fetchrow(
            "SELECT id FROM users WHERE username=$1", username
        )
    if row is None:
        return "", 404
    
    return f"{row['id']}:{username}\n", 201, {"Content-Type": "text/plain"}


# Track upload completion: {node_id: (end_chunk_index, event)}
# Keeps an asyncio.Event to avoid busy waiting
upload_tracking: dict[int, tuple[int, asyncio.Event]] = {}

@app.route("/prep_upload", methods=["POST"])
async def prep_upload():
    """
    Called by do_release to prepare for upload.
    POST /prep_upload?user_id=22&path=foo/bar.txt&size=1048576&end_chunk=2&mtime=123
    """
    user_id = await validate_user(POOL)
    raw_path = request.args.get("path", "").lstrip("/")
    size = request.args.get("size", "0")
    end_chunk = request.args.get("end_chunk", "0")
    true_mtime = request.args.get("mtime", "0");
    
    if not raw_path:
        return "Missing path", 400
    
    try:
        size = int(size)
        end_chunk = int(end_chunk)
        true_mtime = int(true_mtime)
    except ValueError:
        return "Invalid size or end_chunk", 400

    async with POOL.acquire() as conn, conn.transaction():
        node_id = await resolve_node(conn, user_id, raw_path, expected_type=1)
        if not node_id:
            return "File not found", 520


       # Overwrite on-going uploads of same file and delete stale chunks
       # Current reader's will timeout
        if node_id in upload_tracking:
            old_end_chunk, old_event = upload_tracking[node_id]
            
            print(f"Interrupting ongoing upload for node_id={node_id}"
                  f"(old end_chunk={old_end_chunk}, new end_chunk={end_chunk})")
            
            old_chunks = await conn.fetch("SELECT message_id FROM file_chunks WHERE node_id=$1",node_id)
            
            await conn.execute("DELETE FROM file_chunks WHERE node_id=$1", node_id)
            message_ids = [r["message_id"] for r in old_chunks if r["message_id"] is not None]
            channel = discord_client.get_channel(discord_client.channel_id)
            delete_messages(channel, message_ids)
            
        
        # Set size and mark as not ready
        await conn.execute(
            """
            UPDATE nodes 
            SET size = $1, 
                ready = FALSE,
                i_mtime = $2
            WHERE id = $3
            """,
            size, true_mtime, node_id
        )
        

        if node_id not in upload_tracking:
            upload_tracking[node_id] = (end_chunk, asyncio.Event())
        else:
            old_event = upload_tracking[node_id][1]
            old_event.clear()
            upload_tracking[node_id] = (end_chunk, old_event)
    
    return "", 201


@app.route("/upload", methods=["POST"])
async def upload():
    """
    POST /upload?user_id=22&path=foo/bar.txt&chunk=0
    form-file field 'file'
    """
    user_id = await validate_user(POOL)

    file_path = request.args.get("path", "").lstrip("/")
    if not file_path:
        return "Missing path", 400

    try:
        chunk = int(request.args.get("chunk"))
    except ValueError:
        return "Invalid chunk index", 400
    
    data = await request.get_data()
    if not data:
        return "No data", 400

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

         # Check if this is the end chunk
        async with POOL.acquire() as conn:
            node_id = await resolve_node(conn, user_id, file_path, expected_type=1)
            if not node_id:
                return "File not found", 520
            

            if node_id in upload_tracking:
                end_chunk, event = upload_tracking[node_id]
                if chunk == end_chunk:
                    await conn.execute("UPDATE nodes SET ready = TRUE WHERE id = $1", node_id)
                    event.set()  # set the asyncio.Event

    except Exception:
        app.logger.exception("dispatch_upload failed")
        return "Upload failed", 500

    return "", 201



@app.route("/stat", methods=["GET"])
async def stat():
    user_id = await validate_user(POOL)
    raw_path = request.args.get("path", "").lstrip("/")
    if raw_path == "":
        now = int(time.time())
        return jsonify({"type": 2, "atime": now, "mtime": now,
                        "ctime": now, "crtime": now})
    
    async with POOL.acquire() as conn:
        node_id = await resolve_node(conn, user_id, raw_path, expected_type=None)

        if node_id is None:
            return "", 520

        node_row = await conn.fetchrow(
            """
            SELECT type, i_atime, i_mtime, i_ctime, i_crtime, 
                   size, ready
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
            result["size"] = node_row["size"]
            result["ready"] = node_row["ready"]

    return jsonify(result), 201


# Returns the modify time of a file
@app.route("/mtime", methods=["GET"])
async def mtime():
    user_id = await validate_user(POOL)
    raw_path = request.args.get("path", "").lstrip("/")
    if raw_path == "":
        return "", 520
    
    parts = raw_path.split("/")
    async with POOL.acquire() as conn:
        node_id = await resolve_node(conn, user_id, raw_path, expected_type=None)

        if node_id is None:
            return "", 520

        f_type, f_mtime = await conn.fetchrow(
            """
            SELECT type, i_mtime
            FROM nodes WHERE id=$1
            """, node_id)

        if f_type != 1:
            return "", 404
    
    return str(f_mtime), 201





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
            return "", 520

        if dir_path:
            for comp in dir_path.split("/"):
                if not comp:
                    continue
                nid = await conn.fetchval(
                    "SELECT id FROM nodes WHERE user_id=$1 AND parent_id=$2 AND name=$3",
                    user_id, parent_id, comp
                )
                if nid is None:
                    return "", 520
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

    return jsonify(result), 201


@app.route("/mkdir", methods=["POST"])
async def mkdir():
    user_id = await validate_user(POOL)
    raw_path = request.args.get("path", "").lstrip("/")

    if raw_path == "":
        return "", 404
    

    parts = raw_path.split("/")
    filename = parts[-1]
    parent_raw = "/".join(parts[:-1]) if len(parts) > 1 else ""

    filename = parts[-1]

    now = int(time.time())
    async with POOL.acquire() as conn, conn.transaction():
        if parent_raw == "":
            # empty path = root folder
            parent_id = await conn.fetchval(
                "SELECT id FROM nodes WHERE user_id=$1 AND parent_id IS NULL",
                user_id
            )
            if parent_id is None:
                parent_id = await conn.fetchval(
                    """
                    INSERT INTO nodes(user_id, name, parent_id, type,
                                      i_atime, i_mtime, i_ctime, i_crtime)
                    VALUES ($1, '', NULL, 2, $2, $2, $2, $2)
                    RETURNING id
                    """,
                    user_id, now
                )
                await create_closure(conn, parent_id, None)
        else:
          parent_id = await resolve_node(conn, user_id, parent_raw, expected_type=2)

        if parent_id is None:
            return "", 404

        exists = await conn.fetchval(
            "SELECT 1 FROM nodes WHERE user_id=$1 AND parent_id=$2 AND name=$3 AND type=2",
            user_id, parent_id, filename)
        if exists:
            return "", 201

        node_id = await conn.fetchval(
            """
            INSERT INTO nodes(user_id, name, parent_id, type,
                              i_atime, i_mtime, i_ctime, i_crtime)
            VALUES ($1,$2,$3, 2, $4,$4,$4,$4)
            RETURNING id
            """,
            user_id, filename, parent_id, now
        )
        await create_closure(conn, node_id, parent_id)

    return "", 201

@app.route("/wait_ready", methods=["GET"])
async def wait_ready():
    """
    Blocks until file is ready (all chunks uploaded).
    GET /wait_ready?user_id=22&path=foo/bar.txt
    """
    user_id = await validate_user(POOL)
    raw_path = request.args.get("path", "").lstrip("/")
    timeout = FILE_CHUNK_TIMEOUT
    
    if not raw_path:
        return "Missing path", 400
    
    async with POOL.acquire() as conn:
        node_id = await resolve_node(conn, user_id, raw_path, expected_type=1)
        if not node_id:
            return "File not found", 520
    
        ready = await conn.fetchval("SELECT ready FROM nodes WHERE id = $1",node_id)
        if ready:
            return "", 201
        
        if node_id not in upload_tracking:
            return "No upload in progress", 400
        
        _, event = upload_tracking[node_id]
    
    try:
        await asyncio.wait_for(event.wait(), timeout=timeout)
        return "", 201
    except asyncio.TimeoutError:
        return "Upload timeout", 408


@app.route("/download", methods=["GET"])
async def download():
    user_id = await validate_user(POOL)
    raw_path = request.args.get("path", "").lstrip("/")
    if not raw_path:
        return "", 400

    async with POOL.acquire() as conn:
        node_id = await resolve_node(conn, user_id, raw_path, 1)

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
            return "no chunks", 500

    async def streamer():
        for r in rows:
            chunk = await discord_client.download_attachment(r["message_id"])
            yield chunk

    # stream the file over in waves of chunks
    return Response(streamer(), status=201, mimetype="application/octet-stream")



@app.route("/create", methods=["POST"])
async def create_file():
    user_id = await validate_user(POOL)
    raw_path = request.args.get("path", "").lstrip("/")

    if not raw_path:
        return "", 400
    parts = raw_path.split("/")
    file_name = parts.pop()
    now = int(time.time())

    async with POOL.acquire() as conn, conn.transaction():
        parent = await conn.fetchval(
            "SELECT id FROM nodes WHERE user_id=$1 AND parent_id IS NULL",
            user_id
        )

        for comp in parts:
            node_id = await conn.fetchval(
                """
                SELECT id FROM nodes
                WHERE user_id=$1 AND parent_id=$2 AND name=$3
                """,
                user_id, parent, comp
            )
            if node_id is None:
                node_id = await conn.fetchval(
                    """
                    INSERT INTO nodes(user_id, name, parent_id, type,
                    i_atime, i_mtime, i_ctime, i_crtime)
                    VALUES($1,$2,$3, 2 ,$4,$4,$4,$4)
                    RETURNING id
                    """,
                    user_id, comp, parent, now
                )
                await create_closure(conn, node_id, parent)
            parent = node_id



        # Just in case the file DOES exist
        check_exists = await conn.fetchval(
            """
            SELECT 1 FROM nodes
            WHERE user_id=$1 AND parent_id=$2 AND name=$3
            """,
            user_id, parent, file_name
        )
        if check_exists:
            return "", 420
        

        node_id = await conn.fetchval(
            """
            INSERT INTO nodes(user_id, name, parent_id, type,
            i_atime, i_mtime, i_ctime, i_crtime)
            VALUES($1,$2,$3, 1 ,$4,$4,$4,$4)
            RETURNING id
            """,
            user_id, file_name, parent, now)
        await create_closure(conn, node_id, parent)

    return "", 201


@app.route("/truncate", methods=["POST"])
async def truncate_file():
    user_id = await validate_user(POOL)
    raw_path = request.args.get("path", "").lstrip("/")
    size = request.args.get("size")
    if not raw_path or size is None:
        return "Missing path or size", 400
    size = int(size)

    async with POOL.acquire() as conn, conn.transaction():
        node_id = await resolve_node(conn, user_id, raw_path, expected_type=1)

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

        await conn.execute("UPDATE nodes SET i_mtime=$1, size=$2 WHERE id=$3",
                            int(time.time()), size, node_id)
        
    return "", 201


@app.route("/unlink", methods=["POST"])
async def unlink():
    user_id = await validate_user(POOL)
    raw_path = request.args.get("path")
    channel = discord_client.get_channel(discord_client.channel_id)

    async with POOL.acquire() as conn:
        node_id = await resolve_node(conn, user_id, raw_path, expected_type=1)

        if not node_id:
            return "", 520
        
        rows = await conn.fetch(
            "SELECT message_id FROM file_chunks WHERE node_id=$1",
              node_id)

    
    message_ids = [r["message_id"] for r in rows if r["message_id"] is not None]
    await delete_messages(channel, message_ids)

    async with POOL.acquire() as conn, conn.transaction():
        await conn.execute("DELETE FROM nodes WHERE id = $1", node_id)
    return "", 201


@app.route("/rmdir", methods=["POST"])
async def rmdir():
    user_id = await validate_user(POOL)
    raw_path = request.args.get("path")

    async with POOL.acquire() as conn:
        # ensure dir is empty
        dir_id = await resolve_node(conn, user_id, raw_path, expected_type=2)

        if not dir_id:
            return "", 520

        child = await conn.fetchval(
            "SELECT 1 FROM nodes WHERE parent_id=$1 LIMIT 1", dir_id
        )
        if child:
            return "", 404

        await conn.execute("DELETE FROM nodes WHERE id=$1", dir_id)
    return '', 201
    
        


# Simply, rename.
@app.route("/rename", methods=["POST"])
async def rename():
    user_id = await validate_user(POOL)
    a_path = request.args.get("a", "").strip("/")
    b_path = request.args.get("b", "").strip("/")

    
    if not a_path or not b_path:
        return "Missing a or b paths", 400

    a_parent_path, a_name = await split_parent_and_name(a_path)
    b_parent_path, b_name = await split_parent_and_name(b_path)
    if a_parent_path != b_parent_path:
        return "Non-matching parent paths", 400
    if not a_name or not b_name:
        return "Invalid name(s)", 400
    if a_name == b_name:
        return "", 201
    
    async with POOL.acquire() as conn, conn.transaction():
        a_id = await resolve_node(conn, user_id, a_path, expected_type=None)
        if not a_id:
            return "Node not found", 520

        # Double check that b_name doesn't already exist in parent
        parent_id = await resolve_node(conn, user_id, a_parent_path, expected_type=2)
        if not parent_id:
            return "Parent not found", 520

        now = int(time.time())
        try:
            await conn.execute("UPDATE nodes SET i_ctime=$1, name=$2 WHERE id=$3",
                              now, b_name, a_id)
        except UniqueViolationError:
            return "Destination exists", 409
    return "", 201
        



# Handles moving files, non-replace
@app.route("/rename_move", methods=["POST"])
async def rename_move():
    user_id  = await validate_user(POOL)
    a_path  = request.args.get("a", "").strip("/")
    b_path  = request.args.get("b", "").strip("/")

    if not a_path or not b_path:
        return "Missing a or b paths", 400
    
    if a_path == b_path:
        return "", 201

    b_parent_path, b_name = await split_parent_and_name(b_path)

    if not b_name:
        return "Invalid destination name", 400


    async with POOL.acquire() as conn, conn.transaction():
        a_row = await node_info(conn, user_id, a_path)
        if not a_row:  # exit if a doesn't exist
            return "No such file", 520

        if await resolve_node(conn, user_id, b_path, expected_type=None):
            return "Destination exists", 409
        
        b_parent_id = await resolve_node(conn, user_id, b_parent_path, expected_type=2)
        if b_parent_id is None:
            return "Invalid destination(doesn't exist)", 520

        # forbid moving under own descendant (cycle)
        if await is_descendant(conn, a_row["id"], b_parent_id):
            return "Cycle in a and b", 400

        # update the parent_id, name, i_ctime for node a, cascade rewire
        now = int(time.time())
        try:
            await conn.execute(
                "UPDATE nodes SET parent_id=$1, name=$2, i_ctime=$3 WHERE id=$4",
                b_parent_id, b_name, now, a_row["id"]
            )
        except UniqueViolationError:
            return "Distination exists", 409

        await rewire_closure_for_move(conn, a_row["id"], b_parent_id)

    return "", 201


# Handles swapping of files
@app.route("/swap", methods=["POST"])
async def rename_swap():
    user_id = await validate_user(POOL)
    a_path = request.args.get("a", "").strip("/")
    b_path = request.args.get("b", "").strip("/")

    if not a_path or not b_path:
        return "Missing a or b paths", 400
    if a_path == b_path:
        return "", 201

    async with POOL.acquire() as conn, conn.transaction():
        a_node_id = await resolve_node(conn, user_id, a_path, expected_type=None)
        b_node_id = await resolve_node(conn, user_id, b_path, expected_type=None)
        if not a_node_id or not b_node_id:
            return "Destination(s) doesn't exists", 520

        # Check if files are ready
        a_ready = await conn.fetchval("SELECT ready FROM nodes WHERE id=$1", a_node_id)
        b_ready = await conn.fetchval("SELECT ready FROM nodes WHERE id=$1", b_node_id)
    
        # If either file is not ready, wait for uploads to complete
        if not a_ready and a_node_id in upload_tracking:
            _, a_event = upload_tracking[a_node_id]
            try:
                await asyncio.wait_for(a_event.wait(), timeout=FILE_CHUNK_TIMEOUT)
            except asyncio.TimeoutError:
                return "Upload timeout for file A", 408
        
        if not b_ready and b_node_id in upload_tracking:
            _, b_event = upload_tracking[b_node_id]
            try:
                await asyncio.wait_for(b_event.wait(), timeout=FILE_CHUNK_TIMEOUT)
            except asyncio.TimeoutError:
                return "Upload timeout for file B", 408
    

        a_row = await node_info(conn, user_id, a_path)
        b_row = await node_info(conn, user_id, b_path)
        if not b_row or not a_row:
            return "Destination(s) doesn't exists", 520

        # disallow swapping ancestor/descendant (would create cycle)
        if await is_descendant(conn, a_row["id"], b_row["id"]) or \
           await is_descendant(conn, b_row["id"], a_row["id"]):
            return "Cannot swap ancestor/descendant", 400
        
        if a_row["type"] != b_row["type"]:
            return "Cannot swap conflicting types! (dir & file)", 400

        a_parent_id, b_parent_id = a_row["parent_id"], b_row["parent_id"]
        a_name, b_name = a_row["name"], b_row["name"]
        a_id, b_id = a_row["id"], b_row["id"]

        # Check for third party name conflicts
        # Where another node that isn't A or B exist at the destination
        conflict1 = await conn.fetchval(
            """
            SELECT id FROM nodes
            WHERE user_id=$1 AND parent_id=$2 AND name=$3 AND id <> $4 AND id <> $5
            """, user_id, a_parent_id, b_name, a_id, b_id
        )
        conflict2 = await conn.fetchval(
            """
            SELECT id FROM nodes
            WHERE user_id=$1 AND parent_id=$2 AND name=$3 AND id <> $4 AND id <> $5
            """, user_id, b_parent_id, a_name, a_id, b_id
        )
        
        print(f"DEBUG: conflict1={conflict1}, conflict2={conflict2}")
        
        if conflict1 or conflict2:
            return "Destination exists", 409

        # Defer uniqueness for mid-statement collision
        await conn.execute("SET CONSTRAINTS uq_nodes_dirname DEFERRED")
        now = int(time.time())

        await conn.execute(
            """
            UPDATE nodes SET name=$1, parent_id=$2, i_ctime=$3 
            WHERE id=$4
            """,
            b_row["name"], b_row["parent_id"], now, a_id
        )
    
        await conn.execute(
            """
            UPDATE nodes SET name=$1, parent_id=$2, i_ctime=$3 
            WHERE id=$4
            """,
            a_row["name"], a_row["parent_id"], now, b_id
        )

        # rewire closure for both subtrees
        await rewire_closure_for_move(conn, a_id, b_row["parent_id"])
        await rewire_closure_for_move(conn, b_id, a_row["parent_id"])

    return "", 201
        

@app.route("/modi_mtime", methods=["POST"])
async def modi_mtime():
    user_id = await validate_user(POOL)
    raw_path = request.args.get("path", "").lstrip("/")
    new_mtime = request.args.get("mtime")
    if not raw_path or new_mtime is None:
        return "Missing path or new_mtime", 400

    try:
        new_mtime = int(new_mtime)
        if new_mtime < 0:
            return "mtime must be positive", 400
    except ValueError:
        return "mtime must be an integer", 400
    

    async with POOL.acquire() as conn, conn.transaction():
        node_id = await resolve_node(conn, user_id, raw_path, expected_type=1)
        if node_id is None:
            return "Invalid destination(doesn't exist)", 520
        await conn.execute("UPDATE nodes SET i_mtime=$1 WHERE id=$2",
                            new_mtime, node_id)
        
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
