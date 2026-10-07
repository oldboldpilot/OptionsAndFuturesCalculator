#!/usr/bin/env python3
"""Mutation arms for the stated-only assistant contract.

Each arm edits ONE anchor in a module the contract lives in, rebuilds the owning test with
CCACHE_DISABLE=1, runs it, prints which checks go RED, and RESTORES the file byte-identical
(try/finally, verified by sha256). An arm that does not turn anything red is not a check, so an
arm whose anchor is missing is reported SKIPPED and exits non-zero rather than passing silently.

WHY CCACHE_DISABLE=1: ccache hashes a translation unit's own text, not the module BMIs it
imports, so editing a module interface and rebuilding a consumer is a cache HIT that keeps the
OLD inlined body -- a mutation check that cannot fail (the project guide, "ccache ignores module BMIs").

Usage:  scripts/mutation_arms_stated_only.py [--list] [ARM_PREFIX ...]
Run ONE instance per build directory: two ninja processes in one build dir corrupt BMIs.
"""
import hashlib
import os
import subprocess
import sys

ROOT = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))
BUILD = os.path.join(ROOT, "backend", "build-lane")
MOD = os.path.join(ROOT, "backend", "src", "modules")
VER = os.path.join(MOD, "mortgage_verification.cppm")
SVC = os.path.join(MOD, "mortgage_assistant_service.cpp")
FIN = os.path.join(MOD, "finance_service.cpp")
DER = os.path.join(MOD, "mortgage_derivation.cppm")

