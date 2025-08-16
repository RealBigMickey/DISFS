# DISFS – A FUSE File System on Top of the Discord CDN
> *⚠️ **Disclaimer:** DISFS is an independent project and is not affiliated with or endorsed by Discord Inc.*
> *It uses the Discord CDN solely for research and educational purposes.*


DISFS turns **Discord** into a backend object store that you can mount as a normal POSIX file system via **FUSE 3**.
Doesn’t store files locally, instead communicates with a remote server I run (or you) to handle all storage operations.

- Client side: **C FUSE 3 daemon**  
- Server side: **Python (Quart + PostgreSQL)**  

A typical workflow looks like:
- **Write a file** → split into chunks → upload via Discord bot → metadata stored in Postgres  
- **Read a file** → daemon fetches chunks on demand, with caching & mtime reconciliation  

Inspired by GNU naming, **DISFS = “DISFS Is a Service File System.”**

---

## ✨ Features

|    | Feature                        | Details                                                                                      |
| -- | ------------------------------ | -------------------------------------------------------------------------------------------- |
| ✓  | **Mount anywhere**             | `./main /mnt/disfs` – works on vanilla Linux 5.x, no kernel mods                             |
| ✓  | **Smart metadata**             | Closure-table schema in Postgres → O(1) emptiness test + efficient `rename`                  |
| ✓  | **Pseudo-files for commands**  | Login/logout via `cat .command/ping/{user}` and `cat .command/pong`                          |
| ✓  | **Local cache**                | Write-back cache in `~/.cache/disfs/{uid}/...` with delayed flush & `fsync` durability       |
| ✓  | **Async backend**              | `asyncpg` + Discord WebSockets = no thread pools, high concurrency                           |

---

## 🚀 Quick Start – FUSE Client

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

## ⚙️ Quick Start – Server backend
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
Install with:
```bash
sudo apt install postgresql postgresql-contrib
```

Set the connection string in your `.env`:
```bash
# recall this line
DATABASE_URL="postgresql://user:pass@localhost/disfs"
```

Choose your own names, an example database would be:
```sql
CREATE USER admin WITH PASSWORD 'password';
CREATE DATABASE disfs_db OWNER admin;
GRANT ALL PRIVILEGES ON DATABASE disfs_db TO admin;
```


---
## 🗄 Metadata & Storage

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

### ⚠️ Known Limitations / TODO
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


