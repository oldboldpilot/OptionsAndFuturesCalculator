/**
 * Persistence for a signed-in user's saved calculator scenarios.
 *
 * @author Olumuyiwa Oluwasanmi
 *
 * WHY THIS IS AN ABSTRACT INTERFACE RATHER THAN A CONCRETE POSTGRES CLASS
 *
 * backend/CMakeLists.txt states an invariant about this codebase that is worth
 * keeping: calculator_service.cpp and finance_service.cpp must have ZERO
 * reachable symbols from `pg`/`inference_queue`/`inference_admission`, so the
 * pricing RPCs are structurally incapable of touching libpq no matter what else
 * shares their final link line.
 *
 * SaveStrategy/ListStrategies/DeleteStrategy genuinely need Postgres, and they
 * genuinely have to live in CalculatorServiceImpl -- gRPC binds one service
 * implementation per service, and they are RPCs on calculator.OptionsCalculator.
 * A naive `import pg;` in calculator_service.cpp would have satisfied the
 * compiler and quietly destroyed that invariant for every RPC in the file,
 * including the pricing ones.
 *
 * So the seam is here instead. THIS interface unit names no pg type and
 * imports no pg module; `import pg;` appears only in strategy_store.cpp, the
 * module IMPLEMENTATION unit, whose imports are not re-exported. A consumer
 * that writes `import strategy_store;` therefore still has zero reachable pg
 * symbols, and the invariant holds as stated for exactly the reason it always
 * did -- C++20 modules expose only what a translation unit imports.
 *
 * The testability follows from the same seam rather than being a second reason
 * for it: a test can implement IStrategyStore over an in-memory map and drive
 * the real RPCs with no database at all.
 */

export module strategy_store;

import std;

export namespace options_calculator::store {

/**
 * One stored scenario, as the database actually holds it.
 *
 * `payload_json` is an opaque JSON document to this module. It is a serialized
 * calculator.StrategyRequest, but naming that type here would drag the
 * generated protobuf headers into every consumer of this interface, and this
 * module has no reason to understand the shape -- it stores and returns it.
 *
 * Timestamps are RFC3339 UTC strings for the same reason ChainResponse's
 * `fetched_at` is: the browser is a first-class client, and JavaScript's
 * `number` is a float64.
 */
struct SavedRow {
    std::string id;
    std::string name;
    std::string symbol;
    std::string payload_json;
    std::string created_at;
    std::string updated_at;
};

struct SaveOutcome {
    SavedRow row;
    /** True when an existing row of the same (user, name) was updated. */
    bool replaced_existing = false;
};

/**
 * Why a store operation could not be carried out.
 *
 * `AtCapacity` is deliberately distinct from `Invalid`: the caller did nothing
 * wrong and the request would succeed after deleting something, which is a
 * different sentence to show a user and a different gRPC code to return.
 */
enum class StoreError : std::uint8_t {
    Unavailable,  // no database configured, or it could not be reached
    AtCapacity,   // this user already holds the maximum number of scenarios
    Invalid,      // malformed argument that never reached the database
    Internal,     // the query ran and the answer made no sense
};

[[nodiscard]] constexpr auto to_string(StoreError error) noexcept -> std::string_view {
    switch (error) {
        case StoreError::Unavailable: return "unavailable";
        case StoreError::AtCapacity: return "at-capacity";
        case StoreError::Invalid: return "invalid";
        case StoreError::Internal: return "internal";
    }
    return "unknown";
}

/**
 * The most scenarios one user may hold.
 *
 * A cap exists because every row here is written by an authenticated caller and
 * nothing else bounds the table: without one, a single Pro account can grow it
 * without limit, and the cost lands on the shared database rather than on them.
 *
 * 100 is chosen to be far above any plausible manual use -- these are named,
 * hand-built scenarios, not generated ones -- so a user who hits it has almost
 * certainly automated something, which is the case the cap is for.
 */
inline constexpr std::size_t kMaxPerUser = 100;

/** Longest accepted scenario name, in bytes after trimming. */
inline constexpr std::size_t kMaxNameBytes = 120;

/**
 * Saved-scenario storage for one user at a time.
 *
 * EVERY method takes `subject` -- the verified per-user identity -- as its
 * first argument, and every implementation MUST scope its query by it. There is
 * deliberately no "get by id" that omits it: an id-only lookup would return
 * another user's row whenever an id leaked or was guessed, and the way to make
 * that mistake impossible is to give callers no method that permits it.
 */
class IStrategyStore {
  public:
    IStrategyStore() = default;
    IStrategyStore(const IStrategyStore&) = delete;
    auto operator=(const IStrategyStore&) -> IStrategyStore& = delete;
    IStrategyStore(IStrategyStore&&) = delete;
    auto operator=(IStrategyStore&&) -> IStrategyStore& = delete;
    virtual ~IStrategyStore() = default;

    /**
     * Create, or replace the caller's existing scenario of the same name.
     *
     * Save-by-name is an upsert rather than an insert because the alternative
     * -- accumulating rows the list cannot tell apart -- is what a user pressing
     * Save twice would otherwise get.
     */
    [[nodiscard]] virtual auto save(std::string_view subject, std::string_view name,
                                    std::string_view symbol, std::string_view payload_json)
        -> std::expected<SaveOutcome, StoreError> = 0;

    /** This user's scenarios, most recently updated first. */
    [[nodiscard]] virtual auto list(std::string_view subject)
        -> std::expected<std::vector<SavedRow>, StoreError> = 0;

    /**
     * Delete one of this user's scenarios.
     *
     * Returns false -- not an error -- when no such row belongs to this user,
     * whether because it never existed or because it belongs to someone else.
     * Those two are the same answer on purpose: telling them apart would make
     * this an oracle for which ids exist.
     */
    [[nodiscard]] virtual auto remove(std::string_view subject, std::string_view id)
        -> std::expected<bool, StoreError> = 0;
};

/**
 * A Postgres-backed store, or nullptr.
 *
 * Returns nullptr when `conninfo` is empty -- which is the ordinary case for a
 * local build with no DATABASE_URL, and must not be fatal. The service treats a
 * null store as "this feature is unavailable here" and refuses the three RPCs
 * with FAILED_PRECONDITION, leaving every other RPC untouched. That mirrors the
 * assistants' own contract, where an unset MODEL_URL yields MODEL_UNAVAILABLE
 * rather than a broken image.
 *
 * Construction does NOT connect. pg::Pool opens connections lazily and
 * reconnects on its own, so a database that is down at boot does not stop the
 * engine from starting and serving everything else.
 */
[[nodiscard]] auto make_pg_strategy_store(std::string_view conninfo)
    -> std::shared_ptr<IStrategyStore>;

}  // namespace options_calculator::store
