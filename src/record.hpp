#pragma once
//
// record.hpp — the on-disk format of a single write.
//
// Every SET or DELETE we ever do is turned into ONE "record" and appended to
// the end of the data file. Records are never modified in place. This is what
// "log-structured storage" means.
//
// On-disk layout of one record (all integers are stored in the machine's
// native byte order, which is fine because we only ever read the file back on
// the same machine that wrote it):
//
//   +---------+-----------+----------+------------+--------+----------+
//   |  crc32  | timestamp | key_size | value_size |  key   |  value   |
//   | 4 bytes |  8 bytes  |  4 bytes |   4 bytes   | key_sz | value_sz |
//   +---------+-----------+----------+------------+--------+----------+
//   |<-- CRC is computed over everything to the right of it -------->|
//
// The 20-byte header (4 + 8 + 4 + 4) is a FIXED size, so a reader can always
// read the header first, learn how long the key and value are, and then read
// exactly that many more bytes. "Fixed header, then variable body" is how most
// real file and network formats work.
//
#include <cstdint>
#include <string>
#include <vector>

namespace alphadb {

// value_size is set to this sentinel when the record is a "tombstone" — our
// marker for a deleted key. A tombstone has a key but no value bytes.
constexpr uint32_t TOMBSTONE = 0xFFFFFFFFu;

// crc(4) + timestamp(8) + key_size(4) + value_size(4)
constexpr std::size_t HEADER_SIZE = 20;

// A record in memory, before/after it is turned into bytes.
struct Record {
    uint64_t    timestamp = 0;   // when it was written (helps recovery pick the newest)
    std::string key;
    std::string value;           // empty when tombstone == true
    bool        tombstone = false;
};

// CRC32 (the standard zlib/PNG polynomial). A checksum that changes if ANY
// byte of the data changes — we use it to detect a half-written record left
// behind by a crash.
uint32_t crc32(const uint8_t* data, std::size_t len);

// Turn a Record into the exact bytes we append to the file.
std::vector<uint8_t> encode(const Record& rec);

// Result of trying to read one record out of a byte buffer.
struct DecodeResult {
    Record      record;
    std::size_t bytes_consumed = 0;  // how many bytes this record occupied
    bool        ok = false;          // false = incomplete OR corrupt (CRC failed)
};

// Try to decode ONE record from the front of `buf` (which holds `available`
// bytes). Returns ok=false if there aren't enough bytes yet, or if the CRC
// doesn't match (which is how we detect a torn record after a crash).
DecodeResult decode(const uint8_t* buf, std::size_t available);

} // namespace alphadb
