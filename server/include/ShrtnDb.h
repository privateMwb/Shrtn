/**
 * @file            ShrtnDb.h
 *
 * @date            2026-8-12
 *
 * @version         0.1.0
 *
 * @copyright       Copyright (c) 2026 privateMwb
 *                  All rights reserved.
 *                  https://github.com/privateMwb/Shrtn
 *
 * @attention       This source is released under the MIT license
 *                  SPDX-License-Identifier: MIT
 *                  <http://opensource.org/licenses/MIT>
 */

#pragma once

// clang-format off
#include <MiniDB/Common/Type.h>       // Status, ColumnDef, ColumnType, RecordID
#include <MiniDB/Core/Database.h>     // Database
#include <MiniDB/Core/Record.h>       // Record
#include <MiniDB/Core/Table.h>        // Table
#include <MiniDB/Engine/QueryEngine.h> // QueryEngine, FilterPredicate
#include <MiniDB/Engine/StorageEngine.h> // StorageEngine

#include <ArenaPro/Arena.h> // ArenaPro::Arena

#include <VectorPro/Vector.h> // VectorPro::Vector

#include <atomic>  // std::atomic
#include <mutex>   // std::mutex, std::lock_guard
#include <string>  // std::string
// clang-format on

// Owns Shrtn's single "urls" table and everything needed to read/write it:
// the in-memory Database, a QueryEngine for lookups, a StorageEngine for
// whole-database persistence, and a mutex serializing access across
// FalconHTTP's thread-per-connection worker pool.
//
// KNOWN GAP: ArenaPro::Arena's real constructor signature hasn't been
// verified against its actual header -- Arena<>{DBConstants::ARENA_SIZE}
// below is a best guess based on DBConstants::ARENA_SIZE existing as a
// "bytes per query Arena" constant. Confirm against ArenaPro/Arena.h
// before this is expected to compile.

namespace Shrtn {

using namespace MiniDB::Common;
using namespace MiniDB::Core;
using namespace MiniDB::Engine;

/// @brief Owns the "urls" table, its Database, and everything needed to
/// insert/query it under concurrent access.
class ShrtnDb {
  public:
    /// @param dataDirectory Directory StorageEngine reads/writes to. Must
    /// already exist (StorageEngine does not create it).
    explicit ShrtnDb(const std::string& dataDirectory)
        : storage_(dataDirectory), arena_(DBConstants::ARENA_SIZE), query_(arena_), db_("shrtn") {}

    /// @brief Loads the database from disk if present, otherwise creates
    /// the "urls" table fresh.
    /// @return Status::OK on success (whether loaded or freshly created).
    [[nodiscard]] Status init() {
        const std::lock_guard<std::mutex> lock(mutex_);

        const Status loadStatus = storage_.loadDatabase(db_, kDbFile);
        if (loadStatus == Status::OK) {
            urls_ = db_.getTable("urls");
            // Loaded a database that, for whatever reason, doesn't have
            // the table we expect -- fall through and create it instead
            // of returning with urls_ == nullptr.
            if (urls_ != nullptr) {
                return Status::OK;
            }
        }

        const Status createStatus =
            db_.createTable("urls", Vector<ColumnDef>{
                                        {"code", ColumnType::STRING, /*nullable=*/false},
                                        {"originalUrl", ColumnType::STRING, /*nullable=*/false},
                                        {"createdAt", ColumnType::STRING, /*nullable=*/false},
                                        {"clickCount", ColumnType::INT, /*nullable=*/false},
                                        {"isPrivate", ColumnType::INT, /*nullable=*/false},
                                    });
        if (createStatus != Status::OK) {
            return createStatus;
        }

        urls_ = db_.getTable("urls");
        return Status::OK;
    }

    /// @brief Outcome of tryInsertUrl().
    enum class InsertOutcome { Inserted, CodeTaken, Error };

