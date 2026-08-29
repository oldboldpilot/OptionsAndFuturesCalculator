// @author Olumuyiwa Oluwasanmi
//
// Standalone, proto- and gRPC-free tests for the proto-derived CONSTRAINED
// DECODING constraint over the mortgage assistant's `<params>` output
// (src/modules/mortgage_grammar.cppm).
//
// WHAT THIS FILE HAS TO PROVE, AND WHY ALL THREE PARTS ARE MANDATORY
//
// Part one: the LABEL SPACE the grammar constrains to is the same one
// finance.proto declares and the same one mortgage_verification.cppm gates
// on. The module derives it rather than copying it, so agreement is
// structural -- but "structural" is a claim, and this file checks it against
// a FRESH parse of backend/proto/finance.proto with the same section banners
// and the same two exclusions build_mortgage_dataset.py uses. 27 operations,
// 184 fields, in declaration order, and it fails loudly on drift in either
// direction. The one table the module cannot derive (the enum constants,
// which mortgage_verification keeps unexported) is checked BOTH against the
// .proto and against `verify_mortgage_params` itself.
//
// Part two: every measured failure of the 8/30 mortgage fine-tune must be
// UNREPRESENTABLE, not merely refused afterwards. An operation id that does
// not exist, a field name belonging to a different operation, an operation
// confused for its longer sibling, a `]` where a `:` belongs, an unquoted
// key, an object that closes with fields still owed -- each is asserted to
// be a character the automaton will not admit, at the exact character where
// it dies.
//
// Part three, and this is the half that makes the other two mean anything: a
// NON-VACUOUS CONTROL SET. Every legitimate gold params object in
// agent/dataset/data_mortgage/val.jsonl must be ACCEPTED, in full, and end
// the automaton in its completed state. A grammar that rejected everything
// would satisfy part two perfectly and be worthless; this control is the
// only evidence that the constraint DISCRIMINATES. The count is reported as
// a real number, not as a boolean.
//
// Plus a conformance part: the constraint is a `sensen::IGrammar`, so it is
// exercised THROUGH that interface over a real vocabulary (mask, accept,
// EOS gating), and the `params_regex()` projection is handed to sensen's own
// `RegexGrammar::create` to prove it is a pattern that engine actually
// compiles and enforces -- not a pattern that merely looks plausible.
#include <array>
#include <cstdio>
#include <cstdlib>
#include <fstream>
#include <map>
#include <optional>
#include <set>
#include <span>
#include <sstream>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

import mortgage_grammar;
import mortgage_verification;
import sensen.grammar;

namespace mg = mortgage_calculator::assistant::grammar;
namespace mv = mortgage_calculator::assistant::verify;

