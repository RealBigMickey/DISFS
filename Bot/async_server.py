import asyncio
import aioconsole
import os
from quart import Quart
from _config import TOKEN, NOTIFICATIONS_ID
from discord_api import get_client

app = Quart(__name__)
discord_client = get_client(NOTIFICATIONS_ID)

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
                await discord_client.close()
                os._exit(0)
            case "status":
                print("Not implemented yet!")  # TBD
            case "dog":
                await discord_client.send_dog_gif()
                print("Dog gif sent owo")
            case _:  # default case
                print(f"Unknown command: \"{cmd}\"")

async def main():
    asyncio.create_task(discord_client.start(TOKEN))
    asyncio.create_task(admin_console())
    
    # Start Quart HTTP server (async)
    await app.run_task(host="0.0.0.0", port=5050)

if __name__ == "__main__":
    asyncio.run(main())
