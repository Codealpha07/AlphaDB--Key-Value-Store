#include "record.hpp"
#include <cstring>  // std::memcpy

namespace alphadb {

// ---------------------------------------------------------------------------
// CRC32 — table-based implementation of the standard polynomial 0xEDB88320.
//
// We build a 256-entry lookup table once, then process one byte at a time.
// You don't need to memorize the math for an interview; the point to know is:
// a CRC is a checksum that detects accidental corruption. If a single byte of
// the record changed, the CRC we recompute on read won't match the CRC we
// stored on write, and we know the record is bad.
// ---------------------------------------------------------------------------
static uint32_t crc_table[256];
static bool     crc_table_ready = false;

static void build_crc_table() {
    for (uint32_t i = 0; i < 256; ++i) {
        uint32_t c = i;
        for (int k = 0; k < 8; ++k)
            c = (c & 1) ? (0xEDB88320u ^ (c >> 1)) : (c >> 1);
        crc_table[i] = c;
    }
    crc_table_ready = true;
}

uint32_t crc32(const uint8_t* data, std::size_t len) {
    if (!crc_table_ready) build_crc_table();
    uint32_t c = 0xFFFFFFFFu;
    for (std::size_t i = 0; i < len; ++i)
        c = crc_table[(c ^ data[i]) & 0xFF] ^ (c >> 8);
    return c ^ 0xFFFFFFFFu;
}

// ---------------------------------------------------------------------------
// encode — Record -> bytes
// ---------------------------------------------------------------------------
std::vector<uint8_t> encode(const Record& rec) {
    const uint32_t key_size   = static_cast<uint32_t>(rec.key.size());
    const uint32_t value_size = rec.tombstone
                                    ? TOMBSTONE
                                    : static_cast<uint32_t>(rec.value.size());

    const std::size_t body_len = 8 + 4 + 4 + rec.key.size() +
                                 (rec.tombstone ? 0 : rec.value.size());

    std::vector<uint8_t> out(4 + body_len);  // 4 bytes for the CRC up front
    uint8_t* p = out.data() + 4;             // body starts after the CRC slot

    std::memcpy(p, &rec.timestamp, 8); p += 8;
    std::memcpy(p, &key_size,      4); p += 4;
    std::memcpy(p, &value_size,    4); p += 4;
    std::memcpy(p, rec.key.data(), rec.key.size()); p += rec.key.size();
    if (!rec.tombstone) {
        std::memcpy(p, rec.value.data(), rec.value.size());
        p += rec.value.size();
    }

    // CRC covers the body (everything after the 4-byte CRC field).
    const uint32_t crc = crc32(out.data() + 4, body_len);
    std::memcpy(out.data(), &crc, 4);

    return out;
}

// ---------------------------------------------------------------------------
// decode — bytes -> Record  (reads exactly one record from the front of buf)
// ---------------------------------------------------------------------------
DecodeResult decode(const uint8_t* buf, std::size_t available) {
    DecodeResult res;

    // Not even a full header yet -> can't read anything.
    if (available < HEADER_SIZE) return res;

    uint32_t stored_crc, key_size, value_size;
    uint64_t timestamp;
    std::memcpy(&stored_crc, buf + 0,  4);
    std::memcpy(&timestamp,  buf + 4,  8);
    std::memcpy(&key_size,   buf + 12, 4);
    std::memcpy(&value_size, buf + 16, 4);

    const bool tombstone = (value_size == TOMBSTONE);
    const std::size_t value_bytes = tombstone ? 0 : value_size;
    const std::size_t record_len  = HEADER_SIZE + key_size + value_bytes;

    // The full record isn't in the buffer yet (or the file was truncated
    // mid-record by a crash). Either way we stop here.
    if (available < record_len) return res;

    // Recompute the CRC over the body and compare. A mismatch means the record
    // is corrupt / half-written — we treat this as the end of valid data.
    const uint32_t actual_crc = crc32(buf + 4, record_len - 4);
    if (actual_crc != stored_crc) return res;

    res.record.timestamp = timestamp;
    res.record.tombstone = tombstone;
    res.record.key.assign(reinterpret_cast<const char*>(buf + HEADER_SIZE), key_size);
    if (!tombstone)
        res.record.value.assign(
            reinterpret_cast<const char*>(buf + HEADER_SIZE + key_size), value_bytes);

    res.bytes_consumed = record_len;
    res.ok = true;
    return res;
}

} // namespace alphadb
