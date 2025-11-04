# DISFS – A FUSE File System on Top of the Discord CDN
> ***Disclaimer:** DISFS is an independent project and is not affiliated with or endorsed by Discord Inc.*
> *It uses the Discord CDN solely for research and educational purposes.*


**DISFS** turns Discord into a mountable POSIX file system.  
Files aren’t stored locally – instead, they are split into chunks and uploaded as Discord attachments. Reads are streamed back on demand from a remote server.

- Client side: **C FUSE 3 daemon**  
- Server side: **Python (Quart + PostgreSQL)**  

A typical workflow looks like:
- **Write a file** → split into chunks → upload via Discord bot → metadata stored in Postgres  
- **Read a file** → daemon fetches chunks on demand, with caching & mtime reconciliation  

Inspired by GNU naming, **DISFS = “DISFS Is a Service File System.”**

---

## Features

|    | Feature                        | Details                                                                                      |
| -- | ------------------------------ | -------------------------------------------------------------------------------------------- |
| ✓  | **Mount anywhere**             | `./main /mnt/disfs` – works on vanilla Linux 5.x, no kernel mods                             |
| ✓  | **Smart metadata**             | Closure-table schema in Postgres → O(1) emptiness test + efficient `rename`                  |
| ✓  | **Pseudo-files for commands**  | Login/logout via `cat .command/ping/{user}` and `cat .command/pong`                          |
| ✓  | **Local cache**                | Write-back cache in `~/.cache/disfs/{uid}/...` with delayed flush & `fsync` durability       |
| ✓  | **Async backend**              | `asyncpg` + Discord WebSockets = no thread pools, high concurrency                           |

---

## Quick Start – FUSE Client

### 1) Clone

```bash
git clone https://github.com/RealBigMickey/DISFS
cd disfs
```


### 2) Install dependencies

FUSE side uses:
- [`libfuse3`](https://github.com/libfuse/libfuse)
- [`libcurl`](https://curl.se/libcurl/)
- [`cJSON`](https://github.com/DaveGamble/cJSON)

```bash
sudo apt update
sudo apt install libfuse3-dev libcurl4-openssl-dev libcjson-dev
```

### 3) On Debian/Ubuntu, Build & Run:

```bash
make         # Builds + unmount + mounts
make mount   # Mount only
make unmount # Unmount only
make clean   # Clean + unmount
```

---

## Quick Start – Server backend
/* For those who wish to run their own server */

#### 1) Python dependencies

Install with:
```bash
pip install -r requirements.txt
```

#### 2) Environment variables (.env)

```bash
DATABASE_URL="postgresql://user:pass@localhost/disfs"
TOKEN="YOUR_DISCORD_BOT_TOKEN"
# Feel free to change url, though other changes may be necessary
# Get your bot token from: https://discord.com/developers/
```

#### 3) Configure server/_config.py

```python
NOTIFICATIONS_ID = YOUR_NOTIFICATION_CHANNEL_ID  # Your Discord channel ID
VAULT_IDS = [YOUR_NOTIFICATION_CHANNEL_ID]       # Your Discord channel ID
```

#### 4) Run server

```bash
python3 -m server.main
```

Type `exit`, `q`, `quit` to exit server.
`dog` to send a dog gif to notification server! 


## 📚 Quick Start – Database

DISFS requires a PostgreSQL database.

### 1. Install PostgreSQL

```bash
sudo apt install postgresql postgresql-contrib
```

### 2. Set up the database
Create user like so:
```
sudo -i -u postgres psql << EOF
CREATE USER admin WITH PASSWORD 'password' CREATEDB;
CREATE DATABASE disfs_db OWNER admin;
\q
EOF
```

3. Configure the `.env`:
Create a .env file in the project root.
Set up the username and password like you have in step 2
```bash
# Modify the URL
DATABASE_URL="postgres://admin:password@localhost:5432/disfs_db"
DISCORD_TOKEN="your_discord_bot_token_here"
```

That's it! When you run the server, the necessary tables will be automatically created.

---
## Metadata & Storage

- **Database:** PostgreSQL

- **Schema:** Closure tables (nodes, node_closure, file_chunks)

- **Logical path:** Path seen by the user in the mounted FS (/foo/bar.txt)

- **Local cache path:** ~/.cache/disfs/{user_id}/foo/bar.txt

Helper command ↓
```bash
# Deletes all rows from db quickly and simply
psql "$DATABASE_URL" -c "TRUNCATE users, nodes, node_closure, file_chunks RESTART IDENTITY CASCADE;"
```

--- 
### Changelog

v0.1: First working release (basic FUSE FS + Discord backend).  
v0.2: Added first version of cache control (local cache directory + eviction logic).  


### Known Limitations / TODO
- No per-user encryption yet (planned: AES-GCM chunk layer).
- All files currently in a single Discord channel → needs sharding by hash prefix.
- Still using synchronous libcurl in the FUSE path → plan to migrate to fully async.
- Directory listings = CSV hits, no pagination (>1k entries may degrade).
- No chunk integrity verification.


### 🔮 Roadmap
- [ ] Cache record system
- [ ] Cache version control, only downloading new cache when stale
- [ ] Add encryption layer (AES-GCM per user)
- [ ] Multi-channel sharding for scalability
- [ ] Async streaming with aiohttp instead of sync libcurl
- [ ] Per-chunk integrity check (SHA-256)
- [ ] Smarter eviction in local cache

---

### Credits
- FUSE client written in C (libfuse3)
- Server in Python (Quart + asyncpg)
- Metadata schema: Closure Tables in PostgreSQL


