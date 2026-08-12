// @author Olumuyiwa Oluwasanmi
//
// A refusal must name the tier the LIMIT came from, not the tier the caller
// asked for.
//
// `limits_for_tier` falls back to the anonymous allowance for a tier
// QUOTA_POLICY does not define. That fallback is the right direction -- an
// entitlement naming a tier that was renamed must not become unlimited access
// -- but it used to be completely silent, and `Decision::tier` still carried
// the REQUESTED name. The refusal then read "quota exceeded for tier 'pro'"
// while the number in force was anonymous's, which sends an operator to raise
// a limit that is not the one being applied.
//
// `load_policy` already refuses a QUOTA_API_KEYS entry naming an unknown tier,
// so the `admit` path cannot reach this state. `admit_identity` can: it takes
// the tier from a VERIFIED identity -- Supabase `app_metadata.tier` or a signed
// licence -- neither of which is checked against QUOTA_POLICY, because they are
// issued somewhere else entirely. CLAUDE.md records the live case: the running
// QUOTA_POLICY defines `pro`, the example in docs/FINANCE_API.md does not.
//
// The gate has to DISCRIMINATE, which is why both directions are here. A label
// that always carried the marker would pass a test that only checked the
// undefined tier, and would then mislabel every legitimate refusal instead.
// The defined-tier section pins that.
//
// The strong assertion is not the string: it is WHERE EACH CALLER IS CUT OFF.
// `pro` is given 5 requests/minute and `anonymous` 2, so a caller presenting an
// undefined tier being refused on its THIRD request is proof the anonymous
// number is the one in force -- a test that only read the message would still
// pass if the label were fixed and the limits were not.
//
// Plain hand-rolled check()/section() harness, matching
// tests/test_api_key_entitlement.cpp and tests/test_calculator_service.cpp --
// NOT gtest (sensen coding policy, config/cpp_details.txt rule 39, BINDING).
//
// QUOTA_POLICY is set via setenv() at the top of main(), before ANY call to
// QuotaEnforcer::instance(). Unlike PRO_GATE_MODE, this one genuinely is a
// one-time cached decision: the enforcer is a Meyers singleton that parses the
// policy in its constructor, so a setenv after the first instance() call would
// do nothing and the test would silently measure the wrong policy.
#include <cstdio>
#include <cstdlib>
#include <string>
#include <string_view>

#include <grpcpp/grpcpp.h>

import quota;

namespace {

int g_failures = 0;
int g_checks = 0;

void section(std::string_view name) { std::printf("\n-- %s\n", std::string(name).c_str()); }

void check(bool ok, std::string_view what) {
    ++g_checks;
    if (ok) {
        std::printf("  ok   %s\n", std::string(what).c_str());
    } else {
        ++g_failures;
        std::printf("  FAIL %s\n", std::string(what).c_str());
    }
}

/** Substring test spelled out so a failure prints what was actually returned. */
bool contains(const std::string& haystack, std::string_view needle) {
    return haystack.find(needle) != std::string::npos;
}

constexpr const char* kUndefinedMarker = "(undefined in QUOTA_POLICY; anonymous limits)";

/**
 * Charges `n` requests and returns the 1-based index of the first refusal, or 0
 * if every one was admitted.
 *
 * Each caller_id gets its own bucket, so a section can measure one tier without
 * the previous section's spend.
 */
int first_refusal(std::string_view caller_id, std::string_view tier, int n,
                  std::string* refusal_message) {
    for (int i = 1; i <= n; ++i) {
        const auto st = options_calculator::quota::QuotaEnforcer::instance().admit_identity(caller_id, tier,
                                                                        "TestMethod", 1.0);
        if (!st.ok()) {
            if (refusal_message != nullptr) *refusal_message = st.error_message();
            return i;
        }
    }
    return 0;
}

}  // namespace

int main() {
    // 5 requests/minute for `pro`, 2 for `anonymous`. The gap is what makes the
    // "which limit was actually applied" assertion possible at all; equal
    // numbers would make both sections indistinguishable.
    setenv("QUOTA_POLICY",
           R"({"anonymous_tier":"anonymous","tiers":{)"
           R"("anonymous":{"requests_per_minute":2,"compute_units_per_hour":100000},)"
           R"("pro":{"requests_per_minute":5,"compute_units_per_hour":100000}}})",
           1);

    auto& q = options_calculator::quota::QuotaEnforcer::instance();
    check(q.enabled(), "QUOTA_POLICY parsed and quotas are enabled");

    section("a tier the policy DEFINES is metered and named as itself");
    {
        std::string msg;
        const int refused_at = first_refusal("alice", "pro", 8, &msg);
        check(refused_at == 6, "pro is cut off on request 6, i.e. after its own 5/minute");
        check(contains(msg, "tier 'pro'"), "the refusal names the tier plainly");
        check(!contains(msg, kUndefinedMarker),
              "and carries NO undefined marker -- a defined tier must not be labelled one");
    }

    section("a tier the policy does NOT define is metered as anonymous and says so");
    {
        std::string msg;
        const int refused_at = first_refusal("bob", "business", 8, &msg);
        // The load-bearing assertion. 3 means the anonymous 2/minute applied;
        // 6 would mean it had somehow been given pro's allowance, and 0 would
        // mean an unknown tier bought unlimited access -- the outcome the
        // fallback exists to prevent.
        check(refused_at == 3, "an undefined tier is cut off on request 3, i.e. at anonymous's 2");
        check(contains(msg, "tier 'business"), "the refusal still names what the caller presented");
        check(contains(msg, kUndefinedMarker),
              "and marks that the number came from anonymous, not from 'business'");
    }

    section("the anonymous tier itself is not treated as undefined");
    {
        // `anonymous` IS defined by this policy, so it must take the plain
        // label. Getting this wrong would put the marker on every unkeyed
        // refusal in production, which is most of them.
        std::string msg;
        const int refused_at = first_refusal("carol", "anonymous", 8, &msg);
        check(refused_at == 3, "anonymous is cut off on request 3");
        check(!contains(msg, kUndefinedMarker), "and is labelled plainly, not as undefined");
    }

    section("an empty tier resolves to anonymous, and is not marked undefined");
    {
        // charge() maps an empty tier name to anonymous_tier_ before the lookup,
        // so this must not trip the marker either.
        std::string msg;
        const int refused_at = first_refusal("dave", "", 8, &msg);
        check(refused_at == 3, "an empty tier is cut off at anonymous's 2");
        check(!contains(msg, kUndefinedMarker), "and is not labelled undefined");
    }

    std::printf("\n%d checks, %d failures\n", g_checks, g_failures);
    return g_failures == 0 ? 0 : 1;
}
