/**
 * Postgres implementation of IStrategyStore.
 *
 * @author Olumuyiwa Oluwasanmi
 *
 * This is a module IMPLEMENTATION unit. `import pg;` below is therefore NOT
 * visible to anything that writes `import strategy_store;` -- which is the
 * whole point of the split, and is explained in strategy_store.cppm's banner.
 * Do not move this import into the interface unit.
 */

module;
#include <array>
#include <cstddef>
#include <expected>
#include <format>
#include <memory>
#include <optional>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

module strategy_store;

import pg;
import logger;

namespace options_calculator::store {

namespace {

/**
 * Postgres renders a timestamptz as "+00" with neither a `T` separator nor a
 * `Z`; RFC3339 wants both. The conversion happens in the database rather than
 * in C++ so every row arrives already in the shape the wire contract promises,
 * and no call site can forget to convert one.
 */
[[nodiscard]] auto rfc3339_of(std::string_view column) -> std::string {
    return std::format(R"(to_char({} AT TIME ZONE 'utc', 'YYYY-MM-DD"T"HH24:MI:SS"Z"'))", column);
}

/**
 * Is this a syntactically valid UUID?
 *
 * Checked in C++ BEFORE the value reaches SQL, because `$1::uuid` on a
 * malformed string is an ERROR from Postgres, not a zero-row result -- so
 * without this guard a caller passing "banana" as an id would get INTERNAL
 * (and a logged database error) where the honest answer is simply "no such
 * scenario of yours". The parameter is still bound, never interpolated; this
 * guard is about the response, not about injection.
 */
[[nodiscard]] auto is_uuid(std::string_view s) noexcept -> bool {
    if (s.size() != 36) return false;
    for (std::size_t i = 0; i < s.size(); ++i) {
        const char c = s[i];
        if (i == 8 || i == 13 || i == 18 || i == 23) {
            if (c != '-') return false;
            continue;
        }
        const bool hex = (c >= '0' && c <= '9') || (c >= 'a' && c <= 'f') || (c >= 'A' && c <= 'F');
        if (!hex) return false;
    }
    return true;
}

/**
 * A failed acquire and a failed query are both "the database did not answer".
 * They are mapped to the same StoreError because the caller's options are
 * identical in both cases, and a distinction the caller cannot act on is one
 * more thing to get wrong.
 */
[[nodiscard]] auto unavailable(std::string_view what, const pg::Error& err) -> StoreError {
    // A foreign-key violation on saved_strategies_user_id_fkey means the
    // subject verified but auth.users no longer holds that id -- a deleted
    // account whose access token has not expired yet. Reported as its own
    // error, because "temporarily unavailable, try again shortly" is the
    // opposite of true for an account that is gone.
    //
    // Matched on the constraint NAME, which migration 05 fixes, rather than on
    // SQLSTATE alone: this table has exactly one foreign key, so the name is
    // the precise signal and cannot be confused with some future constraint.
    if (err.message.find("saved_strategies_user_id_fkey") != std::string::npos) {
        logger::Logger::getInstance().info(
            "strategy_store: {} refused -- no such auth user (deleted account with a "
            "still-valid token)", what);
        return StoreError::UnknownUser;
    }
    logger::Logger::getInstance().warn("strategy_store: {} failed ({}): {}", what,
                                       pg::to_string(err.code), err.message);
    return StoreError::Unavailable;
}

/**
 * The unprivileged role every statement in this file runs as.
 *
 * Must match migration 04's own CREATE ROLE. A constant rather than a literal
 * repeated at three call sites, because a typo would not fail closed -- it
 * would fail the transaction outright, and finding out which of three copies
 * was wrong is wasted time.
 */
constexpr std::string_view kAppRole = "ofc_app";

/**
 * Runs one statement group as `ofc_app` with the caller's subject bound, and
 * guarantees the transaction is closed however the scope exits.
 *
 * WHY EVERY QUERY IN THIS FILE IS WRAPPED IN A TRANSACTION
 *
 * The engine connects as a superuser, and Postgres unconditionally bypasses row
 * security for one. The policy in migration 04 therefore only bites while
 * `current_user` is a role that is neither superuser nor BYPASSRLS -- which is
 * what `SET LOCAL ROLE` arranges, and `SET LOCAL` needs a transaction to be
 * local TO. The transaction is not incidental; it is the mechanism.
 *
 * ORDER IS DELIBERATE. set_config runs BEFORE the role drop, so the GUC is
 * written by the privileged role rather than depending on ofc_app's ability to
 * set it. Both work today; this ordering does not depend on that staying true.
 *
 * `is_local => true` on set_config is the other half of the pooling story: the
 * setting dies with the transaction, so the next request to borrow this
 * connection cannot inherit the previous caller's subject. A session-level
 * setting here would be a cross-user data leak with a very long fuse.
 *
 * The subject reaches SQL as a BOUND PARAMETER. `SET LOCAL ROLE` cannot take
 * one, which is exactly why the role is a compile-time constant and the subject
 * is not.
 */
class Transaction {
  public:
    explicit Transaction(pg::Connection& conn) : conn_(conn) {}

