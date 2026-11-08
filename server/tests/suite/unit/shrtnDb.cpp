// Unit ShrtnDb Test Suite
// Verifies the table behind the server: inserts, lookups, click counting,
// the public listing, and that links survive a restart.
//
// Covers:
// - A new code inserts; the same code a second time reports CodeTaken
// - findByCode() returns the stored URL, and reports a missing code as not found
// - incrementClickCount() adds one per call and is visible through metadata
// - listPublic() leaves out private links, while getMetadataByCode() still finds them
// - Data written by one ShrtnDb is loaded by a new one on the same directory
// - Concurrent inserts of the same code produce exactly one winner

#include <support/framework.h>
#include <support/tempDir.h>

#include "ShrtnDb.h"

#include <atomic>
#include <string>
#include <thread>
#include <vector>

namespace {

using MiniDB::Common::Status;
using Shrtn::ShrtnDb;

constexpr const char* kNow = "2026-08-12T03:14:07Z";

} // namespace

// Verifies a free code inserts and a repeat of it is refused as taken.
static void insert_then_collision() {
    TempDir dir;
    ShrtnDb db(dir.str());
    CHK(db.init() == Status::OK);

    CHK(db.tryInsertUrl("abc123", "https://example.com", kNow, false) ==
        ShrtnDb::InsertOutcome::Inserted);
    CHK(db.tryInsertUrl("abc123", "https://other.example", kNow, false) ==
        ShrtnDb::InsertOutcome::CodeTaken);
}

// Verifies lookups return the stored URL and unknown codes are not found.
static void find_by_code() {
    TempDir dir;
    ShrtnDb db(dir.str());
    CHK(db.init() == Status::OK);
    CHK(db.tryInsertUrl("abc123", "https://example.com", kNow, false) ==
        ShrtnDb::InsertOutcome::Inserted);

    const auto hit = db.findByCode("abc123");
    CHK(hit.found);
    CHK(hit.originalUrl == "https://example.com");

    CHK(!db.findByCode("nope00").found);
}

// Verifies each increment adds exactly one click.
static void counts_clicks() {
    TempDir dir;
    ShrtnDb db(dir.str());
    CHK(db.init() == Status::OK);
    CHK(db.tryInsertUrl("abc123", "https://example.com", kNow, false) ==
        ShrtnDb::InsertOutcome::Inserted);

    const auto hit = db.findByCode("abc123");
    CHK(hit.found);
    CHK(db.incrementClickCount(hit.id) == Status::OK);
    CHK(db.incrementClickCount(hit.id) == Status::OK);

    const auto meta = db.getMetadataByCode("abc123");
    CHK(meta.found);
    CHK(meta.entry.clickCount == 2);
}

// Verifies private links stay out of the shared list but remain reachable by code.
static void private_links_are_unlisted() {
    TempDir dir;
    ShrtnDb db(dir.str());
    CHK(db.init() == Status::OK);
    CHK(db.tryInsertUrl("pub001", "https://example.com/a", kNow, false) ==
        ShrtnDb::InsertOutcome::Inserted);
    CHK(db.tryInsertUrl("prv001", "https://example.com/b", kNow, true) ==
        ShrtnDb::InsertOutcome::Inserted);

    const auto listed = db.listPublic();
    CHK(listed.size() == 1);
    CHK(listed[0].code == "pub001");

    const auto meta = db.getMetadataByCode("prv001");
    CHK(meta.found);
    CHK(meta.entry.isPrivate);
    CHK(db.findByCode("prv001").found);
}

// Verifies a second ShrtnDb on the same directory sees the first one's data.
static void data_survives_restart() {
    TempDir dir;
    {
        ShrtnDb db(dir.str());
        CHK(db.init() == Status::OK);
        CHK(db.tryInsertUrl("abc123", "https://example.com", kNow, false) ==
            ShrtnDb::InsertOutcome::Inserted);
        const auto hit = db.findByCode("abc123");
        CHK(db.incrementClickCount(hit.id) == Status::OK);
    }

    ShrtnDb reopened(dir.str());
    CHK(reopened.init() == Status::OK);

    const auto meta = reopened.getMetadataByCode("abc123");
    CHK(meta.found);
    CHK(meta.entry.originalUrl == "https://example.com");
    CHK(meta.entry.clickCount == 1);
}

// Verifies the check-and-insert is one critical section: racing on one code yields one winner.
static void concurrent_inserts_of_one_code() {
    TempDir dir;
    ShrtnDb db(dir.str());
    CHK(db.init() == Status::OK);

    constexpr int kThreads = 8;
    std::atomic<int> inserted{0};
    std::atomic<int> taken{0};

    std::vector<std::thread> threads;
    for (int i = 0; i < kThreads; ++i) {
        threads.emplace_back([&] {
            const auto outcome = db.tryInsertUrl("race01", "https://example.com", kNow, false);
            if (outcome == ShrtnDb::InsertOutcome::Inserted)
                ++inserted;
            else if (outcome == ShrtnDb::InsertOutcome::CodeTaken)
                ++taken;
        });
    }
    for (auto& t : threads)
        t.join();

    CHK(inserted == 1);
    CHK(taken == kThreads - 1);
}

static void run_tests() {
    RUN(insert_then_collision);
    RUN(find_by_code);
    RUN(counts_clicks);
    RUN(private_links_are_unlisted);
    RUN(data_survives_restart);
    RUN(concurrent_inserts_of_one_code);
}

REGISTER_TEST_SUITE();
