/**
 * The vendored client proto must not drift from the canonical one.
 *
 * @author Olumuyiwa Oluwasanmi
 *
 * `clients/mortgagefv/proto/finance.proto` is a VENDORED COPY of
 * `backend/proto/finance.proto`, carried so an integrator can generate stubs
 * without cloning this repository. Its own header says "do not edit by hand"
 * and records the upstream commit it was taken from.
 *
 * NOTHING ENFORCED THAT, and it rotted. Measured 2026-08-29: the vendored copy
 * still recorded commit 901ba0c1 (2026-08-19) while the canonical file had
 * since gained `ComputeRentVsBuyBatch`, the seven amortising RentVsBuyRequest
 * fields, and fourteen RentVsBuyResponse fields. Ten days of drift, and a
 * CLIENT TEAM hit it before anybody here did -- they reported field numbering
 * they could not reconcile, which is exactly the symptom a stale contract
 * produces.
 *
 * That report was WRONG about the specific field, and the check still had to
 * exist. `appraisal_fee` is field 5 in the canonical proto, field 5 in the
 * vendored proto, and field 5 on the live wire -- decoded from a production
 * ClosingCostsResponse, not inferred. But an integrator reading a stale
 * contract has no way to tell a real mismatch from an imagined one, and
 * "prove it isn't drifting" is not a thing a client can do from outside.
 *
 * Compares BELOW THE HEADER only. The vendored file's provenance banner is
 * meant to differ -- it names the commit it was synced from -- so comparing
 * whole files would fail on every sync and be switched off within a week.
 */
#include <cstdio>
#include <filesystem>
#include <fstream>
#include <sstream>
#include <string>

namespace {

// Injected by CMake -- see the comment on canonical_proto().
#ifndef kBackendDir
#endif

int g_checks = 0;
int g_failures = 0;

void check(bool condition, const std::string& what) {
    ++g_checks;
    if (condition) {
        std::printf("  PASS: %s\n", what.c_str());
    } else {
        std::printf("  FAIL: %s\n", what.c_str());
        ++g_failures;
    }
}

/** Everything from the first `syntax` line on -- i.e. the contract itself,
 *  with the vendored provenance header excluded by construction. */
[[nodiscard]] auto contract_body(const std::filesystem::path& p) -> std::string {
    std::ifstream in(p);
    if (!in) {
        return {};
    }
    std::stringstream ss;
    ss << in.rdbuf();
    std::string all = ss.str();
    const auto at = all.find("syntax");
    return at == std::string::npos ? std::string{} : all.substr(at);
}

/**
 * Absolute paths baked in at configure time.
 *
 * The first version of this resolved relative to the working directory and
 * "passed" under ctest while failing from the repo root -- a check whose result
 * depends on where you stand is not a check. CMake knows exactly where these
 * files are; ask it once rather than guessing at runtime.
 */
[[nodiscard]] auto canonical_proto() -> std::filesystem::path {
    return std::filesystem::path{kBackendDir} / "proto" / "finance.proto";
}

[[nodiscard]] auto vendored_proto() -> std::filesystem::path {
    return std::filesystem::path{kRepoRoot} / "clients" / "mortgagefv" / "proto" / "finance.proto";
}

}  // namespace

auto main() -> int {
    std::printf("Vendored proto drift check\n\n");

    const auto canonical = canonical_proto();
    const auto vendored = vendored_proto();

    check(std::filesystem::exists(canonical), "found backend/proto/finance.proto");
    check(std::filesystem::exists(vendored), "found clients/mortgagefv/proto/finance.proto");
    if (!std::filesystem::exists(canonical) || !std::filesystem::exists(vendored)) {
        std::printf("\n%d checks, %d failures\n", g_checks, g_failures);
        return 1;
    }

    const std::string a = contract_body(canonical);
    const std::string b = contract_body(vendored);

    check(!a.empty(), "canonical proto has a `syntax` line and a body");
    check(!b.empty(), "vendored proto has a `syntax` line and a body");

    // The whole point. If this fails, re-copy the canonical body under the
    // vendored header and update that header's recorded commit -- do not edit
    // the vendored contract by hand, which is how the two diverged before.
    check(a == b,
          "the vendored client proto is byte-identical to the canonical one below the "
          "provenance header (if this fails, re-sync it; a client generating stubs from a "
          "stale contract cannot tell a real field mismatch from an imagined one)");

    // A named canary for the drift that actually happened, so a future failure
    // says WHAT went missing rather than only that something did.
    check(b.find("ComputeRentVsBuyBatch") != std::string::npos,
          "vendored proto carries ComputeRentVsBuyBatch (absent during the 2026-08-19 to "
          "2026-08-29 drift)");
    check(b.find("monthly_taxes_ins_maintenance") != std::string::npos,
          "vendored proto carries the amortising RentVsBuyRequest fields");
    check(b.find("real_buying_advantage") != std::string::npos,
          "vendored proto carries the inflation-adjusted RentVsBuyResponse fields");

    std::printf("\n%d checks, %d failures\n", g_checks, g_failures);
    return g_failures == 0 ? 0 : 1;
}
