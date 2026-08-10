// @author Olumuyiwa Oluwasanmi
//
// Unit coverage for the entitlement gate's OWN discrimination between why an
// identity is not Pro -- the fix for the mortgagefvcalculator.com incident
// documented in CLAUDE.md and api_key.cppm's own doc comments on
// check_assistant_entitlement/check_strategy_entitlement/AssistantSurface:
// production logs showed 206 requests refused with `outcome=malformed`
// against 4 with `outcome=no-key` and zero authenticated, and every one of
// them got the SAME "is a Pro feature" message, because `Identity` used to
// carry no record of why authentication failed. `Identity::outcome` and the
// switch in check_assistant_entitlement/check_strategy_entitlement fix that;
// this file is the permanent regression gate for it.
//
// Plain hand-rolled check()/section() harness, matching
// tests/test_calculator_service.cpp, tests/test_option_pricing_service.cpp
// and tests/test_finance_service_validation.cpp -- NOT gtest (sensen coding
// policy, config/cpp_details.txt rule 39, BINDING).
//
// Deliberately NOT a gRPC-boundary test like the three files above: the gate
// functions under test (check_assistant_entitlement, check_strategy_entitlement)
// take an `Identity` directly and return a `grpc::Status` with no I/O of their
// own, so a hand-built `Identity` reaches the exact same code a real
// KeyRegistry::authenticate() call would hand it -- without needing a server,
// a channel, or FINANCE_API_KEYS. KeyRegistry itself (the JSON-backed
// registry, hashing, origin binding) is out of scope here; this file is
// scoped to the gate's OWN decision given an Identity, the same way
// test_mortgage_verification.cpp is scoped to mortgage_verification.cppm's
// verdicts given a parsed operation, not to the model that produced it.
//
// PRO_GATE_MODE is set to "enforce" via setenv() at the top of main(), before
// any gate call -- pro_gate_mode() re-reads the environment on every call
// (api_key.cpp), so this is not a one-time cached decision. A later section
// restores it to unset and re-proves the Off-mode admit-everyone behaviour,
// so this file does not leave the process's environment in a state that
// would surprise a test run after it.
#include <cstdlib>
#include <cstdio>
#include <string>
#include <string_view>

#include <grpcpp/grpcpp.h>

import api_key;

namespace {

int g_checks = 0;
int g_failures = 0;

auto check(bool condition, const std::string& what) -> void {
    ++g_checks;
    if (condition) {
        std::printf("  PASS: %s\n", what.c_str());
    } else {
        ++g_failures;
        std::printf("  FAIL: %s\n", what.c_str());
    }
}

auto section(const char* title) -> void { std::printf("\n=== %s ===\n", title); }

using options_calculator::auth::AssistantSurface;
using options_calculator::auth::check_assistant_entitlement;
using options_calculator::auth::check_strategy_entitlement;
using options_calculator::auth::Identity;
using options_calculator::auth::kMortgageSurface;
using options_calculator::auth::kStrategySurface;
using options_calculator::auth::KeyType;
using options_calculator::auth::Outcome;

/** A free-tier, unauthenticated identity carrying the given auth outcome. */
[[nodiscard]] auto identity_with_outcome(Outcome outcome) -> Identity {
    Identity id;
    id.id = "";
    id.tier = "";
    id.type = KeyType::Publishable;
    id.authenticated = false;
    id.outcome = outcome;
    return id;
}

[[nodiscard]] auto pro_identity(std::string tier) -> Identity {
    Identity id;
    id.id = "test-caller";
    id.tier = std::move(tier);
    id.type = KeyType::Secret;
    id.authenticated = true;
    id.outcome = Outcome::Ok;  // a genuinely successful auth
    return id;
}

/** Case-sensitive substring check, for readable failure output. */
[[nodiscard]] auto contains(std::string_view haystack, std::string_view needle) -> bool {
    return haystack.find(needle) != std::string_view::npos;
}

}  // namespace

