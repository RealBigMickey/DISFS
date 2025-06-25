import discord
import asyncio
import aiohttp

class DiscordClient(discord.Client):
    def __init__(self, channel_id, *args, **kwargs):
        super().__init__(*args, **kwargs)
        self.channel_id = channel_id
        self.ready_event = asyncio.Event()

    async def on_ready(self):
        print(f"{self.user} is now online.")
        self.ready_event.set()

    async def wait_until_ready(self):
        await self.ready_event.wait()

    async def send_dog_gif(self):
        await self.wait_until_ready()
        channel = self.get_channel(self.channel_id)
        if channel:
            await channel.send("https://github.com/RealBigMickey/Linux2025/blob/main/doggo%20coding.gif?raw=true")
    

    async def download_attachment(self, message_id):
        await self.wait_until_ready()
        channel = self.get_channel(self.channel_id)
        if not channel:
            raise ValueError("Channel not found")
        try:
            msg = await channel.fetch_message(message_id)
        except discord.NotFound:
            raise ValueError("Message not found")
        if not msg.attachments:
            raise ValueError("No attachments found on this message")
        url = msg.attachments[0].url

        async with aiohttp.ClientSession() as session:
            async with session.get(url) as resp:
                resp.raise_for_status()
                return await resp.read()



def get_client(channel_id):
    intents = discord.Intents.default()
    return DiscordClient(channel_id, intents=intents)
