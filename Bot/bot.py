import discord as dc
import asyncio
import os
import sys
from discord.ext import commands
from dotenv import load_dotenv

VAULT_ID = [1385805289881600000, 1385805609403809803]
NOTIFICATIONS_ID = 1385864919085219860

load_dotenv()
TOKEN = os.getenv("DISCORD_TOKEN")


intents = dc.Intents.default() # an object to keep track of what intents to track
intents.message_content = True

bot = commands.Bot(command_prefix="!", intents=intents)

@bot.command(name="dog")
async def dog_gif(ctx):
    await ctx.send("https://github.com/RealBigMickey/Linux2025/blob/main/doggo%20coding.gif?raw=true")


# for adminstrator to run commands
async def stdin_monitor():
    await bot.wait_until_ready()
    loop = asyncio.get_event_loop()
    while True:
        print("> ", end="", flush=True)
        line = await loop.run_in_executor(None, sys.stdin.readline)
        if line.strip() == "exit":
            text = f"[{bot.user}] is shutting down... 🟥"
            channel = bot.get_channel(NOTIFICATIONS_ID)
            print(text)
            await channel.send(text)
            await bot.close()
            break


@bot.event
async def on_ready():
    text = f"[{bot.user}] is now online ✅"
    channel = bot.get_channel(NOTIFICATIONS_ID)
    if channel:
        print(text)
        await channel.send(text)
    bot.loop.create_task(stdin_monitor())


if __name__ == "__main__":
    bot.run(TOKEN)
