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
#include <cstdio>


import std;
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
                                           {"original_home_value", "350000.00"},
                                           {"annual_repairs", "0.00"},
                                           {"annual_insurance", "0.00"},
                                           {"annual_cost_growth", "0.0000"}, {"monthly_hoa", "0.00"}})};
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
                                           {"original_home_value", "1356200.00"},
                                           {"annual_repairs", "0.00"},
                                           {"annual_insurance", "0.00"},
                                           {"annual_cost_growth", "0.0000"}, {"monthly_hoa", "0.00"}})};
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
                                                 "ComputeRentVsBuyBatch",
                                                 "RefreshStateAssumptions",
                                                 "GetStateAssumptions",
                                                 // A REPORTING call, not an
                                                 // utterance: it describes a
                                                 // scenario already computed.
                                                 "ExplainMortgage"};

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
            for (const auto& [op, all_proto_fields] : from_proto) {
                // EXCLUDED FIELDS ARE NOT PART OF THE EMITTABLE LABEL SPACE, so
                // the comparison is against what the model may emit rather than
                // against every field the message declares. `mortgage_grammar`'s
                // own copy of this gate already skips them; this one did not,
                // so adding a field the service DROPS before the verifier ever
                // sees it failed here while the system was entirely consistent.
                //
                // Keeping the skip in both places is the point -- two gates
                // asking the same question of the same artifact must agree
                // about what the question is, or the one that is wrong reads
                // as a real defect.
                std::vector<std::string> fields;
                for (const auto& f : all_proto_fields) {
                    if (!mv::operation_excludes_field(op, f)) { fields.push_back(f); }
                }
                if (!mv::is_known_operation(op)) {
                    all_match = false;
                    first_mismatch = "module is missing operation " + op;
                    break;
                }
                // BOTH SIDES, or the comparison is asymmetric and the error
                // simply moves to whichever operation excludes something else.
                // Filtering only the proto made ComputeIrr fail instead --
                // 1 against 2 -- which looks like a new defect and is the same
                // one, half-fixed.
                std::vector<mv::FieldSpec> module_fields;
                for (const auto& f : mv::fields_of(op)) {
                    if (!mv::operation_excludes_field(op, f.field)) { module_fields.push_back(f); }
                }
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
                                              {"original_home_value", "350000.00"},
                                              {"annual_repairs", "0.00"},
                                              {"annual_insurance", "0.00"},
                                              {"annual_cost_growth", "0.0000"}, {"monthly_hoa", "0.00"}}),
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
                                              {"original_home_value", "350000.00"},
                                              {"annual_repairs", "0.00"},
                                              {"annual_insurance", "0.00"},
                                              {"annual_cost_growth", "0.0000"}, {"monthly_hoa", "0.00"}}),
               "Amortize a $350,000 loan at 5.75% over 1800 months.", mv::Outcome::Unsafe,
               mv::ReasonCode::OutOfRange, "bounds: a grounded 1800-month term is still out of scope");
    }

    // Structural defects other than the six rows.
    {
        const auto ctl = control_payment();
        // G2b asks for the ESSENTIAL inputs, not every declared field (owner decision,
        // 2026-10-06): a future value the visitor never mentioned is not a gap.
        expect(drop_field(ctl.input, "future_value"), ctl.text, mv::Outcome::Proven, mv::ReasonCode::None,
               "an OPTIONAL declared field left out is served without it, not refused");
        expect(drop_field(ctl.input, "present_value"), ctl.text, mv::Outcome::Unsafe,
               mv::ReasonCode::MissingField,
               "an ESSENTIAL field left out is refused here, never defaulted to zero");
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

    // -----------------------------------------------------------------
    // The TVM SIGN CONVENTION, and the per-VARIANT inert fields.
    //
    // Both are contract facts about the Finance RPCs that the assistant service
    // applies to its own output, so they are tested here as pure predicates
    // rather than through a model.
    {
        // ComputeRate / ComputePeriods solve the annuity balance. With FV = 0
        // it has a root only when PV and PMT oppose. The corpus emits both
        // positive -- "$1,275,100 loan, $7,751.77/month" is how a person says
        // it -- and before this the assistant handed back parameters the RPC it
        // NAMES refuses: ComputeRate on "Newton-Raphson failed to converge" and
        // ComputePeriods with a 200 OK carrying -119.70 periods for a loan
        // whose term is 359.955.
        check(mv::tvm_payment_needs_sign_flip("ComputeRate", "731800.00", "5083.69", "0.00"),
              "sign: ComputeRate with both legs positive and FV 0 needs the flip");
        check(mv::tvm_payment_needs_sign_flip("ComputePeriods", "1275100.00", "7751.77", "0.00"),
              "sign: ComputePeriods likewise -- this is the -119.70 request");
        check(mv::tvm_payment_needs_sign_flip("ComputeRate", "-731800.00", "-5083.69", "0"),
              "sign: BOTH negative also needs it -- the rule is same-sign, not positive");
        check(!mv::tvm_payment_needs_sign_flip("ComputeRate", "731800.00", "-5083.69", "0.00"),
              "sign: already opposed is left alone -- a retrained model that emits the sign "
              "must not have it flipped back");

        // Scoped to FV == 0, which is what keeps it from being a sign
        // heuristic: a savings goal legitimately has both legs on one side.
        check(!mv::tvm_payment_needs_sign_flip("ComputePeriods", "-10000.00", "-500.00",
                                               "100000.00"),
              "sign: a NON-ZERO future value is left alone -- a savings goal is same-signed "
              "on purpose");

        // Zero is not a sign. Flipping against a zero leg would turn a
        // degenerate request into a different degenerate request.
        check(!mv::tvm_payment_needs_sign_flip("ComputeRate", "0.00", "5083.69", "0.00"),
              "sign: a zero present_value has no sign to oppose");
        check(!mv::tvm_payment_needs_sign_flip("ComputeRate", "731800.00", "0", "0.00"),
              "sign: a zero payment likewise");
        check(mv::tvm_payment_needs_sign_flip("ComputeRate", "731800.00", "5083.69", "-0.00"),
              "sign: -0.00 IS zero, so this is still the FV == 0 case and it flips -- a minus "
              "on a zero is notation, not a direction");

        // Scoped to the two operations that SOLVE. ComputePayment,
        // ComputePresentValue and ComputeFutureValue EVALUATE a closed form and
        // their signed output IS the convention.
        for (const char* op : {"ComputePayment", "ComputePresentValue", "ComputeFutureValue",
                               "ComputeAmortization"}) {
            check(!mv::tvm_payment_needs_sign_flip(op, "731800.00", "5083.69", "0.00"),
                  std::string{"sign: "} + op + " is untouched -- it evaluates, it does not solve");
        }
    }
    {
        // Per-variant inert fields. DepreciationRequest is one message serving
        // four methods; six of its eight fields are restricted per method in
        // proto comments no consumer can see. A straight-line request was
        // refused live on `"factor" = 3`, a number that provably changes
        // nothing (2.0/3.0/1.5 all return 5017.9487179487178).
        check(mv::variant_governing_field("ComputeDepreciation") == "method",
              "variant: `method` governs which ComputeDepreciation fields are inert");
        check(mv::variant_governing_field("ComputePayment").empty(),
              "variant: an operation with no variants reports no governing field");

        check(mv::field_is_inert_for_variant("ComputeDepreciation", "STRAIGHT_LINE", "factor"),
              "variant: `factor` is inert for STRAIGHT_LINE -- this is the live refusal");
        check(mv::field_is_inert_for_variant("ComputeDepreciation", "STRAIGHT_LINE", "period"),
              "variant: SLN takes no period -- sln(cost, salvage, life)");
        check(mv::field_is_inert_for_variant("ComputeDepreciation", "SUM_OF_YEARS_DIGITS",
                                             "factor"),
              "variant: SYD takes no factor either");
        check(mv::field_is_inert_for_variant("ComputeDepreciation", "MACRS", "life"),
              "variant: MACRS uses recovery_period instead of life");

        // THE ROW A PROBE GOT WRONG, and the reason this table is derived from
        // the function signatures instead. Varying `salvage` on an early DDB
        // period changes nothing, so a one-field-at-a-time probe called it
        // inert. `sensen::ddb` reads it twice -- the `cost <= salvage` guard and
        // the per-period floor -- and dropping it would change the answer for a
        // late period.
        check(!mv::field_is_inert_for_variant("ComputeDepreciation", "DECLINING_BALANCE",
                                              "salvage"),
              "variant: `salvage` is LIVE for DECLINING_BALANCE -- ddb reads it as the floor, "
              "which a probe on an early period cannot see");
        check(!mv::field_is_inert_for_variant("ComputeDepreciation", "DECLINING_BALANCE",
                                              "factor"),
              "variant: `factor` is LIVE for DECLINING_BALANCE -- it is the whole point of DDB");
        check(!mv::field_is_inert_for_variant("ComputeDepreciation", "MACRS", "recovery_period"),
              "variant: `recovery_period` is LIVE for MACRS");
        check(!mv::field_is_inert_for_variant("ComputeDepreciation", "STRAIGHT_LINE", "cost"),
              "variant: nothing ever makes `cost` inert");

        // G2b MUST honour the same table the serving side drops on. It did not
        // when the drop first shipped, and the sweep caught it end to end:
        // ComputeDepreciation went from `"factor" = 3 ungrounded` straight to
        // `"period" ... was not emitted`, one refusal traded for another. A
        // unit test of the predicate alone could not see it -- only a request
        // built the way the service builds one.
        const std::string dep_text =
            "Depreciate $199,400 of equipment I bought for the rental using straight-line, "
            "39-year life, salvage $3,700. Year 35's deduction?";
        mv::MortgageParamsInput dep;
        dep.params_emitted = true;
        dep.operation = "ComputeDepreciation";
        dep.fields.push_back(mv::EmittedField{
            .name = "method", .values = {"STRAIGHT_LINE"}, .repeated = false});
        dep.fields.push_back(mv::EmittedField{.name = "cost", .values = {"199400"}, .repeated = false});
        dep.fields.push_back(mv::EmittedField{.name = "salvage", .values = {"3700"}, .repeated = false});
        dep.fields.push_back(mv::EmittedField{.name = "life", .values = {"39"}, .repeated = false});
        expect_pass(dep, dep_text,
                    "variant: a STRAIGHT_LINE parse carrying ONLY the fields sln reads is "
                    "Proven -- G2b honours the same table the service drops on");

        // And the fields it DOES read are still required, so the skip is
        // scoped rather than a hole in G2b.
        auto dep_missing_life = dep;
        dep_missing_life.fields.pop_back();
        expect(dep_missing_life, dep_text, mv::Outcome::Unsafe, mv::ReasonCode::MissingField,
               "variant: `life` is still REQUIRED for STRAIGHT_LINE -- sln reads it");

        // A DDB parse must still carry period and factor, which SLN drops.
        mv::MortgageParamsInput ddb;
        ddb.params_emitted = true;
        ddb.operation = "ComputeDepreciation";
        ddb.fields.push_back(mv::EmittedField{
            .name = "method", .values = {"DECLINING_BALANCE"}, .repeated = false});
        ddb.fields.push_back(mv::EmittedField{.name = "cost", .values = {"199400"}, .repeated = false});
        ddb.fields.push_back(mv::EmittedField{.name = "salvage", .values = {"3700"}, .repeated = false});
        ddb.fields.push_back(mv::EmittedField{.name = "life", .values = {"39"}, .repeated = false});
        expect(ddb, dep_text, mv::Outcome::Unsafe, mv::ReasonCode::MissingField,
               "variant: the SAME field set is REFUSED for DECLINING_BALANCE -- it reads "
               "period and factor, so the skip is per variant and not per operation");
    }

    // -----------------------------------------------------------------
    // The BATCH's plural convention fields, missed when the singulars landed.
    //
    // ComputeAmortizationBatch takes parallel arrays. `extra_payments` and
    // `pmi_rates` are the per-offer spellings of `extra_monthly_payment` and
    // `pmi_annual_rate`, both of which were already exempt at 0 -- and neither
    // plural was. A comparison utterance never mentions extra payments or PMI,
    // so the model emits [0, 0] for both and EVERY batch request was refused.
    // Measured against the live ingress on 2026-09-03.
    {
        const std::string batch_text =
            "Compare these loan offers: $362,100 at 5.56% over 15-year; "
            "$256,100 at 7.1% over 30-year.";
        const auto batch = [] {
            mv::MortgageParamsInput in;
            in.params_emitted = true;
            in.operation = "ComputeAmortizationBatch";
            in.fields.push_back(mv::EmittedField{
                .name = "loan_amounts", .values = {"362100", "256100"}, .repeated = true});
            in.fields.push_back(mv::EmittedField{
                .name = "annual_rates", .values = {"0.0556", "0.071"}, .repeated = true});
            in.fields.push_back(mv::EmittedField{
                .name = "term_months", .values = {"180", "360"}, .repeated = true});
            in.fields.push_back(mv::EmittedField{
                .name = "extra_payments", .values = {"0", "0"}, .repeated = true});
            in.fields.push_back(mv::EmittedField{
                .name = "pmi_rates", .values = {"0.0", "0.0"}, .repeated = true});
            in.fields.push_back(mv::EmittedField{
                .name = "home_values", .values = {"362100", "256100"}, .repeated = true});
            return in;
        };
        expect_pass(batch(), batch_text,
                    "batch: the corpus's own label is Proven -- `extra_payments` and "
                    "`pmi_rates` at [0, 0] are the batch spelling of two zeros already exempt");

        // Per ELEMENT, not per field. A stated overpayment must still ground,
        // or the exemption would launder every value in the array.
        auto batch_ungrounded = batch();
        batch_ungrounded.fields[3].values = {"0", "250"};
        expect(batch_ungrounded, batch_text, mv::Outcome::Unsafe,
               mv::ReasonCode::UngroundedValue,
               "batch: [0, 250] is still REFUSED on the 250 -- the exemption is per element, "
               "so a zero beside an invented number does not carry it");

        // And a genuinely stated overpayment grounds, which is what proves the
        // check above is refusing the INVENTION and not the shape.
        auto batch_stated = batch();
        batch_stated.fields[3].values = {"0", "250"};
        expect_pass(batch_stated, batch_text + " On the second one I'd add $250 a month.",
                    "batch: the same [0, 250] is Proven once the utterance states the $250");
    }

    // -----------------------------------------------------------------
    // The SEED, swept across the whole class this time.
    //
    // ComputeRate's `guess` was excluded above and its two semantic twins were
    // not. `finance_service.cpp` dispatches
    // `guess == 0.0 ? xirr(values, dates) : xirr(values, dates, guess)`, so
    // omitting it means "the engine's own starting guess" on ComputeXirr and
    // ComputeIrr EXACTLY as RateRequest's comment says it does -- the proto now
    // states that on all three messages.
    //
    // Being excluded is only half of it, and the half that did not matter.
    // Exclusion stops G2b REQUIRING the field; an emitted one is still ground-
    // ed, and the deployed model DOES emit it. Measured against the live
    // ingress on 2026-09-03: the corpus teaches `guess: 0.1`, the model invents
    // **0.25** for ComputeXirr, nothing whitelisted it, and 3 of 3 dated IRR
    // utterances were refused with `"guess" = 0.25 does not correspond to
    // anything in the request`. ComputeIrr (emits 0.1) and ComputeRate (omits
    // it) both parsed. One operation of twenty-seven, dead, on one constant --
    // which is why the fix is a FIELD exemption and not a 39th convention row.
    {
        const std::string xirr_text =
            "I invest $168,100 today and expect back $127,237.88 after 349 days; "
            "$88,963.55 after 736 days. What return (IRR) am I getting?";
        const auto xirr_base = [&] {
            mv::MortgageParamsInput in;
            in.params_emitted = true;
            in.operation = "ComputeXirr";
            in.fields.push_back(mv::EmittedField{
                .name = "values", .values = {"-168100", "127237.88", "88963.55"}, .repeated = true});
            in.fields.push_back(mv::EmittedField{
                .name = "dates", .values = {"0.0", "349.0", "736.0"}, .repeated = true});
            return in;
        };

        expect_pass(xirr_base(), xirr_text,
                    "seed: ComputeXirr WITHOUT `guess` is Proven -- omission is the proto's "
                    "own 'use the engine's default'");

        auto xirr_025 = xirr_base();
        xirr_025.fields.push_back(
            mv::EmittedField{.name = "guess", .values = {"0.25"}, .repeated = false});
        expect_pass(xirr_025, xirr_text,
                    "seed: ComputeXirr WITH the production `guess` = 0.25 is Proven -- this is "
                    "the exact live refusal that made the operation unreachable");

        auto xirr_odd = xirr_base();
        xirr_odd.fields.push_back(
            mv::EmittedField{.name = "guess", .values = {"0.1734"}, .repeated = false});
        expect_pass(xirr_odd, xirr_text,
                    "seed: an ARBITRARY guess is Proven too -- the exemption is the field, not "
                    "a longer whitelist; the next model's invented constant is covered");

        // The exemption is GROUNDING-only, and this is what says so. Bounds run
        // in translate() (G5) BEFORE grounding is ever consulted, so `guess` is
        // still SlotKind::Rate and still has to be a plausible one. If this
        // check ever passes, the exemption has stopped being scoped.
        auto xirr_negative = xirr_base();
        xirr_negative.fields.push_back(
            mv::EmittedField{.name = "guess", .values = {"-0.25"}, .repeated = false});
        expect(xirr_negative, xirr_text, mv::Outcome::Unsafe, mv::ReasonCode::OutOfRange,
               "seed: a NEGATIVE guess is still REFUSED -- exempt from appearing in the text, "
               "not exempt from being a rate");

        // And the exemption must not leak to the fields beside it on the same
        // request. `rate` is the value XIRR COMPUTES and is excluded from this
        // operation entirely, so the neighbour used here is `values` -- an
        // invented cash flow is still an invented cash flow.
        auto xirr_bad_neighbour = xirr_base();
        xirr_bad_neighbour.fields.push_back(
            mv::EmittedField{.name = "guess", .values = {"0.25"}, .repeated = false});
        xirr_bad_neighbour.fields[0].values = {"-168100", "127237.88", "99999.99"};
        expect(xirr_bad_neighbour, xirr_text, mv::Outcome::Unsafe, mv::ReasonCode::UngroundedValue,
               "seed: an exempt `guess` does NOT license the field next to it -- a corrupted "
               "cash flow on the same request is still refused");
    }
    {
        // The sibling, for the same reason. It parses today only because the
        // model happens to emit the one value the corpus taught.
        const std::string irr_text =
            "I invest $50,000 today and expect back year 1: $20,000; year 2: $22,000; "
            "year 3: $25,000. What return (IRR) am I getting?";
        mv::MortgageParamsInput irr;
        irr.params_emitted = true;
        irr.operation = "ComputeIrr";
        irr.fields.push_back(mv::EmittedField{
            .name = "values", .values = {"-50000", "20000", "22000", "25000"}, .repeated = true});
        expect_pass(irr, irr_text,
                    "seed: ComputeIrr WITHOUT `guess` is Proven -- the same exclusion, on the "
                    "sibling that was missed with it");

        auto irr_with = irr;
        irr_with.fields.push_back(
            mv::EmittedField{.name = "guess", .values = {"0.28"}, .repeated = false});
        expect_pass(irr_with, irr_text,
                    "seed: ComputeIrr with an unlisted guess is Proven -- it emits 0.1 today, "
                    "and nothing guarantees the next model does");

        // The band's UPPER edge, asserted so the limit is written down rather
        // than discovered. `guess` is SlotKind::Rate, so kMaxRateUnits (30%)
        // applies to it, and a seed above that is refused even though the
        // ENGINE would accept it -- the verifier is stricter here.
        //
        // That is accepted, and it is accepted on a MEASUREMENT rather than an
        // argument: bounding the SEED does not bound the ANSWER. Against
        // production on 2026-09-03, ComputeXirr on {-1000, +5200} one year
        // apart returns **4.2** -- a 420% IRR -- from a seed of 0.1, and
        // identically with the seed omitted. Newton does not need to start
        // near the root, so capping the seed at 30% costs no reachable answer.
        auto irr_out_of_band = irr;
        irr_out_of_band.fields.push_back(
            mv::EmittedField{.name = "guess", .values = {"0.31"}, .repeated = false});
        expect(irr_out_of_band, irr_text, mv::Outcome::Unsafe, mv::ReasonCode::OutOfRange,
               "seed: a guess ABOVE the 30% rate band is refused -- the exemption is from "
               "grounding only, and this costs no answer because a 420% IRR is still found "
               "from a seed of 0.1");
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

    // =======================================================================
    section("Down payments: M0 refuses one thing, M9 admits another");
    // =======================================================================
    // Both halves come from ONE production failure, measured 2026-08-29 through
    // the live ingress on "a 500000 home with 20% down at 6.5%":
    //
    //   * `annual_rate = 0.2000` was returned as PROVEN. The model read the
    //     down payment as the interest rate, dropped the stated 6.5%, and the
    //     gate admitted it because 20 -> 0.20 is an ordinary M2 candidate for a
    //     rate slot. A real field, a parseable value, every bound satisfied,
    //     and a 20% mortgage priced. This is the "corrupted value" failure in
    //     its most dangerous form, and per-field grounding could not see it.
    //
    //   * `present_value = 400000` -- the CORRECT loan -- had no candidate at
    //     all, because every map was unary and 400000 is written nowhere.
    //
    // Those pull in opposite directions, which is why one section covers both:
    // a fix for either alone would have been half a rule.
    {
        const std::string text =
            "amortization schedule for a 500000 home with 20% down at 6.5% for 30 years";

        // --- M0: the down-payment percent may not be the rate. -------------
        auto as_rate = params("ComputeAmortization", {{"loan_amount", "500000.00"},
                                                      {"annual_rate", "0.2000"},
                                                      {"term_months", "360"},
                                                      {"monthly_overpayment", "0.00"},
                                                      {"pmi_annual_rate", "0.0000"},
                                                      {"original_home_value", "500000.00"},
                                                      {"annual_repairs", "0.00"},
                                                      {"annual_insurance", "0.00"},
                                                      {"annual_cost_growth", "0.0000"}, {"monthly_hoa", "0.00"}});
        expect(as_rate, text, mv::Outcome::Unsafe, mv::ReasonCode::UngroundedValue,
               "the DOWN PAYMENT percent is refused as an interest rate -- this exact output "
               "was returned as Proven in production and priced a 20% mortgage");

        // The rate the user actually stated still grounds, from the same
        // utterance. Without this the section would pass with a gate that
        // refuses every rate, which is the failure mode it is guarding.
        auto correct_rate = params("ComputeAmortization", {{"loan_amount", "500000.00"},
                                                           {"annual_rate", "0.0650"},
                                                           {"term_months", "360"},
                                                           {"monthly_overpayment", "0.00"},
                                                           {"pmi_annual_rate", "0.0000"},
                                                           {"original_home_value", "500000.00"},
                                                           {"annual_repairs", "0.00"},
                                                           {"annual_insurance", "0.00"},
                                                           {"annual_cost_growth", "0.0000"}, {"monthly_hoa", "0.00"}});
        expect_pass(correct_rate, text,
                    "the STATED 6.5% still grounds as the rate in the same sentence");

        // --- M9: the netted loan is admissible. ----------------------------
        auto netted = params("ComputeAmortization", {{"loan_amount", "400000.00"},
                                                     {"annual_rate", "0.0650"},
                                                     {"term_months", "360"},
                                                     {"monthly_overpayment", "0.00"},
                                                     {"pmi_annual_rate", "0.0000"},
                                                     {"original_home_value", "500000.00"},
                                                     {"annual_repairs", "0.00"},
                                                     {"annual_insurance", "0.00"},
                                                     {"annual_cost_growth", "0.0000"}, {"monthly_hoa", "0.00"}});
        expect_pass(netted, text,
                    "loan_amount = 500000 - 20% = 400000 is GROUNDED, though 400000 appears "
                    "nowhere in the utterance");

        // The money spelling of the same thing must behave identically. A rule
        // that held for "20% down" and not for "100,000 down" would be a trap.
        const std::string money_text =
            "monthly payment on a 500000 home, 100000 down payment, 6.5%, 360 months";
        auto money_netted = params("ComputePayment", {{"rate", "0.005417"},
                                                      {"periods", "360"},
                                                      {"present_value", "400000.00"},
                                                      {"future_value", "0.00"},
                                                      {"timing", "END_OF_PERIOD"}});
        expect_pass(money_netted, money_text,
                    "present_value = 500000 - 100000 is grounded on the MONEY spelling too");

        // --- The bounds of M9, which are the point of scoping it. ----------
        // A difference that is not a down payment is NOT admissible: this is
        // what stops "any two numbers may be subtracted" from being the rule.
        auto not_a_down_payment = params("ComputePayment", {{"rate", "0.005417"},
                                                            {"periods", "360"},
                                                            {"present_value", "400000.00"},
                                                            {"future_value", "0.00"},
                                                            {"timing", "END_OF_PERIOD"}});
        expect(not_a_down_payment,
               "monthly payment on a 500000 loan minus a 100000 credit at 6.5% for 360 months",
               mv::Outcome::Unsafe, mv::ReasonCode::UngroundedValue,
               "a difference of two money literals that is NOT a down payment stays ungrounded");

        // And the netting is scoped to the loan slots by NAME. `future_value`
        // is Money too, and nothing about it means "price less the deposit".
        auto wrong_slot = params("ComputePayment", {{"rate", "0.005417"},
                                                    {"periods", "360"},
                                                    {"present_value", "500000.00"},
                                                    {"future_value", "400000.00"},
                                                    {"timing", "END_OF_PERIOD"}});
        expect(wrong_slot, money_text, mv::Outcome::Unsafe, mv::ReasonCode::UngroundedValue,
               "M9 does not reach `future_value` -- it is scoped to the two loan slots by name");

        // The full price is STILL grounded, deliberately. M9 adds a candidate;
        // it does not remove M1. "the payment on the 500,000" is a question a
        // person can legitimately ask, and the gate's job is not to decide
        // which of two admissible readings the user meant.
        auto gross = params("ComputePayment", {{"rate", "0.005417"},
                                               {"periods", "360"},
                                               {"present_value", "500000.00"},
                                               {"future_value", "0.00"},
                                               {"timing", "END_OF_PERIOD"}});
        expect_pass(gross, money_text,
                    "the GROSS price still grounds -- M9 widens the candidate set, it does not "
                    "replace M1, and choosing between readings is the model's job not the gate's");

        // EVERY SPELLING THE CORPUS GENERATES must be one the lexer tags, and
        // this is the only thing tying the two together. `make_down_payment_
        // extraction` emits "20% down", "20% down payment" and "a 20% deposit";
        // the lexer matches "down", "downpayment" and "deposit". Change one
        // word list without the other and the corpus teaches the model to emit
        // a figure the serving path refuses -- silently, and only for the
        // phrasings that drifted.
        for (const auto* spelling : {"20% down", "20% down payment", "a 20% deposit",
                                     "100000 down", "a 100000 down payment",
                                     "a 100000 deposit"}) {
            const std::string spelt =
                std::string{"monthly payment on a 500000 home with "} + spelling +
                " at 6.5% for 360 months";
            auto netted_spelling = params("ComputePayment", {{"rate", "0.005417"},
                                                             {"periods", "360"},
                                                             {"present_value", "400000.00"},
                                                             {"future_value", "0.00"},
                                                             {"timing", "END_OF_PERIOD"}});
            expect_pass(netted_spelling, spelt,
                        std::string{"corpus spelling \""} + spelling +
                            "\" grounds the netted loan");
        }

        // A down-payment percent in a RATIO slot is exactly right, and M0 must
        // not touch it -- the same literal is correct in one slot and dangerous
        // in another, which is why this could never be a magnitude heuristic.
        // Reusing the closing-costs control from the bounds section verbatim:
        // its utterance says "with 10% down", so it is precisely the shape M0
        // would break if it were scoped one level wider.
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
               "M0 is scoped to Rate: `down_payment_percent` = 0.10 from \"10% down\" is still Proven");
    }

    // -----------------------------------------------------------------
    // Section 26. The DATED SIBLING guard: ComputeNpv/ComputeXnpv and
    // ComputeIrr/ComputeXirr.
    //
    // WHY THIS SECTION EXISTS. On 2026-09-14 a retrained model answered 8 of 9
    // held-out ComputeXnpv rows as ComputeNpv. Every one of those answers
    // parsed, named a real operation, carried the caller's own figures and
    // satisfied every bound -- so nothing already in this file could see it.
    // The dates were simply dropped and an evenly-spaced NPV came back looking
    // exactly like the right answer. It cost 0.9 points of a pooled score,
    // which is indistinguishable from noise.
    //
    // The guard has to be SPECIFIC as well as sensitive, and the controls below
    // are the half that matters: an ordinary NPV utterance must still be served,
    // the dated operation itself must never be refused for carrying dates, and
    // an unrelated operation that happens to mention days must be untouched.
    {
        const std::string dated =
            "I invest $321,700 today and expect back $48,314.06 after 394 days; "
            "$13,975.34 after 725 days; $32,829.43 after 1102 days.";
        const std::string periodic =
            "I invest $125,000 today and expect back year 1: $68,822.83; "
            "year 2: $56,190.54; year 3: $82,357.43. What's the NPV at a 9% discount rate?";

        check(mv::dated_utterance_rejects_operation("ComputeNpv", dated),
              "ComputeNpv on a day-stated series is REFUSED -- it would discard the dates");
        check(mv::dated_utterance_rejects_operation("ComputeIrr", dated),
              "ComputeIrr on a day-stated series is REFUSED -- same pair, same defect");

        // Specificity. Without these three the guard could refuse everything
        // and still pass the two above, which is the shape of a gate that is
        // green and useless.
        check(!mv::dated_utterance_rejects_operation("ComputeNpv", periodic),
              "an evenly-spaced NPV utterance is still served");
        check(!mv::dated_utterance_rejects_operation("ComputeIrr", periodic),
              "an evenly-spaced IRR utterance is still served");
        check(!mv::dated_utterance_rejects_operation("ComputeXnpv", dated),
              "ComputeXnpv is the operation that HANDLES dates and is never refused for them");
        check(!mv::dated_utterance_rejects_operation("ComputeXirr", dated),
              "ComputeXirr likewise");

        // Scoping. "15 days of prepaid interest" is an ordinary closing-costs
        // phrase; a guard that read the word "days" anywhere would break it.
        check(!mv::dated_utterance_rejects_operation(
                  "ComputeClosingCosts",
                  "Closing costs on a $450,000 home with 10% down at 6.75%, "
                  "3 months of tax escrow, 15 days of prepaid interest."),
              "an unrelated operation mentioning days is untouched");
        check(!mv::dated_utterance_rejects_operation(
                  "ComputePayoffTiming",
                  "How many days until the loan is paid off if I add $300 a month?"),
              "the guard is scoped to the two operations that HAVE a dated sibling");
    }

    // -----------------------------------------------------------------
    std::printf("\n27. the lexer's INCREMENT adjacency\n");
    {
        // TESTED HERE, NOT ONLY THROUGH THE DERIVATION LAYER. This flag is set
        // in mortgage_verification.cppm and consumed in mortgage_derivation
        // .cppm, and a test that can only see it through the consumer reports
        // "the solver is wrong" when the LEXER is. The two modules are the
        // places a reader looks; the flag should be falsifiable in both.
        //
        // It exists because "$750 more a month" is an ordinary money literal
        // until the adjacent words say otherwise, and while it was one it
        // paired with an opening turn's "10% down" under the loan rule and
        // derived a $675 mortgage -- two real literals, exact arithmetic, and
        // a rule with no way to know "more" meant an increment.
        const auto flagged = [](std::string_view text) {
            for (const auto& lit : mv::lex_numeric_literals(text)) {
                if (lit.names_increment) { return true; }
            }
            return false;
        };

        // Both spellings the corpus generates, on both sides of the literal.
        check(flagged("what if I pay $750 more a month?"),
              "\"$750 more a month\" is an increment (qualifier AFTER)");
        check(flagged("now add $300 extra a month"),
              "\"$300 extra a month\" is an increment");
        check(flagged("paying an extra $250/month"),
              "\"an extra $250/month\" is an increment (qualifier BEFORE)");
        check(flagged("with $300/month extra"),
              "\"$300/month extra\" is an increment (qualifier past a slash)");

        // AND THE FALSE POSITIVES, which are the dangerous direction: this flag
        // MOVES a literal to another slot, so a wrong one is a wrong answer
        // rather than a refused one.
        check(!flagged("rent $3,200/month, mortgage payment $2,300/month"),
              "an ordinary monthly figure is NOT an increment");
        check(!flagged("Amortize the loan on a $796,000 property, a 10% deposit"),
              "a price and a deposit are not increments");
        check(!flagged("redo it for $817,400"),
              "a REVISION of the price is not an increment");
        check(!flagged("5.97% over 30 years"),
              "a percent is never tagged as a money increment");
    }

    std::printf("\nthe PMI drop-off threshold: exempt at its convention, GROUNDED otherwise\n");
    {
        // PINNED BEFORE THE THRESHOLD BECOMES CALLER-CONFIGURABLE, because all
        // three directions have to keep holding once more operations carry this
        // field, and only the first of them is obvious.
        //
        // WHY IT IS NOT UNGROUNDED THE WAY `guess` IS, which is the tempting
        // move: kUngroundedFields exists because "A SEED CANNOT CARRY A WRONG
        // ANSWER THE WAY A QUANTITY CAN -- it selects a root, it does not state
        // one." An LTV threshold is the opposite. 0.78 against 0.80 moves the
        // month PMI stops and the total cost with it, so a hallucinated
        // threshold is a wrong answer wearing a plausible number, which is
        // exactly what grounding is for. It stays grounded; only the CONVENTION
        // value is exempt.
        const std::string text =
            "I owe $320,000 at 6.5% with 240 months left, paying $5,378.63/month. Home is worth "
            "$500,000. If I refinance to 5.25% over 15 years with $4,000 in closing costs paid "
            "in cash, what's my new payment and break-even?";

        auto refi = [](std::string_view ltv) {
            return params("ComputeRefinance", {{"current_loan_balance", "320000.00"},
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
                                               {"pmi_drop_off_ltv", std::string{ltv}},
                                               {"payments_per_year", "12"}});
        };

        // 1. The convention. The utterance says nothing about PMI, and 0.80 is
        //    how the model spells "I am not specifying one".
        expect_pass(refi("0.80"), text,
                    "0.80 is exempt as the statutory convention");

        // 2. The exemption does NOT license its neighbours. This is the whole
        //    `guess = 0.25` lesson: one unlisted constant made ComputeXirr 100%
        //    unreachable, and the fix there was a FIELD exemption precisely
        //    because a seed states nothing. Here the opposite must hold -- an
        //    unstated 0.78 is a threshold nobody asked for and is refused.
        expect(refi("0.78"), text, mv::Outcome::Unsafe, mv::ReasonCode::UngroundedValue,
               "an unstated 0.78 is refused, so the 0.80 exemption covers only itself");

        // 3. FLEXIBILITY, which is the point of making it configurable: a
        //    threshold the user actually STATES must ground from the text. If
        //    this ever fails, "caller-configurable" is true of the engine and
        //    false of everything in front of it.
        const std::string stated = text + " My lender drops PMI at 78% loan-to-value.";
        expect_pass(refi("0.78"), stated,
                    "a STATED 78% grounds the threshold from the utterance");
    }

    std::printf("\nM10 -- a stated VACANCY grounds the occupancy complement\n");
    {
        // Investors say "8% vacancy" at least as often as "92% occupancy", and
        // the field is occupancy. 0.92 appears NOWHERE in the utterance, so
        // without this map every investor row the corpus teaches would be
        // refused at serving time -- the "squeezed from both sides" failure
        // this project has paid for three times.
        const std::string vacancy_text =
            "I am looking at a $337,500 rental. I would put $84,400 down with $7,100 in "
            "closing costs, borrowing the rest at 6.04% over 15 years. It should let for "
            "$2,200 a month -- assume 8% vacancy. Property tax is $7,000 a year, insurance "
            "$1,500, repairs $1,500, and $1,900 set aside for capital expenditure. What "
            "does the cash flow look like over 5 years?";

        // The whole declared shape, because G2b requires every field; the ones
        // the utterance is silent about carry the convention zero.
        const auto rental = [](std::string_view occupancy, std::string_view rate) {
            return params("ComputeRentalCashFlow",
                          {{"property_price", "337500.00"}, {"down_payment", "84400.00"},
                           {"closing_costs", "7100.00"},    {"loan_annual_rate", std::string{rate}},
                           {"loan_term_years", "15"},       {"monthly_gross_rent", "2200.00"},
                           {"annual_rent_increase", "0.0000"},
                           {"occupancy_rate", std::string{occupancy}},
                           {"annual_property_tax", "7000.00"}, {"annual_insurance", "1500.00"},
                           {"annual_repairs", "1500.00"},   {"annual_capex_reserve", "1900.00"},
                           {"monthly_hoa", "0.00"},         {"management_fee_rate", "0.0000"},
                           {"annual_other_expenses", "0.00"},
                           {"annual_expense_increase", "0.0000"},
                           {"annual_appreciation", "0.0000"},
                           {"selling_cost_percent", "0.0000"}, {"years", "5"},
                           {"heloc_drawn_amount", "0.00"},  {"heloc_annual_rate", "0.0000"},
                           {"heloc_term_years", "0"}});
        };

        expect_pass(rental("0.9200", "0.0604"), vacancy_text,
                    "8% vacancy grounds occupancy_rate = 0.92");

        // THE DANGEROUS DIRECTION. 0.08 is a perfectly ordinary interest rate
        // and sits well inside the 30% band, so nothing downstream would have
        // caught it -- exactly how "20% down" once priced a 20% mortgage.
        expect(rental("0.9200", "0.0800"), vacancy_text, mv::Outcome::Unsafe,
               mv::ReasonCode::UngroundedValue,
               "and the SAME 8% is refused as an interest rate");

        // The identity phrasing needs no map at all; M2 already admits it.
        const std::string occupancy_text =
            "Work out the cash flow on a $337,500 buy-to-let over 5 years. $84,400 deposit, "
            "$7,100 closing, 6.04% for 15 years, rent $2,200 a month, figure on 92% "
            "occupancy. Property tax $7,000 a year, insurance $1,500, repairs $1,500, and "
            "$1,900 for capital expenditure.";
        expect_pass(rental("0.9200", "0.0604"), occupancy_text,
                    "a directly stated 92% occupancy grounds without the complement");

        // FENCED: a percent that names nothing is not evidence of what it
        // names. Without this the map would turn any stray 8% into a 0.92.
        const std::string untagged_text =
            "Work out the cash flow on a $337,500 buy-to-let over 5 years. $84,400 deposit, "
            "$7,100 closing, 6.04% for 15 years, rent $2,200 a month, and 8% of something "
            "else entirely. Property tax $7,000 a year, insurance $1,500, repairs $1,500, "
            "and $1,900 for capital expenditure.";
        expect(rental("0.9200", "0.0604"), untagged_text, mv::Outcome::Unsafe,
               mv::ReasonCode::UngroundedValue,
               "a bare 8% does NOT become 0.92 -- the lexer tag is the evidence");
    }

    std::printf("\n29. a TAX rate is not an INTEREST rate, and the band that "
                "refused 31%% of the corpus\n");
    {
        // `annual_tax_rate` ends in "rate", so it fell to SlotKind::Rate and
        // was judged against the 30% MORTGAGE-INTEREST band. Measured on the
        // training corpus: 205 of 659 ComputeDetailedAmortization rows carry a
        // tax rate above 0.30 -- 31.1% -- every one a correct parse with a
        // label the utterance states outright, refused on bounds alone. The
        // top US federal bracket is 37%, so this is ordinary, not exotic.
        const std::string text =
            "Give me the detailed amortization on a $420,000 loan at 6.25% over 30 years, "
            "I'm in the 34% tax bracket and I put $84,000 down on a $504,000 house.";

        const auto detailed = [](std::string_view tax) {
            return params("ComputeDetailedAmortization",
                          {{"loan_amount", "420000.00"}, {"annual_rate", "0.0625"},
                           {"term_months", "360"}, {"monthly_overpayment", "0.00"},
                           {"pmi_annual_rate", "0.0000"},
                           {"original_home_value", "504000.00"},
                           {"annual_tax_rate", std::string{tax}},
                           // The carrying costs came out of
                           // kOperationExcludedFields when v19 was promoted, so
                           // G2b now REQUIRES them here as it does every other
                           // declared field. They are the convention zeros
                           // because this utterance says nothing about upkeep,
                           // which is exactly what the model emits for it --
                           // measured 27/27 on the v19 holdout. Omitting them
                           // is no longer a valid parse, and the three checks
                           // below failed with
                           // `"annual_repairs" ... was not emitted` until this
                           // fixture caught up.
                           {"annual_repairs", "0.00"},
                           {"annual_insurance", "0.00"},
                           {"annual_cost_growth", "0.0000"}, {"monthly_hoa", "0.00"}});
        };

        expect_pass(detailed("0.3400"),
                    text, "a 34% marginal tax rate is admitted");

        // AND THE BAND STILL BINDS WHERE IT WAS WRITTEN FOR. Widening the rate
        // band instead of reclassifying the field would have made a 34%
        // MORTGAGE admissible as a side effect -- the verifier-looser-than-the
        // -engine failure this file records elsewhere.
        const std::string usury =
            "Give me the detailed amortization on a $420,000 loan at 34% over 30 years, "
            "I'm in the 22% tax bracket and I put $84,000 down on a $504,000 house.";
        expect(params("ComputeDetailedAmortization",
                      {{"loan_amount", "420000.00"}, {"annual_rate", "0.3400"},
                       {"term_months", "360"}, {"monthly_overpayment", "0.00"},
                       {"pmi_annual_rate", "0.0000"},
                       {"original_home_value", "504000.00"},
                       {"annual_tax_rate", "0.2200"},
                       // Required since the carrying costs left
                       // kOperationExcludedFields. They must be present for
                       // this check to reach the BOUNDS gate at all -- without
                       // them it refuses on MissingField, which would pass a
                       // sloppier assertion for entirely the wrong reason.
                       {"annual_repairs", "0.00"},
                       {"annual_insurance", "0.00"},
                       {"annual_cost_growth", "0.0000"}, {"monthly_hoa", "0.00"}}),
               usury, mv::Outcome::Unsafe, mv::ReasonCode::OutOfRange,
               "while a 34% MORTGAGE is still refused -- the field was "
               "reclassified, the interest band was not widened");

        // A marginal rate above 100% is not a bracket, it is a typo. Capped at
        // 1.0 rather than left on the general 1.5 ratio ceiling, which exists
        // for an LTV on an underwater loan and has no analogue here.
        const std::string absurd =
            "Give me the detailed amortization on a $420,000 loan at 6.25% over 30 years, "
            "I'm in the 140% tax bracket and I put $84,000 down on a $504,000 house.";
        expect(detailed("1.4000"), absurd, mv::Outcome::Unsafe,
               mv::ReasonCode::OutOfRange,
               "and a tax rate above 100% is refused, grounded or not");
    }

    std::printf("\n30. the rent-vs-buy owner costs: classified now, speakable later\n");
    {
        // Declared in the label space and excluded from what the model must
        // emit. Gate 0 already proves they CLASSIFY; this proves they classify
        // to the RIGHT slot, which is the half that decides whether a value is
        // bounded as money, as a rate, or as a count of years.
        struct Expect { const char* field; mv::SlotKind kind; const char* why; };
        const std::array<Expect, 6> kExpected{{
            {"annual_repairs", mv::SlotKind::Money, "a yearly budget in currency"},
            {"pmi_annual_rate", mv::SlotKind::Rate, "a rate of insurance on the loan"},
            {"monthly_overpayment", mv::SlotKind::Money, "extra principal, in currency"},
            {"heloc_drawn_amount", mv::SlotKind::Money, "a sum drawn, in currency"},
            {"heloc_annual_rate", mv::SlotKind::Rate, "interest on the draw"},
            {"heloc_term_years", mv::SlotKind::YearCount, "a repayment term in years"},
        }};
        for (const auto& e : kExpected) {
            check(mv::classify_slot(e.field) == e.kind,
                  std::string{e.field} + " is " + e.why);
        }

        // The service DROPS them before the verifier, so nothing the deployed
        // model emits in these slots can reach grounding. Asserted directly,
        // because it is what protects a model that was never taught them.
        for (const auto& e : kExpected) {
            check(mv::operation_excludes_field("ComputeRentVsBuy", e.field),
                  std::string{"ComputeRentVsBuy drops "} + e.field +
                      " until a retrain proves the model emits it");
        }

        // AND THE EXCLUSION IS SCOPED. `annual_repairs` is a REQUIRED field of
        // ComputeRentalCashFlow, where the model was taught it and grounds it
        // today -- excluding it there would silently drop a stated repair
        // budget from the investor model.
        check(!mv::operation_excludes_field("ComputeRentalCashFlow", "annual_repairs"),
              "but ComputeRentalCashFlow still requires annual_repairs -- the "
              "exclusion is per operation, not per field name");
    }

    // =======================================================================
    section("An UNDISCOUNTED payback: the zero rate one other field licenses");
    // =======================================================================
    // Measured against production on 2026-09-16, with the model's output
    // BYTE-IDENTICAL to the gold:
    //
    //   "I'm putting $35,300 into equipment for my rental that saves me about
    //    $3,900 a year. how many years until it pays for itself?"
    //   -> "rate" = 0 does not correspond to anything in the request
    //      (the nearest figure you gave is 3900)
    //
    // 17 of the 28 ComputePaybackPeriod rows in the holdout -- every
    // undiscounted one. The 11 that passed are the 11 stating a discount rate.
    //
    // THIS IS THE FAILURE CLASS NO ACCURACY METRIC CAN SEE. `raw_exact` counts
    // all 17 as correct, because the model IS correct; the user gets a refusal
    // anyway. It only shows up by pairing the raw verdict against the served
    // one, which is how it was found.
    //
    // `discounted` and `rate` are meaningful only together -- with discounting
    // off the engine never reads the rate -- so the corpus teaches the
    // convention zero. The licence is the model's own `discounted: false`, not
    // the field and not the magnitude, and all three directions are asserted
    // because an exemption that cannot be shown to be SCOPED is a hole.
    {
        const std::string undiscounted =
            "I'm putting $35,300 into equipment for my rental that saves me about "
            "$3,900 a year. how many years until it pays for itself?";

        auto ok = with_list(params("ComputePaybackPeriod", {{"discounted", "false"},
                                                           {"rate", "0.0"}}),
                            "values", {"-35300", "3900", "3900", "3900", "3900"});
        auto v1 = mv::ground_emitted_values(ok, undiscounted);
        check(v1.outcome == mv::Outcome::Proven,
              "an undiscounted payback's rate = 0 GROUNDS: " + v1.message);

        // Scope 1 -- `discounted: true` withdraws the licence. This is the case
        // that matters: a zero rate there silently drops the discounting the
        // user asked for, and the answer is a different number.
        auto lying = with_list(params("ComputePaybackPeriod", {{"discounted", "true"},
                                                              {"rate", "0.0"}}),
                               "values", {"-35300", "3900", "3900"});
        check(mv::ground_emitted_values(lying, undiscounted).outcome != mv::Outcome::Proven,
              "... but a DISCOUNTED payback with a zero rate is still refused");

        // Scope 2 -- another operation is untouched. kConventionValues is
        // global (field, value), so a naive {"rate", "0"} entry would have
        // exempted this too, and a fabricated zero prices a 0% loan.
        auto tvm = params("ComputePayment", {{"rate", "0.0"},
                                             {"periods", "360"},
                                             {"present_value", "300000.00"},
                                             {"future_value", "0.00"},
                                             {"timing", "END_OF_PERIOD"}});
        check(mv::ground_emitted_values(
                  tvm, "$300,000 at 6.5% over 30 years -- what's the payment?")
                      .outcome != mv::Outcome::Proven,
              "... and ComputePayment's rate = 0 is STILL refused against a stated 6.5%");

        // Scope 3 -- a NON-zero rate on the same operation is judged normally,
        // so the licence did not turn `rate` into an ungrounded field.
        auto invented = with_list(params("ComputePaybackPeriod", {{"discounted", "false"},
                                                                  {"rate", "0.0725"}}),
                                  "values", {"-35300", "3900", "3900"});
        check(mv::ground_emitted_values(invented, undiscounted).outcome != mv::Outcome::Proven,
              "... and an INVENTED 7.25% on the same request is refused as before");

        // The admit direction on a stated rate, so the whole operation is not
        // simply being waved through.
        const std::string discounted_text =
            "$14,500 out of pocket for new windows that saves me about $1,400 a year. "
            "Using a 3.14% discount rate, how many years until it pays for itself?";
        auto real = with_list(params("ComputePaybackPeriod", {{"discounted", "true"},
                                                              {"rate", "0.0314"}}),
                              "values", {"-14500", "1400", "1400"});
        check(mv::ground_emitted_values(real, discounted_text).outcome == mv::Outcome::Proven,
              "a STATED discount rate still grounds on its own merits");
    }

    // =======================================================================
    section("An em dash is not a minus sign");
    // =======================================================================
    // Found by GroundingCorpusSweepTest the day it was written: 12 of 500
    // generated rows refused against their OWN GOLD, every one of them this.
    //
    //   "Break out the deductible interest -- 35.55% bracket."
    //   -> lexed -35.55, so M2 offered -0.3555
    //   -> "annual_tax_rate" = 0.3555 does not correspond to anything in the
    //      request (the nearest figure you gave is 0.86)
    //
    // Three operations and both literal tags -- percent on
    // ComputeDetailedAmortization and ComputeClosingCosts, money on
    // ComputeAmortizationBatch -- which is what makes it one lexer defect
    // rather than three generator ones.
    //
    // BOTH DIRECTIONS, because the fix narrows a guard that exists for a
    // reason. Carrying a real minus is what stops "-$250,000" and "$250,000"
    // being the same literal; a model that drops the sign would otherwise be
    // grounded against the digits and returned Proven, pricing the opposite of
    // what was asked.
    {
        const std::string dash =
            "Amortization schedule for a $440,100 loan at 4.54% over 30-year, paying an "
            "extra $500/month. Home is worth $487,200, PMI runs 0.86% a year. Break out "
            "the deductible interest -- 35.55% bracket.";
        auto lits = mv::lex_numeric_literals(dash);
        bool saw_negative = false;
        bool saw_positive_bracket = false;
        for (const auto& l : lits) {
            if (l.value.is_negative()) { saw_negative = true; }
            if (l.value.to_string().rfind("35.55", 0) == 0) { saw_positive_bracket = true; }
        }
        check(!saw_negative, "an em dash before a figure produces NO negative literal");
        check(saw_positive_bracket, "the 35.55% bracket is lexed as a POSITIVE 35.55");

        auto ok = params("ComputeDetailedAmortization",
                         {{"loan_amount", "440100.00"},
                          {"annual_rate", "0.0454"},
                          {"term_months", "360"},
                          {"monthly_overpayment", "500.00"},
                          {"pmi_annual_rate", "0.0086"},
                          {"original_home_value", "487200.00"},
                          {"annual_tax_rate", "0.3555"}});
        auto v = mv::ground_emitted_values(ok, dash);
        check(v.outcome == mv::Outcome::Proven,
              "and the row grounds against its own utterance: " + v.message);

        // The guard the fix must NOT have removed: a single leading hyphen is
        // still a sign, on money and with the '$' between it and the digits.
        for (const auto& [text, why] : std::vector<std::pair<std::string, std::string>>{
                 {"a -$250,000 position", "-$250,000 keeps its sign"},
                 {"a -250000 position", "-250000 keeps its sign"}}) {
            bool negative = false;
            for (const auto& l : mv::lex_numeric_literals(text)) {
                if (l.value.is_negative()) { negative = true; }
            }
            check(negative, why);
        }

        // And the em dash does not swallow a genuinely negative figure that
        // follows it with its own sign attached.
        bool both = false;
        for (const auto& l : mv::lex_numeric_literals("cash flows -- -1000 then 5200")) {
            if (l.value.is_negative()) { both = true; }
        }
        check(both, "an em dash followed by an explicitly signed -1000 still lexes negative");
    }


    // =======================================================================
    section("A leading-dot decimal is a FRACTION, not the digits after the dot");
    // =======================================================================
    // The sibling of the em dash above, and worse in the same way: the lexer
    // did not MISS these literals, it reported a DIFFERENT NUMBER.
    //
    //   ".5% per month"   -> lexed 5    -> five percent, not half a percent
    //   ".75% per month"  -> lexed 75   -> seventy-five percent
    //
    // `lex_numeric_literals` began a literal only at a digit, so the '.' was
    // skipped and the scan started after it. A real literal, in a real field,
    // satisfying every bound, with the rate wrong by one to two orders of
    // magnitude -- so nothing downstream can refuse it and only the user's own
    // utterance falsifies it. Typing a rate the way a spreadsheet does is not
    // exotic.
    //
    // FOUND FROM THE PYTHON SIDE. agent/train/encoder_corpus.py's `_NUM` had
    // the identical hole, and its format probe exposed it because
    // "'0.5%' -> '.5%' (leading dot)" is a TRAINED augmentation that scored
    // 100.00% row error on all three retrain seeds. A trained case failing
    // deterministically is a lexer or labelling bug, never a model weakness --
    // and it also meant that rewrite taught the model nothing while reading
    // like coverage.
    //
    // BOTH DIRECTIONS, because the fix widens an entry condition and a wider
    // entry can invent literals that are not there.
    {
        // 1. the value is the FRACTION, and it is tagged a percent
        for (const auto& [text, want, why] :
             std::vector<std::tuple<std::string, std::string, std::string>>{
                 {"a monthly rate of .5% on the balance", "0.5", ".5% lexes as 0.5, not 5"},
                 {"a monthly rate of .75% on the balance", "0.75", ".75% lexes as 0.75, not 75"},
                 {"interest of .125% per month", "0.125", ".125% lexes as 0.125, not 125"}}) {
            const auto lits = mv::lex_numeric_literals(text);
            bool saw_fraction = false;
            bool saw_integer = false;
            bool tagged_percent = false;
            for (const auto& l : lits) {
                const std::string v = l.value.to_string();
                if (v.rfind(want, 0) == 0) {
                    saw_fraction = true;
                    tagged_percent = l.tag == mv::LiteralTag::Percent;
                }
                // the defect's own signature: the digits after the dot as a whole number
                if (v.rfind(want.substr(2), 0) == 0 && v.find('.') == want.substr(2).size()) {
                    saw_integer = true;
                }
            }
            check(saw_fraction, why);
            check(!saw_integer, "and the whole-number reading is GONE: " + text);
            check(tagged_percent, "and it still carries LiteralTag::Percent: " + text);
        }

        // 2. IT GROUNDS. The point of the fix is not the lexer in isolation: a
        // model that correctly emits 0.005 for ".5% per month" was REFUSED
        // before, because 0.005 is not derivable from a literal of 5.
        {
            // The PER-PERIOD rate field, because M3 maps annual -> per-period and there
            // is no monthly -> annual map to carry 0.5%/month up to 6%/year. A first
            // version of this check asserted that map and failed, which is the check
            // being wrong about the gate rather than the gate being wrong.
            const std::string utt =
                "What is the payment on a $300,000 loan at .5% per month over 360 months?";
            auto ok = params("ComputePayment", {{"rate", "0.005"},
                                                {"periods", "360"},
                                                {"present_value", "300000.00"}});
            auto v = mv::ground_emitted_values(ok, utt);
            check(v.outcome == mv::Outcome::Proven,
                  "a per-period 0.005 grounds against a stated .5%: " + v.message);
        }

        // 3. THE GUARDS THE FIX MUST NOT HAVE REMOVED. A '.' that is not the
        // start of a number must still begin nothing, or the lexer invents
        // literals -- which is the same class of defect in the other
        // direction.
        {
            // a decimal point already owned by the literal before it
            const auto money = mv::lex_numeric_literals("paid $495,000.00 in total");
            std::string seen;
            for (const auto& l : money) { seen += " " + l.value.to_string().substr(0, 14); }
            // The VALUE, not its spelling: BigDecimal::to_string normalises 495000.00 to
            // "495000", so an assertion pinning the trailing zeros fails on a correct
            // literal -- this file's own rule about not pinning a digit count.
            check(money.size() == 1 && money[0].value.to_string().rfind("495000", 0) == 0,
                  "$495,000.00 is still ONE literal, not 495,000 plus 0.00 (got" + seen + ")");

            // a dotted triple must not grow a third literal out of the second dot
            const auto triple = mv::lex_numeric_literals("release 3.5.2 shipped");
            bool invented = false;
            for (const auto& l : triple) {
                if (l.value.to_string().rfind("0.2", 0) == 0) { invented = true; }
            }
            check(!invented, "3.5.2 does not invent a 0.2 out of the second dot");

            // a sentence-ending period followed by a space and a digit is not a decimal
            const auto sentence = mv::lex_numeric_literals("costs 5. 25 years remain");
            bool joined = false;
            for (const auto& l : sentence) {
                if (l.value.to_string().rfind("0.25", 0) == 0) { joined = true; }
            }
            check(!joined, "'5. 25' does not become 0.25 (a space is not part of a decimal)");

            // and an ordinary decimal is untouched
            const auto plain = mv::lex_numeric_literals("a rate of 0.5% per month");
            bool half = false;
            for (const auto& l : plain) {
                if (l.value.to_string().rfind("0.5", 0) == 0) { half = true; }
            }
            check(half, "0.5% is unchanged by the fix");
        }
    }

    // =======================================================================
    section("A field the user never stated is a QUESTION, not a refusal");
    // =======================================================================
    // v15 asked a clarifying question on 15 of 90 holdout rows that call for
    // one; v17 on 1 and v18 on 0. The capability is gone from the weights and
    // only a retrain brings it back -- but the serving layer never needed it,
    // because it already knows WHICH field is missing: it just refused on that
    // field. Measured against production before this existed:
    //
    //   "What's the payment on a $420,000 loan at 6.5%?"
    //   -> "periods" = 180 does not correspond to anything in the request
    //      (the nearest figure you gave is 6.5)
    //
    // where the corpus teaches "Over how many years?" on exactly this shape.
    //
    // THE DISCRIMINATOR IS THE LEXER'S TAGS, and it has to be, because the
    // opposite case must keep its refusal: a value that contradicts something
    // the user DID say is the documented dangerous failure, and a question
    // would hide it. Both directions are asserted, and the second is the one
    // that matters.
    {
        // --- ASK: nothing in the utterance could be a term. ---
        const std::string no_term = "What's the payment on a $420,000 loan at 6.5%?";
        check(mv::utterance_states_nothing_for("periods", no_term),
              "no Years/Months literal -> `periods` was never stated");
        auto invented = params("ComputePayment", {{"rate", "0.005417"},
                                                  {"periods", "180"},
                                                  {"present_value", "420000.00"},
                                                  {"future_value", "0.00"},
                                                  {"timing", "END_OF_PERIOD"}});
        auto v = mv::verify_mortgage_output(invented, no_term);
        check(v.reason == mv::ReasonCode::UnstatedField,
              "... so the verdict is UnstatedField, not UngroundedValue");
        check(v.field == "periods", "... naming the field that was missing");
        check(mv::clarifying_question("ComputePayment", "periods") == "Over how many years?",
              "... and the question is the corpus's own wording");

        // --- ASK: an absent RATE arrives as a BOUNDS failure, not a grounding
        // one, on an utterance of the same shape. Both paths are refined or
        // half the cases keep a message the user cannot act on.
        const std::string no_rate =
            "Show me the amortization schedule for a $350,000 loan over 30 years.";
        check(mv::utterance_states_nothing_for("annual_rate", no_rate),
              "a Years-tagged 30 is NOT the user stating an interest rate");
        auto bad_rate = params("ComputeAmortization", {{"loan_amount", "350000.00"},
                                                       {"annual_rate", "0.6000"},
                                                       {"term_months", "360"},
                                                       {"monthly_overpayment", "0.00"},
                                                       {"pmi_annual_rate", "0.0000"},
                                                       {"original_home_value", "350000.00"},
                                                       {"annual_repairs", "0.00"},
                                                       {"annual_insurance", "0.00"},
                                                       {"annual_cost_growth", "0.0000"}, {"monthly_hoa", "0.00"}});
        auto v2 = mv::verify_mortgage_output(bad_rate, no_rate);
        check(v2.reason == mv::ReasonCode::UnstatedField,
              "an out-of-range rate on an utterance with no percent is UnstatedField too");

        // --- REFUSE: the documented dangerous failure, unchanged. -----------
        // 495,000 is Money-tagged and `present_value` is a Money slot, so the
        // user DID state something for this field and the model came back with
        // a different loan. A question here would hide a corrupted value.
        const std::string stated = "What's the payment on a $495,000 loan at 6% over 30 years?";
        check(!mv::utterance_states_nothing_for("present_value", stated),
              "a Money literal means `present_value` WAS stated");
        auto corrupted = params("ComputePayment", {{"rate", "0.005"},
                                                   {"periods", "360"},
                                                   {"present_value", "304000.00"},
                                                   {"future_value", "0.00"},
                                                   {"timing", "END_OF_PERIOD"}});
        auto v3 = mv::verify_mortgage_output(corrupted, stated);
        check(v3.outcome != mv::Outcome::Proven, "the corrupted value is still caught");
        check(v3.reason == mv::ReasonCode::UngroundedValue,
              "... and stays a REFUSAL -- it contradicts a figure the user gave");

        // --- REFUSE: a field with no natural question keeps its refusal. ----
        // A question assembled from a proto field name reads like a stack
        // trace, so the table returning empty is a decision, not a gap.
        check(mv::clarifying_question("ComputeDepreciation", "convention").empty(),
              "a field with no natural wording yields no question");

        // --- The UNTAGGED literal is permissive, so the ambiguous case falls
        // back to the refusal this narrowing exists to avoid widening.
        check(!mv::utterance_states_nothing_for("periods", "payment on 420000 at 6.5 over 30"),
              "a bare untagged number could be anything, so nothing is declared unstated");
    }

    // =======================================================================
    section("A TAG IS A KIND, NOT A FIELD: a claimed literal states nothing");
    // =======================================================================
    // `LiteralTag::Percent` says "this number is a percentage". It does not say
    // WHICH percentage -- and the section above consulted nothing else, so a
    // stated INTEREST RATE reported that the user had also spoken about every
    // other Percent slot in the operation. Measured on the 600-row holdout with
    // v20: of the 40 clarification rows that did not ask, 22 were exactly this,
    // in two families -- `max_ltv_rate` on ComputeHeloc (18) and `annual_rate`
    // on ComputeDetailedAmortization (5, one of them arriving as a BOUNDS
    // failure rather than a grounding one).
    //
    // The discriminator is whether the literal is SPOKEN FOR: it blocks the
    // question only while no OTHER emitted field grounds against it. Both
    // directions are asserted, and as in the section above the refusing one is
    // the one that matters.
    {
        // --- ASK: the only percentage is the rate, and the rate took it. ----
        // Production utterance and production emission, verbatim: the model
        // invented the conventional 80% cap, and the refusal that followed
        // ("max_ltv_rate = 0.80 does not correspond to anything in the request
        // (the nearest figure you gave is 9.6)") named a number against a
        // question the user was never asked.
        const std::string heloc =
            "My home is worth $1,047,400, I owe $576,500. I want to draw $46,900 "
            "from a HELOC at 9.6% over 20 years.";
        auto heloc_in = params("ComputeHeloc", {{"home_value", "1047400.00"},
                                                {"current_mortgage_balance", "576500.00"},
                                                {"max_ltv_rate", "0.80"},
                                                {"drawn_amount", "46900.00"},
                                                {"annual_rate", "0.0960"},
                                                {"repayment_term_years", "20"},
                                                {"payments_per_year", "12"}});

        check(!mv::utterance_states_nothing_for("max_ltv_rate", heloc),
              "text alone: the stated 9.6% looks like the user speaking about the LTV cap");
        check(mv::utterance_states_nothing_for("max_ltv_rate", heloc, heloc_in),
              "... but `annual_rate` already claimed it, so the cap was never stated");

        auto hv = mv::verify_mortgage_output(heloc_in, heloc);
        check(hv.reason == mv::ReasonCode::UnstatedField,
              "... and the whole gate now answers UnstatedField rather than refusing");
        check(hv.field == "max_ltv_rate", "... naming the cap");
        check(!mv::clarifying_question("ComputeHeloc", "max_ltv_rate").empty(),
              "... which has a natural wording, so a question is actually asked");

        // --- REFUSE: a SECOND percentage is left over, so one of them may well
        // be the cap the model got wrong. One unclaimed literal is enough.
        const std::string heloc_ltv =
            "My home is worth $1,047,400, I owe $576,500. I want to draw $46,900 "
            "from a HELOC at 9.6% over 20 years, capped at 85% LTV.";
        check(!mv::utterance_states_nothing_for("max_ltv_rate", heloc_ltv, heloc_in),
              "the stated 85% cap is claimed by nobody, so 0.80 contradicts the user");
        check(mv::verify_mortgage_output(heloc_in, heloc_ltv).reason ==
                  mv::ReasonCode::UngroundedValue,
              "... and that stays a REFUSAL");

        // --- REFUSE: the documented dangerous failure, now through the
        // claim-aware path. It is preserved BY CONSTRUCTION, not by exception:
        // `present_value` is the only Money slot on ComputePayment, so the
        // 495,000 grounds nothing else and is still available to be what the
        // user said.
        const std::string stated495 =
            "What's the payment on a $495,000 loan at 6% over 30 years?";
        auto corrupted495 = params("ComputePayment", {{"rate", "0.005"},
                                                      {"periods", "360"},
                                                      {"present_value", "304000.00"},
                                                      {"future_value", "0.00"},
                                                      {"timing", "END_OF_PERIOD"}});
        check(!mv::utterance_states_nothing_for("present_value", stated495, corrupted495),
              "495,000 is claimed by no other field, so it is still evidence");
        check(mv::verify_mortgage_output(corrupted495, stated495).reason ==
                  mv::ReasonCode::UngroundedValue,
              "... so pricing a different loan is still refused");

        // --- REFUSE: a CONVENTION value claims nothing. --------------------
        // `pmi_annual_rate: 0.0000` grounds against no literal by construction
        // (kConventionValues exempts it), so letting it consume the stated 6.5%
        // would silence a question about a rate the model really did mangle.
        // This is the sharpest mutation target in the section: drop the
        // is_convention_value guard in claimed_literals and this flips to
        // UnstatedField, asking "What's the interest rate?" about a rate the
        // user gave.
        const std::string rate_stated =
            "Amortize a $350,000 loan at 6.5% over 30 years.";
        auto mangled_rate = params("ComputeAmortization",
                                   {{"loan_amount", "350000.00"},
                                    {"annual_rate", "0.0000"},
                                    {"term_months", "360"},
                                    {"monthly_overpayment", "0.00"},
                                    {"pmi_annual_rate", "0.0000"},
                                    {"original_home_value", "350000.00"},
                                    {"annual_repairs", "0.00"},
                                    {"annual_insurance", "0.00"},
                                    {"annual_cost_growth", "0.0000"}, {"monthly_hoa", "0.00"}});
        check(!mv::utterance_states_nothing_for("annual_rate", rate_stated, mangled_rate),
              "a convention zero does not consume the 6.5% the user actually stated");
        check(mv::verify_mortgage_output(mangled_rate, rate_stated).reason ==
                  mv::ReasonCode::UngroundedValue,
              "... so a zeroed rate against a stated one is still a refusal");

        // --- ASK: the same shape with the percentage genuinely spent. -------
        // "26.77% tax bracket" is claimed by `annual_tax_rate`, and there is no
        // other percentage, so the interest rate really was never given.
        const std::string bracket_only =
            "Amortize $1,236,300 over 20-year. I'm in the 26.77% tax bracket.";
        auto no_rate_given = params("ComputeDetailedAmortization",
                                    {{"loan_amount", "1236300.00"},
                                     {"annual_rate", "0.0000"},
                                     {"term_months", "240"},
                                     {"monthly_overpayment", "0.00"},
                                     {"pmi_annual_rate", "0.0000"},
                                     {"original_home_value", "1236300.00"},
                                     {"annual_tax_rate", "0.2677"},
                                     {"annual_repairs", "0.00"},
                                     {"annual_insurance", "0.00"},
                                     {"annual_cost_growth", "0.0000"}, {"monthly_hoa", "0.00"}});
        check(mv::utterance_states_nothing_for("annual_rate", bracket_only, no_rate_given),
              "the tax bracket is spoken for, so the interest rate was never stated");
        check(mv::verify_mortgage_output(no_rate_given, bracket_only).reason ==
                  mv::ReasonCode::UnstatedField,
              "... and the row asks instead of refusing");

        // --- The two-argument form is the three-argument one with no claims. -
        // Every assertion in the section above is therefore still describing
        // live behaviour: an empty input claims nothing, so every compatible
        // literal blocks exactly as it always did.
        check(mv::utterance_states_nothing_for("max_ltv_rate", heloc, mv::MortgageParamsInput{}) ==
                  mv::utterance_states_nothing_for("max_ltv_rate", heloc),
              "no emitted fields means no claims means the original behaviour");

        // --- A MIS-ASSIGNED literal is claimed, and only the missing wording
        // stops a wrong question. Stated rather than hidden, because it is the
        // known limit of reading claims out of a per-field gate.
        //
        // Production row: the model put the $1,400 MORTGAGE PAYMENT into
        // operating expenses and zeroed the payment. The 1400 is duly claimed
        // -- by the wrong field -- so the claim rule would call the payment
        // unstated. What keeps the refusal is that `periodic_mortgage_payment`
        // has no natural wording, and that is the reason not to give it one:
        // the question would ask for a figure the user had just supplied.
        const std::string swapped_utt =
            "Rental worth $524,800, $125,600 cash invested, rent $1,900/month, "
            "mortgage payment $1,400/month. What's my return?";
        auto swapped = params("ComputeRentalRoi", {{"property_value", "524800.00"},
                                                   {"total_cash_invested", "125600.00"},
                                                   {"periodic_gross_rent", "1900.00"},
                                                   {"periodic_operating_expenses", "1400.00"},
                                                   {"periodic_mortgage_payment", "0.00"},
                                                   {"periods_per_year", "12"}});
        check(mv::clarifying_question("ComputeRentalRoi", "periodic_mortgage_payment").empty(),
              "there is deliberately no wording for a payment the user already gave");
        check(mv::verify_mortgage_output(swapped, swapped_utt).reason ==
                  mv::ReasonCode::UngroundedValue,
              "... so a swapped pair keeps its refusal rather than asking");

        // --- REFUSE: a field of the SAME KIND does not speak for the literal.
        // `loan_amount` and `original_home_value` are both Money and on a
        // full-price loan they are the SAME amount, so letting one consume the
        // 467,500 made a mangled `loan_amount` look unstated -- "How much is
        // the loan or the property worth?", asked of someone who had just said
        // it. GroundingCorpusSweepTest's ask sweep found 114 rows of this, and
        // it is the reason a claim is only counted ACROSS kinds.
        const std::string amort =
            "Amortize $467,500 at 5.96% over 15-year. I'm in the 22.46% tax bracket.";
        auto same_kind = params("ComputeDetailedAmortization",
                                {{"loan_amount", "640475.00"},
                                 {"annual_rate", "0.0596"},
                                 {"term_months", "180"},
                                 {"monthly_overpayment", "0.00"},
                                 {"pmi_annual_rate", "0.0000"},
                                 {"original_home_value", "467500.00"},
                                 {"annual_tax_rate", "0.2246"},
                                 {"annual_repairs", "0.00"},
                                 {"annual_insurance", "0.00"},
                                 {"annual_cost_growth", "0.0000"}, {"monthly_hoa", "0.00"}});
        check(!mv::utterance_states_nothing_for("loan_amount", amort, same_kind),
              "another MONEY field holding the same figure does not consume it");
        check(mv::verify_mortgage_output(same_kind, amort).reason ==
                  mv::ReasonCode::UngroundedValue,
              "... so a mangled loan amount is refused, not asked about");

        // And the cross-kind case on the SAME utterance still asks, which is
        // what makes the rule a discriminator rather than a switch: the tax
        // RATIO took the 22.46% and the interest RATE is genuinely absent.
        auto cross_kind = params("ComputeDetailedAmortization",
                                 {{"loan_amount", "467500.00"},
                                  {"annual_rate", "0.0000"},
                                  {"term_months", "180"},
                                  {"monthly_overpayment", "0.00"},
                                  {"pmi_annual_rate", "0.0000"},
                                  {"original_home_value", "467500.00"},
                                  {"annual_tax_rate", "0.2246"},
                                  {"annual_repairs", "0.00"},
                                  {"annual_insurance", "0.00"},
                                  {"annual_cost_growth", "0.0000"}, {"monthly_hoa", "0.00"}});
        const std::string no_rate_amort =
            "Amortize $467,500 over 15-year. I'm in the 22.46% tax bracket.";
        check(mv::verify_mortgage_output(cross_kind, no_rate_amort).reason ==
                  mv::ReasonCode::UnstatedField,
              "a Ratio claiming the only percentage still leaves the Rate unstated");
    }

    // -----------------------------------------------------------------------
    // A repair budget is not an overpayment on the loan.
    //
    // Measured against production 2026-09-16, on a phrasing the corpus itself
    // teaches: "...Budget $3,600 a year for repairs and $1,700 for insurance."
    // came back with monthly_overpayment = 3600.00 and annual_repairs =
    // 1700.00 -- the repair budget paying down the mortgage and the insurance
    // figure counted twice. Every number is in the utterance, so grounding
    // admitted all of them: it is per FIELD, and nothing asked WHICH number
    // belongs where. That is the documented 20%-down defect in money rather
    // than percent, and it prices a loan nobody asked for.
    // -----------------------------------------------------------------------
    {
        section("29. the words beside a money literal decide which cost it is");

        const std::string upkeep =
            "Amortization schedule for a $420,000 loan at 6.25% over 30 years. "
            "Budget $3,600 a year for repairs and $1,700 for insurance.";

        const auto lits = mv::lex_numeric_literals(upkeep);
        int repairs_tagged = 0;
        int insurance_tagged = 0;
        bool loan_tagged = false;
        for (const auto& l : lits) {
            if (!l.names_upkeep) continue;
            // The LOAN must not be swept up by the adjacency scan -- it is in a
            // different sentence, which is what the boundary guard is for.
            if (l.value.units() == mv::parse_strict_decimal("420000")->units()) {
                loan_tagged = true;
            }
            if (l.names_insurance) ++insurance_tagged; else ++repairs_tagged;
        }
        check(repairs_tagged == 1, "the repairs figure is tagged upkeep, not insurance");
        check(insurance_tagged == 1, "the insurance figure is tagged insurance");
        check(!loan_tagged, "the LOAN is not tagged upkeep -- it is a sentence away");

        // The subtractive rule itself: an upkeep figure offers NO candidate for
        // the overpayment slot, so the parse that put it there is refused
        // rather than served.
        expect(params("ComputeAmortization",
                      {{"loan_amount", "420000.00"}, {"annual_rate", "0.0625"},
                       {"term_months", "360"}, {"monthly_overpayment", "3600.00"},
                       {"pmi_annual_rate", "0.0000"},
                       {"original_home_value", "420000.00"},
                       {"annual_repairs", "1700.00"}, {"annual_insurance", "1700.00"},
                       {"annual_cost_growth", "0.0000"}, {"monthly_hoa", "0.00"}}),
               upkeep, mv::Outcome::Unsafe, mv::ReasonCode::UngroundedValue,
               "a repair budget cannot ground monthly_overpayment");

        // THE OPPOSITE DIRECTION, and it is the one that makes the rule safe to
        // ship. "an extra $250/month" stated in the SAME utterance as a repair
        // budget is a real overpayment and must still ground -- the adjacency
        // scan stops at the full stop, so the "Budget" two words after the 250
        // does not reach it. Without this the fix would break the very
        // phrasing that motivated it.
        const std::string both =
            "Amortization schedule for a $420,000 loan at 6.25% over 30 years, "
            "paying an extra $250 a month. Budget $3,600 a year for repairs and "
            "$1,700 for insurance.";
        expect_pass(params("ComputeAmortization",
                           {{"loan_amount", "420000.00"}, {"annual_rate", "0.0625"},
                            {"term_months", "360"}, {"monthly_overpayment", "250.00"},
                            {"pmi_annual_rate", "0.0000"},
                            {"original_home_value", "420000.00"},
                            {"annual_repairs", "3600.00"}, {"annual_insurance", "1700.00"},
                            {"annual_cost_growth", "0.0000"}, {"monthly_hoa", "0.00"}}),
                    both, "a REAL overpayment beside a repair budget still grounds");

        // And the repairs figure still cannot take the overpayment slot even
        // when a genuine increment exists in the same sentence -- the rule is
        // about the WORDS beside the literal, not about which numbers are
        // present.
        expect(params("ComputeAmortization",
                      {{"loan_amount", "420000.00"}, {"annual_rate", "0.0625"},
                       {"term_months", "360"}, {"monthly_overpayment", "3600.00"},
                       {"pmi_annual_rate", "0.0000"},
                       {"original_home_value", "420000.00"},
                       {"annual_repairs", "0.00"}, {"annual_insurance", "1700.00"},
                       {"annual_cost_growth", "0.0000"}, {"monthly_hoa", "0.00"}}),
               both, mv::Outcome::Unsafe, mv::ReasonCode::UngroundedValue,
               "... while the repair budget still cannot become the overpayment");

        // An utterance with NO upkeep words is untouched: the rule must not
        // narrow the ordinary case.
        expect_pass(params("ComputeAmortization",
                           {{"loan_amount", "420000.00"}, {"annual_rate", "0.0625"},
                            {"term_months", "360"}, {"monthly_overpayment", "250.00"},
                            {"pmi_annual_rate", "0.0000"},
                            {"original_home_value", "420000.00"},
                            {"annual_repairs", "0.00"}, {"annual_insurance", "0.00"},
                            {"annual_cost_growth", "0.0000"}, {"monthly_hoa", "0.00"}}),
                    "Amortize $420,000 at 6.25% over 30 years, paying an extra $250 a month.",
                    "an utterance with no upkeep words is unaffected");
    }


    {
        // Section 32. AN HOA FEE IS A COST, NEVER PRINCIPAL -- and an unstated
        // down payment is ZERO.
        //
        // Reported from production 2026-10-04: "the mortgage AI assistant is
        // always explicitly wrong when I specify the HOA payment without
        // specifying the amount put down", and "even when i specify the hoa
        // cost is per month it somehow records the wrong number". Measured
        // against the live ingress with the partner key:
        //
        //   "amortize 500000 at 6% over 30 years with 275 per month HOA dues"
        //   -> ComputeAmortization{monthly_overpayment: 275, ...}   200 OK
        //
        // The HOA fee was billed as EXTRA PRINCIPAL. That retires the loan
        // years early and moves every figure in the schedule, and nothing in
        // the response says where the 275 came from -- the documented
        // dangerous failure, in a new field.
        //
        // THE RULE THAT SHOULD HAVE CAUGHT IT WAS ALREADY HERE, one word
        // short: section 31's upkeep rule removes a repairs figure from
        // exactly these fields and its word list had no HOA in it.
        const std::string hoa_prod =
            "amortize 500000 at 6% over 30 years with 275 per month HOA dues";

        {
            const auto lits = mv::lex_numeric_literals(hoa_prod);
            int hoa_tagged = 0;
            bool loan_tagged = false;
            for (const auto& l : lits) {
                if (!l.names_hoa) continue;
                ++hoa_tagged;
                if (l.value.units() == mv::parse_strict_decimal("500000")->units()) {
                    loan_tagged = true;
                }
            }
            check(hoa_tagged == 1, "the HOA fee is tagged names_hoa (three words before 'HOA')");
            check(!loan_tagged, "the LOAN is NOT tagged HOA -- the window does not reach it");
        }

        // The production reproduction, now refused instead of served.
        expect(params("ComputeAmortization",
                      {{"loan_amount", "500000.00"}, {"annual_rate", "0.0600"},
                       {"term_months", "360"}, {"monthly_overpayment", "275.00"},
                       {"pmi_annual_rate", "0.0000"},
                       {"original_home_value", "500000.00"},
                       {"annual_repairs", "0.00"}, {"annual_insurance", "0.00"},
                       {"annual_cost_growth", "0.0000"}, {"monthly_hoa", "0.00"}}),
               hoa_prod, mv::Outcome::Unsafe, mv::ReasonCode::UngroundedValue,
               "an HOA fee cannot ground monthly_overpayment (the production defect)");

        // NOR can it become repairs or insurance. Without this, routing the
        // literal away from the overpayment would simply bill it as upkeep --
        // a smaller wrong number, still with nothing saying where it came from.
        expect(params("ComputeAmortization",
                      {{"loan_amount", "500000.00"}, {"annual_rate", "0.0600"},
                       {"term_months", "360"}, {"monthly_overpayment", "0.00"},
                       {"pmi_annual_rate", "0.0000"},
                       {"original_home_value", "500000.00"},
                       {"annual_repairs", "275.00"}, {"annual_insurance", "0.00"},
                       {"annual_cost_growth", "0.0000"}, {"monthly_hoa", "0.00"}}),
               hoa_prod, mv::Outcome::Unsafe, mv::ReasonCode::UngroundedValue,
               "... and an HOA fee is not repairs either");

        // THE WORD BEFORE THE FIGURE, which is how people actually say it.
        {
            const auto lits = mv::lex_numeric_literals("HOA is 400 per month on a 700000 home");
            int tagged = 0;
            for (const auto& l : lits) {
                if (l.names_hoa && l.value.units() == mv::parse_strict_decimal("400")->units()) {
                    ++tagged;
                }
            }
            check(tagged == 1, "\"HOA is 400\" tags the 400 (the backward scan)");
        }

        // A BARE "fee" MUST NOT TAG, and this is the false-positive direction
        // that keeps the rule shippable. This file already carries
        // origination_fee_percent, other_lender_fees, appraisal_fee,
        // inspection_fee and recording_fees -- if "fee" tagged, a closing-cost
        // figure would be removed from the only slots it belongs in.
        {
            const auto lits = mv::lex_numeric_literals("appraisal fee of 650 and a 900 inspection fee");
            int tagged = 0;
            for (const auto& l : lits) { if (l.names_hoa) ++tagged; }
            check(tagged == 0, "a bare \"fee\" does NOT tag as HOA (closing costs stay groundable)");
        }

        // THE SENTENCE BOUNDARY, as section 31 needs it: an HOA mentioned in a
        // different sentence must not reach back and disarm a real overpayment.
        {
            const std::string two =
                "Amortize 500000 at 6% over 30 years paying an extra 250 a month. "
                "The HOA is 275 a month.";
            const auto lits = mv::lex_numeric_literals(two);
            bool inc_tagged = false;
            for (const auto& l : lits) {
                if (l.names_hoa && l.value.units() == mv::parse_strict_decimal("250")->units()) {
                    inc_tagged = true;
                }
            }
            check(!inc_tagged, "an HOA in the NEXT sentence does not tag the overpayment");
            expect_pass(params("ComputeAmortization",
                               {{"loan_amount", "500000.00"}, {"annual_rate", "0.0600"},
                                {"term_months", "360"}, {"monthly_overpayment", "250.00"},
                                {"pmi_annual_rate", "0.0000"},
                                {"original_home_value", "500000.00"},
                                {"annual_repairs", "0.00"}, {"annual_insurance", "0.00"},
                                {"annual_cost_growth", "0.0000"}, {"monthly_hoa", "0.00"}}),
                        two, "a REAL overpayment beside an HOA fee still grounds");
        }

        // AN UNSTATED DOWN PAYMENT IS ZERO. Owner decision 2026-10-04: "if a
        // downpayment is not stated, it should be assumed as 0" / "you should
        // not need to state a downpayment when it can be assumed as 0." Zero
        // is 100% financing, an ordinary real structure, and the arithmetic is
        // correct for it -- unlike a fabricated `rate = 0`, which prices a
        // different loan and is why that field is NOT exempt.
        //
        // This is also the field whose absence did the most damage: an
        // operation that REQUIRES it and cannot ground it pushes the model to
        // reach for whatever money literal is in the sentence, which is how a
        // stated HOA fee became a down payment.
        const std::string rental =
            "rental cash flow on a 450000 rental, closing costs 9000, 6.25% over 30 years, "
            "rents for 3100 a month, 95% occupancy, property tax 5400 a year, "
            "300 a month HOA, over 10 years";
        const std::vector<KV> rental_fields{
            {"property_price", "450000.00"},      {"down_payment", "0"},
            {"closing_costs", "9000.00"},         {"loan_annual_rate", "0.0625"},
            {"loan_term_years", "30"},            {"monthly_gross_rent", "3100.00"},
            {"annual_rent_increase", "0"},        {"occupancy_rate", "0.95"},
            {"annual_property_tax", "5400.00"},   {"annual_insurance", "0.00"},
            {"annual_repairs", "0.00"},           {"annual_capex_reserve", "0"},
            {"monthly_hoa", "300.00"},            {"management_fee_rate", "0"},
            {"annual_other_expenses", "0"},       {"annual_expense_increase", "0"},
            {"annual_appreciation", "0"},         {"selling_cost_percent", "0"},
            {"years", "10"},                      {"heloc_drawn_amount", "0"},
            {"heloc_annual_rate", "0"},           {"heloc_term_years", "0"}};
        expect_pass(params("ComputeRentalCashFlow", rental_fields), rental,
                    "down_payment = 0 grounds with NO down payment stated, and the HOA "
                    "fee still reaches monthly_hoa -- the slot it belongs in");

        // EXEMPT AT ZERO ONLY, which is what stops it becoming a licence to
        // invent. A fabricated 20%-of-price down payment is still refused.
        {
            auto invented = rental_fields;
            for (auto& kv : invented) {
                if (kv.first == "down_payment") kv.second = "90000.00";
            }
            expect(params("ComputeRentalCashFlow", invented), rental,
                   mv::Outcome::Unsafe, mv::ReasonCode::UngroundedValue,
                   "an INVENTED down payment is still refused -- the exemption is zero-only");
        }
    }

    // ------------------------------------------------------------------
    // Section 33. A WORDED NONE IS A STATEMENT.
    //
    // This section used to pin `optional_modelling_default` -- "an optional modelling input
    // nobody mentioned is a ZERO" -- and the pass that substituted it. That pass is GONE (owner
    // decision, 2026-10-06): an input the visitor did not state is now OMITTED, not filled with
    // a zero, so the site keeps its own value for it. What survives is the other half of the same
    // idea, which is still true and is what lets "no PMI" reach the engine as a zero: a visitor
    // who says there is none HAS said something, and omitting it would leave the form's own PMI
    // standing against their words. `shape_stated_params` serves a zero exactly when
    // `utterance_states_none_for` says it is worded.
    {
        section("33. a worded none is a statement; an unmentioned zero is not");

        // A WORDED ZERO IS A STATEMENT, NOT AN OMISSION.
        check(mv::utterance_states_none_for(
                  "down_payment", "480000 rental, zero down, 2400 a month rent"),
              "\"zero down\" states that there is no down payment");
        for (const auto* phr : {"zero down", "0 down", "nothing down", "no money down",
                                "without a down payment", "no deposit"}) {
            check(mv::utterance_states_none_for(
                      "down_payment", std::string{"a 480000 rental, "} + phr + ", 2400 rent"),
                  std::string{"a worded zero down payment: \""} + phr + "\"");
        }
        check(mv::utterance_states_none_for("monthly_hoa", "a 480000 condo with no HOA dues"),
              "\"no HOA dues\" states that there are none");
        check(mv::utterance_states_none_for("pmi_annual_rate",
                                            "amortize 500000 at 6% with no mortgage insurance"),
              "\"no mortgage insurance\" states that there is none");

        // AND IT MUST NOT FIRE ON A STATED FIGURE. A phrase rule over words as common as "no" and
        // "zero" would otherwise reach a sentence that states an amount.
        check(!mv::utterance_states_none_for(
                  "down_payment", "a 480000 rental with 96000 down, 2400 rent"),
              "a STATED down payment is not a worded zero");
        check(!mv::utterance_states_none_for(
                  "down_payment", "no more than 20% down on a 480000 rental"),
              "\"no more than 20% down\" states a CAP, not an absence -- the case a proximity "
              "rule over \"no\" would have got wrong");
        check(!mv::utterance_states_none_for("monthly_hoa", "275 a month in HOA dues"),
              "stated dues are not an absence");
        check(!mv::utterance_states_none_for("loan_amount", "no loan_amount here"),
              "a field with no worded-none rule is never reported as stated");
    }

    // =======================================================================
    // 34. A MISSPELLED ROLE WORD, which left a stated figure ungoverned and
    //     priced a $275 house against a $480,000 loan in PRODUCTION.
    //
    //   "amortize 480000 at 6.5% for 30 years with 275 monthly HAO duse"
    //   -> original_home_value = 275, monthly_hoa = 0           200 OK
    //
    // TWO INDEPENDENT HALVES, AND NEITHER MOVES THE ANSWER ALONE -- which is
    // why both arms below exist and why the mutation check names which half it
    // removed:
    //
    //   * the LEXER did not recognise `HAO` or `duse`, so `names_hoa` was
    //     false and the subtractive rule that exists for this never ran;
    //   * the rule's field list did not contain `original_home_value`, so even
    //     a correctly-spelled "HOA dues" would have left the 275 free to price
    //     the property. That half was LATENT in shipped code and no accuracy
    //     number could see it, because the model spells it right.
    // =======================================================================
    {
        const auto hoa_fields = [](const char* value_field, const char* value) {
            // Every declared field of ComputeAmortization, because G2b refuses
            // a missing key -- the figure under test is placed in ONE of them.
            auto in = params("ComputeAmortization", {{"loan_amount", "480000.00"},
                                                     {"annual_rate", "0.0650"},
                                                     {"term_months", "360"},
                                                     {"monthly_overpayment", "0.00"},
                                                     {"pmi_annual_rate", "0.0000"},
                                                     {"original_home_value", "480000.00"},
                                                     {"annual_repairs", "0.00"},
                                                     {"annual_insurance", "0.00"},
                                                     {"annual_cost_growth", "0.0000"},
                                                     {"monthly_hoa", "0.00"}});
            for (auto& f : in.fields) {
                if (f.name == value_field) { f.values = {std::string{value}}; }
            }
            return in;
        };
        const std::string typo =
            "amortize 480000 at 6.5% for 30 years with 275 monthly HAO duse";
        const std::string spelled =
            "amortize 480000 at 6.5% for 30 years with 275 monthly HOA dues";

        // THE PRODUCTION SYMPTOM, on the misspelling that produced it.
        {
            const auto v = mv::verify_mortgage_output(hoa_fields("original_home_value", "275.00"),
                                                      typo);
            check(v.outcome != mv::Outcome::Proven,
                  "a figure beside MISSPELLED dues may not price the property "
                  "(the production wrong answer)");
        }
        // The same, spelled correctly -- the latent half, which has nothing to
        // do with typing and was wrong the whole time.
        {
            const auto v = mv::verify_mortgage_output(hoa_fields("original_home_value", "275.00"),
                                                      spelled);
            check(v.outcome != mv::Outcome::Proven,
                  "a figure beside CORRECTLY-SPELLED dues may not price the "
                  "property either -- the half no accuracy number could see");
        }
        // AND THE ADMIT DIRECTION, which is the half a refuse-only test cannot
        // see: the rule must not keep dues out of the dues slot. A rule that
        // refused everything would pass both checks above.
        expect_pass(hoa_fields("monthly_hoa", "275.00"), spelled,
                    "stated dues still reach monthly_hoa (correct spelling)");
        expect_pass(hoa_fields("monthly_hoa", "275.00"), typo,
                    "stated dues still reach monthly_hoa THROUGH the typo");

        // --- the LEXER's reading of the same words, both directions ----------
        const auto hoa_flag = [](std::string_view text, std::string_view figure) {
            for (const auto& lit : mv::lex_numeric_literals(text)) {
                if (lit.text == figure) { return lit.names_hoa; }
            }
            return false;
        };
        check(hoa_flag("with 275 a month in HAO duse", "275"),
              "a misspelled dues word still tags the figure beside it as dues");
        check(hoa_flag("with 275 a month in HOA dues", "275"),
              "a correctly-spelled dues word tags it (regression)");

        // ONE SUBSTITUTION IS DELIBERATELY NOT A MATCH, and `how` is the word that forced it: at
        // three letters a Levenshtein-1 rule admits `how`, `hot` and `hoe`, and `how` opens every
        // second mortgage question. This is the over-matching direction, which has no alarm -- a
        // wrong match here silently removes a real figure from the principal slots.
        check(!hoa_flag("amortize 480000 at 6.5% for 275 how much is the payment", "275"),
              "\"how\" is not a misspelling of \"hoa\" -- a substitution is not a transposition");
        check(!hoa_flag("its a hot market, amortize 480000 at 6.5% over 275 hot", "275"),
              "\"hot\" is not a misspelling of \"hoa\"");
    }

    // =======================================================================
    section("ESSENTIAL FIELDS: the visitor's question is undefined without them");
    // =======================================================================
    // The owner's report (2026-10-06): the assistant zero-filled every field the visitor never
    // stated, asked for a TERM on a closing-costs question, and refused "600k home" as "left
    // out property_price". The contract it replaces required EVERY declared field (G2b). The
    // contract that replaces THAT is: only what the visitor stated is returned, and a question
    // is asked only when an ESSENTIAL input is missing -- one without which the calculation is
    // undefined. This table is the one place that says which.
    {
        const auto ops = mv::operation_ids();
        std::size_t without = 0;
        std::size_t not_declared = 0;
        for (const auto op : ops) {
            const auto reqs = mv::essential_requirements(op);
            if (reqs.empty()) {
                ++without;
                std::printf("    no essential requirement for %.*s\n", static_cast<int>(op.size()), op.data());
            }
            for (const auto& r : reqs) {
                for (const auto f : r.any_of) {
                    if (mv::find_field(op, f) == nullptr) {
                        ++not_declared;
                        std::printf("    %.*s lists essential field %.*s which it does not declare\n",
                                    static_cast<int>(op.size()), op.data(), static_cast<int>(f.size()), f.data());
                    }
                }
            }
        }
        check(!ops.empty() && without == 0,
              "EVERY operation has at least one essential requirement (a calculation with none "
              "would never ask, and would answer with whatever the model volunteered)");
        check(not_declared == 0, "every essential field is a field its operation DECLARES");

        // THE SWEEP THE OWNER ASKED FOR: no 'left out' refusal may remain reachable for an
        // essential field, so every one of them needs a natural question.
        std::size_t unworded = 0;
        for (const auto op : ops) {
            for (const std::string_view variant : {"", "STRAIGHT_LINE", "SUM_OF_YEARS_DIGITS",
                                                  "DECLINING_BALANCE", "MACRS"}) {
                for (const auto& r : mv::essential_requirements(op, variant)) {
                    if (r.any_of.empty() || mv::clarifying_question(op, r.any_of.front()).empty()) {
                        ++unworded;
                        std::printf("    no question for %.*s.%.*s\n", static_cast<int>(op.size()), op.data(),
                                    static_cast<int>(r.any_of.empty() ? 0 : r.any_of.front().size()),
                                    r.any_of.empty() ? "" : r.any_of.front().data());
                    }
                }
            }
        }
        check(unworded == 0,
              "EVERY essential requirement has a clarifying question, so an absent essential field "
              "can only ever become a question and never a \"left out\" refusal");
        check(mv::clarifying_question("ComputeClosingCosts", "home_price").find("year") == std::string::npos,
              "...and a closing-costs question is not about a TERM (the owner's first live defect)");
    }

    {
        // The two the owner named, by name.
        const auto closing = mv::essential_requirements("ComputeClosingCosts");
        check(closing.size() == 1 && closing[0].any_of.size() == 1 && closing[0].any_of[0] == "home_price",
              "closing costs need a PRICE and nothing else");
        const auto pay = mv::essential_requirements("ComputePayment");
        std::vector<std::string_view> pay_fields;
        for (const auto& r : pay) { for (const auto f : r.any_of) { pay_fields.push_back(f); } }
        std::ranges::sort(pay_fields);
        check((pay_fields == std::vector<std::string_view>{"periods", "present_value", "rate"}),
              "a payment needs principal, rate and term -- and not future_value or timing");
        // A lump-sum future value states no payment, and that is not an omission.
        const auto fv = mv::essential_requirements("ComputeFutureValue");
        const bool has_any_of = std::ranges::any_of(fv, [](const auto& r) { return r.any_of.size() == 2; });
        check(has_any_of, "a future value needs a payment OR a starting amount (a lump sum states no payment)");
        // Depreciation's essentials depend on the method.
        const auto sl = mv::essential_requirements("ComputeDepreciation", "STRAIGHT_LINE");
        const auto macrs = mv::essential_requirements("ComputeDepreciation", "MACRS");
        const auto has = [](const auto& reqs, std::string_view f) {
            return std::ranges::any_of(reqs, [&](const auto& r) { return std::ranges::find(r.any_of, f) != r.any_of.end(); });
        };
        check(has(sl, "life") && !has(sl, "recovery_period") && !has(sl, "period"),
              "straight-line depreciation needs cost and life, and not a period or a recovery period");
        check(has(macrs, "recovery_period") && has(macrs, "year") && !has(macrs, "life"),
              "MACRS needs cost, a recovery period and a year, and not a life");
    }

    // =======================================================================
    section("STATED-ONLY: a field is served only if the visitor's words support it");
    // =======================================================================
    {
        // THE OWNER'S SECOND LIVE DEFECT, verbatim: seven invented fields on "amortization
        // schedule for $350,000 at 6% over 30 years", and the home value set equal to the loan.
        const auto in = params("ComputeAmortization", {{"loan_amount", "350000"},
                                                       {"annual_rate", "0.06"},
                                                       {"term_months", "360"},
                                                       {"monthly_overpayment", "0"},
                                                       {"pmi_annual_rate", "0"},
                                                       {"original_home_value", "350000"},
                                                       {"annual_repairs", "0"},
                                                       {"annual_insurance", "0"},
                                                       {"annual_cost_growth", "0"},
                                                       {"monthly_hoa", "0"}});
        const auto sh = mv::shape_stated_params(in, "amortization schedule for $350,000 at 6% over 30 years");
        std::string kept;
        for (const auto& k : sh.kept) { kept += k + " "; }
        check(sh.missing_field.empty(), "complete request: nothing to ask (asked about '" + sh.missing_field + "')");
        check((sh.kept == std::vector<std::string>{"loan_amount", "annual_rate", "term_months"}),
              "ONLY the loan, the rate and the term are served -- no zero-filled HOA, repairs, "
              "insurance, growth, overpayment or PMI, and no home value invented from the loan (kept: " + kept + ")");
    }
    {
        // The same fields, each STATED: every one of them must survive.
        const auto in = params("ComputeAmortization", {{"loan_amount", "300000"},
                                                       {"annual_rate", "0.0625"},
                                                       {"term_months", "240"},
                                                       {"monthly_overpayment", "150"},
                                                       {"pmi_annual_rate", "0.008"},
                                                       {"original_home_value", "375000"},
                                                       {"annual_repairs", "3600"},
                                                       {"annual_insurance", "0"},
                                                       {"annual_cost_growth", "0"},
                                                       {"monthly_hoa", "275"}});
        const std::string text =
            "amortize $300,000 at 6.25% for 20 years on a $375,000 house with $150 extra a month, "
            "0.8% PMI, $3,600 a year for repairs and $275 a month HOA dues";
        const auto sh = mv::shape_stated_params(in, text);
        const auto kept = [&](std::string_view f) { return std::ranges::find(sh.kept, f) != sh.kept.end(); };
        check(kept("monthly_overpayment") && kept("pmi_annual_rate") && kept("original_home_value") &&
                  kept("annual_repairs") && kept("monthly_hoa"),
              "every field the visitor DID state is served");
        check(!kept("annual_insurance") && !kept("annual_cost_growth"),
              "...and the two they did not are not");
    }
    {
        // A STATED ZERO is a statement; an unstated zero is not.
        const auto in = params("ComputeAmortization", {{"loan_amount", "400000"}, {"annual_rate", "0.065"},
                                                       {"term_months", "360"}, {"pmi_annual_rate", "0"},
                                                       {"monthly_hoa", "0"}});
        const auto said_none =
            mv::shape_stated_params(in, "amortize 400000 at 6.5% for 30 years, no PMI and no HOA dues");
        check(std::ranges::find(said_none.kept, "pmi_annual_rate") != said_none.kept.end() &&
                  std::ranges::find(said_none.kept, "monthly_hoa") != said_none.kept.end(),
              "\"no PMI and no HOA dues\" is a STATEMENT, so the zeros are served (the form's own "
              "PMI would otherwise survive the visitor saying there is none)");
        const auto said_nothing = mv::shape_stated_params(in, "amortize 400000 at 6.5% for 30 years");
        check(std::ranges::find(said_nothing.kept, "pmi_annual_rate") == said_nothing.kept.end() &&
                  std::ranges::find(said_nothing.kept, "monthly_hoa") == said_nothing.kept.end(),
              "...and the same zeros, never mentioned, are not");
    }
    {
        // original_home_value: the loan literal is not a home value just because the model pointed
        // at it twice. Equal to the loan, it needs a property word beside the figure.
        const auto in = params("ComputeAmortization", {{"loan_amount", "350000"}, {"annual_rate", "0.06"},
                                                       {"term_months", "360"}, {"original_home_value", "350000"}});
        const auto bare = mv::shape_stated_params(in, "amortize 350000 at 6% for 30 years");
        check(std::ranges::find(bare.kept, "original_home_value") == bare.kept.end(),
              "a home value equal to the loan with no property word is the loan counted twice: dropped");
        const auto house = mv::shape_stated_params(
            in, "amortize 350000 at 6% for 30 years on a $350,000 house");
        check(std::ranges::find(house.kept, "original_home_value") != house.kept.end(),
              "...but a figure the visitor calls a HOUSE is a home value");
        const auto home_loan = mv::shape_stated_params(
            in, "I want a $350,000 home loan at 6% for 30 years, amortize it");
        check(std::ranges::find(home_loan.kept, "original_home_value") == home_loan.kept.end(),
              "...and \"home loan\" is a loan, not a home value");
    }
    {
        // Cadence and enum conventions are not statements either.
        const auto in = params("ComputePayment", {{"rate", "0.005416666666666"}, {"periods", "360"},
                                                  {"present_value", "400000"}, {"future_value", "0"},
                                                  {"timing", "END_OF_PERIOD"}});
        const auto sh = mv::shape_stated_params(in, "What's my monthly payment on a $400,000 loan at 6.5% for 30 years?");
        check((sh.kept == std::vector<std::string>{"rate", "periods", "present_value"}),
              "ComputePayment serves its three stated fields and not future_value or timing");
        const auto heloc = params("ComputeHeloc", {{"home_value", "500000"}, {"current_mortgage_balance", "300000"},
                                                   {"max_ltv_rate", "0.80"}, {"drawn_amount", "0"},
                                                   {"annual_rate", "0"}, {"repayment_term_years", "0"},
                                                   {"payments_per_year", "12"}});
        const auto hs = mv::shape_stated_params(heloc, "How much HELOC could I get on a $500,000 home with a $300,000 mortgage?");
        check((hs.kept == std::vector<std::string>{"home_value", "current_mortgage_balance"}) && hs.missing_field.empty(),
              "a HELOC question states a home and a balance: the LTV cap, rate, term and cadence are not invented");
        const auto biweekly = params("ComputePayoffTiming", {{"current_loan_balance", "250000"}, {"annual_rate", "0.065"},
                                                             {"current_monthly_payment", "1900"},
                                                             {"extra_monthly_payment", "0"}, {"payments_per_year", "26"}});
        const auto bs = mv::shape_stated_params(biweekly, "I owe $250,000 at 6.5% and pay $1,900 every two weeks, biweekly");
        check(std::ranges::find(bs.kept, "payments_per_year") != bs.kept.end(),
              "a NON-monthly cadence the visitor names is served");
    }
    {
        // An essential field the model put a convention zero in is MISSING, not stated.
        const auto in = params("ComputeAmortization", {{"loan_amount", "0"}, {"annual_rate", "0.065"},
                                                       {"term_months", "360"}});
        const auto sh = mv::shape_stated_params(in, "show me the amortization schedule at 6.5% for 30 years");
        check(sh.missing_field == "loan_amount",
              "a zero loan nobody stated is an ABSENT loan: the visitor is asked, never served a $0 mortgage");
        check(mv::clarifying_question("ComputeAmortization", sh.missing_field) == "How much is the loan?",
              "...with a question about the loan");
    }
    {
        // ASKING: only for an essential field, and the FIRST one missing.
        const auto in = params("ComputePayment", {{"rate", "0.005416666666666"}, {"present_value", "420000"}});
        const auto sh = mv::shape_stated_params(in, "What's the payment on a $420,000 loan at 6.5%?");
        check(sh.missing_field == "periods", "no term stated: the one question is about the term (asked: '" + sh.missing_field + "')");
        const auto closing = params("ComputeClosingCosts", {{"home_price", "500000"}, {"down_payment_percent", "0.2"}});
        const auto cs = mv::shape_stated_params(closing, "What are my closing costs on a $500,000 house with 20% down?");
        check(cs.missing_field.empty() && cs.kept.size() == 2,
              "closing costs on a priced house with 20% down: nothing to ask, and both stated fields served");
        const auto fv = params("ComputeFutureValue", {{"rate", "0.004166666666666"}, {"periods", "120"}});
        const auto fs = mv::shape_stated_params(fv, "what will it grow to at 5% over 10 years?");
        check(!fs.missing_field.empty(),
              "a future value with neither a payment nor a starting amount asks for one of them");
        const auto lump = params("ComputeFutureValue", {{"rate", "0.004166666666666"}, {"periods", "120"},
                                                        {"present_value", "10000"}, {"payment", "0"}});
        const auto ls = mv::shape_stated_params(lump, "What will $10,000 grow to at 5% over 10 years?");
        check(ls.missing_field.empty() && std::ranges::find(ls.kept, "payment") == ls.kept.end(),
              "a lump sum is complete without a payment, and no payment of 0 is invented for it");
    }
    {
        // The documented DANGEROUS failure must stay a refusal and must NOT become a question or
        // be quietly dropped: a value the text contradicts is kept, so verification refuses it.
        const auto in = params("ComputePayment", {{"rate", "0.005625"}, {"periods", "360"}, {"present_value", "304000.00"}});
        const auto sh = mv::shape_stated_params(in, "What is the payment on $495,000 at 6.75% over 30 years?");
        check(std::ranges::find(sh.kept, "present_value") != sh.kept.end() && sh.missing_field.empty(),
              "present_value = 304000 against a 495,000 utterance is KEPT, so G3 refuses it as before");
    }
    {
        // G2b now asks for the ESSENTIAL fields only.
        const auto three = params("ComputePayment", {{"rate", "0.005"}, {"periods", "360"}, {"present_value", "300000"}});
        expect_pass(three, "What's the payment on $300,000 at 6% over 30 years?",
                    "ComputePayment with only its three essential fields is Proven (was MissingField on future_value)");
        expect(drop_field(three, "periods"), "What's the payment on $300,000 at 6% over 30 years?",
               mv::Outcome::Unsafe, mv::ReasonCode::MissingField,
               "...and without the term it is still refused at G2b");
    }

    section("A RANGE states no figure: it is a question, not a pick of one end");
    {
        const auto kept = [](const mv::ShapedParams& sh, std::string_view name) {
            return std::ranges::find(sh.kept, name) != sh.kept.end();
        };
        // The model took 6% from "between 6% and 7%" and priced the loan as though it were said.
        const auto pay = params("ComputePayment", {{"rate", "0.005"}, {"periods", "360"}, {"present_value", "400000"}});
        const auto sp = mv::shape_stated_params(pay, "what would the payment be on $400,000 at somewhere between 6% and 7% for 30 years?");
        check(sp.missing_field == "rate", "a rate given as a range is MISSING and asked (got \"" + sp.missing_field + "\")");
        check(!kept(sp, "rate"), "...and the rate the model took from one end is not served");
        const auto am = params("ComputeAmortization", {{"loan_amount", "300000"}, {"annual_rate", "0.065"}, {"term_months", "360"}});
        const auto sa = mv::shape_stated_params(am, "amortize 300k at 6.5% for 25 to 30 years");
        check(sa.missing_field == "term_months", "a term given as '25 to 30 years' is asked (got \"" + sa.missing_field + "\")");
        const auto rf = params("ComputeRefinance", {{"current_loan_balance", "300000"}, {"current_annual_rate", "0.07"}, {"new_annual_rate", "0.055"}});
        const auto sr = mv::shape_stated_params(rf, "refinance my $300,000 loan from 7% to somewhere between 5.5% and 6%");
        check(sr.missing_field == "new_annual_rate", "...and only the RANGED rate is asked, not the stated one (got \"" + sr.missing_field + "\")");
        check(kept(sr, "current_annual_rate"), "the rate the visitor did state stays");
        // The shapes that LOOK like ranges and are not.
        const auto rf2 = params("ComputeRefinance", {{"current_loan_balance", "320000"}, {"current_annual_rate", "0.07"}, {"new_annual_rate", "0.06"}});
        const auto s2 = mv::shape_stated_params(rf2, "Refinance 320000 from 7% to 6%");
        check(s2.missing_field.empty() && kept(s2, "new_annual_rate"), "'from 7% to 6%' is a refinance, not a range");
        const auto cu = params("ComputeCumulative", {{"component", "INTEREST"}, {"rate", "0.005"}, {"periods", "360"},
                                                     {"present_value", "500000"}, {"start_period", "13"}, {"end_period", "24"}});
        const auto sc = mv::shape_stated_params(cu, "interest paid in months 13 to 24 on a $500,000 loan at 6% over 30 years");
        check(sc.missing_field.empty() && kept(sc, "start_period") && kept(sc, "end_period"),
              "'months 13 to 24' is a window of two stated figures, not a range");
    }

    section("A CONVENTION CONSTANT resting on another field's figure is the default it always was");
    {
        // "double declining balance for year 2": the 2 is the YEAR, and `factor = 2` found the same
        // figure and read as the visitor's own statement.
        auto dep = params("ComputeDepreciation", {{"method", "DECLINING_BALANCE"}, {"cost", "90000"}, {"salvage", "9000"},
                                                  {"life", "6"}, {"period", "2"}, {"factor", "2"}});
        const auto sd = mv::shape_stated_params(dep, "double declining balance depreciation for year 2 on $90,000 of equipment, 6 year life, $9,000 salvage");
        check(std::ranges::find(sd.kept, "factor") == sd.kept.end(), "factor = 2 is not stated by the 'year 2' that is the PERIOD");
        check(std::ranges::find(sd.kept, "period") != sd.kept.end(), "...and the period it belongs to still is");
    }

    section("A cash flow written with a k suffix is grounded, and its outlay may be negative");
    {
        const auto irr = with_list(params("ComputeIrr", {}), "values", {"-75000", "20000", "30000", "40000", "25000"});
        const auto v1 = mv::verify_mortgage_output(irr, "irr if I put in 75k and get back 20k, 30k, 40k and 25k in years 1 to 4");
        std::printf("      [diag] outcome=%s reason=%s msg=%s\n", mv::to_string(v1.outcome).data(),
                    mv::to_string(v1.reason).data(), v1.message.c_str());
        check(v1.outcome == mv::Outcome::Proven, "k-suffixed cash flows ground, outlay negated");
        const auto pb = with_list(params("ComputePaybackPeriod", {}), "values",
                                  {"-12000", "900", "900", "900", "900", "900", "900", "900", "900", "900", "900", "900",
                                   "900", "900", "900", "900", "900", "900", "900", "900", "900"});
        const auto v2 = mv::verify_mortgage_output(pb, "payback on a 12k water heater that saves 900 a year");
        std::printf("      [diag] outcome=%s reason=%s msg=%s\n", mv::to_string(v2.outcome).data(),
                    mv::to_string(v2.reason).data(), v2.message.c_str());
        check(v2.outcome == mv::Outcome::Proven, "a 12k outlay with a 900 saving grounds");
    }

    section("EVERY declared field has a visitor's-words label, and no visitor-facing text names an internal");
    {
        // The site renders a clarification's question and a refusal's message VERBATIM, twice. A field
        // missing from `kFieldLabels` would reach the visitor as "one of the figures", and a wording
        // that carries an operation or a snake_case field name is a leak of internals (observed live
        // 2026-10-06: `The assistant left out "property_price", which ComputeRentVsBuy needs`). The sweep
        // runs over the DECLARED set, so a field finance.proto grows later turns this red instead of
        // reaching a visitor.
        std::size_t fields_checked = 0, unlabeled = 0, leaks = 0;
        std::string first_unlabeled, first_leak;
        const auto leaky = [](std::string_view text) {
            if (text.find("Compute") != std::string_view::npos) { return true; }
            // a snake_case identifier: lowercase letters, an underscore, lowercase letters
            for (std::size_t i = 1; i + 1 < text.size(); ++i) {
                if (text[i] == '_' && std::islower(static_cast<unsigned char>(text[i - 1])) != 0 &&
                    std::islower(static_cast<unsigned char>(text[i + 1])) != 0) {
                    return true;
                }
            }
            return false;
        };
        for (const auto op : mv::operation_ids()) {
            for (const auto& spec : mv::fields_of(op)) {
                if (mv::operation_excludes_field(op, spec.field)) { continue; }
                ++fields_checked;
                const auto label = mv::field_label(spec.field);
                if (label.empty()) {
                    ++unlabeled;
                    if (first_unlabeled.empty()) { first_unlabeled = std::string{op} + "." + std::string{spec.field}; }
                } else if (leaky(label)) {
                    ++leaks;
                    if (first_leak.empty()) { first_leak = "label of " + std::string{spec.field}; }
                }
                const auto q = mv::clarifying_question(op, spec.field);
                if (!q.empty() && leaky(q)) {
                    ++leaks;
                    if (first_leak.empty()) { first_leak = "question for " + std::string{op} + "." + std::string{spec.field}; }
                }
            }
        }
        check(fields_checked > 150, "the sweep covered the declared fields (" + std::to_string(fields_checked) + ")");
        check(unlabeled == 0, "every declared field has a label (" + std::to_string(unlabeled) + " without, first: " + first_unlabeled + ")");
        check(leaks == 0, "no label or question names an operation or a snake_case field (" + std::to_string(leaks) + ", first: " + first_leak + ")");
        check(leaky("left out \"property_price\", which ComputeRentVsBuy needs"),
              "the leak detector fires on the production text it exists to prevent");
        check(!leaky("What's the home price?"), "...and stays quiet on a visitor's own words");
    }

    section("A cadence the calculation cannot serve is named, not dropped");
    {
        for (const char* t : {"bi-weekly payment on a $350,000 loan at 6.25% over 30 years", "Biweekly payments on 300k",
                              "pay every two weeks instead", "twice a month", "a fortnightly schedule", "semi-monthly on $200,000"}) {
            check(mv::names_unsupported_cadence(t), std::string{"named: "} + t);
        }
        for (const char* t : {"What's the monthly payment on $400,000 at 6.5% for 30 years?", "compounded weekly at 5%",
                              "pay $500 more a month", "a week from now"}) {
            check(!mv::names_unsupported_cadence(t), std::string{"not named: "} + t);
        }
    }

    section("A deposit written the way visitors write it: '30 percent down', and a deposit AS a percent");
    {
        // "30 percent" (the word) did not tag the deposit, so the loan netting never ran.
        const auto pay = params("ComputePayment", {{"rate", "0.003958"}, {"periods", "180"}, {"present_value", "330400"}});
        expect_pass(pay, "a 472k house with 30 percent down; across 180 months: how much is the monthly payment? 4.75% fixed",
                    "'30 percent down' nets a 472k price to a 330,400 loan, exactly as '30% down' does");
        const auto pct = params("ComputePayment", {{"rate", "0.003958"}, {"periods", "180"}, {"present_value", "330400"}});
        expect_pass(pct, "a 472k house with 30% down; across 180 months: how much is the monthly payment? 4.75% fixed",
                    "...and the percent-sign spelling still does (the control)");
        // The deposit itself: price x pct.
        const auto rcf = params("ComputeRentalCashFlow", {{"property_price", "631500"}, {"down_payment", "126300"},
                                                          {"monthly_gross_rent", "3500"}});
        expect_pass(rcf, "rental cash flow 631500, 20% down rent $3,500/mo",
                    "'631500, 20% down' states a 126,300 deposit (price x percent), not only a net loan");
        const auto wrong = params("ComputeRentalCashFlow", {{"property_price", "631500"}, {"down_payment", "136300"},
                                                            {"monthly_gross_rent", "3500"}});
        expect(wrong, "rental cash flow 631500, 20% down rent $3,500/mo", mv::Outcome::Unsafe,
               mv::ReasonCode::UngroundedValue, "...and a deposit that is NOT 20% of the price still refuses");
        // "yearly" is a cadence word.
        const auto fvd = params("ComputeFutureValueDetailed", {{"annual_rate", "0.0325"}, {"years", "30"},
                                                               {"annual_contribution", "12000"}, {"current_principal", "145000"},
                                                               {"compound_frequency", "1"}});
        expect_pass(fvd, "and a 3.25% rate, 145k in a 401k, over 30 years, compounded yearly and saving 12k a year, how much will i have at the end?",
                    "'compounded yearly' grounds compound_frequency = 1");
    }

    section("A CONDO is a property: '300k condo' states the home price");
    {
        const auto cc = params("ComputeClosingCosts", {{"home_price", "300000"}, {"down_payment_percent", "0.2"}});
        const auto v1 = mv::verify_mortgage_output(cc, "Should I expect big closing costs on a 300k condo with 20% down?");
        std::printf("      [diag] outcome=%s reason=%s msg=%s\n", mv::to_string(v1.outcome).data(),
                    mv::to_string(v1.reason).data(), v1.message.c_str());
        check(v1.outcome == mv::Outcome::Proven, "a 300k condo grounds home_price = 300000");
        const auto sh = mv::shape_stated_params(cc, "Should I expect big closing costs on a 300k condo with 20% down?");
        check(std::ranges::find(sh.kept, "home_price") != sh.kept.end(), "...and the price is kept, not dropped");
        const auto house = mv::verify_mortgage_output(cc, "Should I expect big closing costs on a 300k house with 20% down?");
        check(house.outcome == mv::Outcome::Proven, "a 300k HOUSE with the same wording is Proven (the control)");
    }

    section("A spaced hyphen is a dash, and 'down to' is a movement: neither is what its neighbour word says");
    {
        // Both found by GroundingCorpusSweepTest the day the visitor-phrased rows entered the mix:
        //   "Atlanta - 479k loan spread over 180 montsh" lexed -479,000, so the correct 479,000 grounded
        //   against nothing ("the nearest figure you gave is 180"); a city or a label before a dash is how
        //   people open a question. A hyphen with a space on BOTH sides is punctuation, not a sign.
        //   "a 25-year refi; 5.375% down to 4.375%" tagged the 5.375 as a DOWN PAYMENT because the next word
        //   is "down", and M0 refuses a down payment as a rate -- so the stated current rate was refused.
        for (const std::string_view text : {"Atlanta - 479k loan spread over 180 months", "Denver - $658,600 house, 30% down",
                                            "Austin -- 300k at 6%", "Boston - 250,000 balance"}) {
            bool negative = false;
            for (const auto& l : mv::lex_numeric_literals(text)) { if (l.value.is_negative()) negative = true; }
            check(!negative, std::string{"a spaced hyphen produces no negative literal: "} + std::string{text});
        }
        for (const std::string_view text : {"a -$250,000 position", "npv at 7%: -100k now then 30k", "outlay of -250000 today",
                                            "a loan of - -5000"}) {
            bool negative = false;
            for (const auto& l : mv::lex_numeric_literals(text)) { if (l.value.is_negative()) negative = true; }
            check(negative, std::string{"an attached minus still carries its sign: "} + std::string{text});
        }
        const auto atl = params("ComputePayment", {{"present_value", "479000"}, {"periods", "180"}, {"rate", "0.006354"}});
        expect_pass(atl, "Atlanta - 479k loan spread over 180 montsh, what would I pay each month?\n7.625% interest",
                    "'Atlanta - 479k loan' grounds a 479,000 principal");
        const auto refi = params("ComputeRefinance", {{"current_annual_rate", "0.05375"}, {"new_annual_rate", "0.04375"},
                                                      {"current_loan_balance", "580000"}, {"new_term_years", "25"}});
        expect_pass(refi, "What would I save refinancing a $580,000 mortgage balance; a 25-year refi; 5.375% down to 4.375%?",
                    "'5.375% down to 4.375%' is a rate coming down, not a 5.375% deposit");
        bool deposit = false;
        for (const auto& l : mv::lex_numeric_literals("a $400,000 house, 20% down, 6.5% for 30 years")) {
            if (l.names_down_payment && l.value.to_string().rfind("20", 0) == 0) deposit = true;
        }
        check(deposit, "...while '20% down' is still a deposit (the control)");
    }

    section("A cost word governs the figure BESIDE it, not every figure within four words");
    {
        // Found by GroundingCorpusSweepTest over 6,000 generated rows (the 500-row gate was green):
        // 6 of 10 refusals were one defect. The HOA word was searched for up to four words AFTER each
        // figure, so "a $387,000 home HOA 100/month" made the PRICE an HOA fee ("your wording makes it
        // an HOA or association fee ... so it cannot fill this field") and "$339,600 borrowed HOA
        // $275/month" did the same to the loan. A cost word followed straight away by its own figure
        // governs THAT figure.
        const auto flag = [](std::string_view text, std::string_view figure, bool vacancy = false, bool increment = false) -> int {
            for (const auto& l : mv::lex_numeric_literals(text)) {
                std::string digits;
                for (const char c : l.text) { if (c != ',' && c != '$') digits += c; }
                if (digits == figure) { return vacancy ? (l.names_vacancy ? 1 : 0) : increment ? (l.names_increment ? 1 : 0) : (l.names_hoa ? 1 : 0); }
            }
            return -1;
        };
        check(flag("What's the schedule on $339,600 borrowed HOA $275/month at 8% over 30 years?", "339600") == 0,
              "'$339,600 borrowed HOA $275/month': the loan is not HOA dues");
        check(flag("What's the schedule on $339,600 borrowed HOA $275/month at 8% over 30 years?", "275") == 1,
              "...and the $275 that follows the HOA word is");
        check(flag("a $387,000 home HOA 100/month set aside $1,200 a year for repairs", "387000") == 0,
              "'a $387,000 home HOA 100/month': the price is not HOA dues");
        check(flag("a $387,000 home HOA 100/month set aside $1,200 a year for repairs", "100") == 1,
              "...and the 100 beside the HOA word is");
        check(flag("mortgage for 550000, insurance of 3k a year, an HOA fee of 300 a month", "3") == 0,
              "'insurance of 3k a year, an HOA fee of 300 a month': the 3k is insurance, not dues");
        check(flag("mortgage for 550000, insurance of 3k a year, an HOA fee of 300 a month", "300") == 1,
              "...and 'HOA fee of 300' is dues");
        // A HOA word already claimed by the figure BEFORE it does not also claim the next one.
        check(flag("amortization for 5.68 percent rate, for 15 years, $300 hoa dues, 286400", "286400") == 0,
              "'$300 hoa dues, 286400': the figure after claimed dues is the loan");
        check(flag("a $375,400 loan, 15 years remaining, $412,000 home, $150/mo HOA and 1.2k insurance", "1.2") == 0,
              "'$150/mo HOA and 1.2k insurance': the insurance is not dues (a unit word does not break the claim)");
        check(flag("a $691k fixed mortgage, $275 a month in HOA dues, 1,800 a year for upkeep, at 4.66%", "1800") == 0,
              "'$275 a month in HOA dues, 1,800 a year for upkeep': the upkeep is not dues");
        check(flag("a $350 a month HOA fee?\n633k mortgage", "633") == 0,
              "a question mark ends the sentence: '633k mortgage' on the next line is not HOA dues");
        check(flag("HOA dues are 275 a month on a $400,000 house", "275") == 1,
              "'HOA dues are 275': a word with no figure of its own before it still names the figure after (the control)");
        // The controls: the word AFTER a figure still names it when no figure follows the word.
        check(flag("a mortgage of $300,000 with $200 HOA, then $300 for insurance", "200") == 1,
              "'$200 HOA, then $300...': a figure directly before the HOA word is still dues (the control)");
        check(flag("over 15 yrs, $150 HOA dues, rent $5k, $667,000, 6.625%", "667000") == 0,
              "a HOA word three words and another figure back does not make '$667,000' dues");
        check(flag("over 15 yrs, $150 HOA dues, rent $5k, $667,000, 6.625%", "150") == 1,
              "...while '$150 HOA dues' is");
        // "extra" before a figure that the HOA word follows directly: the figure is the dues.
        check(flag("$1,003,000 4.875% +1000 extra $300 HOA tax rate 28%", "300") == 1,
              "'+1000 extra $300 HOA': the 300 is dues");
        check(flag("$1,003,000 4.875% +1000 extra $300 HOA tax rate 28%", "300", false, true) == 0,
              "...and not an overpayment, though 'extra' stands before it");
        check(flag("$1,003,000 4.875% +1000 extra $300 HOA tax rate 28%", "1000", false, true) == 1,
              "...while the +1000 that 'extra' follows is the overpayment (the control)");
        // The word BEFORE a percent: "vacancy 10%" is as ordinary as "10% vacancy".
        check(flag("cash flow on 461K buy-to-let, 6% mgmt $1,900 rent 7 years vacancy 10%", "10", true) == 1,
              "'vacancy 10%' names the vacancy");
        check(flag("rental analysis: $307,000, 5% down vacancy rate of 10% rent $4,900", "10", true) == 1,
              "'vacancy rate of 10%' names the vacancy");
        check(flag("cash flow on 461K buy-to-let, 6% mgmt $1,900 rent 7 years vacancy 10%", "6", true) == 0,
              "...and the 6% management fee beside it does not");
        check(flag("a duplex renting at $5,000 a month, a 5% vacancy rate at 7.75% fixed", "7.75", true) == 0,
              "'a 5% vacancy rate at 7.75% fixed': the vacancy word belongs to the 5, so the 7.75 is a loan rate");
        check(flag("a duplex renting at $5,000 a month, a 5% vacancy rate at 7.75% fixed", "5", true) == 1,
              "...and the 5% it follows is the vacancy (the control)");
        const auto occ = params("ComputeRentalCashFlow", {{"occupancy_rate", "0.9000"}});
        check(mv::ground_emitted_values(occ, "cash flow on 461K buy-to-let, 89.8k down 6% mgmt $1,900 rent 7 years vacancy 10%")
                      .outcome == mv::Outcome::Proven,
              "a 10% vacancy grounds occupancy 0.90 whichever side the word is on");
        // A no-deposit purchase: the price IS the loan, and the words say so.
        const auto nodown = params("ComputeAmortization", {{"loan_amount", "900000.00"}, {"annual_rate", "0.05125"},
                                                           {"term_months", "240"}, {"original_home_value", "900000.00"}});
        const auto shaped = mv::shape_stated_params(nodown, "5.125% interest rate for 20 yrs. What if I pay extra? $900,000, no down payment");
        check(std::ranges::find(shaped.kept, "original_home_value") != shaped.kept.end(),
              "'$900,000, no down payment' states a home worth the loan, so it is kept");
        const auto zero_down = mv::shape_stated_params(nodown, "0 down on a $900,000 house; on a 20 year schedule at 5.125%");
        check(std::ranges::find(zero_down.kept, "original_home_value") != zero_down.kept.end(),
              "'0 down on a $900,000 house' is also a no-deposit purchase");
        const auto thirty_k = mv::shape_stated_params(nodown, "$30,000 down on $900,000 over 20 years at 5.125%");
        check(std::ranges::find(thirty_k.kept, "original_home_value") == thirty_k.kept.end(),
              "...but '$30,000 down' is not '0 down' (the tail characters match, the figure does not)");
        const auto bare = params("ComputeAmortization", {{"loan_amount", "900000.00"}, {"annual_rate", "0.05125"},
                                                         {"term_months", "240"}, {"original_home_value", "900000.00"}});
        const auto shaped2 = mv::shape_stated_params(bare, "5.125% interest rate for 20 yrs on $900,000");
        check(std::ranges::find(shaped2.kept, "original_home_value") == shaped2.kept.end(),
              "...while the same figure with no 'no down payment' is still the loan counted twice (the control)");
    }

    section("A ROUND FIGURE IS RENDERED AS A PLAIN DECIMAL, never as an exponent");
    {
        // `std::to_chars(double)` with no format picks the SHORTER of fixed and scientific, and
        // "5e+05" is shorter than "500000": every round figure came out in exponent form, which
        // this contract refuses, so a correct parse of "$500,000" was a refusal on every
        // Double-typed field. Production-found 2026-10-06 (12 of 272 visitor requests).
        check(mv::plain_decimal_text(500000.0) == "500000", "500000 is \"500000\" (was \"5e+05\")");
        check(mv::plain_decimal_text(-100000.0) == "-100000", "-100000 is \"-100000\" (was \"-1e+05\")");
        check(mv::plain_decimal_text(400000.0) == "400000", "400000 is \"400000\"");
        check(mv::plain_decimal_text(1200000.0) == "1200000", "1200000 keeps every digit");
        check(mv::plain_decimal_text(0.0385) == "0.0385", "0.0385 is the shortest round-trip form, not padded");
        check(mv::plain_decimal_text(2413.77) == "2413.77", "2413.77 keeps its cents");
        check(mv::plain_decimal_text(0.0000001) == "0.0000001", "a small figure is positional too");
        check(mv::plain_decimal_text(-0.0) == "0", "negative zero is plain zero");
        for (const double v : {5e5, -1e5, 4e5, 2e6, 1e3, 3e4, 7.5e-5}) {
            const auto text = mv::plain_decimal_text(v);
            check(text.find('e') == std::string::npos && text.find('E') == std::string::npos,
                  "no exponent marker in " + text);
        }
    }

    std::printf("\n%d checks, %d failures\n", g_checks, g_failures);
    return g_failures == 0 ? 0 : 1;
}
