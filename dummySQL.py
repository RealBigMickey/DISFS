import time
import asyncpg
from server.app_utils import create_closure

async def seed_foo_txt(POOL):
    async with POOL.acquire() as conn:
        # seed foo.txt under William’s root, timestamp = epoch(0)
        now0 = 0
        row = await conn.fetchrow(
            "SELECT id FROM users WHERE username=$1", "William"
        )
        if row:
            uid = row["id"]
            # get or create root node
            root = await conn.fetchval(
                "SELECT id FROM nodes WHERE user_id=$1 AND parent_id IS NULL",
                uid
            )
            if root is None:
                root = await conn.fetchval(
                    """
                    INSERT INTO nodes(user_id,name,parent_id,type,
                                      i_atime,i_mtime,i_ctime,i_crtime)
                    VALUES($1,'',NULL,2,$2,$2,$2,$2)
                    RETURNING id
                    """,
                    uid, now0
                )
                await create_closure(conn, root, None)

            # insert foo.txt
            node = await conn.fetchval(
                "SELECT id FROM nodes WHERE user_id=$1 AND parent_id=$2 AND name=$3",
                uid, root, "foo.txt"
            )
            if node is None:
                node = await conn.fetchval(
                    """
                    INSERT INTO nodes(user_id,name,parent_id,type,size,ready,
                                      i_atime,i_mtime,i_ctime,i_crtime)
                    VALUES($1,$2,$3, 1, 31, TRUE, $4,$4,$4,$4)
                    RETURNING id
                    """,
                    uid, "foo.txt", root, now0
                )
                await create_closure(conn, node, root)

            # single‐chunk entry at index 0
            foo_msg_id = 1402502296599793817
            await conn.execute(
                """
                INSERT INTO file_chunks(node_id, chunk_index, message_id, chunk_size)
                VALUES($1,0,$2, 31)
                ON CONFLICT (node_id,chunk_index) DO NOTHING
                """,
                node, foo_msg_id
            )