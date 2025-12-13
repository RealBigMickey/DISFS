import os
from dotenv import load_dotenv

PORT = 5050

load_dotenv()
TOKEN = os.getenv("DISCORD_TOKEN")
DATABASE_URL = os.getenv("DATABASE_URL")

VAULT_IDS = [1385805289881600000, 1385805609403809803]
NOTIFICATIONS_ID = 1385864919085219860

FILE_CHUNK_TIMEOUT = 10

rate_limited_paths = ["/upload", "/download", "/prep_upload", "/truncate", "/unlink", "/dog_gif"]


RATE_LIMIT_REQUESTS = int(os.getenv("RATE_LIMIT_REQUESTS", "100"))
RATE_LIMIT_WINDOW = int(os.getenv("RATE_LIMIT_WINDOW", "60"))