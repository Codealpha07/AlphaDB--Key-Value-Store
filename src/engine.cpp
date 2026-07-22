#include "engine.hpp"

#include <chrono>
#include <stdexcept>
#include <vector>

#include <fcntl.h>     // open

#ifdef _WIN32
#include <io.h>
#include <sys/stat.h>

#ifndef O_RDWR
#define O_RDWR _O_RDWR
#endif
#ifndef O_CREAT
#define O_CREAT _O_CREAT
#endif

static inline int64_t file_pread(int fd, void* buf, size_t count, int64_t offset) {
    int64_t old_pos = _lseeki64(fd, 0, SEEK_CUR);
    _lseeki64(fd, offset, SEEK_SET);
    int r = _read(fd, buf, static_cast<unsigned int>(count));
    _lseeki64(fd, old_pos, SEEK_SET);
    return r;
}

static inline int64_t file_pwrite(int fd, const void* buf, size_t count, int64_t offset) {
    int64_t old_pos = _lseeki64(fd, 0, SEEK_CUR);
    _lseeki64(fd, offset, SEEK_SET);
    int r = _write(fd, buf, static_cast<unsigned int>(count));
    _lseeki64(fd, old_pos, SEEK_SET);
    return r;
}

static inline int file_fsync(int fd) {
    return _commit(fd);
}

static inline int file_ftruncate(int fd, int64_t length) {
    return _chsize_s(fd, length);
}

static inline int file_close(int fd) {
    return _close(fd);
}

static inline int64_t file_lseek(int fd, int64_t offset, int whence) {
    return _lseeki64(fd, offset, whence);
}

static inline int file_open(const char* path, int flags, int mode) {
    int fd = -1;
    _sopen_s(&fd, path, flags | _O_BINARY, _SH_DENYNO, _S_IREAD | _S_IWRITE);
    return fd;
}
#else
#include <unistd.h>    // pread, pwrite, fsync, ftruncate, close, lseek
static inline int file_open(const char* path, int flags, int mode) {
    return ::open(path, flags, mode);
}
static inline int file_close(int fd) {
    return ::close(fd);
}
static inline int64_t file_lseek(int fd, int64_t offset, int whence) {
    return ::lseek(fd, offset, whence);
}
static inline int64_t file_pread(int fd, void* buf, size_t count, int64_t offset) {
    return ::pread(fd, buf, count, offset);
}
static inline int64_t file_pwrite(int fd, const void* buf, size_t count, int64_t offset) {
    return ::pwrite(fd, buf, count, offset);
}
static inline int file_fsync(int fd) {
    return ::fsync(fd);
}
static inline int file_ftruncate(int fd, int64_t length) {
    return ::ftruncate(fd, length);
}
#endif

