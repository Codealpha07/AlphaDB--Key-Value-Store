//
// test_engine.cpp — small, dependency-free tests for the storage engine.
//
// No test framework on purpose: it keeps the project easy to read and build.
// Each CHECK prints PASS/FAIL; the program exits non-zero if anything failed.
//
#include "../src/engine.hpp"
#include "../src/record.hpp"

#include <cstdio>
#include <cstdlib>
#include <fstream>
#include <string>

static int g_failures = 0;

#define CHECK(cond)                                                        \
    do {                                                                   \
        if (cond) {                                                        \
            std::printf("  PASS: %s\n", #cond);                            \
        } else {                                                           \
            std::printf("  FAIL: %s   (line %d)\n", #cond, __LINE__);      \
            ++g_failures;                                                  \
        }                                                                  \
    } while (0)

// Each test uses a fresh file so runs don't interfere.
static std::string fresh_file(const char* name) {
#ifdef _WIN32
    std::string path = std::string("ledgerdb_test_") + name + ".data";
#else
    std::string path = std::string("/tmp/ledgerdb_test_") + name + ".data";
#endif
    std::remove(path.c_str());
    return path;
}

// -- Test 1: basic put / get / overwrite / delete --------------------------
static void test_basic() {
    std::printf("[test_basic]\n");
    std::string f = fresh_file("basic");
    ledgerdb::Engine db(f);

    CHECK(!db.get("missing").has_value());       // absent key -> nullopt

    db.put("name", "aarush");
    CHECK(db.get("name").value() == "aarush");   // read back what we wrote

    db.put("name", "shynii");                    // overwrite
    CHECK(db.get("name").value() == "shynii");   // newest value wins

    db.put("lang", "c++ is great");              // value with spaces
    CHECK(db.get("lang").value() == "c++ is great");

    CHECK(db.del("name") == true);               // delete existing
    CHECK(!db.get("name").has_value());          // now absent
    CHECK(db.del("name") == false);              // deleting again -> false
    CHECK(db.size() == 1);                        // only "lang" remains
}

// -- Test 2: durability across a restart (crash recovery) ------------------
static void test_persistence() {
    std::printf("[test_persistence]\n");
    std::string f = fresh_file("persist");
    {
        ledgerdb::Engine db(f);                    // open #1
        db.put("a", "1");
        db.put("b", "2");
        db.put("a", "11");                       // overwrite
        db.del("b");                             // tombstone
    }                                            // db destroyed = process "exits"

    ledgerdb::Engine db2(f);                       // open #2: replays the log
    CHECK(db2.get("a").value() == "11");         // newest value survived
    CHECK(!db2.get("b").has_value());            // deletion survived
    CHECK(db2.size() == 1);
}

// -- Test 3: CRC catches a corrupted / torn record -------------------------
static void test_crc_detects_corruption() {
    std::printf("[test_crc_detects_corruption]\n");

    ledgerdb::Record rec;
    rec.timestamp = 42;
    rec.key = "k";
    rec.value = "v";
    auto bytes = ledgerdb::encode(rec);

    // A clean record decodes fine.
    auto ok = ledgerdb::decode(bytes.data(), bytes.size());
    CHECK(ok.ok == true);
    CHECK(ok.record.value == "v");

    // Flip one byte in the value -> CRC must reject it.
    bytes.back() ^= 0xFF;
    auto bad = ledgerdb::decode(bytes.data(), bytes.size());
    CHECK(bad.ok == false);
}

// -- Test 4: a torn tail is dropped on recovery ----------------------------
static void test_torn_tail_recovery() {
    std::printf("[test_torn_tail_recovery]\n");
    std::string f = fresh_file("torn");
    {
        ledgerdb::Engine db(f);
        db.put("good", "value");
    }
    // Simulate a crash mid-write: append a few garbage bytes to the file.
    {
        std::ofstream out(f, std::ios::binary | std::ios::app);
        const char junk[] = {0x01, 0x02, 0x03, 0x04, 0x05};
        out.write(junk, sizeof(junk));
    }
    // Recovery should skip the torn tail and still see the good key.
    ledgerdb::Engine db2(f);
    CHECK(db2.get("good").value() == "value");
    CHECK(db2.size() == 1);
    // And it should be usable afterwards (tail was truncated cleanly).
    db2.put("after", "ok");
    CHECK(db2.get("after").value() == "ok");
}

int main() {
    test_basic();
    test_persistence();
    test_crc_detects_corruption();
    test_torn_tail_recovery();

    std::printf("\n%s\n", g_failures == 0 ? "ALL TESTS PASSED" : "SOME TESTS FAILED");
    return g_failures == 0 ? 0 : 1;
}
