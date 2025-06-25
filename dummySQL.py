import time
import asyncpg
from server.app_utils import create_closure

# Dummy files for testing `do_readdir` & `listdir`
async def seed_dummy_data(pool):
    async with pool.acquire() as conn:
        uid = await conn.fetchval("""
            INSERT INTO users(username) VALUES('William')
            ON CONFLICT(username) DO UPDATE SET username=EXCLUDED.username
            RETURNING id
        """)

        exists = await conn.fetchval("SELECT 1 FROM nodes WHERE user_id=$1", uid)
        if exists:
            return

        now = int(time.time())

        # root (parent_id NULL)
        root = await conn.fetchval("""
            INSERT INTO nodes(user_id,name,parent_id,type,
                              i_atime,i_mtime,i_ctime,i_crtime)
            VALUES($1,'/',NULL,2,$2,$2,$2,$2) RETURNING id
        """, uid, now)
        await conn.execute(
            "INSERT INTO node_closure(ancestor,descendant,depth) VALUES($1,$1,0)",
            root
        )

        # /docs folder
        docs = await conn.fetchval("""
            INSERT INTO nodes(user_id,name,parent_id,type,
                              i_atime,i_mtime,i_ctime,i_crtime)
            VALUES($1,'docs',$2,2,$3,$3,$3,$3) RETURNING id
        """, uid, root, now)
        await conn.executemany(
            "INSERT INTO node_closure(ancestor,descendant,depth) VALUES($1,$2,$3)",
            [(root, docs, 1), (docs, docs, 0)]
        )

        # /docs/readme.txt file
        readme = await conn.fetchval("""
            INSERT INTO nodes(user_id,name,parent_id,type,
                              i_atime,i_mtime,i_ctime,i_crtime)
            VALUES($1,'readme.txt',$2,1,$3,$3,$3,$3) RETURNING id
        """, uid, docs, now)
        await conn.executemany(
            "INSERT INTO node_closure(ancestor,descendant,depth) VALUES($1,$2,$3)",
            [(root, readme, 2), (docs, readme, 1), (readme, readme, 0)]
        )

        # empty chunk placeholder
        await conn.execute("""
            INSERT INTO file_chunks(node_id,chunk_index,message_id)
            VALUES($1,0,NULL)
        """, readme)





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
                    INSERT INTO nodes(user_id,name,parent_id,type,
                                      i_atime,i_mtime,i_ctime,i_crtime)
                    VALUES($1,$2,$3, 1,$4,$4,$4,$4)
                    RETURNING id
                    """,
                    uid, "foo.txt", root, now0
                )
                await create_closure(conn, node, root)

            # single‐chunk entry at index 0
            foo_msg_id = 1387459088228810843
            await conn.execute(
                """
                INSERT INTO file_chunks(node_id, chunk_index, message_id, chunk_size)
                VALUES($1,0,$2, 31)
                ON CONFLICT (node_id,chunk_index) DO NOTHING
                """,
                node, foo_msg_id
            )