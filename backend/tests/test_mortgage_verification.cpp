// @author Olumuyiwa Oluwasanmi
//
// Standalone, proto- and gRPC-free tests for the mandatory GP-ARA
// verification stage over the MORTGAGE assistant's output
// (src/modules/mortgage_verification.cppm).
//
// WHAT THIS FILE HAS TO PROVE, AND WHY BOTH HALVES ARE MANDATORY
//
// Half one: every measured failure class of the 31.7%-exact-match mortgage
// fine-tune, reproduced as a real case, must REFUSE -- with the right
// Outcome and the right ReasonCode, not merely "not Proven".
//
// Half two: a non-vacuous control set of legitimate requests must PASS, end
// to end, through the same entry point. This half is not decoration. A
// verifier that refuses everything satisfies half one perfectly and is
// worthless; the controls are the only evidence that this one
// DISCRIMINATES. They are drawn from the same generator families the
// training set is built from (`agent/dataset/build_mortgage_dataset.py`), so
// they are the shape of request the assistant actually receives, not
// hand-tuned to whatever the rules happen to accept.
//
// The pairs are deliberate. `ComputeRefinance` appears twice with an
// identical utterance and identical params except one field: 5378.63
// (control, PASSES) versus 5379.00 (defect, REFUSED). `years` appears twice:
// as a real field of `ComputeRentVsBuy` (PASSES) and as the wrong name for
// `target_years` on `ComputeHomeFutureValue` (REFUSED). `rate: 0.005` from
// "6%" appears twice: on `ComputePayment`, whose `rate` finance.proto
// documents as per-period (PASSES), and as `annual_rate`, whose own name
// says it is not (REFUSED). Each pair isolates exactly one variable, which
// is what makes a pass and a refusal on the two of them mean something.
//
// Plus two structural gates that catch this file rotting rather than the
// model misbehaving:
//   - the LABEL-SPACE DRIFT check re-parses backend/proto/finance.proto with
//     the same section/exclusion rules build_mortgage_dataset.py uses and
//     fails if the module's embedded table has diverged in either direction;
//   - the SLOT-KIND TOTALITY check asserts every field in that label space
//     classifies to a known SlotKind, so a field finance.proto grows later
//     turns this test red instead of silently becoming an Indeterminate in
//     production.
#include <array>
#include <cstdio>
#include <cstdlib>
#include <fstream>
#include <map>
#include <set>
#include <sstream>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

import mortgage_verification;

namespace mv = mortgage_calculator::assistant::verify;

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

// ---------------------------------------------------------------------------
// Building a MortgageParamsInput without ceremony.
// ---------------------------------------------------------------------------

using KV = std::pair<std::string, std::string>;

auto params(std::string operation, std::vector<KV> scalars) -> mv::MortgageParamsInput {
    mv::MortgageParamsInput in;
    in.params_emitted = true;
    in.operation = std::move(operation);
    for (auto& [k, v] : scalars) {
        in.fields.push_back(mv::EmittedField{.name = k, .values = {v}, .repeated = false});
    }
    return in;
}

auto with_list(mv::MortgageParamsInput in, const std::string& name,
               std::vector<std::string> values) -> mv::MortgageParamsInput {
    in.fields.push_back(mv::EmittedField{.name = name, .values = std::move(values), .repeated = true});
    return in;
}

/** Replaces one already-present scalar field's value. Used to build a defect
 * case as a one-field mutation of its own control, so the pair differs by
 * exactly the thing under test. */
auto mutate(mv::MortgageParamsInput in, std::string_view field, std::string value)
    -> mv::MortgageParamsInput {
    bool found = false;
    for (auto& f : in.fields) {
        if (f.name == field) {
            f.values = {std::move(value)};
            found = true;
            break;
        }
    }
    if (!found) {
        std::printf("  FAIL: internal -- mutate() named a field the control does not have: %.*s\n",
                    static_cast<int>(field.size()), field.data());
        ++g_failures;
        ++g_checks;
    }
    return in;
}

/** Replaces one field's NAME, keeping its value -- the "wrong field name"
 * and "invented field name" defect shapes. */
auto rename(mv::MortgageParamsInput in, std::string_view from, std::string to)
    -> mv::MortgageParamsInput {
    for (auto& f : in.fields) {
        if (f.name == from) {
            f.name = std::move(to);
            return in;
        }
    }
    std::printf("  FAIL: internal -- rename() named a field the control does not have\n");
    ++g_failures;
    ++g_checks;
    return in;
}

auto drop_field(mv::MortgageParamsInput in, std::string_view name) -> mv::MortgageParamsInput {
    for (auto it = in.fields.begin(); it != in.fields.end(); ++it) {
        if (it->name == name) {
            in.fields.erase(it);
            return in;
        }
    }
    return in;
}

auto expect(const mv::MortgageParamsInput& in, std::string_view text, mv::Outcome outcome,
            mv::ReasonCode reason, const std::string& label) -> void {
    const auto v = mv::verify_mortgage_output(in, text);
    const bool ok = v.outcome == outcome && v.reason == reason;
    std::string detail = std::string{mv::to_string(v.outcome)} + "/" +
                         std::string{mv::to_string(v.reason)};
    if (!ok) {
        detail += " (wanted " + std::string{mv::to_string(outcome)} + "/" +
                  std::string{mv::to_string(reason)} + "; message: " + v.message + ")";
    }
    check(ok, label + " -> " + detail);
}

auto expect_pass(const mv::MortgageParamsInput& in, std::string_view text,
                 const std::string& label) -> void {
    const auto v = mv::verify_mortgage_output(in, text);
    check(v.outcome == mv::Outcome::Proven,
          label + " -> " + std::string{mv::to_string(v.outcome)} +
              (v.outcome == mv::Outcome::Proven ? "" : (" (" + v.message + ")")));
}

// ===========================================================================
// The control corpus. Each entry is one legitimate request: the user's own
// words plus the params a correct extraction produces, drawn from the
// matching generator in agent/dataset/build_mortgage_dataset.py.
// ===========================================================================

struct Control {
    const char* label;
    std::string text;
    mv::MortgageParamsInput input;
};

auto control_payment() -> Control {
    return {"ComputePayment: monthly payment on a stated loan",
            "What's the monthly payment on a $420,000 loan at 6.5% over 30 years?",
            params("ComputePayment", {{"rate", "0.005417"},
                                      {"periods", "360"},
                                      {"present_value", "420000.00"},
                                      {"future_value", "0.00"},
                                      {"timing", "END_OF_PERIOD"}})};
}

auto control_amortization() -> Control {
    return {"ComputeAmortization: full schedule, no PMI, no overpayment",
            "Amortization schedule for a $350,000 loan at 5.75% over 30 years.",
            params("ComputeAmortization", {{"loan_amount", "350000.00"},
                                           {"annual_rate", "0.0575"},
                                           {"term_months", "360"},
                                           {"monthly_overpayment", "0.00"},
                                           {"pmi_annual_rate", "0.0000"},
                                           {"original_home_value", "350000.00"}})};
}