namespace ledgerdb {

// ---------------------------------------------------------------------------
// Construction / destruction
// ---------------------------------------------------------------------------
Engine::Engine(const std::string& path, bool fsync_on_write)
    : path_(path), fsync_on_write_(fsync_on_write) {
    // O_RDWR: we both read and write.  O_CREAT: create it if missing.
    fd_ = file_open(path_.c_str(), O_RDWR | O_CREAT, 0644);
    if (fd_ < 0) throw std::runtime_error("could not open data file: " + path_);
    recover();  // rebuild the in-memory index from whatever is already on disk
}

Engine::~Engine() {
    if (fd_ >= 0) file_close(fd_);
}

uint64_t Engine::now_micros() {
    using namespace std::chrono;
    return duration_cast<microseconds>(system_clock::now().time_since_epoch()).count();
}

// ---------------------------------------------------------------------------
// recover() — the crash-recovery / startup path.
//
// We read the entire data file into memory and decode records one after
// another. For each record we record WHERE its value lives, so a GET later can
// jump straight to it. Because records are in write order, a later record for
// the same key simply overwrites the earlier entry — the newest write wins.
//
// If decode() ever fails partway through, it means the file's tail is a torn,
// half-written record (a crash happened mid-write). We truncate the file back
// to the last good record and carry on. This is why the store never gets
// corrupted by a power loss: at worst we lose the single in-flight write.
// ---------------------------------------------------------------------------
void Engine::recover() {
    const int64_t file_size = file_lseek(fd_, 0, SEEK_END);
    if (file_size <= 0) { write_offset_ = 0; return; }

    std::vector<uint8_t> buf(static_cast<std::size_t>(file_size));
    if (file_pread(fd_, buf.data(), buf.size(), 0) != static_cast<int64_t>(buf.size()))
        throw std::runtime_error("failed to read data file during recovery");

    std::size_t pos = 0;
    while (pos < buf.size()) {
        DecodeResult r = decode(buf.data() + pos, buf.size() - pos);
        if (!r.ok) break;  // torn tail -> stop; everything before pos is valid

        if (r.record.tombstone) {
            keydir_.erase(r.record.key);
        } else {
            Entry e;
            e.value_size   = static_cast<uint32_t>(r.record.value.size());
            e.value_offset = pos + HEADER_SIZE + r.record.key.size();
            e.timestamp    = r.record.timestamp;
            keydir_[r.record.key] = e;
        }
        pos += r.bytes_consumed;
    }

    // Drop any torn bytes at the end so future appends start from clean data.
    if (pos < buf.size()) {
        if (file_ftruncate(fd_, static_cast<int64_t>(pos)) != 0)
            throw std::runtime_error("failed to truncate torn tail during recovery");
    }
    write_offset_ = pos;
}

// ---------------------------------------------------------------------------
// put() — write path
// ---------------------------------------------------------------------------
void Engine::put(const std::string& key, const std::string& value) {
    std::lock_guard<std::mutex> lock(mu_);

    Record rec;
    rec.timestamp = now_micros();
    rec.key = key;
    rec.value = value;
    rec.tombstone = false;

    std::vector<uint8_t> bytes = encode(rec);

    // Append at the current end of file. We use pwrite with an explicit offset
    // (rather than a shared file cursor) so the offset math is fully under our
    // control — the same reason reads use pread.
    if (file_pwrite(fd_, bytes.data(), bytes.size(), static_cast<int64_t>(write_offset_)) !=
        static_cast<int64_t>(bytes.size()))
        throw std::runtime_error("pwrite failed in put()");

    // fsync forces the OS to flush the write from its in-memory page cache down
    // to the physical disk. Safe but slow. Turning it off is faster but risks
    // losing recent writes on a power failure — the classic durability tradeoff.
    if (fsync_on_write_) file_fsync(fd_);

    Entry e;
    e.value_size   = static_cast<uint32_t>(value.size());
    e.value_offset = write_offset_ + HEADER_SIZE + key.size();
    e.timestamp    = rec.timestamp;
    keydir_[key] = e;

    write_offset_ += bytes.size();
}

// ---------------------------------------------------------------------------
// del() — append a tombstone, then drop the key from the index.
//
// We do NOT erase anything from the file. The old value bytes still sit in the
// log; they only disappear later during compaction (a talking point, not built
// here). Removing the key from the in-memory index is what makes GET return
// "not found".
// ---------------------------------------------------------------------------
bool Engine::del(const std::string& key) {
    std::lock_guard<std::mutex> lock(mu_);

    auto it = keydir_.find(key);
    if (it == keydir_.end()) return false;  // nothing to delete

    Record rec;
    rec.timestamp = now_micros();
    rec.key = key;
    rec.tombstone = true;

    std::vector<uint8_t> bytes = encode(rec);
    if (file_pwrite(fd_, bytes.data(), bytes.size(), static_cast<int64_t>(write_offset_)) !=
        static_cast<int64_t>(bytes.size()))
        throw std::runtime_error("pwrite failed in del()");
    if (fsync_on_write_) file_fsync(fd_);

    write_offset_ += bytes.size();
    keydir_.erase(it);
    return true;
}

// ---------------------------------------------------------------------------
// get() — read path
// ---------------------------------------------------------------------------
std::optional<std::string> Engine::get(const std::string& key) {
    std::lock_guard<std::mutex> lock(mu_);

    auto it = keydir_.find(key);
    if (it == keydir_.end()) return std::nullopt;  // not in the index -> absent

    const Entry e = it->second;
    std::string value(e.value_size, '\0');

    // pread reads at an explicit offset without moving any shared file cursor,
    // so it is safe to call from multiple threads on the same fd.
    if (file_pread(fd_, value.data(), e.value_size, static_cast<int64_t>(e.value_offset)) !=
        static_cast<int64_t>(e.value_size))
        throw std::runtime_error("pread failed in get()");

    return value;
}

std::size_t Engine::size() {
    std::lock_guard<std::mutex> lock(mu_);
    return keydir_.size();
}

} // namespace ledgerdb
