/**
 * Implementation of the Census ACS state-assumptions refresh.
 *
 * @author Olumuyiwa Oluwasanmi
 *
 * This is a module IMPLEMENTATION unit. `import pg;` below is therefore NOT
 * visible to anything writing `import state_refresh;` -- which is the whole
 * point of the split and is explained in state_refresh.cppm's banner. Do not
 * move this import into the interface unit.
 */
module;

// httplib is the only textual include: it is not a module, and the project
// links it as a header-only library. Everything else comes from `import std`
// per rules 11, 12 and 41 -- std::getenv, std::tolower and std::round included.
#include <httplib.h>

module state_refresh;

import std;
import pg;
import logger;
import fastjson;

namespace {

constexpr std::string_view kHost = "https://api.census.gov";
constexpr std::string_view kJobName = "state-assumptions-refresh";

/**
 * A fixed 64-bit key for `pg_try_advisory_lock`.
 *
 * `numReplicas` is 2, so a weekly timer fires on BOTH replicas within
 * milliseconds of each other. A double run is not a correctness problem --
 * both would fetch the same vintage and write the same numbers -- but it
 * spends a second call against a rate-limited third-party API and interleaves
 * two sets of `job_runs` writes, which makes the bookkeeping unreadable
 * exactly when someone is reading it to find out what went wrong.
 *
 * DOCUMENTED BESIDE ITS NEIGHBOUR ON PURPOSE: `inference_queue.cpp` already
 * runs this same pattern for its cross-replica sweeper. Two advisory locks
 * sharing a key would silently serialise two unrelated jobs, and the symptom
 * -- one of them "sometimes not running" -- points nowhere near the cause.
 */
constexpr std::int64_t kAdvisoryLockKey = 0x5354415445524653LL;  // "STATERFS"













/**
 * Fetches one ACS vintage. Returns the raw body, or nullopt.
 *
 * THE API KEY IS A QUERY PARAMETER -- that is Census's design, not a choice
 * available here. It is encrypted on the wire, and it lands in anything that
 * logs a URL. So nothing in this function ever logs the path it built:
 * failures are reported with the host, the year and the HTTP status, and never
 * with the request line. `market_data.cppm` logs `host + path` on error, which
 * is correct there and would leak the key here; the shape is deliberately not
 * reused.
 */
[[nodiscard]] auto fetch_vintage(int year, const std::string& api_key) -> std::optional<std::string> {
    httplib::Client cli{std::string{kHost}};
    cli.set_connection_timeout(5, 0);
    cli.set_read_timeout(30, 0);
    cli.set_follow_location(true);

    const std::string path =
        std::format("/data/{}/acs/acs5?get=NAME,B25077_001E,B25064_001E,B25103_001E"
                    "&for=state:*&key={}",
                    year, api_key);

    auto res = cli.Get(path);
    if (!res) {
        logger::Logger::getInstance().warn("state_refresh: ACS {} unreachable (transport)", year);
        return std::nullopt;
    }
    if (res->status != 200) {
        // Status and a short body only. The body of a Census error does not
        // echo the key; the URL would.
        logger::Logger::getInstance().warn("state_refresh: ACS {} returned HTTP {} ({})", year, res->status,
                     res->body.substr(0, 120));
        return std::nullopt;
    }
    return res->body;
}

/**
 * Holds the refresh advisory lock for a scope.
 *
 * The lock is TAKEN by the caller -- taking it can fail, and a constructor that
 * can fail either throws or leaves a half-built object, neither of which this
 * codebase wants on a path whose whole job is to decline politely. Releasing
 * cannot fail in any way the caller can act on, so that half belongs in a
 * destructor.
 */
class AdvisoryLockGuard {
  public:
    explicit AdvisoryLockGuard(options_calculator::pg::Connection& conn) noexcept : conn_(conn) {}

    AdvisoryLockGuard(const AdvisoryLockGuard&) = delete;
    auto operator=(const AdvisoryLockGuard&) -> AdvisoryLockGuard& = delete;
    AdvisoryLockGuard(AdvisoryLockGuard&&) = delete;
    auto operator=(AdvisoryLockGuard&&) -> AdvisoryLockGuard& = delete;