auto main() -> int {
    // Enforce for every section below except the explicit Off-mode control
    // near the end.
    setenv("PRO_GATE_MODE", "enforce", 1);

    // =======================================================================
    section("1. check_strategy_entitlement: REFUSAL message class per outcome");
    // =======================================================================
    // A 2-leg request (leg_count > 1, so the gate is actually consulted) for
    // each of the four outcomes the gate now distinguishes, plus the two
    // outcomes that keep the ORIGINAL generic message (NoKey and a
    // well-formed but merely free-tier identity -- genuinely the same story:
    // nothing is wrong with what the caller sent).
    {
        const auto malformed = identity_with_outcome(Outcome::Malformed);
        const auto status = check_strategy_entitlement(malformed, 2);
        check(!status.ok(), "malformed key on a 2-leg request: refused");
        check(status.error_code() == grpc::StatusCode::PERMISSION_DENIED,
              "...with PERMISSION_DENIED (code 7), not UNAUTHENTICATED or anything else (got " +
                  std::to_string(static_cast<int>(status.error_code())) + ")");
        check(static_cast<int>(status.error_code()) == 7, "...specifically the integer 7");
        check(contains(status.error_message(), "malformed"),
              "...message names the actual problem: \"" + status.error_message() + "\"");
        check(contains(status.error_message(), "pk_live_") &&
                  contains(status.error_message(), "sk_live_"),
              "...message states the expected key prefixes");
        check(contains(status.error_message(), "43") && contains(status.error_message(), "51"),
              "...message states the expected shape (43 chars, 51 total)");
        check(!contains(status.error_message(), "is a Pro feature") &&
                  !contains(status.error_message(), "Pro feature"),
              "...message does NOT reuse the generic entitlement copy -- a malformed key is not "
              "an entitlement problem");
    }
    {
        const auto unknown = identity_with_outcome(Outcome::Unknown);
        const auto status = check_strategy_entitlement(unknown, 2);
        check(!status.ok(), "unknown (well-formed, unregistered) key on a 2-leg request: refused");
        check(static_cast<int>(status.error_code()) == 7, "...status code is 7");
        check(contains(status.error_message(), "record") ||
                  contains(status.error_message(), "recognised") ||
                  contains(status.error_message(), "recognized"),
              "...message names an UNKNOWN key, not entitlement: \"" + status.error_message() +
                  "\"");
        check(!contains(status.error_message(), "malformed"),
              "...and does not call a well-formed key malformed");
        check(!contains(status.error_message(), "revoked"),
              "...and does not call an unregistered key revoked");
    }
    {
        const auto revoked = identity_with_outcome(Outcome::Revoked);
        const auto status = check_strategy_entitlement(revoked, 2);
        check(!status.ok(), "revoked key on a 2-leg request: refused");
        check(static_cast<int>(status.error_code()) == 7, "...status code is 7");
        check(contains(status.error_message(), "revoked"),
              "...message says REVOKED, a different fix from malformed/unknown: \"" +
                  status.error_message() + "\"");
        check(contains(status.error_message(), "support"),
              "...and points the caller at support for a replacement, not at retyping the key");
    }
    {
        const auto no_key = identity_with_outcome(Outcome::NoKey);
        const auto status = check_strategy_entitlement(no_key, 2);
        check(!status.ok(), "no key at all on a 2-leg request: refused");
        check(static_cast<int>(status.error_code()) == 7, "...status code is 7");
        check(contains(status.error_message(), "Pro feature"),
              "...message is the GENERIC entitlement text -- no key was sent, so there is "
              "nothing to name as broken: \"" +
                  status.error_message() + "\"");
        check(!contains(status.error_message(), "malformed") &&
                  !contains(status.error_message(), "revoked") &&
                  !contains(status.error_message(), "does not match any key"),
              "...and does not fabricate a key-shape complaint that never happened");
    }
    {
        // A well-formed, successfully authenticated, but merely free-tier
        // identity -- the case that existed before this fix and must be
        // completely unchanged: the caller did nothing wrong, they are just
        // not Pro.
        Identity free_identity;
        free_identity.id = "known-caller";
        free_identity.tier = "free";
        free_identity.authenticated = true;
        free_identity.outcome = Outcome::Ok;
        const auto status = check_strategy_entitlement(free_identity, 3);
        check(!status.ok(), "authenticated free-tier identity on a 3-leg request: refused");
        check(static_cast<int>(status.error_code()) == 7, "...status code is 7");
        check(contains(status.error_message(), "Pro feature"),
              "...generic entitlement text, unchanged from before this fix: \"" +
                  status.error_message() + "\"");
        check(contains(status.error_message(), "3 legs"),
              "...and still names the actual leg count (pre-existing behaviour): \"" +
                  status.error_message() + "\"");
    }

    // =======================================================================
    section("2. check_strategy_entitlement: ADMIT path is unchanged");
    // =======================================================================
    // The half that matters most, per the task's own framing: a gate that
    // refuses everyone passes a refuse-only test. Pro and partner identities
    // must still sail through with grpc::Status::OK, REGARDLESS of `outcome`
    // -- is_pro() is checked before outcome is ever consulted.
    {
        const auto pro = pro_identity("pro");
        const auto status = check_strategy_entitlement(pro, 4);
        check(status.ok(), "pro identity, 4-leg iron condor: admitted (got " +
                                (status.ok() ? std::string{"OK"}
                                            : std::to_string(static_cast<int>(status.error_code())) +
                                                  ": " + status.error_message()) +
                                ")");
    }
    {
        const auto partner = pro_identity("partner");
        const auto status = check_strategy_entitlement(partner, 4);
        check(status.ok(), "partner identity, 4-leg iron condor: admitted");
    }
    {
        // Single-leg positions stay free unconditionally -- even for an
        // identity that would otherwise be refused on outcome grounds. This
        // is the SAME control test_calculator_service.cpp's own section 3
        // relies on, exercised here directly against the gate function.
        const auto malformed = identity_with_outcome(Outcome::Malformed);
        const auto status = check_strategy_entitlement(malformed, 1);
        check(status.ok(), "single-leg call with a MALFORMED key: still admitted (leg_count <= 1 "
                            "is unconditionally free)");
    }

    // =======================================================================
    section("3. check_assistant_entitlement (kStrategySurface): message class per outcome");
    // =======================================================================
    {
        const auto malformed = identity_with_outcome(Outcome::Malformed);
        const auto status = check_assistant_entitlement(malformed, kStrategySurface);
        check(!status.ok(), "ParseStrategy, malformed key: refused");
        check(static_cast<int>(status.error_code()) == 7, "...status code is 7");
        check(status.error_message() == std::string{kStrategySurface.malformed_message},
              "...message is EXACTLY kStrategySurface.malformed_message, not the generic text");
        check(status.error_message() != std::string{kStrategySurface.message},
              "...and is NOT the generic 'is a Pro feature' message");
    }
    {
        const auto unknown = identity_with_outcome(Outcome::Unknown);
        const auto status = check_assistant_entitlement(unknown, kStrategySurface);
        check(!status.ok(), "ParseStrategy, unknown key: refused");
        check(static_cast<int>(status.error_code()) == 7, "...status code is 7");
        check(status.error_message() == std::string{kStrategySurface.unknown_message},
              "...message is EXACTLY kStrategySurface.unknown_message");
    }
    {
        const auto revoked = identity_with_outcome(Outcome::Revoked);
        const auto status = check_assistant_entitlement(revoked, kStrategySurface);
        check(!status.ok(), "ParseStrategy, revoked key: refused");
        check(static_cast<int>(status.error_code()) == 7, "...status code is 7");
        check(status.error_message() == std::string{kStrategySurface.revoked_message},
              "...message is EXACTLY kStrategySurface.revoked_message");
    }
    {
        const auto no_key = identity_with_outcome(Outcome::NoKey);
        const auto status = check_assistant_entitlement(no_key, kStrategySurface);
        check(!status.ok(), "ParseStrategy, no key: refused");
        check(static_cast<int>(status.error_code()) == 7, "...status code is 7");
        check(status.error_message() == std::string{kStrategySurface.message},
              "...message is EXACTLY the ORIGINAL generic text -- unchanged for this outcome");
    }
    {
        Identity free_identity;
        free_identity.tier = "free";
        free_identity.authenticated = true;
        free_identity.outcome = Outcome::Ok;
        const auto status = check_assistant_entitlement(free_identity, kStrategySurface);
        check(!status.ok(), "ParseStrategy, authenticated free-tier identity: refused");
        check(static_cast<int>(status.error_code()) == 7, "...status code is 7");
        check(status.error_message() == std::string{kStrategySurface.message},
              "...message is EXACTLY the original generic text -- unchanged for this outcome");
    }

    // =======================================================================
    section("4. check_assistant_entitlement (kMortgageSurface): the ACTUAL incident");
    // =======================================================================
    // mortgagefvcalculator.com's own surface. Same shape as section 3, but
    // this is the exact combination that produced 206 misleading refusals in
    // production: a malformed key on ParseOperation.
    {
        const auto malformed = identity_with_outcome(Outcome::Malformed);
        const auto status = check_assistant_entitlement(malformed, kMortgageSurface);
        check(!status.ok(), "ParseOperation, malformed key: refused");
        check(static_cast<int>(status.error_code()) == 7, "...status code is 7");
        check(status.error_message() == std::string{kMortgageSurface.malformed_message},
              "...message is EXACTLY kMortgageSurface.malformed_message: \"" +
                  status.error_message() + "\"");
        check(status.error_message() != std::string{kMortgageSurface.message},
              "...and is NOT the 'is a Pro feature' text this incident was misdiagnosed against");
    }
    {
        const auto revoked = identity_with_outcome(Outcome::Revoked);
        const auto status = check_assistant_entitlement(revoked, kMortgageSurface);
        check(!status.ok(), "ParseOperation, revoked key: refused");
        check(static_cast<int>(status.error_code()) == 7, "...status code is 7");
        check(status.error_message() == std::string{kMortgageSurface.revoked_message},
              "...message is EXACTLY kMortgageSurface.revoked_message");
    }
    {
        const auto unknown = identity_with_outcome(Outcome::Unknown);
        const auto status = check_assistant_entitlement(unknown, kMortgageSurface);
        check(!status.ok(), "ParseOperation, unknown key: refused");
        check(static_cast<int>(status.error_code()) == 7, "...status code is 7");
        check(status.error_message() == std::string{kMortgageSurface.unknown_message},
              "...message is EXACTLY kMortgageSurface.unknown_message");
    }

    // =======================================================================
    section("5. check_assistant_entitlement: ADMIT path is unchanged");
    // =======================================================================
    {
        const auto pro = pro_identity("pro");
        const auto status = check_assistant_entitlement(pro, kStrategySurface);
        check(status.ok(), "pro identity, ParseStrategy: admitted");
    }
    {
        const auto partner = pro_identity("partner");
        const auto status = check_assistant_entitlement(partner, kMortgageSurface);
        check(status.ok(), "partner identity, ParseOperation: admitted");
    }
    {
        // A Pro identity that somehow also carries a non-Ok outcome (should
        // not occur through the real authenticate() path, but the gate must
        // not be fooled by it either way -- is_pro() is checked FIRST).
        auto pro_but_odd = pro_identity("partner");
        pro_but_odd.outcome = Outcome::Malformed;
        const auto status = check_assistant_entitlement(pro_but_odd, kMortgageSurface);
        check(status.ok(), "pro/partner tier wins regardless of a stray outcome value: admitted");
    }

    // =======================================================================
    section("6. GateMode::Off: every outcome is admitted, matching pre-existing behaviour");
    // =======================================================================
    {
        unsetenv("PRO_GATE_MODE");
        const auto malformed = identity_with_outcome(Outcome::Malformed);
        const auto strategy_status = check_strategy_entitlement(malformed, 4);
        check(strategy_status.ok(),
              "PRO_GATE_MODE unset: even a malformed-key 4-leg request is admitted (gate Off)");
        const auto assistant_status = check_assistant_entitlement(malformed, kMortgageSurface);
        check(assistant_status.ok(),
              "PRO_GATE_MODE unset: even a malformed-key ParseOperation call is admitted");
        setenv("PRO_GATE_MODE", "enforce", 1);  // restore for anything run after this binary
    }

    // -----------------------------------------------------------------
    std::printf("\n%d checks, %d failures\n", g_checks, g_failures);
    return g_failures == 0 ? 0 : 1;
}
