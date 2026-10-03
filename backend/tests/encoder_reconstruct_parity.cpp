/**
 * @file encoder_reconstruct_parity.cpp
 * @brief Parity harness proving the C++ encoder_reconstruct module reproduces
 *        Python's reconstruct() in agent/train/encoder_corpus.py exactly.
 *
 * @author Olumuyiwa Oluwasanmi
 *
 * ===========================================================================
 * PURPOSE AND ARCHITECTURE
 * ===========================================================================
 * In the small encoder assistant architecture, the model never autoregressively
 * generates digits; it only identifies input spans (numeric literals) and
 * points to transformation mechanisms (maps) or convention classes.
 *
 * All numeric parameter computation is therefore performed exclusively on the
 * serving side by `reconstruct()`. To ensure zero serving-side drift, this
 * program loads an exported schema JSON and an evaluation fixture JSON array,
 * executes C++ reconstruction for each row, and outputs canonical JSON:
 *
 *   {"row":<int>,"params":<reconstructed_params_or_null>}
 *
 * or on per-row evaluation failure:
 *
 *   {"row":<int>,"error":"<message>"}
 *
 * ---------------------------------------------------------------------------
 * CANONICAL DECIMAL SERIALIZATION (CRITICAL AMBIGUITY NOTE)
 * ---------------------------------------------------------------------------
 * In Python's `encoder_corpus.py`, `reconstruct()` produces numbers as
 * `decimal.Decimal` instances. When dumped to JSON via `default=str` in `C_eq`,
 * Python renders each Decimal with its dynamic operand scale (e.g. "300000" or
 * "300000.00" depending on original literal formatting, and 60 fractional digits
 * for divisions under getcontext().prec = 60).
 *
 * In C++, `sensen::BigDecimal` is an exact fixed-point decimal arithmetic type
 * with storage word `Int256` scaled by 10^38 (SCALE = 38). It intentionally
 * does not track lexical input formatting, and `BigDecimal::to_string()`
 * emits exactly 38 fractional digits (e.g. "300000.000000000000000000000000000000000000").
 *
 * Because the textual scale of Python's Decimal cannot be inferred from a
 * fixed-point 38-place BigDecimal, the canonical textual form is AMBIGUOUS.
 * Per the project requirement ("If the canonical form is ambiguous, print the
 * value exactly as BigDecimal::to_string gives it and SAY SO in a comment, so
 * the harness can normalise rather than silently disagree"), all numbers are
 * printed here exactly as `BigDecimal::to_string()` produces them. The Python
 * comparison harness normalizes both sides (e.g., via Decimal(got) == Decimal(expected)
 * or `params_match` / `_num_eq`) rather than expecting byte-level agreement on
 * arbitrary trailing zeros.
 *
 * ---------------------------------------------------------------------------
 * NO EXTERNAL TEST FRAMEWORKS (Rule 39)
 * ---------------------------------------------------------------------------
 * Conforms strictly to config/cpp_details.txt rule 39: internal test style with
 * standard library utilities and fastjson, avoiding GTest/Catch2.
 * ===========================================================================
 */

// `import std;` exports no macros, and `stderr`/`stdout` require <cstdio>.
// <new> is an ODR anchor required by this repository's module conventions.
#include <cstdio>
#include <new>

import std;
import encoder_reconstruct;
import fastjson;
import sensen.bigdecimal;

