# DisFS
A distributed file system with a Python backend and FUSE-based C frontend.

## 📦 Features
- Chunked file uploads via Python API
- Discord integration (uploads, notifications)
- FUSE-based virtual filesystem in C
- JSON handling with cJSON
- Network requests with libcurl
- blah blah blah... TBD.

---

## 🐍 Python Setup

Install Python dependencies with:
```bash
pip install -r requirements.txt
```

---

## 🧱 C Dependencies

The C side uses:

- [`libfuse3`](https://github.com/libfuse/libfuse)
- [`libcurl`](https://curl.se/libcurl/)
- [`cJSON`](https://github.com/DaveGamble/cJSON)

### Install on Debian/Ubuntu:

```bash
sudo apt update
sudo apt install libfuse3-dev libcurl4-openssl-dev libcjson-dev
```

---


**Note: uses ext4 format, thus is made for LINUX**
type = 1 -> file
type = 2 -> directory

Uses 'Closure tables' in PostgreSQL.


logical_path -> virtual file path as seen by the FUSE user
local_path -> ACTUAL path on local filesystem


file cache stored at -> HOME/.cache/disfs/{user_id}/{local_path}