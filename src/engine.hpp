#pragma once
//
// engine.hpp — the storage engine (the "Bitcask core").
//
// This is the heart of the database. It ties together:
//   * one append-only data file on disk (the log), and
//   * one in-memory hash table (the "KeyDir") that maps each key to the exact
//     byte offset of its latest value in that file.
//
// Read  = one hash lookup (find the offset) + one pread (read the value).
// Write = append a record to the file + update the hash table.
// Restart = scan the file once and rebuild the hash table.
//
#include "record.hpp"
#include <cstdint>
#include <mutex>
#include <optional>
#include <string>
#include <unordered_map>

namespace ledgerdb {

class Engine {
public:
    // Open (or create) a store backed by the file at `path`. If the file
    // already exists, its contents are replayed to rebuild the in-memory index.
    explicit Engine(const std::string& path, bool fsync_on_write = true);
    ~Engine();

    // Look up a key. Returns the value, or std::nullopt if it doesn't exist
    // (or was deleted).
    std::optional<std::string> get(const std::string& key);

    // Insert or overwrite a key.
    void put(const std::string& key, const std::string& value);

    // Delete a key. Returns true if the key existed.
    bool del(const std::string& key);

    // Number of live keys currently in the index (handy for tests/status).
    std::size_t size();

private:
    // One entry in the KeyDir. Note it does NOT hold the value — only WHERE the
    // value lives on disk. This is why keys must fit in RAM but values need not.
    struct Entry {
        uint32_t value_size;    // how many bytes to read
        uint64_t value_offset;  // byte offset of the value within the file
        uint64_t timestamp;     // when it was written
    };

    void recover();             // rebuild the KeyDir by scanning the file
    static uint64_t now_micros();

    std::string   path_;
    int           fd_ = -1;         // the one file descriptor for the data file
    uint64_t      write_offset_ = 0; // where the next append will go (= file size)
    bool          fsync_on_write_;

    std::unordered_map<std::string, Entry> keydir_;

    // A SINGLE lock guards every operation. Simple and obviously correct.
    // (The natural optimization — a reader/writer lock so GETs run in parallel —
    // is discussed in the study guide but intentionally NOT built here.)
    std::mutex mu_;
};

} // namespace ledgerdb