namespace {

/**
 * Escapes characters in a string for safe inclusion in a JSON string literal.
 */
[[nodiscard]] auto escape_json(std::string_view s) -> std::string {
    std::string out;
    out.reserve(s.size() + 8);
    for (char c : s) {
        switch (c) {
            case '"':  out += "\\\""; break;
            case '\\': out += "\\\\"; break;
            case '\b': out += "\\b"; break;
            case '\f': out += "\\f"; break;
            case '\n': out += "\\n"; break;
            case '\r': out += "\\r"; break;
            case '\t': out += "\\t"; break;
            default:
                if (static_cast<unsigned char>(c) < 0x20) {
                    out += std::format("\\u{:04x}", static_cast<unsigned int>(static_cast<unsigned char>(c)));
                } else {
                    out += c;
                }
                break;
        }
    }
    return out;
}

/**
 * Reads an entire file from disk into a std::string.
 */
[[nodiscard]] auto read_file(const std::filesystem::path& path) -> std::expected<std::string, std::string> {
    std::error_code ec;
    if (!std::filesystem::exists(path, ec)) {
        return std::unexpected(std::format("File does not exist: '{}'", path.string()));
    }
    std::ifstream in(path, std::ios::binary);
    if (!in.is_open()) {
        return std::unexpected(std::format("Failed to open file: '{}'", path.string()));
    }
    std::ostringstream ss;
    ss << in.rdbuf();
    return ss.str();
}

/**
 * Extracts an integer from a fastjson::json_value.
 *
 * REFUSES rather than defaulting, and that is the whole point of the optional.
 * A first version returned 0 for a value it could not read -- and 0 is `<NONE>`
 * in this schema's operation vocabulary, so a fixture whose `op` failed to parse
 * would have reported "no operation" and compared EQUAL to a null-params row.
 * A default that looks like an answer is the failure this repository records
 * against the LIVE badge and against `sensen::macrs` returning 0.0 both for "no
 * charge this year" and "I have no table for that class".
 *
 * fastjson has no `is_int64()`; its numeric variants are `json_number` (double),
 * `json_number_128`, `json_int_128` and `json_uint_128`, and `as_int64()`
 * narrows whichever is held.
 */
[[nodiscard]] auto get_int(const fastjson::json_value& v) -> std::optional<int> {
    if (v.is_number() || v.is_number_128() || v.is_int_128() || v.is_uint_128()) {
        return static_cast<int>(v.as_int64());
    }
    return std::nullopt;
}

/**
 * Formats a FieldValue into compact JSON.
 *
 * Numbers are rendered as quoted decimal strings matching the wire protocol
 * and Python's json.dumps(..., default=str) behavior.
 */
[[nodiscard]] auto format_field_value(
    std::string_view key,
    const encoder_reconstruct::FieldValue& fval,
    const encoder_reconstruct::Schema& sch) -> std::string {

    if (fval.is_missing()) {
        return "null";
    }

    if (fval.is_string()) {
        auto s = *fval.as_string();
        // Categorical booleans serialize as unquoted JSON literals `true`/`false`.
        if (s == "true" || s == "false") {
            std::string key_str(key);
            auto it = sch.field_kind.find(key_str);
            if (it == sch.field_kind.end()) {
                // Check if the schema field was declared with the array suffix "[]"
                it = sch.field_kind.find(key_str + "[]");
            }
            if (it != sch.field_kind.end() && it->second == "cat") {
                return std::string(s);
            }
        }
        return "\"" + escape_json(s) + "\"";
    }

    if (fval.is_decimal()) {
        // As documented in the file header comment, sensen::BigDecimal has fixed
        // 38-decimal-place scale. We output BigDecimal::to_string() directly.
        return "\"" + fval.as_decimal()->to_string() + "\"";
    }

    if (fval.is_array()) {
        std::string arr_str = "[";
        auto arr = *fval.as_array();
        for (std::size_t i = 0; i < arr.size(); ++i) {
            if (i > 0) arr_str += ",";
            arr_str += "\"" + arr[i].to_string() + "\"";
        }
        arr_str += "]";
        return arr_str;
    }

    return "null";
}

/**
 * Formats the reconstructed parameters object with alphabetically sorted keys
 * and zero insignificant whitespace.
 */
[[nodiscard]] auto format_params(
    const encoder_reconstruct::Schema& sch,
    const encoder_reconstruct::ReconstructedParams& rec) -> std::string {

    std::string out = "{";

    // Collect all field keys plus the operation discriminator key (sch.op_key).
    std::vector<std::string> keys;
    keys.reserve(rec.fields.size() + 1);
    keys.push_back(sch.op_key);
    for (const auto& [k, _] : rec.fields) {
        keys.push_back(k);
    }
    std::ranges::sort(keys);

    for (std::size_t i = 0; i < keys.size(); ++i) {
        if (i > 0) out += ",";
        const auto& key = keys[i];
        out += "\"" + escape_json(key) + "\":";
        if (key == sch.op_key) {
            out += "\"" + escape_json(rec.operation) + "\"";
        } else {
            const auto& fval = rec.fields.at(key);
            out += format_field_value(key, fval, sch);
        }
    }

    out += "}";
    return out;
}

}  // namespace