    /**
     * @brief Atomically checks `code` for a collision and inserts if free.
     * @param code Candidate short code.
     * @param originalUrl Destination URL.
     * @param createdAtIso8601 Creation timestamp, ISO 8601 UTC.
     * @return InsertOutcome::Inserted on success, ::CodeTaken if `code` is
     * already in use (caller should retry with a new candidate), ::Error
     * on any other MiniDB failure.
     * @details Collision check and insert happen under a single lock
     * acquisition -- checking codeExists() and inserting as two separate
     * locked calls would leave a window for two concurrent requests to
     * both pass the check before either inserts, producing a duplicate
     * code Table::insertRecord() itself can't catch (it only rejects
     * duplicate RecordIDs, not duplicate field values).
     */
    [[nodiscard]] InsertOutcome tryInsertUrl(const std::string& code,
                                             const std::string& originalUrl,
                                             const std::string& createdAtIso8601, bool isPrivate) {
        const std::lock_guard<std::mutex> lock(mutex_);

        const FilterPredicate pred{"code", Op::EQ, Json(code)};
        if (query_.count(*urls_, std::span<const FilterPredicate>(&pred, 1)) > 0) {
            return InsertOutcome::CodeTaken;
        }

        Record record(nextRecordId_.fetch_add(1, std::memory_order_relaxed));
        if (record.setField("code", code) != Status::OK) {
            return InsertOutcome::Error;
        }
        if (record.setField("originalUrl", originalUrl) != Status::OK) {
            return InsertOutcome::Error;
        }
        if (record.setField("createdAt", createdAtIso8601) != Status::OK) {
            return InsertOutcome::Error;
        }
        if (record.setField("clickCount", 0) != Status::OK) {
            return InsertOutcome::Error;
        }
        if (record.setField("isPrivate", isPrivate ? 1 : 0) != Status::OK) {
            return InsertOutcome::Error;
        }

        if (urls_->insertRecord(record) != Status::OK) {
            return InsertOutcome::Error;
        }

        // Persists the whole database to disk on success -- simplest-
        // correct durability for Shrtn's write volume; revisit if this
        // becomes a bottleneck (see StorageEngine's separate, not-yet-
        // wired page-cache facility for where that would go).
        if (storage_.saveDatabase(db_, kDbFile) != Status::OK) {
            return InsertOutcome::Error;
        }

        return InsertOutcome::Inserted;
    }

    /// @brief Outcome + payload of findByCode().
    struct LookupResult {
        bool found = false;
        RecordID id = 0;         ///< Valid only if found.
        std::string originalUrl; ///< Valid only if found.
    };

    /**
     * @brief Looks up a row by its short code.
     * @param code Short code to search for.
     * @return LookupResult with found == true and the row's RecordID +
     * originalUrl on a match, or found == false otherwise (missing code,
     * or a MiniDB error -- both surface identically to the caller as
     * "not found", since Shrtn has no separate error path for GET
     * /:code beyond a 404).
     */
    [[nodiscard]] LookupResult findByCode(const std::string& code) {
        const std::lock_guard<std::mutex> lock(mutex_);

        const FilterPredicate pred{"code", Op::EQ, Json(code)};
        const QueryResult result = query_.select(*urls_, std::span<const FilterPredicate>(&pred, 1),
                                                 /*sort=*/nullptr, /*limit=*/1);
        if (result.status != Status::OK || result.records.empty()) {
            return LookupResult{};
        }

        const Record& record = result.records[0];
        LookupResult out;
        out.found = true;
        out.id = record.getID();
        out.originalUrl = record.getField("originalUrl").asString();
        return out;
    }

    /**
     * @brief Increments the clickCount field of the row with the given id.
     * @param id RecordID to update (as returned by findByCode()).
     * @return Status::OK on success, or whatever Table::getRecord()/
     * updateRecord() returned on failure.
     * @details Read-modify-write under a single lock acquisition, so a
     * concurrent increment on the same row can't be lost between the
     * read and the write.
     */
    [[nodiscard]] Status incrementClickCount(RecordID id) {
        const std::lock_guard<std::mutex> lock(mutex_);

        Record record;
        if (const Status s = urls_->getRecord(id, record); s != Status::OK) {
            return s;
        }

        const int currentCount = static_cast<int>(record.getField("clickCount").asNumber());
        if (const Status s = record.setField("clickCount", currentCount + 1); s != Status::OK) {
            return s;
        }

        if (const Status s = urls_->updateRecord(record); s != Status::OK) {
            return s;
        }

        return storage_.saveDatabase(db_, kDbFile);
    }

