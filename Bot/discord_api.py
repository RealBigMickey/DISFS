import discord
import asyncio

class DiscordClient(discord.Client):
    def __init__(self, channel_id, *args, **kwargs):
        super().__init__(*args, **kwargs)
        self.channel_id = channel_id
        self.ready_event = asyncio.Event()

    async def on_ready(self):
        print(f"{self.user} is now online.")
        self.ready_event.set()

    async def send_dog_gif(self):
        await self.wait_until_ready()
        channel = self.get_channel(self.channel_id)
        if channel:
            await channel.send("https://github.com/RealBigMickey/Linux2025/blob/main/doggo%20coding.gif?raw=true")

def get_client(channel_id):
    intents = discord.Intents.default()
    return DiscordClient(channel_id, intents=intents)
