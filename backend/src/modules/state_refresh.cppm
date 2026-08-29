/**
 * Weekly refresh of per-state housing assumptions from the US Census ACS.
 *
 * @author Olumuyiwa Oluwasanmi
 *
 * ---------------------------------------------------------------------------
 * WHAT THIS IS
 *
 * `public.state_assumptions` holds fifty rows -- one per US state -- feeding the
 * programmatic pages (`/tools/rent-vs-buy/{state}` and friends). Three of its
 * numbers come from the Census Bureau's American Community Survey 5-year
 * estimates and are refreshed weekly; the rest are hand-authored and must never
 * be touched by a machine.
 *
 * This module fetches, validates and writes. It is the backend half of a job
 * that used to run in the web app.
 *
 * ---------------------------------------------------------------------------
 * WHY THE INTERFACE NAMES NO `pg` TYPE
 *
 * Exactly the reason `strategy_store.cppm` gives, and it is enforced the same
 * way: this file imports only `std`, and `import pg;` lives solely in
 * `state_refresh.cpp`, whose imports are not re-exported. A single-file module
 * would satisfy the compiler and silently make libpq symbols reachable from
 * `finance_service.cpp` -- every pricing RPC in that translation unit included.
 * Verify on the built objects, not by reading:
 *
 *     nm -uC finance_service.cpp.o | grep -c 'pg::'   -> 0
 *     nm -uC state_refresh.cpp.o   | grep -c 'pg::'   -> non-zero
 *
 * ---------------------------------------------------------------------------
 * THE FOUR RULES THIS JOB EXISTS TO NOT BREAK
 *
 *  1. EDITORIAL COLUMNS ARE NEVER WRITTEN. `insurance_annual`,
 *     `state_income_tax` and `note` are not published by the ACS. A refresh
 *     that touched them would silently destroy hand-authored content, and it
 *     would look like a successful run. Enforced in the DATABASE by a
 *     column-scoped GRANT to `ofc_refresh`, not merely by this code
 *     remembering -- see migration 07.
 *
 *  2. BAD UPSTREAM DATA NEVER REACHES THE SITE. Out-of-bounds values are
 *     REFUSED, never clamped. Clamping is a fabricated default wearing a
 *     validator's clothes: it produces a number nobody measured and nothing
 *     downstream can tell from a real one.
 *
 *  3. AN ABORTED RUN DOES NOT TOUCH `refreshed_at`. Stale-but-honest beats
 *     fresh-but-wrong, and it only stays honest if the freshness marker keeps
 *     telling the truth. Bumping `refreshed_at` on a run that wrote nothing
 *     would make every downstream staleness check lie -- the same defect as a
 *     LIVE badge derived from request status rather than from the data's own
 *     timestamp, which this project has already shipped once and fixed.
 *
 *  4. ONE VINTAGE PER RUN. The year fallback accepts the FIRST year with
 *     enough usable rows and writes only that year's numbers. A table holding
 *     2023 figures for forty states and 2022 for ten is not a dataset; it is
 *     two datasets wearing one `data_year`.
 */
export module state_refresh;

import std;