auto main(int argc, char** argv) -> int {
    if (argc < 3) {
        std::fprintf(stderr, "Usage: %s <schema.json> <fixture.json>\n", argv[0]);
        std::fprintf(stderr, "  argv[1]: path to schema JSON file (e.g. exported sensen-encoder.schema_json)\n");
        std::fprintf(stderr, "  argv[2]: path to fixture JSON file (array of holdout rows)\n");
        return 1;
    }

    const std::string schema_path = argv[1];
    const std::string fixture_path = argv[2];

    // 1. Load and parse Schema
    auto schema_text = read_file(schema_path);
    if (!schema_text) {
        std::fprintf(stderr, "Error reading schema file: %s\n", schema_text.error().c_str());
        return 1;
    }

    auto sch_res = encoder_reconstruct::Schema::from_json(*schema_text);
    if (!sch_res) {
        std::fprintf(stderr, "Error parsing schema JSON: %s\n", sch_res.error().c_str());
        return 1;
    }
    const auto& sch = *sch_res;

    // 2. Load and parse Fixture
    auto fixture_text = read_file(fixture_path);
    if (!fixture_text) {
        std::fprintf(stderr, "Error reading fixture file: %s\n", fixture_text.error().c_str());
        return 1;
    }

    auto fixture_parse = fastjson::parse(*fixture_text);
    if (!fixture_parse) {
        std::fprintf(stderr, "Error parsing fixture JSON: %s\n", fixture_parse.error().to_string().c_str());
        return 1;
    }

    if (!fixture_parse->is_array()) {
        std::fprintf(stderr, "Error: Fixture JSON root must be an array of row objects\n");
        return 1;
    }

    // 3. Process each row in the fixture
    for (const auto& elem : fixture_parse->as_array()) {
        if (!elem.is_object()) {
            std::printf("{\"row\":-1,\"error\":\"Fixture array element is not an object\"}\n");
            continue;
        }

        int row_id = -1;
        if (elem.contains("row")) {
            row_id = get_int(elem["row"]).value_or(-1);
        }

        if (!elem.contains("op")) {
            std::printf("{\"row\":%d,\"error\":\"Fixture element missing required 'op' field\"}\n", row_id);
            continue;
        }
        const auto op_opt = get_int(elem["op"]);
        if (!op_opt) {
            std::printf("{\"row\":%d,\"error\":\"Fixture 'op' is not a number\"}\n", row_id);
            continue;
        }
        const int op = *op_opt;

        if (!elem.contains("lits")) {
            std::printf("{\"row\":%d,\"error\":\"Fixture element missing required 'lits' field\"}\n", row_id);
            continue;
        }

        auto lits_res = encoder_reconstruct::Literal::from_json_array(elem["lits"]);
        if (!lits_res) {
            std::printf("{\"row\":%d,\"error\":\"Failed to parse literals: %s\"}\n",
                        row_id, escape_json(lits_res.error()).c_str());
            continue;
        }

        std::vector<std::vector<int>> lit_pairs;
        if (elem.contains("lit_pairs") && elem["lit_pairs"].is_array()) {
            for (const auto& sub : elem["lit_pairs"].as_array()) {
                std::vector<int> pids;
                if (sub.is_array()) {
                    for (const auto& p : sub.as_array()) {
                        const auto pid = get_int(p);
                        if (!pid) {
                            std::fprintf(stderr,
                                "row %d: lit_pairs entry is not a number\n", row_id);
                            return 2;
                        }
                        pids.push_back(*pid);
                    }
                }
                lit_pairs.push_back(std::move(pids));
            }
        }

        std::unordered_map<std::string, int> conv;
        if (elem.contains("conv") && elem["conv"].is_object()) {
            for (const auto& [field, idx_val] : elem["conv"].as_object()) {
                const auto idx = get_int(idx_val);
                if (!idx) {
                    std::fprintf(stderr,
                        "row %d: conv['%s'] is not a number\n", row_id, std::string(field).c_str());
                    return 2;
                }
                conv[std::string(field)] = *idx;
            }
        }

        // Execute reconstruct
        auto res = encoder_reconstruct::reconstruct(sch, op, *lits_res, lit_pairs, conv);
        if (!res) {
            std::printf("{\"row\":%d,\"error\":\"%s\"}\n",
                        row_id, escape_json(res.error()).c_str());
            continue;
        }

        if (!res->has_value()) {
            // op == 0 (<NONE>) or unfulfilled reconstruct yields null params
            std::printf("{\"row\":%d,\"params\":null}\n", row_id);
            continue;
        }

        const auto& rec = **res;
        std::string params_json = format_params(sch, rec);
        std::printf("{\"row\":%d,\"params\":%s}\n", row_id, params_json.c_str());
    }

    std::fflush(stdout);
    return 0;
}
