# DISFS – Discord Integrated Storage File System

DISFS turns **Discord** into a backend object store that you can mount
as a normal POSIX file system via **FUSE 3**.  
Write a file → it’s chunked and uploaded as Discord attachments;  
read a file → the daemon streams chunks back on-demand.

---

## Features

|   | Feature                | Details                                                                                           |
|---|------------------------|----------------------------------------------------------------------------------------------------|
| ✓ | **Mount anywhere**     | `./main /mnt/disfs` – no kernel patches, runs on vanilla Linux 5.x                                 |
| ✓ | **Smart metadata**     | Closure-table schema gives O(1) emptiness test and fast `rename`                                   |
| ✓ | **Pseudo-file commands**| Login with `cat /.ping/William`, logout with `cat /.pong` – UNIX-style control surface            |
| ✓ | **Fully async backend**| `asyncpg` + Discord websockets, no thread pools, high concurrency                                 |

---

## Quick-start (fuse side)
#### 1) clone
```bash
git clone https://github.com/RealBigMickey/DISFS
```
cd disfs

#### 2) C Dependencies

FUSE side uses:

- [`libfuse3`](https://github.com/libfuse/libfuse)
- [`libcurl`](https://curl.se/libcurl/)
- [`cJSON`](https://github.com/DaveGamble/cJSON)

#### 3) Install Debian/Ubuntu dependencies:

```bash
sudo apt update
sudo apt install libfuse3-dev libcurl4-openssl-dev libcjson-dev
```

#### 4) Run make file at repo directory
```bash
make
```
Automatically builds + unmount + mounts.
To unmount do `make unmount`, only mount with `make mount`

---

## Quick-start (server side)
#### 1) Python Setup

Install Python dependencies:
```bash
pip install -r requirements.txt
```

#### 2) env vars 
```bash
echo 'DATABASE_URL="postgresql://user:pass@/disfs"
TOKEN="YOUR_DISCORD_BOT_TOKEN"' > .env
# Get bot token from: https://discord.com/developers/
```

#### 3) Manually set server/_config.py
```python
NOTIFICATIONS_ID = YOUR_NOTIFICATION_CHANNEL_ID
VAULT_IDS = [YOUR_NOTIFICATION_CHANNEL_ID]  # Just 1 implemented right now
```

#### 4) run server (Quart, hot-reload off)
```bash
python -m server.main
```
Type `exit` to exit server.

## Known limitations / TODO
- No per-user encryption (planned AES-GCM chunk layer).

- Single Discord channel; should shard by hash prefix.

- Synchronous libcurl in server path – migrate to aiohttp for zero‐copy.

- Directory listings are CSV hits; add pagination for >1 k entries.

##  License
MIT for demo code. Discord™ is a trademark of Discord Inc.; this project is neither affiliated with nor endorsed by them.


---

#### miscellaneous:

type = 1 -> file
type = 2 -> directory

Uses 'Closure tables' in PostgreSQL.


logical_path -> virtual file path as seen by the FUSE user
local_path -> ACTUAL path on local filesystem


file cache stored at -> HOME/.cache/disfs/{user_id}/{local_path}

```bash
# Delete all rows from sql quick and simple
psql "$DATABASE_URL" -c "TRUNCATE users, nodes, node_closure, file_chunks RESTART IDENTITY CASCADE;"
```