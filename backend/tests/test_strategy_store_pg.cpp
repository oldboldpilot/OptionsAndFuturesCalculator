// @author Olumuyiwa Oluwasanmi
//
// Real-Postgres gate for the SQL behind saved scenarios.
//
// WHY THIS FILE EXISTS SEPARATELY FROM test_calculator_service.cpp
//
// That file covers the saved-scenario RPCs against an in-memory FakeStore, and
// that is genuinely useful -- it proves the auth, entitlement, validation,
// JSON round-trip and error-mapping layers. What it CANNOT prove is that the
// SQL agrees with the fake, and a fake that quietly disagrees with the real
// query is worse than no fake: every test goes green while production leaks
// rows between users.
//
// So the properties asserted here are deliberately the ones a fake can fake:
//
//   * per-user scoping is enforced by the WHERE clause, not by the caller
//     (the cross-user section, which is the reason this file exists);
//   * ON CONFLICT (user_id, name) really does upsert, and `xmax <> 0` really
//     does distinguish the UPDATE arm from the INSERT arm;
//   * the per-user cap is enforced by the statement, and an at-cap user can
//     still UPDATE an existing scenario;
//   * a malformed id is a zero-row answer, not a Postgres cast error.
//
// NOT registered via add_test(), for the same reason test_inference_queue_pg
// is not: it needs a live database. Run it explicitly against a throwaway one:
//
//   DATABASE_URL="postgresql://postgres@127.0.0.1:55432/postgres" \
//     ./test_strategy_store_pg
//
// It refuses to run without DATABASE_URL rather than passing vacuously, and it
// deletes only rows whose user_id carries its own test prefix.
#include <algorithm>
#include <chrono>
#include <cstdio>
#include <cstdlib>
#include <memory>
#include <string>
#include <string_view>
#include <vector>

import strategy_store;
import pg;

namespace {

namespace store = options_calculator::store;

int g_checks = 0;
int g_failures = 0;

auto check(bool condition, const std::string& what) -> void {
    ++g_checks;
    if (condition) {
        std::printf("  PASS: %s\n", what.c_str());
    } else {
        std::printf("  FAIL: %s\n", what.c_str());
        ++g_failures;
    }
}

auto section(const char* title) -> void { std::printf("\n=== %s ===\n", title); }

// Distinctive so the cleanup below can never match a real user's rows.
constexpr std::string_view kAlice = "test-store-pg-alice";
constexpr std::string_view kBob = "test-store-pg-bob";

/** How many rows a subject currently holds, straight from the store. */
auto count_for(store::IStrategyStore& s, std::string_view subject) -> std::size_t {
    auto rows = s.list(subject);
    return rows ? rows->size() : 0U;
}

auto payload_for(std::string_view symbol) -> std::string {
    return std::string{R"({"underlyingSymbol":")"} + std::string{symbol} +
           R"(","currentPrice":585,"legs":[{"strike":580},{"strike":600}]})";
}

}  // namespace