namespace {

int g_checks = 0;
int g_failures = 0;

auto section(const std::string& title) -> void {
    std::printf("\n== %s ==\n", title.c_str());
}

auto check(bool condition, const std::string& what) -> void {
    ++g_checks;
    if (condition) {
        std::printf("  ok    %s\n", what.c_str());
    } else {
        ++g_failures;
        std::printf("  FAIL  %s\n", what.c_str());
    }
}

// ---------------------------------------------------------------------------
// finance.proto, re-parsed independently. Same rules as
// agent/dataset/build_mortgage_dataset.py's parse_finance_proto() +
// build_operations(), and the same rules tests/test_mortgage_verification.cpp
// applies -- deliberately a SEPARATE implementation from the module under
// test, because a drift check that shared the module's parse would prove
// nothing.
// ---------------------------------------------------------------------------

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

auto find_matching_brace(const std::string& text, std::size_t open) -> std::size_t {
    int depth = 0;
    for (std::size_t i = open; i < text.size(); ++i) {
        if (text[i] == '{') {
            ++depth;
        } else if (text[i] == '}') {
            --depth;
            if (depth == 0) {
                return i;
            }
        }
    }
    return std::string::npos;
}

/** `<type> <name> = <n>[ [opts]];` -- two identifiers before the '=', which is
 * exactly what keeps a proto3 enum constant (one identifier) from parsing as a
 * field. */
auto parse_field_line(std::string_view line) -> std::optional<std::pair<std::string, std::string>> {
    std::string_view s = trim(line);
    if (s.starts_with("//")) {
        return std::nullopt;
    }
    // `optional` is proto3 EXPLICIT PRESENCE -- a presence marker, not a type.
    // Unstripped it parses AS the type and shifts the name across, silently
    // dropping the real field from the drift comparison.
    if (s.starts_with("optional ")) {
        s.remove_prefix(9);
        s = trim(s);
    }
    if (s.starts_with("repeated ")) {
        s.remove_prefix(9);
        s = trim(s);
    }
    const auto take_ident = [&]() -> std::string {
        std::size_t i = 0;
        while (i < s.size() && is_ident_char(s[i])) {
            ++i;
        }
        std::string out{s.substr(0, i)};
        s.remove_prefix(i);
        return out;
    };
    const std::string type = take_ident();
    if (type.empty()) {
        return std::nullopt;
    }
    if (s.empty() || (s.front() != ' ' && s.front() != '\t')) {
        return std::nullopt;
    }
    s = trim(s);
    const std::string name = take_ident();
    if (name.empty()) {
        return std::nullopt;
    }
    s = trim(s);
    if (s.empty() || s.front() != '=') {
        return std::nullopt;
    }
    s.remove_prefix(1);
    s = trim(s);
    std::size_t digits = 0;
    while (digits < s.size() && s[digits] >= '0' && s[digits] <= '9') {
        ++digits;
    }
    if (digits == 0) {
        return std::nullopt;
    }
    s.remove_prefix(digits);
    s = trim(s);
    if (s.starts_with("[")) {
        const auto close = s.find(']');
        if (close == std::string_view::npos) {
            return std::nullopt;
        }
        s.remove_prefix(close + 1);
        s = trim(s);
    }
    if (s.empty() || s.front() != ';') {
        return std::nullopt;
    }
    return std::make_pair(type, name);
}

auto read_file(const std::vector<std::string>& candidates) -> std::optional<std::string> {
    for (const auto& path : candidates) {
        std::ifstream f(path);
        if (!f) {
            continue;
        }
        std::ostringstream ss;
        ss << f.rdbuf();
        return ss.str();
    }
    return std::nullopt;
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
    return read_file(candidates);
}

auto read_val_jsonl() -> std::optional<std::string> {
    std::vector<std::string> candidates;
#ifdef MORTGAGE_VAL_JSONL_PATH
    candidates.emplace_back(MORTGAGE_VAL_JSONL_PATH);
#endif
    if (const char* env = std::getenv("MORTGAGE_VAL_JSONL"); env != nullptr) {
        candidates.emplace_back(env);
    }
    candidates.emplace_back("../../agent/dataset/data_mortgage/val.jsonl");
    candidates.emplace_back("../agent/dataset/data_mortgage/val.jsonl");
    candidates.emplace_back("agent/dataset/data_mortgage/val.jsonl");
    return read_file(candidates);
}

struct ProtoView {
    // rpc name -> declared fields of its request message, in declaration order
    std::map<std::string, std::vector<std::string>> operations;
    // enum type name -> constants, in declaration order
    std::map<std::string, std::vector<std::string>> enums;
    // "Operation.field" -> declared proto type
    std::map<std::string, std::string> field_types;
};

auto parse_proto(const std::string& text) -> ProtoView {
    static const std::set<std::string> kInScope{"Time value of money", "Mortgages, HELOC",
                                                "Cash-flow analysis", "Depreciation",
                                                "Real estate"};
    // Mirrors build_mortgage_dataset.py's EXCLUDE_RPCS, which carries the
    // reasoning for each entry. ComputeRentVsBuyBatch is a BULK API rather than
    // an utterance -- it takes a repeated RentVsBuyRequest, and no single
    // sentence grounds a thousand scenarios. This check FAILED the moment that
    // RPC was added to finance.proto, which is the drift gate working: the
    // assistant's label space must never grow silently because the proto did.
    static const std::set<std::string> kExcluded{"ConvertInterestRate", "ComputeFisherRate",
                                                 "ComputeRentVsBuyBatch",
                                                 "RefreshStateAssumptions",
                                                 "GetStateAssumptions"};

    ProtoView view;
    const auto svc = text.find("service Finance {");
    if (svc == std::string::npos) {
        return view;
    }
    const auto svc_open = text.find('{', svc);
    const auto svc_close = find_matching_brace(text, svc_open);
    const std::string svc_body = text.substr(svc_open + 1, svc_close - svc_open - 1);

    std::vector<std::pair<std::string, std::string>> rpcs;
    {
        std::istringstream lines(svc_body);
        std::string line;
        std::string current_section;
        while (std::getline(lines, line)) {
            const std::string_view t = trim(line);
            if (t.starts_with("// --")) {
                std::string_view s = t.substr(5);
                s = trim(s);
                while (!s.empty() && s.back() == '-') {
                    s.remove_suffix(1);
                }
                current_section = std::string{trim(s)};
                continue;
            }
            const auto rpc_pos = t.find("rpc ");
            if (rpc_pos == std::string_view::npos) {
                continue;
            }
            std::string_view s = t.substr(rpc_pos + 4);
            std::size_t i = 0;
            while (i < s.size() && is_ident_char(s[i])) {
                ++i;
            }
            const std::string name{s.substr(0, i)};
            const auto open = s.find('(');
            const auto close = s.find(')');
            if (open == std::string_view::npos || close == std::string_view::npos) {
                continue;
            }
            const std::string request{trim(s.substr(open + 1, close - open - 1))};
            if (kInScope.count(current_section) == 0) {
                continue;
            }
            if (kExcluded.count(name) != 0) {
                continue;
            }
            rpcs.emplace_back(name, request);
        }
    }

    for (const auto& [rpc, request] : rpcs) {
        const std::string needle = "message " + request + " {";
        auto pos = text.find("\n" + needle);
        if (pos == std::string::npos && text.starts_with(needle)) {
            pos = 0;
        } else if (pos != std::string::npos) {
            pos += 1;
        }
        if (pos == std::string::npos) {
            continue;
        }
        const auto open = text.find('{', pos);
        const auto close = find_matching_brace(text, open);
        const std::string body = text.substr(open + 1, close - open - 1);

        std::istringstream lines(body);
        std::string line;
        std::vector<std::string> fields;
        while (std::getline(lines, line)) {
            if (auto f = parse_field_line(line); f.has_value()) {
                fields.push_back(f->second);
                view.field_types[rpc + "." + f->second] = f->first;
            }
        }
        view.operations[rpc] = std::move(fields);
    }

    // Every `enum Name { CONST = n; ... }` anywhere in the file, nested or not.
    std::size_t at = 0;
    while (true) {
        const auto pos = text.find("enum ", at);
        if (pos == std::string::npos) {
            break;
        }
        at = pos + 5;
        std::size_t i = at;
        while (i < text.size() && is_ident_char(text[i])) {
            ++i;
        }
        const std::string name = text.substr(at, i - at);
        const auto open = text.find('{', i);
        if (open == std::string::npos) {
            continue;
        }
        const auto close = find_matching_brace(text, open);
        if (close == std::string::npos) {
            continue;
        }
        const std::string body = text.substr(open + 1, close - open - 1);
        std::istringstream lines(body);
        std::string line;
        std::vector<std::string> constants;
        while (std::getline(lines, line)) {
            std::string_view s = trim(line);
            if (s.starts_with("//")) {
                continue;
            }
            std::size_t j = 0;
            while (j < s.size() && is_ident_char(s[j])) {
                ++j;
            }
            if (j == 0) {
                continue;
            }
            const std::string constant{s.substr(0, j)};
            std::string_view rest = trim(s.substr(j));
            if (rest.empty() || rest.front() != '=') {
                continue;
            }
            constants.push_back(constant);
        }
        if (view.enums.count(name) == 0) {
            view.enums[name] = std::move(constants);
        }
    }
    return view;
}

// ---------------------------------------------------------------------------
// The gold control set. Each val.jsonl line is one ShareGPT conversation whose
// last turn is the assistant's; the `<params>` payload is lifted out of it and
// its JSON string escaping undone.
// ---------------------------------------------------------------------------

auto json_unescape(std::string_view s) -> std::string {
    std::string out;
    out.reserve(s.size());
    for (std::size_t i = 0; i < s.size(); ++i) {
        if (s[i] != '\\' || i + 1 >= s.size()) {
            out.push_back(s[i]);
            continue;
        }
        ++i;
        switch (s[i]) {
            case 'n': out.push_back('\n'); break;
            case 't': out.push_back('\t'); break;
            case 'r': out.push_back('\r'); break;
            case 'b': out.push_back('\b'); break;
            case 'f': out.push_back('\f'); break;
            default: out.push_back(s[i]); break;  // \" \\ \/ and anything else
        }
    }
    return out;
}

struct GoldSet {
    std::vector<std::string> params;  ///< the unescaped `{...}` payloads
    std::size_t rows = 0;
    std::size_t rows_without_params = 0;
};

auto load_gold(const std::string& jsonl) -> GoldSet {
    GoldSet gold;
    std::istringstream lines(jsonl);
    std::string line;
    while (std::getline(lines, line)) {
        if (trim(line).empty()) {
            continue;
        }
        ++gold.rows;
        // `<params>{`, not `<params>`: every row's SYSTEM turn quotes the empty
        // tag pair when it tells the model how to answer, so matching the tag
        // alone finds the instruction rather than the answer and silently turns
        // 46 clarifying-question rows into 46 empty "gold" objects.
        const auto open = line.rfind("<params>{");
        const auto close = (open == std::string::npos) ? std::string::npos
                                                       : line.find("</params>", open);
        if (open == std::string::npos || close == std::string::npos) {
            ++gold.rows_without_params;
            continue;
        }
        const auto body_start = open + std::string_view{"<params>"}.size();
        gold.params.push_back(json_unescape(line.substr(body_start, close - body_start)));
    }
    return gold;
}

/** A flat, whitespace-free params object -> the struct the verifier consumes.
 * Only what this dataset actually contains: scalars and arrays of scalars. */
auto parse_params(std::string_view json) -> std::optional<mv::MortgageParamsInput> {
    mv::MortgageParamsInput input;
    input.params_emitted = true;
    if (json.size() < 2 || json.front() != '{' || json.back() != '}') {
        return std::nullopt;
    }
    std::string_view body = json.substr(1, json.size() - 2);
    std::size_t i = 0;
    const auto read_string = [&]() -> std::optional<std::string> {
        if (i >= body.size() || body[i] != '"') {
            return std::nullopt;
        }
        ++i;
        std::string out;
        while (i < body.size() && body[i] != '"') {
            out.push_back(body[i]);
            ++i;
        }
        if (i >= body.size()) {
            return std::nullopt;
        }
        ++i;
        return out;
    };
    const auto read_scalar = [&]() -> std::string {
        if (i < body.size() && body[i] == '"') {
            return read_string().value_or(std::string{});
        }
        std::string out;
        while (i < body.size() && body[i] != ',' && body[i] != ']') {
            out.push_back(body[i]);
            ++i;
        }
        return out;
    };
    while (i < body.size()) {
        auto key = read_string();
        if (!key.has_value() || i >= body.size() || body[i] != ':') {
            return std::nullopt;
        }
        ++i;
        mv::EmittedField field;
        field.name = *key;
        if (i < body.size() && body[i] == '[') {
            ++i;
            field.repeated = true;
            while (i < body.size() && body[i] != ']') {
                field.values.push_back(read_scalar());
                if (i < body.size() && body[i] == ',') {
                    ++i;
                }
            }
            if (i >= body.size()) {
                return std::nullopt;
            }
            ++i;
        } else {
            field.values.push_back(read_scalar());
        }
        if (field.name == "operation") {
            input.operation = field.values.empty() ? std::string{} : field.values.front();
        } else {
            input.fields.push_back(std::move(field));
        }
        if (i < body.size() && body[i] == ',') {
            ++i;
        }
    }
    return input;
}

// ---------------------------------------------------------------------------
// Automaton helpers.
// ---------------------------------------------------------------------------

/** Feed `prefix`, then assert `next` is refused -- i.e. the defect dies at
 * exactly that character rather than somewhere convenient later. */
auto dies_at(const mg::Schema& schema, std::string_view prefix, char next) -> bool {
    mg::ParamsAutomaton a{schema};
    if (!a.feed_text(prefix)) {
        return false;  // the prefix itself was refused; the assertion is not about that
    }
    return !a.feed(next);
}

auto prefix_ok(const mg::Schema& schema, std::string_view prefix) -> bool {
    mg::ParamsAutomaton a{schema};
    return a.feed_text(prefix);
}

}  // namespace

