import asyncio
import os
import tempfile
import time
import aioconsole
from discord import File
from quart import abort, request
from server._config import NOTIFICATIONS_ID


current_vault = 1  # channel used for storage, on 1 currently for simplicity. TBD!


async def split_parent_and_name(path: str):
    # case root
    if path == "":
        return None, ""
    
    parts = path.split("/")
    name = parts[-1]
    parts.pop()
    parent = "/".join(parts)
    return parent, name



async def validate_user(POOL):
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


async def dispatch_upload(POOL, discord_client, user_id, file_path: str, chunk, chunk_size ,tmp_name):
    """
    Upload one chunk file to Discord and record message_id in DB.
    """
    await discord_client.wait_until_ready()

    node_id = None
    parts = file_path.split("/")

    # Check path
    async with POOL.acquire() as conn:
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
                # path should've been added by `/upload`
                raise RuntimeError(f"Path not found in nodes: {file_path}")
            parent = nid
        node_id = parent


    print(f"dispatch_upload: node_id={node_id}, chunk={chunk}, size={chunk_size}")

    # Send to discord
    channel = discord_client.get_channel(discord_client.channel_id)
    msg = await channel.send(file=File(tmp_name))

    # Insert into db
    async with POOL.acquire() as conn:
        await conn.execute(
            """
            INSERT INTO file_chunks(node_id, chunk_index, chunk_size, message_id)
            VALUES($1,$2,$3,$4)
            ON CONFLICT (node_id, chunk_index) DO NOTHING
            """,
            node_id, chunk, chunk_size, msg.id
        )

        # update access time
        await conn.execute(
            "UPDATE nodes SET i_mtime=$1 WHERE id=$2",
            int(time.time()), node_id
        )


    try:
        os.unlink(tmp_name)
    except OSError:
        print("An upload request failed!")
        pass    



async def admin_console(discord_client, app):
    await discord_client.wait_until_ready() 
    while True:
        cmd = await aioconsole.ainput("")
        cmd = cmd.strip().lower()
        match cmd:
            case "exit":
                print("Shutting down...")
                await app.shutdown()
            case "quit":
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



async def node_info(conn, user_id: int, path: str):
    """
    Resolves node info from path, returns entire ROW
    -> (id, user_id, name, parent_id, type,
        i_atime, i_mtime, i_ctime, i_crtime)
    or None if not found
    """
    if not path:
        return None
    parts = path.split("/")


    parent_id = await conn.fetchval(
        "SELECT id FROM nodes WHERE user_id=$1 AND parent_id IS NULL", user_id
    )
    
    if parent_id is None:
        return None
    
    node_id = None
    n_parts = len(parts)
    for idx, comp in enumerate(parts):
        row = await conn.fetchrow(
            "SELECT id, type FROM nodes "
            "WHERE user_id=$1 AND parent_id=$2 AND name=$3",
            user_id, parent_id, comp
        )
        if not row:
            return None
        node_id = row["id"]
        n_type = row["type"]

        if idx < n_parts - 1 and n_type != 2:
            return None
        
        parent_id = node_id
    
    full_row = await conn.fetchrow(
        """
        SELECT id, user_id, name, parent_id, type,
          i_atime, i_mtime, i_ctime, i_crtime FROM nodes WHERE id=$1
        """, node_id
    )
    return full_row  # returns object asyncpg.Record
    


async def is_descendant(conn, ancestor_id: int, maybe_descendant_id: int) -> bool:
    return bool(await conn.fetchval(
        "SELECT 1 FROM node_closure WHERE ancestor=$1 AND descendant=$2",
        ancestor_id, maybe_descendant_id
    ))



async def get_parent_id(conn, user_id: int, parent_path: str):
    """
    Ensures parent dir exists and IS a directory, returns parent_id or None
    """
    # if not root
    if parent_path == "":
        return await conn.fetchval(
            "SELECT id FROM nodes WHERE user_id=$1 AND parent_id IS NULL",
            user_id
        )
    return await resolve_node(conn, user_id, parent_path, expected_type=2)



# Might not be necessary???
async def rewire_closure_for_move(conn, node_id: int, new_parent_id: int):
    # Detach from ancestors
    await conn.execute(
        """
        DELETE FROM node_closure
         WHERE descendant IN (SELECT descendant FROM node_closure WHERE ancestor=$1)
         AND ancestor IN(SELECT ancestor FROM node_closure WHERE descendant=$1 AND ancestor!=$1)
        """, node_id)

    # Attach under new_parent's ancestors
    await conn.execute(
        """
        INSERT INTO node_closure (ancestor, descendant, depth)
        SELECT super.ancestor, sub.descendant, super.depth + sub.depth + 1
          FROM node_closure AS super
          JOIN node_closure AS sub
            ON super.descendant = $1  -- new parent
           AND sub.ancestor   = $2  -- moved node
        """,
        new_parent_id, node_id
    )



async def create_closure(conn, new_id: int, parent_id: int | None):
    await conn.execute(
        """
        INSERT INTO node_closure (ancestor, descendant, depth)
        VALUES ($1, $1, 0)
        ON CONFLICT (ancestor, descendant) DO NOTHING
        """,
        new_id
    )

    if parent_id is None:
        return

    rows = await conn.fetch(
        """
        SELECT ancestor, depth
          FROM node_closure
         WHERE descendant = $1
        """,
        parent_id
    )

    await conn.executemany(
        """
        INSERT INTO node_closure (ancestor, descendant, depth)
        VALUES ($1, $2, $3)
        ON CONFLICT (ancestor, descendant) DO NOTHING
        """,
        [(r["ancestor"], new_id, r["depth"] + 1) for r in rows]
    )


# type 1 -> file, type 2 -> directory
async def resolve_node(conn, user_id: int, path: str, expected_type: int | None = None):
    parts = [p for p in path.split("/") if p]  # skip empty components

    node_id = None
    parent_id = await conn.fetchval(
        "SELECT id FROM nodes WHERE user_id=$1 AND parent_id IS NULL",
        user_id)

    if parent_id is None:
        return None
    n_parts = len(parts)
    for idx, comp in enumerate(parts):
        row = await conn.fetchrow(
            "SELECT id, type FROM nodes WHERE user_id=$1 AND parent_id=$2 AND name=$3",
            user_id, parent_id, comp
        )
        if not row:
            return None
        node_id, n_type = row["id"], row["type"]

        # intermediate must be directories
        if idx < n_parts - 1 and n_type != 2:
            return None

        if idx == n_parts - 1 and expected_type is not None and n_type != expected_type:
            return None

        parent_id = node_id
    return parent_id