# (name, file, old, new, [test targets], optional extra (old,new) applied to the same file)
ARMS = [
    ("A1 essential table: ComputePayment also REQUIRES future_value", VER,
     '    {"ComputePayment", "present_value", "", ""},',
     '    {"ComputePayment", "present_value", "", ""},\n    {"ComputePayment", "future_value", "", ""},',
     ["test_mortgage_verification"]),
    ("A2 stated-only filter: a Defaulted field is KEPT", VER,
     '        if (evidence[i] == Evidence::Defaulted) { continue; }\n        out.kept.push_back(input.fields[i].name);',
     '        out.kept.push_back(input.fields[i].name);',
     ["test_mortgage_verification"]),
    ("A3 range masking removed", VER,
     'auto mask_ranges(std::string_view text) -> std::string {\n    std::string out{text};',
     'auto mask_ranges(std::string_view text) -> std::string {\n    return std::string{text};\n    std::string out{text};',
     ["test_mortgage_verification"]),
    ("A4 convention constant claimed by another field still counts as stated", VER,
     'if (!v.is_zero() && is_convention_value(f.name, v) && !grounded_by_unclaimed_literal(ctx, f, kind, v)) {',
     'if (false && !v.is_zero() && is_convention_value(f.name, v) && !grounded_by_unclaimed_literal(ctx, f, kind, v)) {',
     ["test_mortgage_verification"]),
    ("A5 scaled literal loses the cash-flow negation", VER,
     '        if (field_name == "values") {\n            const std::size_t n = out.size();\n            for (std::size_t i = 0; i < n; ++i) out.push_back(out[i].negated());\n        }\n        return out;\n    }\n\n    switch (kind) {',
     '        return out;\n    }\n\n    switch (kind) {',
     ["test_mortgage_verification"]),
    ("A6 plain decimal text: default to_chars (exponent form)", VER,
     'std::to_chars(buffer.data(), buffer.data() + buffer.size(), v, std::chars_format::fixed);',
     'std::to_chars(buffer.data(), buffer.data() + buffer.size(), v);',
     ["test_mortgage_verification"]),
    ("A7 a bare condo is dues again", VER,
     '                if (detail::is_condo_word(w)) {',
     '                if (false && detail::is_condo_word(w)) {',
     ["test_mortgage_verification"]),
    ("A8 the spelled percent word no longer advances the adjacency scan", VER,
     '                i = w;\n', '                // i = w;\n',
     ["test_mortgage_verification"]),
    ("A9 a deposit as a percent of the price no longer grounds", VER,
     '    for (const auto& candidate : expand_down_from_percent(literals, kind, field)) {\n        if (value.within(candidate, tolerance)) { return true; }\n    }\n',
     '',
     ["test_mortgage_verification"]),
    ("A10 yearly is not a cadence word", VER,
     '{.word = "yearly", .per_year = 1},       ', '',
     ["test_mortgage_verification"]),
    ("A11 no cadence is ever unsupported", VER,
     'auto names_unsupported_cadence(std::string_view text) -> bool {\n    const std::string lower = detail::to_lower_copy(text);',
     'auto names_unsupported_cadence(std::string_view text) -> bool {\n    return false;\n    const std::string lower = detail::to_lower_copy(text);',
     ["test_mortgage_verification"]),
    ("A12 a declared field without a label", VER,
     '    {"annual_repairs", ', '    {"annual_repairs_UNLABELLED", ',
     ["test_mortgage_verification"]),
    ("A13 a spaced hyphen is a minus again (\"Atlanta - 479k\" lexes negative)", VER,
     '                negative_prefix = !em_dash && !follows_label;',
     '                negative_prefix = !em_dash;',
     ["test_mortgage_verification"]),
    ("A14 \"down to\" is a deposit again", VER,
     '            if ((w1 == "down" && !down_to) || w1 == "downpayment" || w1 == "deposit") {',
     '            if (w1 == "down" || w1 == "downpayment" || w1 == "deposit") {',
     ["test_mortgage_verification"]),
    ("A15 a HOA word followed by its own figure also claims the figure BEFORE it", VER,
     '                    if (governs_next) break;\n                    lit.names_hoa = true;',
     '                    lit.names_hoa = true;',
     ["test_mortgage_verification"]),
    ("A16 a HOA word already claimed by a figure before it claims the next one too", VER,
     '                        if (!claimed) { lit.names_hoa = true; }',
     '                        { lit.names_hoa = true; }',
     ["test_mortgage_verification"]),
    ("A17 an increment word before a figure beats a HOA word right after it", VER,
     'if (detail::names_concept(kBefore, wp) && !detail::is_hoa_word(detail::next_word(text, i))) {',
     'if (detail::names_concept(kBefore, wp)) {',
     ["test_mortgage_verification"]),
    ("A18 vacancy is only recognised AFTER the figure", VER,
     '            } else if (lit.tag == LiteralTag::Percent) {\n                // ...and it PRECEDES the figure',
     '            } else if (false) {\n                // ...and it PRECEDES the figure',
     ["test_mortgage_verification"]),
    ("A19 a no-deposit purchase no longer states the home value", VER,
     'if (lower_has(ctx.lower, "no down") || lower_has(ctx.lower, "nothing down") ||',
     'if (false && lower_has(ctx.lower, "no down") || lower_has(ctx.lower, "nothing down") ||',
     ["test_mortgage_verification"]),
    ("S1 advice fix: the carve-out is removed (an advice phrase + figures is refused again)", SVC,
     '        !has_calculable_content(ctx->utterance)) {\n        populate_refusal(',
     '        true) {\n        populate_refusal(',
     ["test_mortgage_assistant_service"]),
    ("S2 advice fix: two figures only (the one-figure-and-a-named-calculation clause is removed)", SVC,
     '    if (!amount_or_rate) { return false; }\n    std::string lower;',
     '    if (true) { return false; }\n    std::string lower;',
     ["test_mortgage_assistant_service"]),
    ("F1 finance: ComputeFutureValue requires a payment again", FIN,
     '        if (request->payment().empty() && request->present_value().empty()) {\n            return missing_field("payment (or present_value)");\n        }\n        READ_DECIMAL(pmt_v, request->payment(), "payment");\n        READ_DECIMAL(pv_v, request->present_value(), "present_value");\n        if (auto s = check_compound_growth_safe_periods',
     '        REQUIRE_DECIMAL(pmt_v, request->payment(), "payment");\n        READ_DECIMAL(pv_v, request->present_value(), "present_value");\n        if (auto s = check_compound_growth_safe_periods',
     ["test_finance_service_validation"]),
    ("F2 finance: a batch with omitted optional arrays is ragged again", FIN,
     'return m == 0 ? Status::OK : same(m, which);', 'return same(m, which);',
     ["test_finance_service_validation"]),
    ("D1 derivation: a bare figure of money size is no longer a money fact", DER,
     'if (lit.value.units() >= 1000 * mv::Decimal::kScale && lit.scale == 1 && !lit.names_hoa) {',
     'if (false && lit.value.units() >= 1000 * mv::Decimal::kScale && lit.scale == 1 && !lit.names_hoa) {',
     ["test_mortgage_derivation"]),
    ("D3 derivation: an annual rate is rewritten to a monthly one", DER,
     '        if (annual_cadence && c.field == "rate") {',
     '        if (false && annual_cadence && c.field == "rate") {',
     ["test_mortgage_derivation"]),
    ("D4 derivation: periods stay in months beside an annual rate", DER,
     '(annual_periods && c.field == "periods") ? *annual_periods : c.values.front();',
     'c.values.front();',
     ["test_mortgage_derivation"]),
    ("D2 derivation: a stated non-monthly compounding no longer stops the x12 rules", DER,
     'const bool other_cadence = names_non_monthly_compounding(earlier) || names_non_monthly_compounding(latest);',
     'const bool other_cadence = false;',
     ["test_mortgage_derivation"]),
]