    /** BEGIN, bind the subject, drop privilege. Any failure leaves it unopened. */
    [[nodiscard]] auto begin(std::string_view subject) -> std::optional<StoreError> {
        // Checked BEFORE anything is sent. Migration 05's policy compares
        // `user_id = nullif(current_setting(...), '')::uuid`, and that cast
        // RAISES on a malformed value rather than matching nothing -- so a
        // non-uuid subject would surface as a database error instead of a
        // fail-closed empty result. Refusing here makes the cast safe by
        // construction. Every real subject is a Supabase `sub`, which is a
        // uuid, so this rejects nothing legitimate.
        if (!is_uuid(subject)) return StoreError::Invalid;

        if (auto r = conn_.exec("BEGIN"); !r) return unavailable("begin", r.error());
        open_ = true;

        std::array<std::optional<std::string>, 1> params{std::string{subject}};
        if (auto r = conn_.exec_params("SELECT set_config('app.current_user_id', $1, true)",
                                       params);
            !r) {
            return unavailable("set subject", r.error());
        }
        // A failure here means migration 04 has not been applied. Named
        // explicitly rather than left as a generic query failure, because the
        // remedy is a specific file and nothing else will suggest it.
        if (auto r = conn_.exec(std::format("SET LOCAL ROLE {}", kAppRole)); !r) {
            logger::Logger::getInstance().error(
                "strategy_store: could not SET LOCAL ROLE {} ({}). Row-level security is NOT "
                "in force. Apply backend/migrations/04_saved_strategies_rls.sql.",
                kAppRole, r.error().message);
            return StoreError::Unavailable;
        }
        return std::nullopt;
    }

    [[nodiscard]] auto commit() -> std::optional<StoreError> {
        if (!open_) return StoreError::Internal;
        auto r = conn_.exec("COMMIT");
        open_ = false;
        if (!r) return unavailable("commit", r.error());
        return std::nullopt;
    }

    /**
     * Rolls back anything still open.
     *
     * This is the reason the class exists rather than three hand-written
     * BEGIN/COMMIT pairs: a connection returned to the pool inside an open or
     * aborted transaction poisons it for whoever borrows it next, and every
     * early return in the three methods below is a path that would do exactly
     * that. Destruction order makes this safe -- each caller declares its
     * Transaction AFTER its pool Lease, so this runs before the lease is
     * released.
     */
    ~Transaction() {
        if (open_) {
            // Return code deliberately ignored: this runs on the failure path,
            // and there is nothing further to do if the connection is already
            // gone -- the pool's own liveness check will retire it.
            (void)conn_.exec("ROLLBACK");
        }
    }

