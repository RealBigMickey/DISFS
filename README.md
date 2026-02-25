# DISFS – A POSIX-compliant FUSE filesystem using Discord’s CDN as an object store.
> ***Disclaimer:** DISFS is an independent project and is not affiliated with or endorsed by Discord Inc.*  
> *Uses the Discord CDN solely for research and educational purposes.*  


Turns Discord into a mountable POSIX file system!
Files are split into chunks and uploaded as Discord attachments. Reads and writes go through a backend server, which forwards requests to Discord when necessary.
The goal is to have just one backend service multiple clients. Thus all requests are stateless HTTP.  
A passion project of mine •~•.

- Client side: **C FUSE 3 daemon**  
- Server side: **Python (Quart + PostgreSQL)**  


Inspired by GNU naming, **DISFS = “DISFS Is a Service File System”**  

<img src="https://raw.githubusercontent.com/RealBigMickey/Freshman-projects/main/2025-11-30%2023-48-26~2.gif" width="600">


---

**NOTE:**  
Testing on the hosted server only requires the FUSE client.  
```https://linuxer.tail0ed11f.ts.net/```  ![Status](https://img.shields.io/uptimerobot/status/m802424205-7bc8735dbd2b1972743faca2)

## Testing on the hosted server:
After setting up the FUSE client, start from the DISFS directory:  

### 1) make it
```bash
$ make
```
### 2) change url to the host
```bash
$ cat mnt/.command/changeurl/linuxer.tail0ed11f.ts.net
```
Done.  
Run ```make test-01``` or see [How to use](#how-to-use) for further instructions.

---
## Quick Start – FUSE Client

### 1) Clone

```bash
git clone https://github.com/RealBigMickey/DISFS
cd disfs
```

### 2) Install dependencies
- [`libfuse3`](https://github.com/libfuse/libfuse)
- [`libcurl`](https://curl.se/libcurl/)
- [`cJSON`](https://github.com/DaveGamble/cJSON)

Simply run:
```bash
sudo apt update
sudo apt install libfuse3-dev libcurl4-openssl-dev libcjson-dev
```

### 3) Make it
```bash
$ make
```
---

## Quick Start – Server backend
/* To host your own server */

#### 1) Python dependencies

Install with:
```bash
pip install -r requirements.txt
```

#### 2) Environment variables (.env)

```bash
DATABASE_URL="postgresql://user:pass@localhost/disfs"
TOKEN="YOUR_DISCORD_BOT_TOKEN"
# Feel free to change url, though other modifications may be necessary for it to work
# Get a bot token from: https://discord.com/developers/
```
> *Sadly, details on Discord Bot's setup won't be provided by me*  

#### 3) Configure server/_config.py
```python
NOTIFICATIONS_ID = YOUR_NOTIFICATION_CHANNEL_ID  # Your Discord channel ID
VAULT_IDS = [YOUR_NOTIFICATION_CHANNEL_ID]       # Your Discord channel ID
```

#### 4) Send it

```bash
python3 -m server.main
```

Type `exit`, `q`, `quit` to exit server
`dog` to send a dog gif to notification server

---

## Quick Start – Database
DISFS requires a PostgreSQL database

### 1. Install PostgreSQL
```bash
sudo apt install postgresql postgresql-contrib
```

### 2. Set up the database
Create a user with:
```
sudo -i -u postgres psql << EOF
CREATE USER admin WITH PASSWORD 'password' CREATEDB;
CREATE DATABASE disfs_db OWNER admin;
\q
EOF
```

### 3. Configure `.env`:
Create a .env file in the project root. Set up the username and password like you have in step 2.
```bash
# Modify the URL
DATABASE_URL="postgres://admin:password@localhost:5432/disfs_db"
DISCORD_TOKEN="your_discord_bot_token_here"
```

That's it! When you run the server, the necessary tables should be automatically created.

Helper command if needed ↓
```bash
# Deletes all rows from db quickly and simply
psql "$DATABASE_URL" -c "TRUNCATE users, nodes, node_closure, file_chunks RESTART IDENTITY CASCADE;"
```
--- 
### How to use
By default, DISFS always mounts to DISFS/mnt
```bash
$ make  # makes and mounts fuse
$ make mount    # mounts fuse
$ make unmount  # unmounts fuse
$ make clean    # cleans and unmounts
```
Use `.command` prefix to access commands.  
`cat` -> do said thing   
`ls` -> see command list   


#### Trying it yourself
By default there should be the user William:
```bash
$ ls mnt/.command   # Command list
$ cat mnt/.command/register/Arthur  # Register user 'Arthur'
$ cat mnt/.command/pong # Logout
$ cat mnt/.command/serverip/192.0.2.123 # Change IP if the server isn't on local
```
Once mounted and logged in, use it as if a standard directory.

Run tests and test features quickly with:  
```bash
$ make test
```

### Changelog

v0.1: First working release (basic FUSE FS + Discord backend).  
v0.2: Add first version of cache control (local cache directory + eviction logic).  
v0.3: Add blocking to reads, test cases and fix a TON of bugs. *WIP*  

---

### Future plans (TODO)

#### Probably, hopefully soon:
- Linting
- Lots and lots and lots of testcases.
- Unify bracket indentation on functions
- Don't call getenv("HOME") in every BUILD_CACHE_PATH
- Test sure pipes and redirection thoroughly

#### Probably not soon:
- Change logging logic (currently just writes to .txt files lol)
- Change cache manager to use a hashmap-linkedlist
- Make Discord Bot be more wary of API limits
- Add more Discord Bots, relieving API limits
- Add a threadpool or worker tasks to handle multiple user requests
- Add keys to wait_ready, ensuring that download requests are only valid when the file is fully ready
- Add security to account logins and sessions, rather than just an integer
- Safety nets to try and prevent manual malicious http requests
- Some sort of encryption would be nice

Any and all input is greatly appreciated! Contact me at williamclivestevens1205@gmail.com or submit a pull request.

---

### Credits
*Prof. Jserv, for pushing me to go above and beyond.*  
*Peers of NCKU CSIE for their valuable input.*  