export namespace state_refresh {

/** Why a run produced no write. Every value is a decided answer, not an error. */
enum class Abort : std::uint8_t {
    None,
    NotConfigured,     ///< CENSUS_API_KEY absent -- a SUPPORTED state, not a failure
    UpstreamFailed,    ///< every candidate vintage failed to fetch or parse
    TooFewUsableRows,  ///< a vintage returned data, but under the floor
    AlreadyRunning,    ///< another replica holds the advisory lock
    StoreUnavailable,  ///< no database
    WriteFailed,       ///< the transaction did not commit
};

/**
 * What a SUCCESSFUL run produced. Failure never travels in here.
 *
 * Rule 32 -- railway-oriented: every fallible function in this module returns
 * `std::expected`, and an abort is an `Abort` on the error channel rather than
 * a flag inside the success type. The earlier draft carried `bool ok` beside
 * the counts, which lets a caller read `states_updated` from a run that wrote
 * nothing simply by forgetting to check the flag. `std::expected` makes that
 * unrepresentable: there is no Outcome to read unless there was one.
 *
 * The RPC layer maps the error channel to a REFUSAL rather than a transport
 * error, because an unusable upstream is an answer -- that translation belongs
 * at the boundary, not in this type.
 */
struct Outcome {
    std::int32_t data_year = 0;
    std::int32_t states_updated = 0;
    std::int32_t states_rejected = 0;  ///< parsed but failed the plausibility bounds
    std::string data_source;           ///< "US Census ACS 5-year 2023"
    std::string message;               ///< human-readable; safe to show a caller
};

/** An abort with the sentence to show a caller. */
struct Refusal {
    Abort reason = Abort::None;
    std::string message;
};

/** One state's refreshed figures, after validation. */
struct StateRow {
    std::string slug;
    std::string median_price;       ///< decimal string
    std::string median_rent;        ///< decimal string
    std::string property_tax_rate;  ///< percent, 2dp, decimal string
};

namespace detail {

/**
 * An environment variable as a value, never as a `const char*`.
 *
 * Rule 3. `std::getenv` hands back a raw pointer into environ, and every call
 * site that keeps one has to remember it may be null and may be invalidated by
 * a later `setenv`. Converting once, at the boundary, means no raw pointer
 * survives into this module's logic.
 */
[[nodiscard]] auto env_value(const char* name) -> std::optional<std::string> {
    const char* const raw = std::getenv(name);
    if (raw == nullptr || raw[0] == '\0') {
        return std::nullopt;
    }
    return std::string{raw};
}

// PURE HELPERS, DEFINED IN THE INTERFACE ON PURPOSE.
//
// `validate_acs_row` and `candidate_years` touch no database, no network and no
// clock -- they are total functions over their arguments. Defining them here
// rather than in the implementation unit means a test can exercise them by
// importing this module ALONE: no libpq, no SGEE, no engine.
//
// The first attempt put them in state_refresh.cpp beside the store code, and
// `test_state_refresh` then failed to build with `module 'sgee.runtime.resilience'
// not found` -- a validation test transitively dragging in a Raft queue. That
// is the signal that the split was in the wrong place, not that the test needed
// more link libraries.

/** Trims ASCII whitespace. */
[[nodiscard]] constexpr auto trim(std::string_view s) noexcept -> std::string_view {
    while (!s.empty() && (s.front() == ' ' || s.front() == '\t' || s.front() == '\n' ||
                          s.front() == '\r')) {
        s.remove_prefix(1);
    }
    while (!s.empty() && (s.back() == ' ' || s.back() == '\t' || s.back() == '\n' ||
                          s.back() == '\r')) {
        s.remove_suffix(1);
    }
    return s;
}

/** Parses a bare decimal. Returns nullopt for anything that is not one. */
[[nodiscard]] auto to_double(std::string_view s) -> std::optional<double> {
    s = detail::trim(s);
    if (s.empty()) {
        return std::nullopt;
    }
    double out = 0;
    const auto* first = s.data();
    const auto* last = s.data() + s.size();
    const auto res = std::from_chars(first, last, out);
    if (res.ec != std::errc{} || res.ptr != last) {
        return std::nullopt;
    }
    return out;
}

/** Fixed-point render, `places` decimals, half-up. Used for the money and rate
 *  columns so what lands in Postgres is the string this code decided on rather
 *  than whatever a locale-sensitive default formatter produced. */
[[nodiscard]] auto fixed(double v, int places) -> std::string {
    return std::format("{:.{}f}", v, places);
}

/** Lower-case, spaces to hyphens: "New Hampshire" -> "new-hampshire". Mirrors
 *  the slug convention migration 07 seeded. */
[[nodiscard]] constexpr auto slugify(std::string_view name) -> std::string {
    std::string out;
    out.reserve(name.size());
    for (const char c : name) {
        if (c == ' ') {
            out.push_back('-');
        } else {
            // Not std::tolower: it is locale-dependent and not constant-evaluable.
            // State names are ASCII by contract, so the arithmetic is both correct
            // and usable at compile time.
            out.push_back((c >= 'A' && c <= 'Z') ? static_cast<char>(c - 'A' + 'a') : c);
        }
    }
    return out;
}

/** The two jurisdictions the ACS returns that are not one of the fifty states.
 *  Skipped rather than refused: their presence is expected, so counting them as
 *  rejections would make a healthy run look degraded. */
[[nodiscard]] constexpr auto is_not_a_state(std::string_view name) noexcept -> bool {
    return name == "District of Columbia" || name == "Puerto Rico";
}

}  // namespace detail

inline auto candidate_years(std::int32_t year_now) -> std::vector<std::int32_t> {
    if (const auto override_list = detail::env_value("CENSUS_ACS_YEARS"); override_list.has_value()) {
        std::vector<std::int32_t> years;
        std::string_view v{*override_list};
        while (!v.empty()) {
            const auto comma = v.find(',');
            const auto tok = detail::trim(v.substr(0, comma));
            std::int32_t y = 0;
            if (std::from_chars(tok.data(), tok.data() + tok.size(), y).ec == std::errc{} &&
                y > 1900) {
                years.push_back(y);
            }
            if (comma == std::string_view::npos) {
                break;
            }
            v.remove_prefix(comma + 1);
        }
        if (!years.empty()) {
            return years;
        }
    }
    // ACS 5-year releases lag ~18 months, so the newest plausible vintage is
    // two years back. Three candidates rather than two: a January run before
    // that year's release would otherwise have only one chance.
    return {year_now - 2, year_now - 3, year_now - 4};
}

inline auto validate_acs_row(std::string_view name, std::string_view raw_price,
                      std::string_view raw_rent, std::string_view raw_taxes)
    -> std::expected<StateRow, std::string> {
    // ACS encodes "not available" as a large NEGATIVE sentinel (-666666666 and
    // friends) rather than as null. Treating any non-positive as missing covers
    // every sentinel without hardcoding the list, and a zero median price is
    // not a measurement either.
    const auto price = detail::to_double(raw_price);
    const auto rent = detail::to_double(raw_rent);
    const auto taxes = detail::to_double(raw_taxes);
    if (!price || !rent || !taxes || *price <= 0 || *rent <= 0 || *taxes <= 0) {
        return std::unexpected(std::string{"missing or sentinel value from the ACS"});
    }

    // Derived, never guessed: both inputs must be real for the rate to mean
    // anything, which the check above already guarantees.
    const double rate = std::round((*taxes / *price) * 100.0 * 100.0) / 100.0;

    // REFUSED, not clamped. A clamped value is a number nobody measured, and
    // nothing downstream can distinguish it from one that was.
    if (*price < 50'000 || *price > 3'000'000) {
        return std::unexpected(
            std::format("median_price {:.0f} outside 50000..3000000", *price));
    }
    if (*rent < 300 || *rent > 8'000) {
        return std::unexpected(std::format("median_rent {:.0f} outside 300..8000", *rent));
    }
    if (rate < 0.05 || rate > 4.0) {
        return std::unexpected(
            std::format("property_tax_rate {:.2f} outside 0.05..4", rate));
    }

    return StateRow{.slug = detail::slugify(name),
                    .median_price = detail::fixed(*price, 2),
                    .median_rent = detail::fixed(*rent, 2),
                    .property_tax_rate = detail::fixed(rate, 2)};
}

/**
 * Validates one ACS row and derives the tax rate, or explains the refusal.
 *
 * SPLIT OUT so a test exercises the SAME refusal the RPC does -- the reason
 * `validate_closing_costs` is split out of `finance_service.cpp`. A validator
 * reachable only through a network call is a validator nobody tests at the
 * boundaries.
 *
 * `name` is the ACS `NAME` column; matching to a slug is the caller's job.
 *
 * Returns the reason on the error channel rather than through an out-parameter
 * (rule 32). An out-param `std::string& reason` compiles fine and is readable
 * on a run that SUCCEEDED, which is how a refusal message ends up logged beside
 * a row that was actually accepted.
 */


/** Candidate ACS vintages, newest first, derived from `year_now`.
 *
 *  ACS 5-year releases lag roughly eighteen months, so the newest plausible
 *  vintage is two years back. DERIVED rather than hardcoded: a literal
 *  `{2023, 2022}` is a line that silently rots every January and whose failure
 *  looks like an upstream outage. `CENSUS_ACS_YEARS` overrides it as a
 *  comma-separated list, which is how a bad vintage gets pinned away from
 *  without a deploy. */


/** The floor below which a run aborts and keeps existing data. */
inline constexpr std::int32_t kMinUsableStates = 40;

/** Runs one refresh. `dry_run` validates and reports without writing.
 *  `pinned_year` of 0 means "try the candidates newest-first".
 *
 *  Never throws. An abort arrives as `std::unexpected<Refusal>` -- the caller
 *  wants to know the site is serving last week's numbers, not to receive a
 *  transport error, and the RPC layer turns that into a refusal response. */
[[nodiscard]] auto run_refresh(bool dry_run, std::int32_t pinned_year)
    -> std::expected<Outcome, Refusal>;

/** The most recent run's bookkeeping, for an admin surface. */
struct JobStatus {
    bool found = false;
    std::string status;
    std::string last_run_at;
    std::string last_success_at;
    std::string last_error;
    std::int32_t items_processed = 0;
};

/** Bookkeeping for the last run, or the reason it could not be read. */
[[nodiscard]] auto last_run() -> std::expected<JobStatus, Refusal>;

/** One row as stored, including the editorial columns this job never writes. */
struct StoredAssumption {
    std::string slug;
    std::string name;
    std::string abbr;
    std::string median_price;
    std::string property_tax_rate;
    std::string insurance_annual;   ///< editorial; empty when unset
    std::string state_income_tax;   ///< editorial; empty when unset
    std::string median_rent;
    std::string note;               ///< editorial; empty when unset
    std::string data_source;
    std::int32_t data_year = 0;
    std::string refreshed_at;       ///< RFC3339; EMPTY when never refreshed
};

/**
 * Reads the table. An empty `slug` returns all fifty, ordered by slug.
 *
 * `refreshed_at` comes back EMPTY rather than as an epoch when the column is
 * NULL, and that distinction is the whole point: a caller deriving a freshness
 * badge must be able to tell "never refreshed" from "refreshed in 1970". This
 * mirrors `ChainResponse.fetched_at`, where the frontend already learned that
 * a missing timestamp has to render as unknown and never as stale-but-dated.
 */
[[nodiscard]] auto read_assumptions(std::string_view slug)
    -> std::expected<std::vector<StoredAssumption>, Refusal>;

/**
 * Starts the weekly refresh on a background thread. Idempotent; returns a
 * stopper that joins the thread.
 *
 * RUNS ON BOTH REPLICAS ON PURPOSE. A cron that fires on one replica needs a
 * leader, and there is no leader here -- Railway replicas have no stable
 * identity and no ordinal (CLAUDE.md records why a consensus cluster across
 * `numReplicas` is structurally impossible on this platform). The advisory
 * lock already makes a double run harmless, so the cheap correct answer is to
 * let both try and let one win.
 *
 * CATCH-UP RATHER THAN A CALENDAR. The tick asks "has a successful run
 * happened in the last `kStaleAfter`?" rather than "is it Monday 06:00?". A
 * calendar trigger misses its slot whenever a deploy, a restart or a crash
 * lands on the hour -- and this job's whole point is cheap insurance, so a
 * missed week that nothing notices is the failure mode to design against.
 */
class Scheduler {
  public:
    Scheduler() = default;
    Scheduler(const Scheduler&) = delete;
    auto operator=(const Scheduler&) -> Scheduler& = delete;
    Scheduler(Scheduler&&) = delete;
    auto operator=(Scheduler&&) -> Scheduler& = delete;
    ~Scheduler();

    /** No-op when CENSUS_API_KEY or DATABASE_URL is absent, so a local build
     *  and a model-only deploy both start clean. */
    auto start() -> void;

  private:
    std::jthread worker_;
};

/** A successful run older than this makes the next tick act. Eight days rather
 *  than seven: a weekly job compared against exactly seven days races itself
 *  every week, running twice on the boundary or skipping depending on which
 *  side of a second the tick lands. */
inline constexpr auto kStaleAfter = std::chrono::hours{24 * 8};

/** How often the thread wakes. Cheap: it is one indexed SELECT unless a run is
 *  actually due. */
inline constexpr auto kTickInterval = std::chrono::hours{6};

}  // namespace state_refresh