auto main() -> int {
    const char* const url = std::getenv("DATABASE_URL");
    if (url == nullptr || *url == '\0') {
        std::fprintf(stderr,
                     "test_strategy_store_pg: DATABASE_URL is unset.\n"
                     "This test talks to a real database and will not pass vacuously.\n");
        return 77;  // ctest's conventional "skipped"
    }

    auto s = store::make_pg_strategy_store(url);
    if (!s) {
        std::fprintf(stderr, "test_strategy_store_pg: could not construct the store\n");
        return 2;
    }

    // Start from a known-empty state for these two subjects only.
    for (const auto subject : {kAlice, kBob}) {
        auto rows = s->list(subject);
        if (!rows) {
            std::fprintf(stderr,
                         "test_strategy_store_pg: could not reach the database (%s). Is "
                         "DATABASE_URL right, and have migrations 01 and 03 been applied?\n",
                         std::string{store::to_string(rows.error())}.c_str());
            return 2;
        }
        for (const auto& row : *rows) (void)s->remove(subject, row.id);
    }

    // ---------------------------------------------------------------
    // The database-side control is asserted DIRECTLY, not inferred from
    // behaviour, because behaviour cannot see it: the application's own
    // `WHERE user_id = $1` produces identical results whether or not row-level
    // security exists. Drop the policy and every other check in this file still
    // passes, while the second layer is silently gone. These four checks are
    // the only thing that would notice.
    //
    // Same lesson this repository already paid for with the AdSense denylist:
    // assert the state that is actually in force, not the one the code intends.
    section("0. RLS is actually configured");
    {
        namespace pg = options_calculator::pg;
        auto conn = pg::Connection::connect(url, std::chrono::milliseconds{2000},
                                            std::chrono::milliseconds{2000});
        if (!conn) {
            std::fprintf(stderr, "could not open a direct connection for the RLS probe\n");
            return 2;
        }
        auto q = [&](const char* sql) -> std::string {
            auto r = conn->exec(sql);
            if (!r || r->rows() == 0) return {};
            return std::string{r->text(0, 0)};
        };

        check(q("SELECT relrowsecurity FROM pg_class WHERE relname = 'saved_strategies'") == "t",
              "row-level security is ENABLED on saved_strategies");
        check(q("SELECT relforcerowsecurity FROM pg_class WHERE relname = 'saved_strategies'") == "t",
              "row-level security is FORCEd on saved_strategies");
        check(q("SELECT count(*) FROM pg_policy "
                "WHERE polrelid = 'public.saved_strategies'::regclass "
                "  AND polname = 'saved_strategies_own_rows'") == "1",
              "the saved_strategies_own_rows policy exists");
        // The attribute that decides whether the policy filters anything at
        // all. A superuser or BYPASSRLS role makes every policy inert, which is
        // precisely the trap migration 04's banner documents.
        check(q("SELECT (rolsuper OR rolbypassrls)::text FROM pg_roles WHERE rolname = 'ofc_app'")
                  == "false",
              "ofc_app is neither superuser nor BYPASSRLS, so the policy is not inert");
    }

    // ---------------------------------------------------------------
    section("1. Insert and upsert");
    std::string alice_id;
    {
        auto saved = s->save(kAlice, "Earnings play", "SPY", payload_for("SPY"));
        check(saved.has_value(), "save() succeeds");
        if (saved) {
            check(!saved->replaced_existing, "a first save is an INSERT, not an UPDATE");
            check(!saved->row.id.empty(), "the row comes back with a server-assigned id");
            check(saved->row.name == "Earnings play", "the row comes back with its name");
            // The RFC3339 shaping happens in SQL, so it is asserted on real output.
            check(saved->row.created_at.size() == 20 && saved->row.created_at[10] == 'T' &&
                      saved->row.created_at.back() == 'Z',
                  "created_at is RFC3339 (got \"" + saved->row.created_at + "\")");
            alice_id = saved->row.id;
        }
    }
    {
        auto again = s->save(kAlice, "Earnings play", "QQQ", payload_for("QQQ"));
        check(again.has_value(), "re-saving the same name succeeds");
        if (again) {
            // This is the `xmax <> 0` discriminator doing its job against a
            // real Postgres, which is the only place it can be verified.
            check(again->replaced_existing, "re-saving the same name reports replaced_existing");
            check(again->row.id == alice_id, "an upsert keeps the SAME row id");
            check(again->row.symbol == "QQQ", "an upsert overwrites the stored symbol");
        }
        auto rows = s->list(kAlice);
        check(rows.has_value() && rows->size() == 1,
              "an upsert leaves ONE row, not two (got " +
                  std::to_string(rows ? rows->size() : 0) + ")");
    }

    // ---------------------------------------------------------------
    section("2. Ordering and scoping");
    {
        (void)s->save(kAlice, "Second scenario", "IWM", payload_for("IWM"));
        auto rows = s->list(kAlice);
        check(rows.has_value() && rows->size() == 2, "Alice now has two scenarios");
        if (rows && rows->size() == 2) {
            check((*rows)[0].name == "Second scenario",
                  "list() is newest-updated-first (got \"" + (*rows)[0].name + "\" first)");
        }
        auto bob_rows = s->list(kBob);
        check(bob_rows.has_value() && bob_rows->empty(),
              "Bob's list is empty -- the WHERE clause scopes by user");
    }

    // ---------------------------------------------------------------
    section("3. Cross-user isolation (the reason this file exists)");
    {
        // Bob saves under the SAME name Alice used. The unique index is on
        // (user_id, name), so this must create Bob's own row rather than
        // colliding with, or overwriting, hers.
        auto bob_saved = s->save(kBob, "Earnings play", "TLT", payload_for("TLT"));
        check(bob_saved.has_value(), "Bob may use a name Alice already used");
        check(bob_saved.has_value() && !bob_saved->replaced_existing,
              "Bob's same-name save is an INSERT, not an overwrite of Alice's row");
        check(bob_saved.has_value() && bob_saved->row.id != alice_id,
              "Bob's row is a DIFFERENT row from Alice's");

        auto alice_rows = s->list(kAlice);
        const bool alice_intact =
            alice_rows.has_value() && alice_rows->size() == 2 &&
            std::any_of(alice_rows->begin(), alice_rows->end(), [](const store::SavedRow& r) {
                return r.name == "Earnings play" && r.symbol == "QQQ";
            });
        check(alice_intact, "Alice's same-named scenario is untouched by Bob's save");

        // Bob knows Alice's id. He must still not be able to delete it.
        auto stolen = s->remove(kBob, alice_id);
        check(stolen.has_value() && !*stolen,
              "Bob deleting Alice's id reports false, not success");
        auto after = s->list(kAlice);
        check(after.has_value() && after->size() == 2,
              "Alice's rows survive Bob's delete attempt");
    }

    // ---------------------------------------------------------------
    section("4. Delete");
    {
        auto bad = s->remove(kAlice, "not-a-uuid");
        check(bad.has_value() && !*bad,
              "a malformed id is a false result, NOT a Postgres cast error");

        auto gone = s->remove(kAlice, alice_id);
        check(gone.has_value() && *gone, "Alice deletes her own row");
        auto twice = s->remove(kAlice, alice_id);
        check(twice.has_value() && !*twice, "deleting the same row twice reports false");
    }

    // ---------------------------------------------------------------
    section("5. Per-user cap");
    {
        // Fill Bob to the cap. He already holds 1.
        bool fill_ok = true;
        for (std::size_t i = count_for(*s, kBob); i < store::kMaxPerUser; ++i) {
            auto r = s->save(kBob, "scenario-" + std::to_string(i), "SPY", payload_for("SPY"));
            if (!r) { fill_ok = false; break; }
        }
        check(fill_ok, "filling to the cap succeeds");

        auto over = s->save(kBob, "one too many", "SPY", payload_for("SPY"));
        check(!over.has_value() && over.error() == store::StoreError::AtCapacity,
              "a NEW scenario past the cap is refused with AtCapacity");

        // The exemption that makes the cap a cap rather than a freeze.
        auto update_at_cap = s->save(kBob, "Earnings play", "DIA", payload_for("DIA"));
        check(update_at_cap.has_value() && update_at_cap->replaced_existing,
              "an at-cap user can still UPDATE a scenario they already have");
    }

    // Cleanup: only ever rows carrying this file's own subjects.
    for (const auto subject : {kAlice, kBob}) {
        auto rows = s->list(subject);
        if (rows) {
            for (const auto& row : *rows) (void)s->remove(subject, row.id);
        }
        auto left = s->list(subject);
        check(left.has_value() && left->empty(),
              std::string{"cleanup leaves no rows for "} + std::string{subject});
    }

    std::printf("\n%d checks, %d failures\n", g_checks, g_failures);
    return g_failures == 0 ? 0 : 1;
}