    ~AdvisoryLockGuard() {
        const std::array<std::optional<std::string>, 1> params{
            std::to_string(kAdvisoryLockKey)};
        (void)conn_.exec_params("SELECT pg_advisory_unlock($1::bigint)", params);
    }

  private:
    // A REFERENCE, not a pointer -- rule 3. Copy and move are deleted above, so
    // the usual objection to reference members (they make a type unassignable)
    // does not apply: this type was never going to be assignable. The connection
    // outlives the guard by construction, both being locals of run_refresh.
    options_calculator::pg::Connection& conn_;
};

}  // namespace

namespace state_refresh {





namespace {

/** Parses one vintage's array-of-arrays into validated rows. */
struct ParsedVintage {
    std::vector<StateRow> rows;
    int rejected = 0;
};

[[nodiscard]] auto parse_vintage(const std::string& body) -> std::optional<ParsedVintage> {
    auto doc = fastjson::parse(body);
    if (!doc.has_value() || !doc->is_array() || doc->size() < 2) {
        return std::nullopt;
    }
    // Row 0 is the header; column ORDER is what the request asked for, but
    // reading it from the header rather than assuming positions means a
    // reordered upstream response is handled instead of silently transposing
    // rent into price.
    const auto& header = (*doc)[0];
    int i_name = -1;
    int i_price = -1;
    int i_rent = -1;
    int i_taxes = -1;
    for (std::size_t c = 0; c < header.size(); ++c) {
        const auto col = header[static_cast<int>(c)].as_string();
        if (col == "NAME") i_name = static_cast<int>(c);
        else if (col == "B25077_001E") i_price = static_cast<int>(c);
        else if (col == "B25064_001E") i_rent = static_cast<int>(c);
        else if (col == "B25103_001E") i_taxes = static_cast<int>(c);
    }
    if (i_name < 0 || i_price < 0 || i_rent < 0 || i_taxes < 0) {
        logger::Logger::getInstance().warn("state_refresh: ACS header is missing an expected column");
        return std::nullopt;
    }

    ParsedVintage out;
    for (std::size_t r = 1; r < doc->size(); ++r) {
        const auto& row = (*doc)[static_cast<int>(r)];
        if (!row.is_array()) {
            continue;
        }
        const auto name = std::string{row[i_name].as_string()};
        if (detail::is_not_a_state(name)) {
            continue;  // expected, so not a rejection
        }
        auto valid = validate_acs_row(name, row[i_price].as_string(), row[i_rent].as_string(),
                                      row[i_taxes].as_string());
        if (!valid.has_value()) {
            ++out.rejected;
            logger::Logger::getInstance().warn("state_refresh: rejected {} -- {}", name,
                                               valid.error());
            continue;
        }
        out.rows.push_back(std::move(*valid));
    }
    return out;
}

/** Records the run. Best-effort: bookkeeping must never fail the job. */
auto record_run(options_calculator::pg::Connection& conn, std::string_view status, int processed,
                std::string_view error) -> void {
    const std::array<std::optional<std::string>, 4> params{
        std::string{kJobName}, std::string{status}, std::to_string(processed),
        error.empty() ? std::optional<std::string>{} : std::optional<std::string>{error}};
    (void)conn.exec_params(
        "INSERT INTO public.job_runs (job, status, last_run_at, last_success_at, "
        "                             last_error, items_processed) "
        "VALUES ($1, $2, NOW(), CASE WHEN $2 = 'ok' THEN NOW() END, $4, $3::int) "
        "ON CONFLICT (job) DO UPDATE SET "
        "  status = EXCLUDED.status, "
        "  last_run_at = EXCLUDED.last_run_at, "
        // Preserved on failure: the last time this job actually SUCCEEDED is
        // the number an operator needs, and overwriting it with the time of a
        // failed run destroys exactly that.
        "  last_success_at = COALESCE(EXCLUDED.last_success_at, public.job_runs.last_success_at), "
        "  last_error = EXCLUDED.last_error, "
        "  items_processed = EXCLUDED.items_processed",
        params);
}

}  // namespace

auto run_refresh(bool dry_run, std::int32_t pinned_year) -> std::expected<Outcome, Refusal> {
    Outcome out;

    const auto api_key_opt = detail::env_value("CENSUS_API_KEY");
    if (!api_key_opt.has_value()) {
        // A SUPPORTED configuration, exactly like an empty MODEL_URL: this one
        // capability is unavailable and every other RPC is unaffected.
        return std::unexpected(Refusal{
            Abort::NotConfigured,
            "CENSUS_API_KEY is not set on this engine, so the state-assumptions refresh is "
            "disabled. Every other operation is unaffected."});
    }
    const std::string& api_key = *api_key_opt;

    const auto url_opt = detail::env_value("DATABASE_URL");
    if (!url_opt.has_value()) {
        return std::unexpected(
            Refusal{Abort::StoreUnavailable, "DATABASE_URL is unset; there is nowhere to write."});
    }

    auto conn = options_calculator::pg::Connection::connect(*url_opt, std::chrono::seconds{5},
                                                           std::chrono::seconds{60});
    if (!conn) {
        return std::unexpected(
            Refusal{Abort::StoreUnavailable, "could not reach the database"});
    }

    // SINGLE-FLIGHT. Non-blocking: a second replica returns immediately rather
    // than queueing behind a run it would only duplicate. Held for the whole
    // run and released on every path, including the early returns below --
    // which is why it is taken through a scope guard rather than by hand.
    {
        const std::array<std::optional<std::string>, 1> lock_params{
            std::to_string(kAdvisoryLockKey)};
        auto got = conn->exec_params("SELECT pg_try_advisory_lock($1::bigint)", lock_params);
        if (!got || got->rows() != 1 || got->text(0, 0) != "t") {
            return std::unexpected(
                Refusal{Abort::AlreadyRunning, "another replica is already running this refresh"});
        }
    }
    // RAII, per rules 3 and 6: no raw pointer, no type-erased deleter, and the
    // unlock runs on EVERY exit path including the early returns below. A hand
    // written unlock before each `return` is the shape that eventually grows a
    // path somebody forgets, and an advisory lock leaked until the connection
    // closes would block next week's run for as long as the pool holds it.
    const AdvisoryLockGuard lock_guard{*conn};

    // ---- fetch and validate, ONE VINTAGE, before anything is written ----
    // std::chrono calendar types, per rule 27. The previous shape divided epoch
    // hours by 24 and then by 365, which drifts a day every leap year and is
    // simply wrong near a year boundary -- and the symptom would be the job
    // asking for a vintage that does not exist yet, which reads as an upstream
    // outage.
    const auto today = std::chrono::year_month_day{
        std::chrono::floor<std::chrono::days>(std::chrono::system_clock::now())};
    const auto year_now = static_cast<int>(today.year());
    const std::vector<std::int32_t> years =
        pinned_year > 0 ? std::vector<std::int32_t>{pinned_year} : candidate_years(year_now);

    ParsedVintage chosen;
    std::int32_t chosen_year = 0;
    for (const std::int32_t y : years) {
        auto body = fetch_vintage(y, api_key);
        if (!body) {
            continue;
        }
        auto parsed = parse_vintage(*body);
        if (!parsed) {
            continue;
        }
        if (static_cast<int>(parsed->rows.size()) >= kMinUsableStates) {
            chosen = std::move(*parsed);
            chosen_year = y;
            break;
        }
        logger::Logger::getInstance().warn("state_refresh: ACS {} yielded only {} usable states (floor {})", y,
                     parsed->rows.size(), kMinUsableStates);
    }

    if (chosen_year == 0) {
        const std::string why =
            "no ACS vintage returned enough usable states; existing data kept";
        record_run(*conn, "aborted", 0, why);
        return std::unexpected(Refusal{
            years.empty() ? Abort::UpstreamFailed : Abort::TooFewUsableRows, why});
    }

    out.data_year = chosen_year;
    out.data_source = std::format("US Census ACS 5-year {}", chosen_year);
    out.states_rejected = chosen.rejected;

    if (dry_run) {
        out.states_updated = static_cast<int>(chosen.rows.size());
        out.message = std::format("dry run: {} states would be updated, {} rejected, from {}",
                                  chosen.rows.size(), chosen.rejected, out.data_source);
        return out;
    }

    // ---- ONE TRANSACTION for all fifty UPDATEs ----
    //
    // Readers never observe a half-refreshed table, and any failure rolls the
    // whole run back to the previous vintage rather than leaving a mixture.
    if (!conn->exec("BEGIN")) {
        return std::unexpected(
            Refusal{Abort::WriteFailed, "could not begin the write transaction"});
    }

    // FAIL CLOSED ON THE ROLE DROP. This is the inverse of `saved_strategies`,
    // where forgetting the subject GUC yields zero rows. Here the connection is
    // SUPERUSER, so a failed `SET LOCAL ROLE` leaves us with full privileges and
    // the UPDATEs would succeed -- writing columns this job is supposed to be
    // incapable of touching. Refusing to proceed is the only thing that makes
    // the column grant meaningful at runtime.
    if (!conn->exec("SET LOCAL ROLE ofc_refresh")) {
        (void)conn->exec("ROLLBACK");
        logger::Logger::getInstance().error(
            "state_refresh: SET LOCAL ROLE ofc_refresh failed -- refusing to write");
        return std::unexpected(Refusal{Abort::WriteFailed,
                                       "could not drop to the ofc_refresh role; refusing to "
                                       "write with superuser privileges"});
    }

    std::int32_t updated = 0;
    std::int32_t missing = 0;
    for (const auto& row : chosen.rows) {
        const std::array<std::optional<std::string>, 6> params{
            row.slug, row.median_price, row.median_rent, row.property_tax_rate,
            out.data_source, std::to_string(chosen_year)};
        auto res = conn->exec_params(
            "UPDATE public.state_assumptions SET "
            "  median_price = $2::numeric, median_rent = $3::numeric, "
            "  property_tax_rate = $4::numeric, data_source = $5, "
            "  data_year = $6::int, refreshed_at = NOW() "
            "WHERE slug = $1",
            params);
        if (!res) {
            (void)conn->exec("ROLLBACK");
            return std::unexpected(Refusal{
                Abort::WriteFailed,
                std::format("write failed on {}; no changes were kept", row.slug)});
        }
        // A slug that matches nothing is table-population drift, not a row to
        // create. Counted and reported; never INSERTed -- the contract is
        // update-by-slug over a pre-seeded fifty.
        if (res->command_tuples() == 1) {
            ++updated;
        } else {
            ++missing;
            logger::Logger::getInstance().warn("state_refresh: no row for slug '{}'", row.slug);
        }
    }

    // A RUN THAT WROTE NOTHING IS NOT A SUCCESSFUL RUN.
    //
    // Measured, and it is why this check exists: the first live run validated
    // fifty states, issued fifty UPDATEs, committed, and reported ok=true with
    // states_updated=0. The cause was an RLS SELECT policy that named `ofc_app`
    // and not `ofc_refresh`, so under the role drop the job could not SEE the
    // rows and every UPDATE matched zero. Nothing errored -- an UPDATE that
    // matches no row is a perfectly ordinary UPDATE.
    //
    // `states_updated` was in the response the whole time, so the number was
    // visible to anyone who looked. That is not the same as being caught: a
    // weekly job reporting ok is a job nobody looks at. The success channel has
    // to be wrong for the failure to surface.
    if (updated == 0) {
        (void)conn->exec("ROLLBACK");
        const std::string why =
            std::format("validated {} states but wrote none -- every UPDATE matched zero rows. "
                        "Existing data kept.", chosen.rows.size());
        record_run(*conn, "aborted", 0, why);
        return std::unexpected(Refusal{Abort::WriteFailed, why});
    }

    record_run(*conn, "ok", updated, "");

    if (!conn->exec("COMMIT")) {
        return std::unexpected(Refusal{
            Abort::WriteFailed, "the write transaction did not commit; existing data kept"});
    }

    out.states_updated = updated;
    out.message = std::format("{} states updated from {}{}", updated, out.data_source,
                              missing > 0 ? std::format(" ({} slugs matched no row)", missing) : "");
    logger::Logger::getInstance().info("state_refresh: {} ({} rejected)", out.message, out.states_rejected);
    return out;
}

auto last_run() -> std::expected<JobStatus, Refusal> {
    JobStatus st;
    const auto url_opt = detail::env_value("DATABASE_URL");
    if (!url_opt.has_value()) {
        return std::unexpected(Refusal{Abort::StoreUnavailable, "DATABASE_URL is unset"});
    }
    auto conn = options_calculator::pg::Connection::connect(*url_opt, std::chrono::seconds{5},
                                                           std::chrono::seconds{10});
    if (!conn) {
        return std::unexpected(Refusal{Abort::StoreUnavailable, "could not reach the database"});
    }
    const std::array<std::optional<std::string>, 1> params{std::string{kJobName}};
    auto res = conn->exec_params(
        "SELECT status, COALESCE(to_char(last_run_at, 'YYYY-MM-DD\"T\"HH24:MI:SSOF'), ''), "
        "       COALESCE(to_char(last_success_at, 'YYYY-MM-DD\"T\"HH24:MI:SSOF'), ''), "
        "       COALESCE(last_error, ''), COALESCE(items_processed, 0) "
        "FROM public.job_runs WHERE job = $1",
        params);
    if (!res || res->rows() != 1) {
        return st;  // no run recorded yet: `found` stays false, which is an answer
    }
    st.found = true;
    st.status = std::string{res->text(0, 0)};
    st.last_run_at = std::string{res->text(0, 1)};
    st.last_success_at = std::string{res->text(0, 2)};
    st.last_error = std::string{res->text(0, 3)};
    const auto n = detail::to_double(res->text(0, 4));
    st.items_processed = n ? static_cast<int>(*n) : 0;
    return st;
}

auto read_assumptions(std::string_view slug)
    -> std::expected<std::vector<StoredAssumption>, Refusal> {
    const auto url_opt = detail::env_value("DATABASE_URL");
    if (!url_opt.has_value()) {
        return std::unexpected(Refusal{Abort::StoreUnavailable, "DATABASE_URL is unset"});
    }
    auto conn = options_calculator::pg::Connection::connect(*url_opt, std::chrono::seconds{5},
                                                           std::chrono::seconds{10});
    if (!conn) {
        return std::unexpected(Refusal{Abort::StoreUnavailable, "could not reach the database"});
    }

    // COALESCE to '' for every nullable text column, and to_char for the
    // timestamp, so an unrefreshed row comes back as empty strings rather than
    // as SQL NULL. The distinction the caller needs -- never refreshed versus
    // refreshed long ago -- survives as "" versus a real RFC3339 stamp.
    //
    // ORDER BY slug so the response is stable across calls. An unordered read
    // of fifty rows would let a caller diffing two responses see spurious
    // movement that is only Postgres choosing a different plan.
    const std::array<std::optional<std::string>, 1> params{
        slug.empty() ? std::optional<std::string>{} : std::optional<std::string>{std::string{slug}}};
    auto res = conn->exec_params(
        "SELECT slug, name, abbr, "
        "       COALESCE(median_price::text, ''), COALESCE(property_tax_rate::text, ''), "
        "       COALESCE(insurance_annual::text, ''), COALESCE(state_income_tax::text, ''), "
        "       COALESCE(median_rent::text, ''), COALESCE(note, ''), "
        "       COALESCE(data_source, ''), COALESCE(data_year, 0), "
        // FORMATTED IN SQL, and 'Z' rather than OF. Postgres's OF renders the
        // SHORTEST offset -- "+00", not "+00:00" -- which is not valid RFC3339
        // and is not what finance.proto documents this field as. V8 parses it
        // anyway; JavaScriptCore returns Invalid Date, so it would have worked
        // in the browser it was tested in and failed in Safari. Converting to
        // UTC and appending a literal Z is unambiguous everywhere.
        "       COALESCE(to_char(refreshed_at AT TIME ZONE 'UTC', "
        "                       'YYYY-MM-DD\"T\"HH24:MI:SS\"Z\"'), '') "
        "FROM public.state_assumptions "
        "WHERE $1::text IS NULL OR slug = $1 "
        "ORDER BY slug",
        params);
    if (!res) {
        return std::unexpected(Refusal{Abort::StoreUnavailable, "the read failed"});
    }

    std::vector<StoredAssumption> out;
    out.reserve(static_cast<std::size_t>(res->rows()));
    for (int r = 0; r < res->rows(); ++r) {
        const auto year = detail::to_double(res->text(r, 10));
        out.push_back(StoredAssumption{
            .slug = std::string{res->text(r, 0)},
            .name = std::string{res->text(r, 1)},
            .abbr = std::string{res->text(r, 2)},
            .median_price = std::string{res->text(r, 3)},
            .property_tax_rate = std::string{res->text(r, 4)},
            .insurance_annual = std::string{res->text(r, 5)},
            .state_income_tax = std::string{res->text(r, 6)},
            .median_rent = std::string{res->text(r, 7)},
            .note = std::string{res->text(r, 8)},
            .data_source = std::string{res->text(r, 9)},
            .data_year = year ? static_cast<std::int32_t>(*year) : 0,
            .refreshed_at = std::string{res->text(r, 11)}});
    }
    return out;
}

namespace {

/** True when no successful run is recorded, or the last one is older than
 *  kStaleAfter. Read through last_run() so the scheduler and the admin surface
 *  agree on what "when did this last work" means. */
[[nodiscard]] auto refresh_is_due() -> bool {
    const auto status = last_run();
    if (!status.has_value()) {
        return false;  // no database: nothing to do, and nothing to complain about
    }
    if (!status->found || status->last_success_at.empty()) {
        return true;  // never succeeded -- the first boot after a deploy
    }
    // Parsed by field rather than with std::chrono::parse, which this libc++
    // does not provide, and rather than compared as strings -- "2026-01-02"
    // sorts before "2026-1-3" lexically and the question here is elapsed time.
    // The shape is fixed by the to_char format in last_run(), so a positional
    // read is exact rather than lenient.
    const auto& stamp = status->last_success_at;
    const auto field = [&stamp](std::size_t at, std::size_t len) -> std::optional<int> {
        if (at + len > stamp.size()) {
            return std::nullopt;
        }
        int v = 0;
        const auto* const first = stamp.data() + at;
        if (std::from_chars(first, first + len, v).ec != std::errc{}) {
            return std::nullopt;
        }
        return v;
    };
    const auto y = field(0, 4);
    const auto mo = field(5, 2);
    const auto d = field(8, 2);
    const auto h = field(11, 2);
    const auto mi = field(14, 2);
    const auto sec = field(17, 2);
    if (!y || !mo || !d || !h || !mi || !sec) {
        // An unparseable stamp is not evidence the job ran. Treat it as due --
        // the cost of an extra run is one Census call; the cost of skipping is
        // stale data nobody notices.
        return true;
    }
    const auto day = std::chrono::year_month_day{std::chrono::year{*y}, std::chrono::month{
                                                     static_cast<unsigned>(*mo)},
                                                 std::chrono::day{static_cast<unsigned>(*d)}};
    if (!day.ok()) {
        return true;
    }
    const auto when = std::chrono::sys_days{day} + std::chrono::hours{*h} +
                      std::chrono::minutes{*mi} + std::chrono::seconds{*sec};
    return (std::chrono::system_clock::now() - when) > kStaleAfter;
}

}  // namespace

Scheduler::~Scheduler() = default;  // std::jthread joins and requests stop

auto Scheduler::start() -> void {
    if (!detail::env_value("CENSUS_API_KEY").has_value() ||
        !detail::env_value("DATABASE_URL").has_value()) {
        logger::Logger::getInstance().info(
            "state_refresh: scheduler not started (CENSUS_API_KEY or DATABASE_URL unset)");
        return;
    }
    worker_ = std::jthread{[](const std::stop_token& stop) {
        while (!stop.stop_requested()) {
            if (refresh_is_due()) {
                // The advisory lock inside run_refresh is what makes this safe
                // to do on every replica at once. A loser returns AlreadyRunning
                // immediately and logs nothing alarming.
                const auto result = run_refresh(/*dry_run=*/false, /*pinned_year=*/0);
                if (result.has_value()) {
                    logger::Logger::getInstance().info("state_refresh: scheduled run -- {}",
                                                       result->message);
                } else if (result.error().reason != Abort::AlreadyRunning) {
                    logger::Logger::getInstance().warn("state_refresh: scheduled run -- {}",
                                                       result.error().message);
                }
            }
            // Interruptible sleep: a container shutting down must not wait six
            // hours to exit. `std::this_thread::sleep_for` would do exactly
            // that, and the symptom is a deploy that appears to hang.
            std::condition_variable_any cv;
            std::mutex m;
            std::unique_lock lock{m};
            cv.wait_for(lock, stop, kTickInterval, [&stop] { return stop.stop_requested(); });
        }
    }};
    logger::Logger::getInstance().info(
        "state_refresh: scheduler started (tick {}h, stale after {}h)",
        kTickInterval.count(), kStaleAfter.count());
}

}  // namespace state_refresh