    Transaction(const Transaction&) = delete;
    auto operator=(const Transaction&) -> Transaction& = delete;
    Transaction(Transaction&&) = delete;
    auto operator=(Transaction&&) -> Transaction& = delete;

  private:
    pg::Connection& conn_;
    bool open_ = false;
};

class PgStrategyStore final : public IStrategyStore {
  public:
    explicit PgStrategyStore(std::shared_ptr<pg::Pool> pool) : pool_(std::move(pool)) {}

    [[nodiscard]] auto save(std::string_view subject, std::string_view name,
                            std::string_view symbol, std::string_view payload_json)
        -> std::expected<SaveOutcome, StoreError> override {
        if (subject.empty() || name.empty()) return std::unexpected(StoreError::Invalid);

        auto slot = pool_->acquire();
        if (!slot) return std::unexpected(unavailable("acquire", slot.error()));
        auto& conn = *slot;

        // Declared AFTER the lease, so it rolls back before the connection goes
        // back to the pool. See Transaction's destructor.
        Transaction tx{*conn.get()};
        if (auto err = tx.begin(subject); err) return std::unexpected(*err);

        // One statement does four things, and each of them is here rather than
        // in C++ for a reason:
        //
        //   * the per-user cap is a subquery, so the count and the insert see
        //     one snapshot -- a count-then-insert pair in C++ would let two
        //     concurrent saves both observe count == 99 and both write;
        //   * EXISTS(...) exempts a rename-free re-save, so a user at the cap
        //     can still update scenarios they already have. Without it, hitting
        //     the cap would freeze their existing scenarios as read-only, which
        //     is not what a cap is for;
        //   * ON CONFLICT makes save-by-name an upsert (see uq_saved_strategies
        //     _user_name in migration 03);
        //   * `xmax <> 0` is Postgres's own way to tell an upsert's UPDATE arm
        //     from its INSERT arm on the RETURNING row -- xmax is nonzero only
        //     on a row this transaction updated. It saves a second round trip
        //     to answer "did I replace something".
        //
        // Zero rows back therefore means exactly one thing: the WHERE excluded
        // the insert, i.e. this user is at capacity with a new name.
        const std::string sql = std::format(
            "INSERT INTO public.saved_strategies (user_id, name, symbol, payload) "
            "SELECT $1, $2, $3, $4::jsonb "
            "WHERE (SELECT count(*) FROM public.saved_strategies WHERE user_id = $1) < $5::bigint "
            "   OR EXISTS (SELECT 1 FROM public.saved_strategies "
            "              WHERE user_id = $1 AND name = $2) "
            "ON CONFLICT (user_id, name) DO UPDATE "
            "   SET symbol = EXCLUDED.symbol, payload = EXCLUDED.payload, updated_at = NOW() "
            "RETURNING id::text, name, symbol, payload::text, {}, {}, (xmax <> 0)",
            rfc3339_of("created_at"), rfc3339_of("updated_at"));

        std::array<std::optional<std::string>, 5> params{
            std::string{subject}, std::string{name}, std::string{symbol},
            std::string{payload_json}, std::to_string(kMaxPerUser)};

        auto res = conn->exec_params(sql, params);
        if (!res) return std::unexpected(unavailable("save", res.error()));
        // Zero rows now has TWO possible causes, and they are indistinguishable
        // from here on purpose: the per-user cap excluded the insert, or the
        // RLS policy's WITH CHECK refused a row whose user_id is not this
        // caller's. The second cannot happen -- the statement writes $1, which
        // IS the subject the policy checks against -- so reporting AtCapacity
        // describes the only reachable cause.
        if (res->rows() == 0) return std::unexpected(StoreError::AtCapacity);
        if (res->cols() < 7) return std::unexpected(StoreError::Internal);

        SaveOutcome out;
        out.row = row_at(*res, 0);
        // libpq renders a boolean as "t"/"f" in text mode.
        out.replaced_existing = res->text(0, 6) == "t";
        if (auto err = tx.commit(); err) return std::unexpected(*err);
        return out;
    }