auto control_home_future_value() -> Control {
    return {"ComputeHomeFutureValue: projected equity (target_years, spelled correctly)",
            "My house is worth $650,000 today, appreciating 3.5% a year. I owe $410,000 at "
            "6.25%, paying $2,600.00/month. What's my equity in 10 years?",
            params("ComputeHomeFutureValue", {{"current_property_value", "650000.00"},
                                              {"annual_appreciation_rate", "0.0350"},
                                              {"current_loan_balance", "410000.00"},
                                              {"annual_mortgage_rate", "0.0625"},
                                              {"current_monthly_payment", "2600.00"},
                                              {"target_years", "10"},
                                              {"payments_per_year", "12"}})};
}

auto control_future_value_detailed() -> Control {
    return {"ComputeFutureValueDetailed: savings growth",
            "I have $20,000 saved and add $6,000 a year at 7% for 25 years, compounded monthly. "
            "What will it be worth?",
            params("ComputeFutureValueDetailed", {{"annual_rate", "0.0700"},
                                                  {"years", "25"},
                                                  {"annual_contribution", "6000.00"},
                                                  {"current_principal", "20000.00"},
                                                  {"annual_inflation_rate", "0.0000"},
                                                  {"compound_frequency", "12"}})};
}

/** The control twin of the corrupted-payment defect: identical utterance,
 * identical params, the payment transcribed EXACTLY as stated. */
auto control_refinance() -> Control {
    return {"ComputeRefinance: break-even, payment transcribed exactly",
            "I owe $320,000 at 6.5% with 240 months left, paying $5,378.63/month. Home is worth "
            "$500,000. If I refinance to 5.25% over 15 years with $4,000 in closing costs paid "
            "in cash, what's my new payment and break-even?",
            params("ComputeRefinance", {{"current_loan_balance", "320000.00"},
                                        {"current_monthly_payment", "5378.63"},
                                        {"current_annual_rate", "0.0650"},
                                        {"current_remaining_months", "240"},
                                        {"property_value", "500000.00"},
                                        {"new_annual_rate", "0.0525"},
                                        {"new_term_years", "15"},
                                        {"closing_costs", "4000.00"},
                                        {"closing_cost_type", "PAID_IN_CASH"},
                                        {"cash_out_amount", "0.00"},
                                        {"current_pmi_monthly", "0.00"},
                                        {"new_pmi_monthly", "0.00"},
                                        {"pmi_drop_off_ltv", "0.80"},
                                        {"payments_per_year", "12"}})};
}

auto control_heloc() -> Control {
    return {"ComputeHeloc: draw against equity with an LTV cap",
            "My house is worth $700,000 and I owe $250,000 on it. If I draw $80,000 from a HELOC "
            "at 9% with an 80% LTV cap, repaid over 15 years, what are the payments?",
            params("ComputeHeloc", {{"home_value", "700000.00"},
                                    {"current_mortgage_balance", "250000.00"},
                                    {"max_ltv_rate", "0.80"},
                                    {"drawn_amount", "80000.00"},
                                    {"annual_rate", "0.0900"},
                                    {"repayment_term_years", "15"},
                                    {"payments_per_year", "12"}})};
}

/** Exercises the repeated-field path and map M8 (a cash-flow outlay is the
 * negation of a stated amount). */
auto control_npv() -> Control {
    auto in = params("ComputeNpv", {{"rate", "0.08"}});
    in = with_list(std::move(in), "values",
                   {"-60000.00", "20000.00", "25000.00", "30000.00"});
    return {"ComputeNpv: cash-flow series with a negated outlay",
            "I invest $60,000 today and expect back year 1: $20,000; year 2: $25,000; year 3: "
            "$30,000. What's the NPV at a 8% discount rate?",
            std::move(in)};
}

/** The control that proves `years` is a REAL field name -- on the operation
 * whose request message declares it. Its refusal twin is the same word on
 * ComputeHomeFutureValue, which spells it `target_years`. */
auto control_rent_vs_buy() -> Control {
    return {"ComputeRentVsBuy: `years` is a legitimate field HERE",
            "Rent vs. buy over 7 years: renting is $2,200.00/month (+3% a year), or buying at "
            "$500,000 with $100,000 down, $3,100.00/month, 4% appreciation, 6% on the down "
            "payment invested instead.",
            params("ComputeRentVsBuy", {{"property_price", "500000.00"},
                                        {"down_payment", "100000.00"},
                                        {"monthly_piti_and_maintenance", "3100.00"},
                                        {"annual_home_appreciation", "0.0400"},
                                        {"current_monthly_rent", "2200.00"},
                                        {"annual_rent_increase", "0.0300"},
                                        {"annual_investment_return", "0.0600"},
                                        {"years", "7"},
                                        // The seven amortising inputs, at their
                                        // convention values: G2 requires every
                                        // declared field to be emitted, and these
                                        // are exempt from grounding precisely so a
                                        // legacy-shape utterance stays parseable.
                                        {"loan_annual_rate", "0"},
                                        {"loan_term_years", "0"},
                                        {"loan_amount", "0"},
                                        {"monthly_taxes_ins_maintenance", "0"},
                                        {"closing_costs_buy", "0"},
                                        {"selling_cost_percent", "0"},
                                        {"annual_inflation_rate", "0"}})};
}

auto control_payoff_timing() -> Control {
    return {"ComputePayoffTiming: the operation ComputePayoff was a corruption of",
            "I owe $280,000 at 6% , paying $2,100.00/month. If I add $300 extra a month, how much "
            "sooner do I pay it off?",
            params("ComputePayoffTiming", {{"current_loan_balance", "280000.00"},
                                           {"annual_rate", "0.0600"},
                                           {"current_monthly_payment", "2100.00"},
                                           {"extra_monthly_payment", "300.00"},
                                           {"payments_per_year", "12"}})};
}

/** Magnitude suffixes and a currency-formatted figure, both of which the
 * brief names explicitly as transformations that must NOT be refused. */
auto control_suffixes() -> Control {
    return {"ComputeAmortization: `$300k` and `$1,356,200` notation",
            "Amortize a $300k loan at 4.68% over 30 years on a home worth $1,356,200.",
            params("ComputeAmortization", {{"loan_amount", "300000.00"},
                                           {"annual_rate", "0.0468"},
                                           {"term_months", "360"},
                                           {"monthly_overpayment", "0.00"},
                                           {"pmi_annual_rate", "0.0000"},
                                           {"original_home_value", "1356200.00"}})};
}

