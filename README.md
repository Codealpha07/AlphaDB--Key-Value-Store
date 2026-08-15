# LedgerDB

A small, persistent **key-value store** written in C++17. Writes are appended
to a log file on disk; an in-memory hash table maps each key to the byte offset
of its latest value; on restart the log is replayed to rebuild the index. It
serves multiple clients concurrently over TCP.

This is a learning project modeled on **Bitcask** (the storage engine behind
Riak). It is intentionally small so that every line can be understood and
explained.

## What it does

- `SET`, `GET`, `DEL` over a plain-text TCP protocol
- Durable across restarts and **crash-safe** (CRC-checked records; a torn tail
  from a power loss is detected and dropped on recovery)
- Handles many concurrent clients (one thread per connection, a single mutex
  guarding the engine)

## Build

```bash
# with CMake
cmake -B build && cmake --build build
./build/ledgerdb                 # data file ./ledgerdb.data, port 5555

# or a one-line g++ build
g++ -std=c++17 -pthread src/main.cpp src/server.cpp src/record.cpp src/engine.cpp -o ledgerdb
./ledgerdb mydata.data 5555
```

## Try it

```bash
# in one terminal
./ledgerdb

# in another
nc localhost 5555
SET name aarush
GET name          # -> aarush
DEL name
GET name          # -> (nil)
PING              # -> +PONG
```

## Run the tests

```bash
g++ -std=c++17 -pthread tests/test_engine.cpp src/record.cpp src/engine.cpp -o ledgerdb_tests
./ledgerdb_tests
```

Tests cover put/get/overwrite/delete, durability across a restart, CRC catching
a corrupted record, and torn-tail recovery.

## Source layout

| File | Responsibility |
|------|----------------|
| `src/record.{hpp,cpp}` | On-disk record format + CRC32 (encode/decode) |
| `src/engine.{hpp,cpp}` | The Bitcask core: KeyDir index, get/put/del, crash recovery |
| `src/server.{hpp,cpp}` | TCP server + text protocol |
| `src/main.cpp` | Startup / wiring |
| `tests/test_engine.cpp` | Unit tests |

## Deliberately not built (discussion / future work)

Due to certain trade off decisions in this project ; focusing to come to optimal trade off points. :)

Compaction (merging the log to reclaim space), hint files for faster startup,
multiple data files with rotation, a reader-writer lock and a thread pool, and
range queries.