def sha(path):
    with open(path, "rb") as f:
        return hashlib.sha256(f.read()).hexdigest()


def run_arm(name, path, old, new, targets, env):
    original = open(path).read()
    before = sha(path)
    status = 0
    try:
        if old not in original:
            print(f"## {name}\n   SKIPPED: anchor not found (the code moved; an arm that cannot mutate proves nothing)", flush=True)
            return 2
        with open(path, "w") as f:
            f.write(original.replace(old, new, 1))
        build = subprocess.run(["ninja", "-C", BUILD, "-j5", "-l", "28", *targets],
                               capture_output=True, text=True, env=env)
        if build.returncode != 0:
            print(f"## {name}\n   BUILD FAILED: {(build.stdout + build.stderr)[-400:]}", flush=True)
            return 2
        print(f"## {name}", flush=True)
        red = False
        for target in targets:
            t = subprocess.run([os.path.join(BUILD, target)], capture_output=True, text=True)
            out = (t.stdout + t.stderr).splitlines()
            summary = [l.strip() for l in out if "checks" in l and ("failure" in l or "failed" in l)]
            print(f"   [{target}] rc={t.returncode} {summary[-1] if summary else ''}", flush=True)
            for line in [l.strip() for l in out if l.strip().startswith("FAIL")][:6]:
                print("     " + line[:200], flush=True)
            red = red or t.returncode != 0
        if not red:
            print("   NOT RED: this arm did not fail anything, so it is not evidence", flush=True)
            status = 1
    finally:
        with open(path, "w") as f:
            f.write(original)
        print(f"   restored == baseline: {sha(path) == before}", flush=True)
    return status


def main(argv):
    if "--list" in argv:
        for arm in ARMS:
            print(arm[0])
        return 0
    only = [a for a in argv if not a.startswith("--")]
    env = dict(os.environ, CCACHE_DISABLE="1")
    worst = 0
    for name, path, old, new, targets in ARMS:
        if only and not any(name.startswith(o) for o in only):
            continue
        worst = max(worst, run_arm(name, path, old, new, targets, env))
    print("\nrebuild the unmutated tree before measuring anything: "
          "CCACHE_DISABLE=1 ninja -C backend/build-lane build_tests calculator_engine")
    return worst


if __name__ == "__main__":
    sys.exit(main(sys.argv[1:]))