auto all_controls() -> std::vector<Control> {
    std::vector<Control> out;
    out.push_back(control_payment());
    out.push_back(control_amortization());
    out.push_back(control_home_future_value());
    out.push_back(control_future_value_detailed());
    out.push_back(control_refinance());
    out.push_back(control_heloc());
    out.push_back(control_npv());
    out.push_back(control_rent_vs_buy());
    out.push_back(control_payoff_timing());
    out.push_back(control_suffixes());
    return out;
}

// ===========================================================================
// Label-space drift check: re-parse backend/proto/finance.proto.
//
// Mirrors agent/dataset/build_mortgage_dataset.py's parse_finance_proto() /
// build_operations(): section banners inside `service Finance { ... }` select
// scope, two identifiers before '=' distinguish a message field from a nested
// enum constant, and the two rate-theory RPCs are excluded by name.
// ===========================================================================

auto find_matching_brace(const std::string& text, std::size_t open_idx) -> std::size_t {
    int depth = 0;
    for (std::size_t i = open_idx; i < text.size(); ++i) {
        if (text[i] == '{') ++depth;
        else if (text[i] == '}') {
            --depth;
            if (depth == 0) return i;
        }
    }
    return std::string::npos;
}

auto trim(std::string_view s) -> std::string_view {
    while (!s.empty() && (s.front() == ' ' || s.front() == '\t' || s.front() == '\r')) {
        s.remove_prefix(1);
    }
    while (!s.empty() && (s.back() == ' ' || s.back() == '\t' || s.back() == '\r')) {
        s.remove_suffix(1);
    }
    return s;
}

auto is_ident_char(char c) -> bool {
    return (c >= 'a' && c <= 'z') || (c >= 'A' && c <= 'Z') || (c >= '0' && c <= '9') || c == '_';
}

/** `[repeated] <type> <name> = <n>[ [...] ];` -> {type, name}; nullopt for a
 * line that is not a field (an enum constant has ONE identifier before '='). */
auto parse_field_line(std::string_view line) -> std::optional<std::pair<std::string, std::string>> {
    std::string_view s = trim(line);
    // `optional` is proto3 EXPLICIT PRESENCE -- a presence marker, not a
    // type. Unstripped it parses as the type and shifts the name, which
    // silently drops the real field from the drift comparison.
    if (s.starts_with("optional ")) s.remove_prefix(9);
    s = trim(s);
    if (s.starts_with("repeated ")) s.remove_prefix(9);
    s = trim(s);

    auto take_ident = [&s]() -> std::string {
        std::size_t i = 0;
        while (i < s.size() && is_ident_char(s[i])) ++i;
        std::string out{s.substr(0, i)};
        s.remove_prefix(i);
        return out;
    };

    const std::string type = take_ident();
    if (type.empty()) return std::nullopt;
    if (s.empty() || (s.front() != ' ' && s.front() != '\t')) return std::nullopt;
    s = trim(s);
    const std::string name = take_ident();
    if (name.empty()) return std::nullopt;
    s = trim(s);
    if (s.empty() || s.front() != '=') return std::nullopt;
    s.remove_prefix(1);
    s = trim(s);
    std::size_t digits = 0;
    while (digits < s.size() && s[digits] >= '0' && s[digits] <= '9') ++digits;
    if (digits == 0) return std::nullopt;
    s.remove_prefix(digits);
    s = trim(s);
    if (s.starts_with("[")) {
        const auto close = s.find(']');
        if (close == std::string_view::npos) return std::nullopt;
        s.remove_prefix(close + 1);
        s = trim(s);
    }
    if (s.empty() || s.front() != ';') return std::nullopt;
    return std::make_pair(type, name);
}

auto read_proto() -> std::optional<std::string> {
    std::vector<std::string> candidates;
#ifdef MORTGAGE_FINANCE_PROTO_PATH
    candidates.emplace_back(MORTGAGE_FINANCE_PROTO_PATH);
#endif
    if (const char* env = std::getenv("FINANCE_PROTO_PATH"); env != nullptr) {
        candidates.emplace_back(env);
    }
    candidates.emplace_back("../proto/finance.proto");
    candidates.emplace_back("proto/finance.proto");
    candidates.emplace_back("backend/proto/finance.proto");
    for (const auto& path : candidates) {
        std::ifstream f(path);
        if (!f) continue;
        std::ostringstream ss;
        ss << f.rdbuf();
        return ss.str();
    }
    return std::nullopt;
}

auto proto_label_space(const std::string& text)
    -> std::map<std::string, std::vector<std::string>> {
    static const std::set<std::string> kInScope{"Time value of money", "Mortgages, HELOC",
                                                "Cash-flow analysis", "Depreciation",
                                                "Real estate"};
    // THIRD mirror of build_mortgage_dataset.py's EXCLUDE_RPCS -- after the
    // generator itself and test_mortgage_grammar.cpp. Adding one RPC to
    // finance.proto failed BOTH of the others before this one, which is the
    // four-tables lesson this project already paid for: an operation reaches
    // the generator's label space, this verifier's declared-field table, the
    // convention list, and the service's own dispatch list, and a change that
    // stops short of all of them fails somewhere far from where it was made.
    // ComputeRentVsBuyBatch is a bulk API, not an utterance -- see EXCLUDE_RPCS
    // for the reasoning.
    static const std::set<std::string> kExcluded{"ConvertInterestRate", "ComputeFisherRate",
                                                 "ComputeRentVsBuyBatch"};

    std::map<std::string, std::vector<std::string>> out;

    const auto svc = text.find("service Finance {");
    if (svc == std::string::npos) return out;
    const auto svc_open = text.find('{', svc);
    const auto svc_close = find_matching_brace(text, svc_open);
    const std::string svc_body = text.substr(svc_open + 1, svc_close - svc_open - 1);

    // First: which request message each in-scope rpc uses.
    std::vector<std::pair<std::string, std::string>> rpcs;  // (rpc name, request message)
    {
        std::istringstream lines(svc_body);
        std::string line;
        std::string section;
        while (std::getline(lines, line)) {
            const std::string_view t = trim(line);
            if (t.starts_with("// --")) {
                std::string_view s = t.substr(5);
                s = trim(s);
                while (!s.empty() && s.back() == '-') s.remove_suffix(1);
                section = std::string{trim(s)};
                continue;
            }
            const auto rpc_pos = t.find("rpc ");
            if (rpc_pos == std::string_view::npos) continue;
            std::string_view s = t.substr(rpc_pos + 4);
            std::size_t i = 0;
            while (i < s.size() && is_ident_char(s[i])) ++i;
            const std::string name{s.substr(0, i)};
            const auto open = s.find('(');
            const auto close = s.find(')');
            if (open == std::string_view::npos || close == std::string_view::npos) continue;
            const std::string request{trim(s.substr(open + 1, close - open - 1))};
            if (kInScope.count(section) == 0) continue;
            if (kExcluded.count(name) != 0) continue;
            rpcs.emplace_back(name, request);
        }
    }

    // Second: the declared fields of each of those request messages.
    for (const auto& [rpc, request] : rpcs) {
        const std::string needle = "message " + request + " {";
        auto pos = text.find("\n" + needle);
        if (pos == std::string::npos && text.starts_with(needle)) pos = 0;
        else if (pos != std::string::npos) pos += 1;
        if (pos == std::string::npos) continue;
        const auto open = text.find('{', pos);
        const auto close = find_matching_brace(text, open);
        const std::string body = text.substr(open + 1, close - open - 1);

        std::istringstream lines(body);
        std::string line;
        std::vector<std::string> fields;
        while (std::getline(lines, line)) {
            if (auto f = parse_field_line(line); f.has_value()) fields.push_back(f->second);
        }
        out[rpc] = std::move(fields);
    }
    return out;
}

}  // namespace