auto main() -> int {
    // ---------------------------------------------------------------------
    section("Gate 0 -- the schema derives, and validates against mortgage_verification");
    // ---------------------------------------------------------------------
    const auto& built = mg::default_schema();
    if (!built.has_value()) {
        check(false, "Schema::build() failed: " + built.error());
        std::printf("\n%d checks, %d failures\n", g_checks, g_failures);
        return 1;
    }
    const mg::Schema& schema = *built;
    check(true, "Schema::build() succeeded");

    {
        const auto valid = mg::validate_label_space(schema);
        check(valid.has_value(),
              valid.has_value() ? "validate_label_space: schema matches mortgage_verification"
                                : ("validate_label_space FAILED: " + valid.error()));
        check(schema.operation_count() == 27,
              "27 operations (schema has " + std::to_string(schema.operation_count()) + ")");
        check(schema.field_count() == 184,
              "184 fields (schema has " + std::to_string(schema.field_count()) + ")");
    }

    // ---------------------------------------------------------------------
    section("Gate 1 -- the derived label space still matches finance.proto");
    // ---------------------------------------------------------------------
    std::optional<ProtoView> proto;
    {
        const auto text = read_proto();
        if (!text.has_value()) {
            check(false,
                  "could not open backend/proto/finance.proto (set MORTGAGE_FINANCE_PROTO_PATH at "
                  "build time or FINANCE_PROTO_PATH in the environment) -- the drift check cannot "
                  "be skipped silently");
        } else {
            proto = parse_proto(*text);
            const auto& ops = proto->operations;
            check(ops.size() == schema.operation_count(),
                  "operation count: proto has " + std::to_string(ops.size()) + ", schema has " +
                      std::to_string(schema.operation_count()));

            std::size_t proto_fields = 0;
            for (const auto& [op, fields] : ops) {
                proto_fields += fields.size();
            }
            check(proto_fields == schema.field_count(),
                  "field count: proto has " + std::to_string(proto_fields) + ", schema has " +
                      std::to_string(schema.field_count()));

            bool all_match = true;
            std::string mismatch;
            for (const auto& [op, fields] : ops) {
                const auto idx = schema.index_of_operation(op);
                if (!idx.has_value()) {
                    all_match = false;
                    mismatch = "schema is missing operation " + op;
                    break;
                }
                const auto planned = schema.fields_at(*idx);
                if (planned.size() != fields.size()) {
                    all_match = false;
                    mismatch = op + ": proto has " + std::to_string(fields.size()) +
                               " fields, schema has " + std::to_string(planned.size());
                    break;
                }
                for (std::size_t i = 0; i < fields.size(); ++i) {
                    if (std::string{planned[i].name} != fields[i]) {
                        all_match = false;
                        mismatch = op + " field " + std::to_string(i) + ": proto says \"" +
                                   fields[i] + "\", schema says \"" +
                                   std::string{planned[i].name} + "\"";
                        break;
                    }
                }
                if (!all_match) {
                    break;
                }
            }
            check(all_match, all_match
                                 ? "every operation, field and field ORDER matches finance.proto"
                                 : ("label space has drifted -- " + mismatch));

            for (std::size_t i = 0; i < schema.operation_count(); ++i) {
                if (ops.count(std::string{schema.operation_at(i)}) == 0) {
                    check(false, "schema declares operation \"" +
                                     std::string{schema.operation_at(i)} +
                                     "\" that finance.proto does not put in scope");
                }
            }
        }
    }

    // ---------------------------------------------------------------------
    section("Gate 2 -- the enum constant sets, checked against the .proto AND the verifier");
    // ---------------------------------------------------------------------
    {
        // Direction A: every enum this module carries must spell exactly what
        // finance.proto declares, in order. This is the one table the module
        // could not derive, so it is the one that could drift.
        if (proto.has_value()) {
            for (const auto& table : schema.enum_table()) {
                const auto it = proto->enums.find(std::string{table.type_name});
                if (it == proto->enums.end()) {
                    check(false, "finance.proto declares no enum " + std::string{table.type_name});
                    continue;
                }
                bool same = it->second.size() == table.constants.size();
                for (std::size_t i = 0; same && i < table.constants.size(); ++i) {
                    same = (std::string{table.constants[i]} == it->second[i]);
                }
                check(same, std::string{table.type_name} + ": " +
                                std::to_string(table.constants.size()) +
                                " constants match finance.proto");
            }
            // And every enum-typed field in the label space must have a table.
            for (std::size_t i = 0; i < schema.operation_count(); ++i) {
                for (const auto& field : schema.fields_at(i)) {
                    const auto* spec = mv::find_field(schema.operation_at(i), field.name);
                    if (spec == nullptr) {
                        continue;
                    }
                    const bool proto_enum = proto->enums.count(std::string{spec->proto_type}) != 0;
                    const bool module_enum = (field.shape.form == mg::ValueForm::EnumConstant);
                    if (proto_enum != module_enum) {
                        check(false, std::string{schema.operation_at(i)} + "." +
                                         std::string{field.name} + ": proto enum=" +
                                         (proto_enum ? "yes" : "no") + ", module enum=" +
                                         (module_enum ? "yes" : "no"));
                    }
                }
            }
            check(true, "every enum-typed field in the label space has a constant table");
        }

        // Direction B: the verifier must agree. A constant this grammar permits
        // must not be refused as InvalidEnumValue, and a constant it forbids
        // must be. Built on a REAL gold row rather than a hand-invented one, so
        // the surrounding fields are the ones the operation actually declares.
        // (Populated below, once the gold set has been read.)
    }

    // ---------------------------------------------------------------------
    section("Gate 3 -- the measured failures are UNREPRESENTABLE");
    // ---------------------------------------------------------------------
    {
        const std::string P = std::string{schema.prelude()};  // <params>{"operation":"

        // --- operation ids that do not exist -------------------------------
        // `ComputePayoff` is a prefix of the real `ComputePayoffTiming`, so it
        // is not refused at a character -- it is refused at the CLOSING QUOTE,
        // which is where "an id that is a prefix of a real id" has to die.
        check(prefix_ok(schema, P + "ComputePayoff"),
              "\"ComputePayoff\" is a live prefix (of ComputePayoffTiming)");
        check(dies_at(schema, P + "ComputePayoff", '"'),
              "ComputePayoff cannot close: no operation has that exact id");
        check(prefix_ok(schema, P + "ComputePayoffTiming\""),
              "...while ComputePayoffTiming closes normally");

        check(dies_at(schema, P + "ComputeRent", 'B'), "ComputeRentBuy dies at 'B'");
        check(dies_at(schema, P + "ComputeRentalR", 'c'), "ComputeRentalRccapRate dies at 'c'");
        check(dies_at(schema, P + "ComputeAmortizationBatch", 'e'),
              "a longer-than-real id cannot continue past the real one");

        // --- ComputeFutureValue confused for ComputeFutureValueDetailed -----
        // Both are real, so neither can be refused as an id. The confusion is
        // caught one level down: the two have DISJOINT first fields, and the
        // grammar knows which belongs to which.
        check(prefix_ok(schema, P + "ComputeFutureValue\""), "ComputeFutureValue is a real id");
        check(prefix_ok(schema, P + "ComputeFutureValueDetailed\""),
              "ComputeFutureValueDetailed is a real id");
        check(dies_at(schema, P + "ComputeFutureValue\",\"", 'a'),
              "ComputeFutureValue cannot take ComputeFutureValueDetailed's \"annual_rate\"");
        check(prefix_ok(schema, P + "ComputeFutureValueDetailed\",\"annual_rate\":"),
              "...while ComputeFutureValueDetailed can");
        check(dies_at(schema, P + "ComputeFutureValueDetailed\",\"", 'r'),
              "ComputeFutureValueDetailed cannot take ComputeFutureValue's \"rate\"");

        // --- ComputeHomeFutureValue: years / property_price ----------------
        const std::string HFV = P + "ComputeHomeFutureValue\",\"";
        check(dies_at(schema, HFV, 'y'),
              "\"years\" cannot follow ComputeHomeFutureValue (it spells it target_years)");
        check(dies_at(schema, HFV, 'p'),
              "\"property_price\" cannot follow ComputeHomeFutureValue");
        check(prefix_ok(schema, HFV + "current_property_value\":\"450000.00\""),
              "...while current_property_value is admitted");

        // --- ComputeMortgageRecast: extra_payment vs lump_sum_payment -------
        check(dies_at(schema, P + "ComputeMortgageRecast\",\"", 'e'),
              "\"extra_payment\" cannot follow ComputeMortgageRecast");
        check(prefix_ok(schema, P + "ComputeMortgageRecast\",\"current_loan_balance\":\"1\""),
              "...while its real first field is admitted");

        // --- ComputeDetailedAmortization: monthly_tax_rate vs annual_tax_rate
        // `monthly_overpayment` is a real field, so "monthly_" is a live
        // prefix; the defect dies at the character where the two diverge.
        {
            mg::ParamsAutomaton a{schema};
            const bool ok = a.feed_text(P + "ComputeDetailedAmortization\",\"loan_amount\":\"1\","
                                            "\"annual_rate\":\"0.06\",\"term_months\":360,\"");
            check(ok, "ComputeDetailedAmortization's first three fields are admitted");
            check(a.feed_text("monthly_"), "\"monthly_\" is a live prefix (monthly_overpayment)");
            check(!a.feed('t'), "\"monthly_tax_rate\" dies at 't' -- the field is annual_tax_rate");
            check(a.feed('o'), "...and monthly_overpayment continues");
        }

        // --- not-even-JSON ------------------------------------------------
        const std::string CUM = P + "ComputeCumulative\",\"component\":\"INTEREST\",\"rate\":0.0058,"
                                    "\"periods\":240,\"present_value\":158700,\"start_period\":26,"
                                    "\"end_period\":37,\"timing";
        check(prefix_ok(schema, CUM), "a full ComputeCumulative prefix up to the last key");
        check(dies_at(schema, CUM, ']'), "`\"timing]` -- a ']' cannot appear inside a key");
        check(dies_at(schema, CUM + "\"", ']'), "`\"timing\"]` -- a ']' cannot follow a closed key");
        check(dies_at(schema, CUM + "\"", '='), "`\"timing\"=` -- an '=' cannot replace the ':'");
        check(prefix_ok(schema, CUM + "\":"), "...only ':' may follow a closed key");

        check(dies_at(schema, P + "ComputePayment\",", 'r'),
              "an UNQUOTED key is refused -- only '\"' may open one");
        check(dies_at(schema, P + "ComputePayment\"", '}'),
              "the object cannot close with all five fields still owed");
        check(dies_at(schema,
                      P + "ComputePayment\",\"rate\":\"0.005\",\"periods\":360,"
                          "\"present_value\":\"300000.00\",\"future_value\":\"0.00\","
                          "\"timing\":\"END_OF_PERIOD\"",
                      ','),
              "no SIXTH field: a ',' is refused once every declared field is emitted");
        check(prefix_ok(schema,
                        P + "ComputePayment\",\"rate\":\"0.005\",\"periods\":360,"
                            "\"present_value\":\"300000.00\",\"future_value\":\"0.00\","
                            "\"timing\":\"END_OF_PERIOD\"}</params>"),
              "...and the complete object closes");

        // --- value forms ---------------------------------------------------
        check(dies_at(schema, P + "ComputePayment\",\"rate\":", '0'),
              "a proto-string (BigDecimal) field cannot take a BARE number");
        check(dies_at(schema, P + "ComputePayment\",\"rate\":\"", '-'),
              "a rate cannot be negative -- the sign is refused at generation time");
        check(dies_at(schema, P + "ComputePayment\",\"rate\":\"0.005\",\"periods\":36", '.'),
              "term/period counts are integral: '.' is refused on an int32 count");
        check(prefix_ok(schema,
                        P + "ComputeDepreciation\",\"method\":\"MACRS\",\"cost\":262000,"
                            "\"salvage\":37100,\"life\":39,\"period\":33.0"),
              "...while a double-typed `period` may carry a trailing .0");
        check(prefix_ok(schema,
                        P + "ComputeDepreciation\",\"method\":\"STRAIGHT_LINE\",\"cost\":1,"
                            "\"salvage\":0,\"life\":27.5,\"period\":1,\"factor\":2.0,"
                            "\"recovery_period\":27.5"),
              "...and recovery_period may carry MACRS's fractional 27.5-year life");
        check(dies_at(schema, P + "ComputeDepreciation\",\"method\":\"", 'X'),
              "an invented enum constant is refused at its first wrong character");
        check(dies_at(schema, P + "ComputeDepreciation\",\"method\":\"MACR", '"'),
              "a TRUNCATED enum constant cannot close");
        check(prefix_ok(schema, P + "ComputeNpv\",\"rate\":0.08,\"values\":[-60000,12500"),
              "`values` -- the one slot where a negative is the contract -- takes a '-'");
        check(dies_at(schema, P + "ComputeNpv\",\"rate\":", '-'),
              "...and the very same object's `rate` does not");
        check(dies_at(schema, P + "ComputeNpv\",\"rate\":0.08,\"values\":[", ']'),
              "an EMPTY repeated value is refused");
        check(dies_at(schema, P + "ComputePaybackPeriod\",\"values\":[1000],\"discounted\":", '1'),
              "a bool field refuses a number");
        check(prefix_ok(schema, P + "ComputePaybackPeriod\",\"values\":[1000],\"discounted\":true"),
              "...and takes `true`");
    }

    // ---------------------------------------------------------------------
    section("Gate 4 -- completion, and what a completed automaton refuses");
    // ---------------------------------------------------------------------
    const std::string kGoldPayment =
        R"({"operation":"ComputePayment","rate":"0.005","periods":360,)"
        R"("present_value":"300000.00","future_value":"0.00","timing":"END_OF_PERIOD"})";
    {
        mg::ParamsAutomaton a{schema};
        check(a.feed_text("<params>" + kGoldPayment), "a whole gold object is consumed");
        check(!a.complete(), "...but is NOT complete before </params>");
        check(a.feed_text("</params>"), "</params> is consumed");
        check(a.complete(), "...and now it is complete");
        check(!a.feed(' '), "a completed automaton admits nothing further");
        check(a.resolved_operation().has_value() &&
                  *a.resolved_operation() == std::string_view{"ComputePayment"},
              "the resolved operation is reported");
        check(a.pending_fields().empty(), "no field is left pending");

        mg::ParamsAutomaton partial{schema};
        check(partial.feed_text("<params>" + kGoldPayment.substr(0, kGoldPayment.size() - 1)),
              "an object one '}' short is consumed");
        check(!partial.complete(), "...and is not complete");
        check(partial.pending_fields().empty(),
              "...with every field emitted, so only the '}' is owed");
    }

    // ---------------------------------------------------------------------
    section("Gate 5 -- order-free mode is still closed, and still complete");
    // ---------------------------------------------------------------------
    {
        const auto relaxed = mg::Schema::build(
            mg::GrammarOptions{.wrap_in_params_tags = true, .require_declaration_order = false});
        check(relaxed.has_value(), "an order-free schema builds");
        if (relaxed.has_value()) {
            const std::string P = std::string{relaxed->prelude()};
            check(prefix_ok(*relaxed, P + "ComputePayment\",\"timing\":\"END_OF_PERIOD\",\"rate\":"),
                  "order-free: a LATER field may come first");
            check(dies_at(*relaxed, P + "ComputePayment\",\"timing\":\"END_OF_PERIOD\",\"", 't'),
                  "order-free: a field cannot be emitted TWICE");
            check(dies_at(*relaxed, P + "ComputeHomeFutureValue\",\"", 'y'),
                  "order-free: \"years\" is still not a field of ComputeHomeFutureValue");
            check(dies_at(*relaxed, P + "ComputeHomeFutureValue\",\"p", 'r'),
                  "order-free: \"property_price\" is still not one either");
            check(dies_at(*relaxed,
                          P + "ComputePayment\",\"timing\":\"END_OF_PERIOD\",\"rate\":\"0.005\"",
                          '}'),
                  "order-free: the object still cannot close with fields owed");
        }
    }

    // ---------------------------------------------------------------------
    section("Gate 6 -- THE CONTROL SET: every gold params object is ACCEPTED");
    // ---------------------------------------------------------------------
    GoldSet gold;
    {
        const auto jsonl = read_val_jsonl();
        if (!jsonl.has_value()) {
            check(false,
                  "could not open agent/dataset/data_mortgage/val.jsonl (set "
                  "MORTGAGE_VAL_JSONL_PATH at build time or MORTGAGE_VAL_JSONL in the environment) "
                  "-- the control set is what proves this grammar discriminates and cannot be "
                  "skipped silently");
        } else {
            gold = load_gold(*jsonl);
            check(gold.rows > 0, "val.jsonl has " + std::to_string(gold.rows) + " rows");
            check(gold.params.size() >= 500,
                  std::to_string(gold.params.size()) + " of them carry a <params> block (" +
                      std::to_string(gold.rows_without_params) +
                      " are clarifying questions, which this grammar deliberately does not cover)");

            std::size_t accepted = 0;
            std::size_t accepted_relaxed = 0;
            std::string first_reject;
            const auto relaxed = mg::Schema::build(mg::GrammarOptions{
                .wrap_in_params_tags = true, .require_declaration_order = false});
            for (const auto& params : gold.params) {
                const std::string whole = "<params>" + params + "</params>";
                if (mg::ParamsAutomaton::accepts(schema, whole)) {
                    ++accepted;
                } else if (first_reject.empty()) {
                    first_reject = params;
                }
                if (relaxed.has_value() && mg::ParamsAutomaton::accepts(*relaxed, whole)) {
                    ++accepted_relaxed;
                }
            }
            check(accepted == gold.params.size(),
                  "strict (declaration-order) grammar accepts " + std::to_string(accepted) + "/" +
                      std::to_string(gold.params.size()) + " gold params objects" +
                      (first_reject.empty() ? "" : (" -- first rejection: " + first_reject)));
            check(accepted_relaxed == gold.params.size(),
                  "order-free grammar accepts " + std::to_string(accepted_relaxed) + "/" +
                      std::to_string(gold.params.size()) + " gold params objects");

            // Non-vacuity of the control itself: the gold set must exercise
            // every operation, or "600/600 accepted" would be 600 rows of the
            // same shape.
            std::set<std::string> covered;
            for (const auto& params : gold.params) {
                mg::ParamsAutomaton a{schema};
                if (a.feed_text("<params>" + params) || a.resolved_operation().has_value()) {
                    if (const auto op = a.resolved_operation(); op.has_value()) {
                        covered.insert(std::string{*op});
                    }
                }
            }
            check(covered.size() == schema.operation_count(),
                  "the control set covers " + std::to_string(covered.size()) + "/" +
                      std::to_string(schema.operation_count()) + " operations");
        }
    }

    // ---------------------------------------------------------------------
    section("Gate 2b -- the enum constants, against verify_mortgage_params itself");
    // ---------------------------------------------------------------------
    {
        // Take a REAL gold row per enum-bearing operation and substitute each
        // constant this grammar permits, then one it does not. The verifier
        // must accept the former and answer InvalidEnumValue for the latter.
        std::map<std::string, std::string> sample_for;  // operation -> params json
        for (const auto& params : gold.params) {
            mg::ParamsAutomaton a{schema};
            (void)a.feed_text("<params>" + params);
            if (const auto op = a.resolved_operation(); op.has_value()) {
                sample_for.emplace(std::string{*op}, params);
            }
        }

        struct EnumSite {
            const char* operation;
            const char* field;
            const char* type_name;
        };
        static constexpr std::array<EnumSite, 4> kSites{{
            {.operation = "ComputePayment", .field = "timing", .type_name = "AnnuityTiming"},
            {.operation = "ComputeCumulative", .field = "component", .type_name = "Component"},
            {.operation = "ComputeRefinance",
             .field = "closing_cost_type",
             .type_name = "ClosingCostType"},
            {.operation = "ComputeDepreciation", .field = "method", .type_name = "Method"},
        }};

        for (const auto& site : kSites) {
            const auto it = sample_for.find(site.operation);
            if (it == sample_for.end()) {
                check(false, std::string{"no gold row for "} + site.operation +
                                 " -- cannot cross-check " + site.type_name);
                continue;
            }
            auto base = parse_params(it->second);
            if (!base.has_value()) {
                check(false, std::string{"could not parse the gold row for "} + site.operation);
                continue;
            }
            std::span<const std::string_view> constants;
            for (const auto& table : schema.enum_table()) {
                if (table.type_name == std::string_view{site.type_name}) {
                    constants = table.constants;
                }
            }
            check(!constants.empty(), std::string{site.type_name} + " has a constant table");

            bool all_accepted = true;
            for (const auto constant : constants) {
                auto input = *base;
                for (auto& f : input.fields) {
                    if (f.name == site.field) {
                        f.values = {std::string{constant}};
                    }
                }
                const auto verdict = mv::verify_mortgage_params(input);
                if (verdict.reason == mv::ReasonCode::InvalidEnumValue) {
                    all_accepted = false;
                    check(false, std::string{site.operation} + "." + site.field + " = \"" +
                                     std::string{constant} +
                                     "\" is permitted by the grammar but refused by the verifier "
                                     "as InvalidEnumValue");
                }
            }
            if (all_accepted) {
                check(true, std::string{site.type_name} + ": all " +
                                std::to_string(constants.size()) +
                                " constants the grammar permits are accepted by the verifier");
            }

            auto bogus = *base;
            for (auto& f : bogus.fields) {
                if (f.name == site.field) {
                    f.values = {"NOT_A_REAL_CONSTANT"};
                }
            }
            const auto verdict = mv::verify_mortgage_params(bogus);
            check(verdict.reason == mv::ReasonCode::InvalidEnumValue,
                  std::string{site.operation} + "." + site.field +
                      " = \"NOT_A_REAL_CONSTANT\" is InvalidEnumValue to the verifier -- and "
                      "unrepresentable to the grammar");
            // The same refusal, one level earlier, in the grammar: replay the
            // gold row up to and including that field's opening quote, then
            // offer a character no constant of this enum begins with.
            const std::string opener = std::string{"\""} + site.field + "\":\"";
            const auto at = it->second.find(opener);
            check(at != std::string::npos,
                  std::string{"the gold row for "} + site.operation + " carries " + site.field);
            if (at != std::string::npos) {
                check(dies_at(schema, "<params>" + it->second.substr(0, at + opener.size()), 'Z'),
                      std::string{"...and the grammar refuses a non-constant at the very first "
                                  "character (for "} +
                          site.operation + "." + site.field + ")");
            }
        }
    }

    // ---------------------------------------------------------------------
    section("Gate 7 -- driven THROUGH sensen::IGrammar over a real vocabulary");
    // ---------------------------------------------------------------------
    {
        // A byte vocabulary plus a few word pieces, so the mask has to reason
        // about multi-character tokens rather than single bytes only. Id 128
        // is the empty control token; 129.. are the pieces; the last id is EOS.
        std::vector<std::string> vocab;
        vocab.reserve(160);
        for (int c = 0; c < 128; ++c) {
            vocab.emplace_back(1, static_cast<char>(c));
        }
        vocab.emplace_back("");  // 128: a control token with no text
        const std::vector<std::string> pieces{"Compute",
                                              "Payoff",
                                              "PayoffTiming",
                                              "FutureValue",
                                              "Detailed",
                                              "Payment",
                                              "\"operation\":\"",
                                              "current_",
                                              "property_value",
                                              "\",\"",
                                              "END_OF_PERIOD",
                                              "<params>",
                                              "</params>"};
        for (const auto& p : pieces) {
            vocab.push_back(p);
        }
        const auto eos = static_cast<std::uint32_t>(vocab.size());
        vocab.emplace_back("<|im_end|>");

        const auto id_of = [&](std::string_view text) -> std::uint32_t {
            for (std::uint32_t i = 0; i < vocab.size(); ++i) {
                if (vocab[i] == text) {
                    return i;
                }
            }
            return 0xFFFFFFFFU;
        };

        mg::MortgageParamsGrammar grammar{schema, vocab, eos};
        sensen::IGrammar& as_interface = grammar;  // it IS the sensen contract

        check(as_interface.allowedMask().size() == vocab.size(),
              "the mask is sized to the vocabulary");
        check(!as_interface.isComplete(), "a fresh grammar is not complete");
        check(!as_interface.allowedMask()[eos], "EOS is masked out before anything is emitted");
        check(!as_interface.allowedMask()[128], "an empty-text token is never allowed");

        // Only the first character of the prelude may open.
        {
            std::size_t allowed = 0;
            for (std::size_t i = 0; i < 128; ++i) {
                if (as_interface.allowedMask()[i]) {
                    ++allowed;
                }
            }
            check(allowed == 1 && as_interface.allowedMask()[static_cast<unsigned char>('<')],
                  "exactly one single-byte token is allowed at step 0, and it is '<'");
            check(as_interface.allowedMask()[id_of("<params>")],
                  "the multi-character `<params>` token is allowed at step 0");
        }

        check(as_interface.accept(id_of("<params>")), "accept(`<params>`)");
        check(as_interface.accept(static_cast<std::uint32_t>('{')), "accept('{')");
        check(as_interface.accept(id_of("\"operation\":\"")), "accept(`\"operation\":\"`)");
        check(as_interface.accept(id_of("Compute")), "accept(`Compute`)");

        check(as_interface.allowedMask()[id_of("PayoffTiming")], "`PayoffTiming` is allowed");
        check(as_interface.allowedMask()[id_of("FutureValue")], "...and so is `FutureValue`");
        check(!as_interface.allowedMask()[static_cast<std::uint32_t>('Z')],
              "...while 'Z' is masked out: no operation continues `Compute` with it");

        // `Payoff` is allowed as a TOKEN -- it is a live prefix of
        // ComputePayoffTiming -- so the id `ComputePayoff` is not killed by the
        // mask that admits it. It is killed one step later, at the quote that
        // would close it, which is exactly what "unrepresentable" has to mean
        // for an id that is a prefix of a real one.
        {
            mg::MortgageParamsGrammar probe{schema, vocab, eos};
            check(probe.prime("<params>{\"operation\":\"Compute"), "probe primed at `Compute`");
            check(probe.allowedMask()[id_of("Payoff")], "`Payoff` is allowed as a live prefix");
            check(probe.accept(id_of("Payoff")), "accept(`Payoff`)");
            check(!probe.allowedMask()[static_cast<std::uint32_t>('"')],
                  "...and the closing quote is then MASKED OUT: ComputePayoff cannot be produced");
            check(!probe.accept(static_cast<std::uint32_t>('"')),
                  "accept() refuses the masked quote rather than desyncing");
            check(probe.allowedMask()[static_cast<std::uint32_t>('T')],
                  "...while 'T' is allowed, because ComputePayoffTiming is real");
        }

        check(as_interface.accept(id_of("Payment")), "accept(`Payment`)");
        check(!as_interface.allowedMask()[eos], "EOS is still masked mid-object");
        for (const char c : std::string{
                 "\",\"rate\":\"0.005\",\"periods\":360,\"present_value\":\"300000.00\","
                 "\"future_value\":\"0.00\",\"timing\":\""}) {
            if (!as_interface.accept(static_cast<std::uint32_t>(c))) {
                check(false, std::string{"byte-wise accept failed at '"} + c + "'");
                break;
            }
        }
        check(as_interface.allowedMask()[id_of("END_OF_PERIOD")],
              "the enum constant token is allowed where the enum belongs");
        check(as_interface.accept(id_of("END_OF_PERIOD")), "accept(`END_OF_PERIOD`)");
        check(as_interface.accept(static_cast<std::uint32_t>('"')), "accept('\"')");
        check(as_interface.accept(static_cast<std::uint32_t>('}')), "accept('}')");
        check(!as_interface.isComplete(), "not complete until </params>");
        check(!as_interface.allowedMask()[eos], "EOS still masked before </params>");
        check(as_interface.accept(id_of("</params>")), "accept(`</params>`)");
        check(as_interface.isComplete(), "complete");
        check(as_interface.allowedMask()[eos], "EOS is allowed exactly now");
        check(grammar.text() ==
                  std::string{"<params>"} + kGoldPayment + "</params>",
              "the grammar's accumulated text is the whole params object");
        check(as_interface.accept(eos), "accept(EOS)");

        as_interface.reset();
        check(!as_interface.isComplete(), "reset() returns it to the start");
        check(grammar.text().empty(), "reset() clears the accumulated text");

        // The trigger-activated integration: arm the constraint on a prefix the
        // unconstrained decode already produced.
        mg::MortgageParamsGrammar armed{schema, vocab, eos};
        check(armed.prime("<params>{\"operation\":\"ComputeHomeFutureValue\",\""),
              "prime() replays an already-emitted prefix");
        check(!armed.allowedMask()[static_cast<unsigned char>('y')],
              "...and the armed grammar masks out 'y' (there is no `years` here)");
        check(armed.allowedMask()[id_of("current_")], "...while `current_` is allowed");
        check(!armed.prime("nonsense"), "prime() reports a prefix that is not on a valid path");
    }

    // ---------------------------------------------------------------------
    section("Gate 8 -- the regex projection compiles and enforces in sensen's own engine");
    // ---------------------------------------------------------------------
    {
        const auto pattern = mg::params_regex(schema);
        check(pattern.has_value(),
              pattern.has_value() ? ("params_regex() produced a " +
                                     std::to_string(pattern->size()) + "-character pattern")
                                  : ("params_regex() failed: " + pattern.error()));
        const auto relaxed = mg::Schema::build(
            mg::GrammarOptions{.wrap_in_params_tags = true, .require_declaration_order = false});
        if (relaxed.has_value()) {
            check(!mg::params_regex(*relaxed).has_value(),
                  "params_regex() REFUSES order-free mode rather than approximating it");
        }

        if (pattern.has_value()) {
            std::vector<std::string> bytes;
            bytes.reserve(128);
            for (int c = 0; c < 128; ++c) {
                bytes.emplace_back(1, static_cast<char>(c));
            }
            const auto compiled = sensen::RegexGrammar::create(*pattern, bytes, std::nullopt);
            check(compiled.has_value(),
                  compiled.has_value()
                      ? "sensen::RegexGrammar::create compiled the pattern"
                      : ("sensen::RegexGrammar::create REJECTED the pattern: " + compiled.error()));

            if (compiled.has_value()) {
                const auto drive = [&](std::string_view text) -> std::pair<bool, bool> {
                    sensen::RegexGrammar g = *compiled;
                    for (const char c : text) {
                        const auto id = static_cast<std::uint32_t>(static_cast<unsigned char>(c));
                        if (id >= bytes.size() || !g.allowedMask()[id] || !g.accept(id)) {
                            return {false, false};
                        }
                    }
                    return {true, g.isComplete()};
                };

                const auto whole = std::string{"<params>"} + kGoldPayment + "</params>";
                const auto [ok, done] = drive(whole);
                check(ok && done, "the regex accepts a whole gold object and reports it complete");
                check(!drive("<params>{\"operation\":\"ComputePayoff\"").first,
                      "the regex refuses ComputePayoff");
                check(!drive("<params>{\"operation\":\"ComputeHomeFutureValue\",\"years\"").first,
                      "the regex refuses `years` on ComputeHomeFutureValue");

                // EVERY gold row through sensen's own engine, not a sample:
                // the claim that this pattern is exactly as strict as the
                // automaton is only worth making if both accept the same set.
                std::size_t regex_accepted = 0;
                std::size_t sampled = 0;
                for (std::size_t i = 0; i < gold.params.size(); ++i) {
                    ++sampled;
                    const auto [a, c] = drive("<params>" + gold.params[i] + "</params>");
                    if (a && c) {
                        ++regex_accepted;
                    }
                }
                check(regex_accepted == sampled,
                      "sensen's RegexGrammar accepts " + std::to_string(regex_accepted) + "/" +
                          std::to_string(sampled) +
                          " gold objects -- the same set the automaton accepts");
            }
        }
    }

    std::printf("\n%d checks, %d failures\n", g_checks, g_failures);
    return g_failures == 0 ? 0 : 1;
}
