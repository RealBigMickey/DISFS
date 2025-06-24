import asyncio
import os
import tempfile

import aioconsole
from discord import File
from quart import abort, request
from server._config import NOTIFICATIONS_ID


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


async def dispatch_upload(POOL, discord_client, user_id, path, chunk, tmp_name):
    """
    Upload one chunk file to Discord and record message_id in DB.
    """
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


async def admin_console(discord_client, app):
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