    /// @brief One row as returned by listPublic() / getMetadataByCode().
    struct UrlEntry {
        std::string code;
        std::string originalUrl;
        std::string createdAt;
        int clickCount = 0;
        bool isPrivate = false;
    };

    /**
     * @brief Returns every non-private row in the urls table.
     * @return Public rows only, in scan order (not guaranteed to be
     * insertion order -- QueryEngine doesn't sort). Private rows are
     * excluded here specifically so the shared `/links` listing can
     * never surface them to anyone browsing it.
     */
    [[nodiscard]] Vector<UrlEntry> listPublic() {
        const std::lock_guard<std::mutex> lock(mutex_);

        const FilterPredicate pred{"isPrivate", Op::EQ, Json(0)};
        const QueryResult result = query_.select(*urls_, std::span<const FilterPredicate>(&pred, 1),
                                                 /*sort=*/nullptr, /*limit=*/0);

        Vector<UrlEntry> entries;
        entries.reserve(result.records.size());
        for (const auto& record : result.records) {
            entries.push_back(toUrlEntry(record));
        }
        return entries;
    }

    /// @brief Outcome + payload of getMetadataByCode().
    struct MetadataResult {
        bool found = false;
        UrlEntry entry; ///< Valid only if found.
    };

    /**
     * @brief Looks up full row metadata for one code, private or not.
     * @param code Short code to search for.
     * @details Deliberately does NOT filter on isPrivate -- this exists
     * so a private link's own creator can still poll its live
     * clickCount (the frontend remembers codes it created locally and
     * calls this per-code). "Private" here means "excluded from the
     * shared listing," not "inaccessible to anyone who already has the
     * code" -- same as the redirect itself, which never checked
     * isPrivate either. Worth being explicit about that with users if
     * this ever needs to be a stronger guarantee.
     */
    [[nodiscard]] MetadataResult getMetadataByCode(const std::string& code) {
        const std::lock_guard<std::mutex> lock(mutex_);

        const FilterPredicate pred{"code", Op::EQ, Json(code)};
        const QueryResult result = query_.select(*urls_, std::span<const FilterPredicate>(&pred, 1),
                                                 /*sort=*/nullptr, /*limit=*/1);
        if (result.status != Status::OK || result.records.empty()) {
            return MetadataResult{};
        }

        MetadataResult out;
        out.found = true;
        out.entry = toUrlEntry(result.records[0]);
        return out;
    }

  private:
    [[nodiscard]] static UrlEntry toUrlEntry(const Record& record) {
        UrlEntry entry;
        entry.code = record.getField("code").asString();
        entry.originalUrl = record.getField("originalUrl").asString();
        entry.createdAt = record.getField("createdAt").asString();
        entry.clickCount = static_cast<int>(record.getField("clickCount").asNumber());
        entry.isPrivate = record.getField("isPrivate").asNumber() != 0;
        return entry;
    }

    static constexpr const char* kDbFile = "shrtn.json";

    std::mutex mutex_; ///< Guards all access below -- Table/Database aren't
                       ///< internally thread-safe, unlike Metrics's ring
                       ///< buffer, and Server dispatches requests across a
                       ///< thread pool.

    StorageEngine storage_;
    ArenaPro::Arena<> arena_;
    QueryEngine query_;
    Database db_;
    Table* urls_ = nullptr;

    /// Monotonic internal storage key -- NOT the "id column" the project
    /// guide says urls deliberately omits. That rule is about the
    /// exposed schema (code/originalUrl/createdAt/clickCount); MiniDB's
    /// Table::insertRecord() still requires a RecordID as its storage
    /// key regardless, and Table provides no auto-increment of its own.
    std::atomic<RecordID> nextRecordId_{0};
};

} // namespace Shrtn
