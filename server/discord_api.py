import discord
import asyncio
import aiohttp
import datetime

"""
Note: Functions are subject to changes and may need maintance on Discord api changes.
"""

DISCORD_EPOCH = 1420070400000

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



def snowflake_to_datetime(snowflake: int) -> datetime.datetime:
    """
    Convert Discord snowflake (message ID) to UTC datetime of creation.
    """
    timestamp_ms = (snowflake >> 22) + DISCORD_EPOCH
    return datetime.datetime.fromtimestamp(timestamp_ms / 1000.0, tz=datetime.timezone.utc)


def is_recent(snowflake: int, days: float = 13.9) -> bool:
    """
    Return True if the snowflake's timestamp is within `days` days from now.
    """
    msg_time = snowflake_to_datetime(snowflake)
    now = datetime.datetime.now(tz=datetime.timezone.utc)
    delta = now - msg_time
    return delta.total_seconds() < days * 24 * 3600



async def delete_message_single(channel, message_id: int, max_retries: int = 3):
    """
    Delete a single message by ID, handling rate limits and 404s.
    """
    for attempt in range(1, max_retries + 1):
        try:
            msg = await channel.fetch_message(message_id)
            await msg.delete()
            return
        except discord.HTTPException as e:
            status = e.status
            if status == 404:
                # already deleted
                return
            if status == 429:
                # rate limited, retries
                retry_after = e.response.headers.get("retry_after") or e.response.headers.get("Retry-After")
                try:
                    wait = float(retry_after)
                except Exception:
                    wait = 1.0 * attempt
                await asyncio.sleep(wait)
                continue
            discord.logging.exception(f"Failed to delete message {message_id}: HTTP {status}")
            break
        except Exception:
            discord.logging.exception(f"Unexpected exception deleting message {message_id}")
            break


async def delete_messages_bulk(channel, message_ids: list[int]):
    """
    Try to delete messages in bulk (<=100) via bulk API endpoint.
    Fallback to individual on failure.
    As of 2025-10-07 only works for messages younger than 14 days
    """
    objs = [discord.Object(id=m) for m in message_ids]
    try:
        await channel.delete_messages(objs)
    except discord.HTTPException as e:
        if e.status == 429:
            # rate limited
            retry_after = e.response.headers.get("retry_after") or e.response.headers.get("Retry-After")
            try:
                wait = float(retry_after)
            except:
                wait = 1.0
            await asyncio.sleep(wait)
            # retry once
            try:
                await channel.delete_messages(objs)
                return
            except discord.HTTPException:
                pass
        # fallback to single deletion
        for m in message_ids:
            await delete_message_single(channel, m)


async def delete_messages(channel, message_ids: list[int]):
    """
    Delete a list of message_ids. Use bulk for recent ones, single for older.
    """
    recent = [m for m in message_ids if is_recent(m)]
    older = [m for m in message_ids if not is_recent(m)]

    # bulk deletion for new messages
    for i in range(0, len(recent), 100):
        chunk = recent[i : i + 100]
        await delete_messages_bulk(channel, chunk)
        await asyncio.sleep(0.1)

    for m in older:
        await delete_message_single(channel, m)
        await asyncio.sleep(0.1)