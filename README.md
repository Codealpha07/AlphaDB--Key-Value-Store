# AlphaDB

[![CI](https://github.com/Codealpha07/AlphaDB--Key-Value-Store/actions/workflows/ci.yml/badge.svg)](https://github.com/Codealpha07/AlphaDB--Key-Value-Store/actions/workflows/ci.yml)

A small, persistent **key-value store** written in C++17, modeled on
**Bitcask** (the storage engine behind Riak). Every write is appended to a log
file on disk; an in-memory hash table maps each key to the byte offset of its
latest value; on restart the log is replayed to rebuild the index. It serves
multiple clients concurrently over TCP.

It is intentionally small so that every line can be understood and explained.
The [known limitations](#known-limitations) below are listed on purpose: fixing
them is the current work.

## What it does

- `SET`, `GET`, `DEL` and `PING` over a plain-text TCP protocol.
- Persists across restarts: each write is `fsync`ed before the client gets `+OK`,
  and the log is replayed at startup.
- Detects a torn record at the end of the log. If the process or machine dies
  mid-append, the partly written record fails its CRC32 check at startup and is
  dropped.
- Serves concurrent clients with one thread per connection and a single mutex
  around the engine.

A read is one hash lookup plus one positional disk read (`pread`). Keys must fit
in memory; values stay on disk.

## On-disk format

One append-only file. Each record:

| Field        | Bytes        | Notes                                   |
|--------------|--------------|-----------------------------------------|
| `crc32`      | 4            | Covers every byte after it in the record |
| `timestamp`  | 8            | Microseconds since the epoch            |
| `key_size`   | 4            |                                         |
| `value_size` | 4            | `0xFFFFFFFF` marks a delete (tombstone) |
| `key`        | `key_size`   |                                         |
| `value`      | `value_size` | Absent for a tombstone                  |

## Build

```bash
cmake -B build
cmake --build build --config Release
```

The server binary is `build/alphadb` (or `build\Release\alphadb.exe` with Visual
Studio). It uses `./alphadb.data` and port 5555 by default:

```bash
./build/alphadb                  # defaults
./build/alphadb mydata.data 5555 # explicit data file and port
```

Without CMake:

```bash
g++ -std=c++17 -pthread src/main.cpp src/server.cpp src/record.cpp src/engine.cpp -o alphadb
```

## Try it

```bash
# in one terminal
./build/alphadb

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
ctest --test-dir build -C Release --output-on-failure
```

The tests cover put, get, overwrite and delete; persistence across a restart; the
CRC rejecting a corrupted record; and recovery from a torn tail. The torn tail is
simulated by appending bytes to the file. Nothing yet kills a process, injects an
I/O error, or exercises the server or concurrency.

CI builds and runs the tests on Linux and Windows on every push, plus a Linux
build with AddressSanitizer and UndefinedBehaviorSanitizer.

## Known limitations

These are known bugs or gaps, not design goals.

- **Mid-log damage loses data.** Recovery stops at the first record that fails to
  decode and truncates the file there, so damage in the middle of the log loses
  every record after it. Opening a file that is not an AlphaDB log truncates it
  to zero bytes.
- **Reads do not verify the CRC.** Corrupted bytes on disk are returned as a
  valid value.
- **A failed `fsync` is not detected.** The client still receives `+OK`.
- **No file lock.** Two processes must not open the same data file.
- **Logs over 2 GiB cannot be reopened.** Recovery reads the whole file in one
  call and needs memory equal to its size.
- **The protocol is ambiguous.** A missing key and the stored value `(nil)` give
  the same reply, keys cannot contain spaces, and values cannot contain newlines.
- **One client can stop the server.** On Linux, a client that disconnects without
  reading its replies can kill the process with `SIGPIPE`, and any storage error
  in a client thread terminates the whole server.
- **Writes do not scale with clients.** The single mutex is held across `fsync`,
  so concurrent writers wait for each other's flushes and readers wait too.
- **The log grows forever.** There is no compaction yet.

## Source layout

| File | Responsibility |
|------|----------------|
| `src/record.{hpp,cpp}` | On-disk record format and CRC32 (encode/decode) |
| `src/engine.{hpp,cpp}` | The Bitcask core: KeyDir index, get/put/del, recovery |
| `src/server.{hpp,cpp}` | TCP server and text protocol |
| `src/main.cpp` | Startup and wiring |
| `tests/test_engine.cpp` | Unit tests |

## Not built, and why

- **Compaction, multiple data files and hint files.** Correct compaction needs
  an ordering of records across files and a safe rule for dropping tombstones.
  It comes after recovery is fixed.
- **Reader-writer lock.** It would not help while the lock is held across
  `fsync`; the real fix is moving disk I/O out from under the lock.
- **Thread pool.** Thread creation is not the bottleneck; the flush under the
  lock is.
- **Range queries.** The hash index is unordered; range scans need an ordered
  index, which is a different engine design.
