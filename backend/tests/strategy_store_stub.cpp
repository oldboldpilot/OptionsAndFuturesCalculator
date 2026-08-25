/**
 * TEST-ONLY module implementation unit for `strategy_store`.
 *
 * @author Olumuyiwa Oluwasanmi
 *
 * strategy_store.cppm DECLARES make_pg_strategy_store; the real definition
 * lives in src/modules/strategy_store.cpp and pulls in `pg`, and therefore
 * libpq. test_calculator_service must not link libpq -- that is the invariant
 * backend/CMakeLists.txt documents around this service -- but it still has to
 * resolve the symbol, because RegisterCalculatorService references it.
 *
 * A plain test .cpp cannot supply that definition: the function belongs to
 * module `strategy_store`, and only a unit inside that module's purview may
 * define it. Hence this file, which is that unit and nothing else.
 *
 * It returns nullptr, which is a REAL supported value rather than a fiction --
 * it is exactly what a deployment with no DATABASE_URL gets, and the service
 * answers FAILED_PRECONDITION for it. The saved-scenario RPCs are covered in
 * the test through an in-memory fake injected via
 * RegisterCalculatorServiceForTest, not through this factory.
 */

module strategy_store;

namespace options_calculator::store {

auto make_pg_strategy_store(std::string_view conninfo) -> std::shared_ptr<IStrategyStore> {
    (void)conninfo;
    return nullptr;
}

}  // namespace options_calculator::store
