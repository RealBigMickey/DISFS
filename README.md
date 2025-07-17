# DISFS – FUSE-based file system using the Discord CDN as a backend
> *⚠️ **Disclaimer:** DISFS is an independent project and is not affiliated with or endorsed by Discord Inc.*
> *It uses the Discord CDN solely for research and demonstration purposes.*


DISFS turns **Discord** into a backend object store that you can mount as a normal POSIX file system via **FUSE 3**.
Doesn’t store files locally, instead communicates with a remote server I run (or you) to handle all storage operations.

Write a file → it’s chunked and uploaded as Discord attachments;
read a file → the daemon streams chunks back on-demand.

DISFS — acronym for "**D**ISFS **I**s a **S**ervice **F**ile **S**ystem", inspired by GNU.

---

## Features

|    | Feature                        | Details                                                                                     |
| -- | ------------------------------ | ------------------------------------------------------------------------------------------- |
| ✓ | **Mount anywhere**       | `./main /mnt/disfs` – no kernel patches, runs on vanilla Linux 5.x                       |
| ✓ | **Smart metadata**       | Closure-table schema gives O(1) emptiness test and fast `rename`                          |
| ✓ | **Pseudo-file commands** | Login with `cat /.ping/William`, logout with `cat /.pong` – UNIX-style control surface |
| ✓ | **Fully async backend**  | `asyncpg` + Discord websockets, no thread pools, high concurrency                         |

---

## Quick-start (fuse side)

#### 1) Clone and enter

```bash
git clone https://github.com/RealBigMickey/DISFS
cd disfs
```


#### 2) C Dependencies

FUSE side uses:

- [`libfuse3`](https://github.com/libfuse/libfuse)
- [`libcurl`](https://curl.se/libcurl/)
- [`cJSON`](https://github.com/DaveGamble/cJSON)

On Debian/Ubuntu, run:

```bash
sudo apt update
sudo apt install libfuse3-dev libcurl4-openssl-dev libcjson-dev
```

#### 4) Run make file at repo directory

```bash
make         # Builds + unmount + mounts
make mount   # Mount only
make unmount # Unmount only
make clean   # Clean build + unmount
```

---

## Quick-start (server side)
/* For those who wish to run their own server */

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
NOTIFICATIONS_ID = YOUR_NOTIFICATION_CHANNEL_ID  # Your Discord channel ID
VAULT_IDS = [YOUR_NOTIFICATION_CHANNEL_ID]       # Your Discord channel ID
```

#### 4) run server (Quart, hot-reload off)

```bash
python3 -m server.main
```

Type `exit` to exit server.

## Known limitations / TODO

- No per-user encryption (planned AES-GCM chunk layer).
- Single Discord channel; should shard by hash prefix.
- Synchronous libcurl in server path – migrate to aiohttp for zero‐copy.
- Directory listings are CSV hits; add pagination for >1 k entries.
- No chunk integrity verification

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
