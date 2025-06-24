import time
import asyncpg

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