auto main() -> int {
    // -----------------------------------------------------------------
    section("Gate 0 -- the embedded label space still matches finance.proto");
    // -----------------------------------------------------------------
    {
        const auto text = read_proto();
        if (!text.has_value()) {
            check(false,
                  "could not open backend/proto/finance.proto (set MORTGAGE_FINANCE_PROTO_PATH at "
                  "build time or FINANCE_PROTO_PATH in the environment) -- the drift check cannot "
                  "be skipped silently");
        } else {
            const auto from_proto = proto_label_space(*text);
            const auto ids = mv::operation_ids();
            check(from_proto.size() == ids.size(),
                  "operation count: proto has " + std::to_string(from_proto.size()) +
                      ", module has " + std::to_string(ids.size()));

            bool all_match = true;
            std::string first_mismatch;
            for (const auto& [op, fields] : from_proto) {
                if (!mv::is_known_operation(op)) {
                    all_match = false;
                    first_mismatch = "module is missing operation " + op;
                    break;
                }
                const auto module_fields = mv::fields_of(op);
                if (module_fields.size() != fields.size()) {
                    all_match = false;
                    first_mismatch = op + ": proto has " + std::to_string(fields.size()) +
                                     " fields, module has " + std::to_string(module_fields.size());
                    break;
                }
                for (std::size_t i = 0; i < fields.size(); ++i) {
                    if (std::string{module_fields[i].field} != fields[i]) {
                        all_match = false;
                        first_mismatch = op + " field " + std::to_string(i) + ": proto says \"" +
                                         fields[i] + "\", module says \"" +
                                         std::string{module_fields[i].field} + "\"";
                        break;
                    }
                }
                if (!all_match) break;
            }
            check(all_match, all_match ? "every in-scope operation and field matches finance.proto"
                                       : ("label space has drifted -- " + first_mismatch));

            for (const auto id : ids) {
                if (from_proto.count(std::string{id}) == 0) {
                    check(false, "module declares operation \"" + std::string{id} +
                                     "\" that finance.proto does not put in scope");
                }
            }
        }
    }

    // -----------------------------------------------------------------
    section("Gate 0 -- every field in the label space classifies to a known SlotKind");
    // -----------------------------------------------------------------
    {
        std::vector<std::string> unclassified;
        for (const auto op : mv::operation_ids()) {
            for (const auto& spec : mv::fields_of(op)) {
                if (mv::classify_slot(spec.field) == mv::SlotKind::Unclassified) {
                    unclassified.emplace_back(std::string{op} + "." + std::string{spec.field});
                }
            }
        }
        std::string joined;
        for (const auto& u : unclassified) joined += (joined.empty() ? "" : ", ") + u;
        check(unclassified.empty(),
              unclassified.empty() ? "all 176 (operation, field) pairs classify"
                                   : ("unclassified: " + joined));

        // Spot checks on the rules that are easiest to get backwards.
        check(mv::classify_slot("annual_rate") == mv::SlotKind::Rate, "annual_rate -> Rate");

        // ComputeClosingCosts. Without these eight in kMoneyFields the slot is
        // Unclassified, translate() returns Indeterminate, and EVERY
        // closing-cost parse is refused -- a failure that looks like the model
        // being bad rather than the table being short.
        check(mv::classify_slot("home_price") == mv::SlotKind::Money, "home_price -> Money");
        check(mv::classify_slot("appraisal_fee") == mv::SlotKind::Money, "appraisal_fee -> Money");
        check(mv::classify_slot("recording_fees") == mv::SlotKind::Money, "recording_fees -> Money");
        check(mv::classify_slot("seller_lender_credits") == mv::SlotKind::Money,
              "seller_lender_credits -> Money");
        check(mv::classify_slot("other_lender_fees") == mv::SlotKind::Money,
              "other_lender_fees -> Money");
        check(mv::classify_slot("homeowners_insurance_annual") == mv::SlotKind::Money,
              "homeowners_insurance_annual -> Money");
        check(mv::classify_slot("property_tax_annual") == mv::SlotKind::Money,
              "property_tax_annual -> Money");
        check(mv::classify_slot("inspection_fee") == mv::SlotKind::Money, "inspection_fee -> Money");
        check(mv::classify_slot("transfer_tax_percent") == mv::SlotKind::Ratio,
              "transfer_tax_percent -> Ratio");
        check(mv::classify_slot("down_payment_percent") == mv::SlotKind::Ratio,
              "down_payment_percent -> Ratio");
        check(mv::classify_slot("tax_escrow_months") == mv::SlotKind::MonthCount,
              "tax_escrow_months -> MonthCount");
        // prepaid_interest_days is a DAY COUNT, and zero is legitimate. As a
        // MonthCount it would be refused by the positivity bound.
        check(mv::classify_slot("prepaid_interest_days") == mv::SlotKind::DayOffsets,
              "prepaid_interest_days -> DayOffsets (zero days must be expressible)");
        check(mv::classify_slot("annual_rent_increase") == mv::SlotKind::Rate,
              "annual_rent_increase -> Rate (not Money via a 'rent' substring)");
        check(mv::classify_slot("max_ltv_rate") == mv::SlotKind::Ratio,
              "max_ltv_rate -> Ratio (the ltv rule must beat the rate rule)");
        check(mv::classify_slot("pmi_drop_off_ltv") == mv::SlotKind::Ratio,
              "pmi_drop_off_ltv -> Ratio (0.80 would fail the 30% rate band)");
        check(mv::classify_slot("payments_per_year") == mv::SlotKind::Frequency,
              "payments_per_year -> Frequency (not a YearCount)");
        check(mv::classify_slot("periodic_gross_rent") == mv::SlotKind::Money,
              "periodic_gross_rent -> Money (not a PeriodIndex)");
        check(mv::classify_slot("recovery_period") == mv::SlotKind::YearCount,
              "recovery_period -> YearCount (not a PeriodIndex)");
        check(mv::classify_slot("start_period") == mv::SlotKind::PeriodIndex,
              "start_period -> PeriodIndex");
        check(mv::classify_slot("selling_closing_cost_percent") == mv::SlotKind::Ratio,
              "selling_closing_cost_percent -> Ratio");
        check(mv::classify_slot("no_such_field_anywhere") == mv::SlotKind::Unclassified,
              "an unknown name -> Unclassified (which is an Indeterminate, which is a refusal)");
    }

    // -----------------------------------------------------------------
    section("The strict decimal grammar");
    // -----------------------------------------------------------------
    {
        check(mv::parse_strict_decimal("5378.63").has_value(), "\"5378.63\" parses");
        check(mv::parse_strict_decimal("-60000.00").has_value(), "\"-60000.00\" parses");
        check(mv::parse_strict_decimal("0").has_value(), "\"0\" parses");
        check(!mv::parse_strict_decimal("").has_value(), "\"\" is refused");
        check(!mv::parse_strict_decimal("NaN").has_value(), "\"NaN\" is refused");
        check(!mv::parse_strict_decimal("Infinity").has_value(), "\"Infinity\" is refused");
        check(!mv::parse_strict_decimal("1e309").has_value(), "\"1e309\" is refused");
        check(!mv::parse_strict_decimal("0x1p4").has_value(), "\"0x1p4\" is refused");
        check(!mv::parse_strict_decimal("+1").has_value(), "\"+1\" is refused");
        check(!mv::parse_strict_decimal(" 1").has_value(), "\" 1\" is refused");
        check(!mv::parse_strict_decimal("1,000").has_value(),
              "\"1,000\" is refused (separators are notation in TEXT, never in a param)");
        check(!mv::parse_strict_decimal("1234567890123456").has_value(),
              "16 integer digits is refused");
    }

    // -----------------------------------------------------------------
    section("The lexer");
    // -----------------------------------------------------------------
    {
        const auto lits = mv::lex_numeric_literals(
            "a $1,356,200 home at 4.68% over 30 years, $300k drawn, 246 months left");
        check(lits.size() == 5, "five literals lexed, got " + std::to_string(lits.size()));
        if (lits.size() == 5) {
            check(lits[0].value.to_string() == "1356200" && lits[0].tag == mv::LiteralTag::Money,
                  "$1,356,200 -> 1356200 tagged MONEY (separators consumed as notation)");
            check(lits[1].value.to_string() == "4.68" && lits[1].tag == mv::LiteralTag::Percent,
                  "4.68% -> 4.68 tagged PERCENT");
            check(lits[2].value.to_string() == "30" && lits[2].tag == mv::LiteralTag::Years,
                  "30 years -> 30 tagged YEARS");
            check(lits[3].value.to_string() == "300" && lits[3].scale == 1000 &&
                      lits[3].tag == mv::LiteralTag::Money,
                  "$300k -> 300 x1000 tagged MONEY");
            check(lits[4].value.to_string() == "246" && lits[4].tag == mv::LiteralTag::Months,
                  "246 months -> 246 tagged MONTHS");
        }
        const auto money_year = mv::lex_numeric_literals("add $6,000 a year");
        check(money_year.size() == 1 && money_year[0].tag == mv::LiteralTag::Money,
              "\"$6,000 a year\" stays MONEY -- a currency prefix beats a trailing unit word");

        // A leading minus is part of the literal, not decoration on it.
        //
        // Found by scripts/probe_mortgage_adversarial.py against a live engine:
        // the lexer looked back for '$' and not for '-', so "-$250,000" and
        // "$250,000" lexed to the SAME literal. The deployed model silently
        // drops the minus, emitted `loan_amount = 250000.00`, and the gate
        // grounded it against the digits and returned Proven -- a repair the
        // verifier could not see, on a module whose stated rule is that nothing
        // is repaired.
        const auto neg = mv::lex_numeric_literals("Amortize -$250,000 at 5% over 30 years.");
        check(!neg.empty() && neg[0].value.to_string() == "-250000",
              "\"-$250,000\" lexes as -250000, sign carried (got " +
                  (neg.empty() ? std::string{"nothing"} : neg[0].value.to_string()) + ")");
        check(!neg.empty() && neg[0].tag == mv::LiteralTag::Money,
              "a negative currency literal is still tagged MONEY");

        const auto pos = mv::lex_numeric_literals("Amortize $250,000 at 5% over 30 years.");
        check(!pos.empty() && !neg.empty() && pos[0].value.to_string() != neg[0].value.to_string(),
              "the signed and unsigned forms no longer lex identically");

        const auto spaced = mv::lex_numeric_literals("a balance of - $1,200 this month");
        check(!spaced.empty() && spaced[0].value.to_string() == "-1200",
              "the minus is found across '$' and a space, the same lookback '$' itself uses");
    }

    // ===================================================================
    section("DIRECTION 1 -- every measured failure class REFUSES");
    // ===================================================================

    // --- Row 1: invented operation. Wanted ComputePayoffTiming, emitted
    //     ComputePayoff, which is not an RPC at all. Must be REFUSED, and
    //     specifically must not be repaired into its near neighbour.
    {
        const auto ctl = control_payoff_timing();
        auto defect = ctl.input;
        defect.operation = "ComputePayoff";
        expect(defect, ctl.text, mv::Outcome::Unsafe, mv::ReasonCode::UnknownOperation,
               "row 1: invented operation ComputePayoff");

        const auto v = mv::verify_mortgage_output(defect, ctl.text);
        check(v.message.find("ComputePayoffTiming") == std::string::npos,
              "row 1: the refusal does not name a repair target -- no operation is guessed");
    }

    // --- Row 2: wrong operation. Wanted ComputeFutureValueDetailed, emitted
    //     ComputeFutureValue. Caught here because the two request messages
    //     have DISJOINT field sets, so the first emitted field is already not
    //     a field of the named operation. Stated plainly because it is a real
    //     limit: had the two shapes overlapped, this layer would have passed
    //     it and only the holdout would have caught it (see the module's
    //     "HONEST LIMITS" note on operation choice).
    {
        const auto ctl = control_future_value_detailed();
        auto defect = ctl.input;
        defect.operation = "ComputeFutureValue";
        expect(defect, ctl.text, mv::Outcome::Unsafe, mv::ReasonCode::UnknownField,
               "row 2: wrong operation ComputeFutureValue carrying FVD's fields");
    }

    // --- Row 3: invented field names. `annual_compounding_rate` and
    //     `compounding_periods_per_year` are neither of them anywhere in
    //     finance.proto.
    {
        const auto ctl = control_future_value_detailed();
        expect(rename(ctl.input, "annual_inflation_rate", "annual_compounding_rate"), ctl.text,
               mv::Outcome::Unsafe, mv::ReasonCode::UnknownField,
               "row 3: invented field name annual_compounding_rate");
        expect(rename(ctl.input, "compound_frequency", "compounding_periods_per_year"), ctl.text,
               mv::Outcome::Unsafe, mv::ReasonCode::UnknownField,
               "row 3: invented field name compounding_periods_per_year");
    }

    // --- Row 4: wrong field name. `years` for `target_years`. The identical
    //     word is a REAL field of ComputeRentVsBuy (control below), so this
    //     refusal is per-operation, not a blanket ban on a string.
    {
        const auto ctl = control_home_future_value();
        expect(rename(ctl.input, "target_years", "years"), ctl.text, mv::Outcome::Unsafe,
               mv::ReasonCode::UnknownField,
               "row 4: `years` on ComputeHomeFutureValue (the field is `target_years`)");
    }

    // --- Row 5: THE CORRUPTED VALUE. Structurally perfect, prices a
    //     different loan. This is the case the whole module exists for, so it
    //     is asserted twice: once that the STRUCTURAL gates alone pass it
    //     (proving a schema validator would have served it), and once that
    //     the composed gate refuses it.
    {
        const auto ctl = control_refinance();
        const auto defect = mutate(ctl.input, "current_monthly_payment", "5379.00");

        const auto structural = mv::verify_mortgage_params(defect);
        check(structural.outcome == mv::Outcome::Proven,
              "row 5: the rounded payment passes EVERY structural check -- this is why grounding "
              "exists (structural verdict: " +
                  std::string{mv::to_string(structural.outcome)} + ")");

        expect(defect, ctl.text, mv::Outcome::Unsafe, mv::ReasonCode::UngroundedValue,
               "row 5: current_monthly_payment 5379.00 against a stated 5378.63");

        const auto v = mv::verify_mortgage_output(defect, ctl.text);
        check(v.message.find("5378.63") != std::string::npos,
              "row 5: the refusal names the figure the user actually gave (" + v.message + ")");
    }

    // --- Row 6: no output at all. Never a fabricated default.
    {
        mv::MortgageParamsInput absent;  // params_emitted defaults to false
        expect(absent, "What's the payment on a $420,000 loan at 6.5% over 30 years?",
               mv::Outcome::Indeterminate, mv::ReasonCode::NoParamsEmitted,
               "row 6: no <params> block -> Indeterminate, never a default-filled block");

        // And the same input must not become Proven by any other door.
        check(mv::verify_mortgage_params(absent).outcome != mv::Outcome::Proven,
              "row 6: the structural gate alone also refuses an absent params block");
        check(mv::ground_emitted_values(absent, "anything").outcome != mv::Outcome::Proven,
              "row 6: the grounding gate alone also refuses an absent params block");
    }

    // ===================================================================
    section("DIRECTION 1c -- `periods` is grounded against the RATE's period");
    // ===================================================================

    // `finance.proto` documents `rate` as PER-PERIOD and `periods` as a count
    // of those same periods, and the pair is only meaningful together. The
    // grounding gate used to check them independently: the rate grounded
    // against any cadence, `periods` grounded against months by convention.
    // That refused correct annual parses AND accepted mismatched pairs.
    //
    // All four combinations are asserted here, because the fix has to loosen
    // one direction without loosening the other.
    {
        // (1) ANNUAL rate, annual periods -- was REFUSED before the cadence is
        // inferred, with the self-contradicting "10 does not correspond to
        // anything in the request (the nearest figure you gave is 10)".
        // Observed on production 2026-08-12.
        auto annual = params("ComputeFutureValue", {{"rate", "0.0500"},
                                                    {"periods", "10"},
                                                    {"present_value", "1000.00"},
                                                    {"payment", "0.00"},
                                                    {"timing", "END_OF_PERIOD"}});
        expect_pass(annual, "Compute the future value of 1000 at 5% for 10 years",
                    "annual rate with annual periods is grounded");

        // (2) MONTHLY rate, monthly periods -- the mortgage case, which must
        // keep working.
        auto monthly = params("ComputePayment", {{"rate", "0.0050"},
                                                 {"periods", "360"},
                                                 {"present_value", "300000.00"},
                                                 {"future_value", "0.00"},
                                                 {"timing", "END_OF_PERIOD"}});
        expect_pass(monthly,
                    "What is the monthly payment on a 300000 loan at 6 percent for 30 years?",
                    "monthly rate with monthly periods is grounded");

        // (3) MONTHLY rate, YEAR count in `periods` -- a thirty-MONTH loan
        // answered as thirty years. This is the slip the old hardcoded x12
        // existed to catch, and it must STILL be caught.
        auto short_loan = params("ComputePayment", {{"rate", "0.0050"},
                                                    {"periods", "30"},
                                                    {"present_value", "300000.00"},
                                                    {"future_value", "0.00"},
                                                    {"timing", "END_OF_PERIOD"}});
        expect(short_loan,
               "What is the monthly payment on a 300000 loan at 6 percent for 30 years?",
               mv::Outcome::Unsafe, mv::ReasonCode::UngroundedValue,
               "a monthly rate with periods=30 is still refused");

        // (4) ANNUAL rate, MONTH count in `periods` -- the mirror slip, and the
        // one the old rule ACCEPTED: months grounded by convention while the
        // rate was never checked against them. Now refused.
        auto mismatched = params("ComputePayment", {{"rate", "0.0600"},
                                                    {"periods", "360"},
                                                    {"present_value", "300000.00"},
                                                    {"future_value", "0.00"},
                                                    {"timing", "END_OF_PERIOD"}});
        expect(mismatched,
               "What is the monthly payment on a 300000 loan at 6 percent for 30 years?",
               mv::Outcome::Unsafe, mv::ReasonCode::UngroundedValue,
               "an annual rate with periods=360 is now refused too");
    }

    // ===================================================================
    section("DIRECTION 1b -- the misuse classes the grounding gate adds");
    // ===================================================================

    // Unit confusion: "20% down" is not a $20 down payment. A PERCENT literal
    // is structurally inadmissible for a money slot, so this is refused
    // without any magnitude heuristic.
    {
        auto in = control_rent_vs_buy().input;
        in = mutate(std::move(in), "down_payment", "20.00");
        expect(in,
               "Rent vs. buy over 7 years: renting is $2,200.00/month (+3% a year), or buying at "
               "$500,000 with 20% down, $3,100.00/month, 4% appreciation, 6% on the down payment "
               "invested instead.",
               mv::Outcome::Unsafe, mv::ReasonCode::UngroundedValue,
               "unit confusion: down_payment 20 from \"20% down\"");
    }

    // Hallucinated magnitude: "$300k" means 300000, and neither 300 nor
    // 3000000 is admissible for it.
    {
        const auto ctl = control_suffixes();
        expect(mutate(ctl.input, "loan_amount", "3000000.00"), ctl.text, mv::Outcome::Unsafe,
               mv::ReasonCode::UngroundedValue, "hallucinated magnitude: $300k -> 3000000");
        expect(mutate(ctl.input, "loan_amount", "300.00"), ctl.text, mv::Outcome::Unsafe,
               mv::ReasonCode::UngroundedValue,
               "dropped magnitude: $300k -> 300 (the suffix REPLACES the identity candidate)");
    }

    // The per-period/annual 12x error, refused wherever the slot's own name
    // settles the question. Its control twin (`rate` on ComputePayment, which
    // finance.proto documents as per-period) passes below.
    {
        const auto ctl = control_amortization();
        expect(mutate(ctl.input, "annual_rate", "0.004792"), ctl.text, mv::Outcome::Unsafe,
               mv::ReasonCode::UngroundedValue,
               "12x error: annual_rate 0.004792 from a stated 5.75%");
    }

    // A number nobody said at all.
    {
        const auto ctl = control_payment();
        expect(mutate(ctl.input, "present_value", "999999999.00"), ctl.text, mv::Outcome::Unsafe,
               mv::ReasonCode::UngroundedValue, "injected balance 999999999 nobody stated");
    }

    // Parameter smuggling that the grammar kills before any arithmetic runs.
    {
        const auto ctl = control_payment();
        expect(mutate(ctl.input, "present_value", "1e309"), ctl.text, mv::Outcome::Unsafe,
               mv::ReasonCode::MalformedNumber, "smuggling: present_value 1e309");
        expect(mutate(ctl.input, "present_value", "NaN"), ctl.text, mv::Outcome::Unsafe,
               mv::ReasonCode::MalformedNumber, "smuggling: present_value NaN");
        expect(mutate(ctl.input, "present_value", "-420000.00"), ctl.text, mv::Outcome::Unsafe,
               mv::ReasonCode::OutOfRange, "smuggling: negative principal");
    }

    // Product-scope bounds. Both figures below are GROUNDED -- the user
    // really did say them -- and are refused anyway, which is the point of
    // having bounds as well as grounding.
    {
        expect(params("ComputeAmortization", {{"loan_amount", "350000.00"},
                                              {"annual_rate", "0.4500"},
                                              {"term_months", "360"},
                                              {"monthly_overpayment", "0.00"},
                                              {"pmi_annual_rate", "0.0000"},
                                              {"original_home_value", "350000.00"}}),
               "Amortize a $350,000 loan at 45% over 30 years.", mv::Outcome::Unsafe,
               mv::ReasonCode::OutOfRange, "bounds: a grounded 45% rate is still out of scope");

        // ComputeClosingCosts. The control PASSES and its twin, differing in
        // exactly one field, is REFUSED -- which is what makes the bound mean
        // something rather than proving a verifier that refuses everything.
        expect(params("ComputeClosingCosts", {{"home_price", "450000.00"},
                                              {"down_payment_percent", "0.100000"},
                                              {"annual_rate", "0.067500"},
                                              {"origination_fee_percent", "0.007500"},
                                              {"discount_points_percent", "0.000000"},
                                              {"other_lender_fees", "1400.00"},
                                              {"title_settlement_percent", "0.005500"},
                                              {"appraisal_fee", "650.00"},
                                              {"inspection_fee", "500.00"},
                                              {"recording_fees", "225.00"},
                                              {"transfer_tax_percent", "0.005000"},
                                              {"homeowners_insurance_annual", "2100.00"},
                                              {"property_tax_annual", "6300.00"},
                                              {"tax_escrow_months", "3"},
                                              {"seller_lender_credits", "0.00"},
                                              {"prepaid_interest_days", "15"}}),
               "Closing costs on a $450,000 home with 10% down at 6.75%: 0.75% origination, 0% discount points, $1,400 other lender fees, 0.55% title and settlement, $650 appraisal, $500 inspection, $225 recording, 0.5% transfer tax, $2,100 a year homeowners insurance, $6,300 a year property tax, 3 months of tax escrow, $0 seller credits, 15 days of prepaid interest.",
               mv::Outcome::Proven, mv::ReasonCode::None,
               "ComputeClosingCosts: every one of the sixteen figures stated -> Proven");

        // A share above 1.0 is refused. `sensen::validate_closing_costs`
        // refuses the same value with INVALID_ARGUMENT; this is the verifier
        // half of that pair (the engine half is
        // test_finance_service_validation.cpp section 23). The GLOBAL ratio
        // ceiling is 1.5, so without kUnitCappedRatioFields this would be
        // Proven here and refused by the engine -- the caller getting a
        // transport error where an honest refusal belongs.
        expect(params("ComputeClosingCosts", {{"home_price", "450000.00"},
                                              {"down_payment_percent", "0.100000"},
                                              {"annual_rate", "0.067500"},
                                              {"origination_fee_percent", "1.200000"},
                                              {"discount_points_percent", "0.000000"},
                                              {"other_lender_fees", "1400.00"},
                                              {"title_settlement_percent", "0.005500"},
                                              {"appraisal_fee", "650.00"},
                                              {"inspection_fee", "500.00"},
                                              {"recording_fees", "225.00"},
                                              {"transfer_tax_percent", "0.005000"},
                                              {"homeowners_insurance_annual", "2100.00"},
                                              {"property_tax_annual", "6300.00"},
                                              {"tax_escrow_months", "3"},
                                              {"seller_lender_credits", "0.00"},
                                              {"prepaid_interest_days", "15"}}),
               "Closing costs on a $450,000 home with 10% down at 6.75%: 120% origination, "
               "0% discount points, $1,400 other lender fees, 0.55% title and settlement, "
               "$650 appraisal, $500 inspection, $225 recording, 0.5% transfer tax, "
               "$2,100 a year homeowners insurance, $6,300 a year property tax, "
               "3 months of tax escrow, $0 seller credits, 15 days of prepaid interest.",
               mv::Outcome::Unsafe, mv::ReasonCode::OutOfRange,
               "bounds: a share above 1.0 is refused, matching the engine's own cap");

        expect(params("ComputeAmortization", {{"loan_amount", "350000.00"},
                                              {"annual_rate", "0.0575"},
                                              {"term_months", "1800"},
                                              {"monthly_overpayment", "0.00"},
                                              {"pmi_annual_rate", "0.0000"},
                                              {"original_home_value", "350000.00"}}),
               "Amortize a $350,000 loan at 5.75% over 1800 months.", mv::Outcome::Unsafe,
               mv::ReasonCode::OutOfRange, "bounds: a grounded 1800-month term is still out of scope");
    }

    // Structural defects other than the six rows.
    {
        const auto ctl = control_payment();
        expect(drop_field(ctl.input, "future_value"), ctl.text, mv::Outcome::Unsafe,
               mv::ReasonCode::MissingField,
               "a declared field left out is refused, not defaulted to zero");
        expect(mutate(ctl.input, "timing", "MIDDLE_OF_PERIOD"), ctl.text, mv::Outcome::Unsafe,
               mv::ReasonCode::InvalidEnumValue, "an invented enum constant is refused");

        auto dup = ctl.input;
        dup.fields.push_back(
            mv::EmittedField{.name = "periods", .values = {"180"}, .repeated = false});
        expect(dup, ctl.text, mv::Outcome::Unsafe, mv::ReasonCode::DuplicateField,
               "the same field emitted twice is refused");

        auto shape = ctl.input;
        for (auto& f : shape.fields) {
            if (f.name == "periods") f.repeated = true;
        }
        expect(shape, ctl.text, mv::Outcome::Unsafe, mv::ReasonCode::ShapeMismatch,
               "a scalar field emitted as a list is refused");
    }

    // An out-of-scope operation that IS a real RPC of sensen.finance.Finance
    // but is not this assistant's business.
    {
        expect(params("AnalyzeBond", {{"face_value", "1000.00"}}), "Price a 10-year bond at 5%.",
               mv::Outcome::Unsafe, mv::ReasonCode::UnknownOperation,
               "a real Finance RPC outside the mortgage label space is still refused");
    }

    // ===================================================================
    section("DIRECTION 2 -- legitimate requests still PASS (the controls)");
    // ===================================================================
    {
        for (const auto& c : all_controls()) {
            expect_pass(c.input, c.text, c.label);
        }
    }

    // The pair that isolates the per-period rule: 0.005417 from "6.5%" is
    // correct for `rate` (finance.proto: per-period) and would be wrong for a
    // field named `annual_*`, and both halves are asserted.
    {
        const auto ctl = control_payment();
        expect_pass(ctl.input, ctl.text,
                    "pair: `rate` 0.005417 from a stated 6.5% annual (per-period, PASSES)");
        expect(mutate(ctl.input, "rate", "0.0054"), ctl.text, mv::Outcome::Unsafe,
               mv::ReasonCode::UngroundedValue,
               "pair: `rate` 0.0054 -- a 4-place rounding of the same figure -- is REFUSED "
               "(6 places is the contract's rate precision, and 0.0054 is coarser)");
    }

    // The pair that isolates the field-name rule.
    {
        expect_pass(control_rent_vs_buy().input, control_rent_vs_buy().text,
                    "pair: `years` PASSES on ComputeRentVsBuy, which declares it");
    }

    // -----------------------------------------------------------------
    // Per-operation excluded fields: NOT REQUIRED, but still ACCEPTED.
    //
    // finance.proto declares `rate` and `guess` on request messages SHARED by
    // more than one operation, and restricts them per-operation in a COMMENT:
    // "XNPV only; ignored by XIRR", "XIRR only", "omit for the engine's own
    // starting guess". Nothing that parses the message can see a comment, so
    // G2b required a field the operation ignores.
    //
    // What that cost, measured through the real ParseOperation RPC on the
    // Q8_0 GGUF: ComputeXirr's `rate` is the value XIRR COMPUTES -- the corpus
    // wrote the ANSWER into a discarded field on an utterance that never
    // states it, and 9 of 9 held-out rows failed on it. ComputeRate's `guess`
    // is a solver seed: 11 of 11 rows differed on that field ALONE, every
    // other field exact.
    //
    // BOTH directions are asserted, and the second is the one that protects
    // production. The field stays DECLARED, so G2a keeps accepting it -- the
    // model deployed today emits `guess`, and deleting the declaration would
    // refuse every ComputeRate parse coming from it. Absence is now what the
    // proto always said it was: "use the engine's default".
    {
        // ComputeRate. The utterance states loan, payment and term; it does
        // not state a Newton seed, because no user has ever stated one.
        const std::string rate_text =
            "$731,800 loan, $5,083.69/month, 20-year -- back out the interest rate.";
        const auto rate_without = params("ComputeRate", {{"periods", "240"},
                                                         {"payment", "5083.69"},
                                                         {"present_value", "731800.00"},
                                                         {"future_value", "0.00"},
                                                         {"timing", "END_OF_PERIOD"}});
        expect_pass(rate_without, rate_text,
                    "excluded: ComputeRate WITHOUT `guess` is Proven (was MissingField -- the "
                    "proto says omit it, and 11/11 held-out rows failed on this field alone)");

        auto rate_with = rate_without;
        rate_with.fields.push_back(mv::EmittedField{.name = "guess", .values = {"0.005000"}, .repeated = false});
        expect_pass(rate_with, rate_text,
                    "excluded: ComputeRate WITH `guess` is STILL Proven -- the deployed model "
                    "emits it, and refusing it would break production");
    }
    {
        // ComputeXirr. `rate` is ignored by XIRR and is the answer it returns.
        const std::string xirr_text =
            "I invest $168,100 today and expect back $127,237.88 after 349 days; "
            "$88,963.55 after 736 days.";
        mv::MortgageParamsInput xirr_without;
        xirr_without.params_emitted = true;
        xirr_without.operation = "ComputeXirr";
        xirr_without.fields.push_back(mv::EmittedField{
            .name = "values", .values = {"-168100", "127237.88", "88963.55"}, .repeated = true});
        xirr_without.fields.push_back(mv::EmittedField{
            .name = "dates", .values = {"0.0", "349.0", "736.0"}, .repeated = true});
        xirr_without.fields.push_back(mv::EmittedField{
            .name = "guess", .values = {"0.1"}, .repeated = false});
        expect_pass(xirr_without, xirr_text,
                    "excluded: ComputeXirr WITHOUT `rate` is Proven -- `rate` is the value XIRR "
                    "COMPUTES and the engine ignores it");
    }
    {
        // ComputeXnpv. `guess` is XIRR-only; XNPV takes a stated discount rate.
        const std::string xnpv_text =
            "I invest $243,800 today and expect back $77,840.61 after 331 days. "
            "What's the NPV at a 4.63% discount rate?";
        mv::MortgageParamsInput xnpv_without;
        xnpv_without.params_emitted = true;
        xnpv_without.operation = "ComputeXnpv";
        xnpv_without.fields.push_back(mv::EmittedField{
            .name = "rate", .values = {"0.0463"}, .repeated = false});
        xnpv_without.fields.push_back(mv::EmittedField{
            .name = "values", .values = {"-243800", "77840.61"}, .repeated = true});
        xnpv_without.fields.push_back(mv::EmittedField{
            .name = "dates", .values = {"0.0", "331.0"}, .repeated = true});
        expect_pass(xnpv_without, xnpv_text,
                    "excluded: ComputeXnpv WITHOUT `guess` is Proven -- the proto says the field "
                    "is XIRR only");
    }

    // -----------------------------------------------------------------
    std::printf("\n%d checks, %d failures\n", g_checks, g_failures);
    return g_failures == 0 ? 0 : 1;
}