    [[nodiscard]] auto list(std::string_view subject)
        -> std::expected<std::vector<SavedRow>, StoreError> override {
        if (subject.empty()) return std::unexpected(StoreError::Invalid);

        auto slot = pool_->acquire();
        if (!slot) return std::unexpected(unavailable("acquire", slot.error()));
        auto& conn = *slot;

        Transaction tx{*conn.get()};
        if (auto err = tx.begin(subject); err) return std::unexpected(*err);

        // LIMIT is kMaxPerUser, the same constant the cap uses, so this cannot
        // silently truncate a legitimately-sized result: a user can never hold
        // more rows than this returns.
        const std::string sql = std::format(
            "SELECT id::text, name, symbol, payload::text, {}, {} "
            "FROM public.saved_strategies WHERE user_id = $1 "
            "ORDER BY updated_at DESC LIMIT $2::bigint",
            rfc3339_of("created_at"), rfc3339_of("updated_at"));

        std::array<std::optional<std::string>, 2> params{std::string{subject},
                                                          std::to_string(kMaxPerUser)};
        auto res = conn->exec_params(sql, params);
        if (!res) return std::unexpected(unavailable("list", res.error()));

        std::vector<SavedRow> rows;
        rows.reserve(static_cast<std::size_t>(res->rows()));
        for (int i = 0; i < res->rows(); ++i) rows.push_back(row_at(*res, i));
        if (auto err = tx.commit(); err) return std::unexpected(*err);
        return rows;
    }

    [[nodiscard]] auto remove(std::string_view subject, std::string_view id)
        -> std::expected<bool, StoreError> override {
        if (subject.empty()) return std::unexpected(StoreError::Invalid);
        // A malformed id is "no such scenario of yours", not an error -- see
        // is_uuid's own comment.
        if (!is_uuid(id)) return false;

        auto slot = pool_->acquire();
        if (!slot) return std::unexpected(unavailable("acquire", slot.error()));
        auto& conn = *slot;

        Transaction tx{*conn.get()};
        if (auto err = tx.begin(subject); err) return std::unexpected(*err);

        // `user_id = $1` is the load-bearing half of this WHERE clause. Without
        // it a leaked or guessed id would delete another user's row.
        std::array<std::optional<std::string>, 2> params{std::string{subject}, std::string{id}};
        auto res = conn->exec_params(
            "DELETE FROM public.saved_strategies WHERE user_id = $1 AND id = $2::uuid", params);
        if (!res) return std::unexpected(unavailable("delete", res.error()));
        const bool deleted = res->command_tuples() > 0;
        if (auto err = tx.commit(); err) return std::unexpected(*err);
        return deleted;
    }

  private:
    /** Column order here must match every SELECT/RETURNING list above. */
    [[nodiscard]] static auto row_at(const pg::Result& res, int i) -> SavedRow {
        return SavedRow{
            .id = std::string{res.text(i, 0)},
            .name = std::string{res.text(i, 1)},
            .symbol = std::string{res.text(i, 2)},
            .payload_json = std::string{res.text(i, 3)},
            .created_at = std::string{res.text(i, 4)},
            .updated_at = std::string{res.text(i, 5)},
        };
    }

    std::shared_ptr<pg::Pool> pool_;
};

}  // namespace

auto make_pg_strategy_store(std::string_view conninfo) -> std::shared_ptr<IStrategyStore> {
    if (conninfo.empty()) return nullptr;

    pg::PoolConfig cfg;
    cfg.conninfo = std::string{conninfo};
    // 4 (PoolConfig's own default) is right here and is NOT the 16 the
    // inference queue asks for. These three RPCs are user-initiated and rare --
    // a person pressing Save -- not a polling loop, so the queue's sizing
    // rationale does not transfer.
    cfg.size = 4;
    return std::make_shared<PgStrategyStore>(std::make_shared<pg::Pool>(std::move(cfg)));
}

}  // namespace options_calculator::store
