import asyncio
from server.app import app, startup

async def main():
    await startup()
    await app.run_task(host="0.0.0.0", port=5050)

if __name__ == "__main__":
    asyncio.run(main())
