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
import sensen.encoder_reconstruct;
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
    const sensen::encoder_reconstruct::FieldValue& fval,
    const sensen::encoder_reconstruct::Schema& sch) -> std::string {

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
    const sensen::encoder_reconstruct::Schema& sch,
    const sensen::encoder_reconstruct::ReconstructedParams& rec) -> std::string {

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
        std::fprintf(stderr, "  --mask                restrict each literal's pairs to the "
                             "operation's admissible set\n");
        std::fprintf(stderr, "  --inject-distractors  add one inadmissible pair per literal "
                             "(the arm the mask must undo)\n");
        return 1;
    }

    const std::string schema_path = argv[1];
    const std::string fixture_path = argv[2];

    // Two optional arms, so one binary can produce the whole four-cell table that
    // makes the per-operation pair mask a MEASUREMENT rather than an assertion:
    //
    //                      no --mask        --mask
    //   plain fixture        600/600        600/600   <- the mask never removes a correct pair
    //   --inject-distractors  differs       600/600   <- and removes exactly the wrong ones
    //
    // The top-right cell is the positive control. Without it, "the mask fixed the
    // injected rows" is consistent with a mask that simply drops everything.
    bool apply_mask = false;
    bool inject = false;
    // Counted and reported on stderr, because an arm that cannot bite on part of the
    // corpus has to say which part rather than let a smaller number read as a pass.
    int injected_rows = 0;
    int uninjectable_rows = 0;
    for (int i = 3; i < argc; ++i) {
        const std::string_view flag{argv[i]};
        if (flag == "--mask") {
            apply_mask = true;
        } else if (flag == "--inject-distractors") {
            inject = true;
        } else {
            std::fprintf(stderr, "Unknown flag: %s\n", argv[i]);
            return 1;
        }
    }

    // 1. Load and parse Schema
    auto schema_text = read_file(schema_path);
    if (!schema_text) {
        std::fprintf(stderr, "Error reading schema file: %s\n", schema_text.error().c_str());
        return 1;
    }

    auto sch_res = sensen::encoder_reconstruct::Schema::from_json(*schema_text);
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

        auto lits_res = sensen::encoder_reconstruct::Literal::from_json_array(elem["lits"]);
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

        // --inject-distractors: give every literal one pair the operation does NOT admit,
        // which is what an unmasked head emitting a plausible-but-wrong pair looks like.
        //
        // THE CHOICE OF DISTRACTOR IS THE WHOLE ARM, AND THE FIRST ONE MEASURED NOTHING.
        // It took the lowest GLOBALLY inadmissible pair id, and that arm changed not one
        // byte of 600 rows -- because `reconstruct` already discards a pair whose SLOT is
        // not a field of the named operation, so a globally-inadmissible pair is almost
        // always harmless by construction. Reading that 0 as "the mask is redundant" would
        // have been wrong.
        //
        // What `op_pairs` actually restricts, measured on the schema: it is a strict SUBSET
        // of "every pair whose slot the operation has", in 19 of 28 operations -- the same
        // field with a MAP never observed for that operation. ComputeRate admits 3 of the 7
        // pairs on its own fields; ComputePeriods 3 of 8. THAT is the distractor reconstruct
        // acts on, because the slot survives the field filter and only the map is wrong.
        //
        // The lowest such id is taken, deliberately rather than at random: the arm must be
        // reproducible, and reconstruct resolves a contested slot by pair order, so a low id
        // is the HARDEST distractor to ignore rather than the easiest.
        //
        // On the other 9 operations `op_pairs` already equals that set, so no such distractor
        // EXISTS and those rows are unchanged. The probe counts them rather than hiding it:
        // an arm that cannot bite on a third of the corpus must say so.
        if (inject) {
            std::vector<int> admissible;
            std::vector<std::string> fields;
            if (op > 0 && static_cast<std::size_t>(op) < sch.ops.size()) {
                const auto& op_name = sch.ops[static_cast<std::size_t>(op)];
                if (const auto it = sch.op_pairs.find(op_name); it != sch.op_pairs.end()) {
                    admissible = it->second;
                }
                if (const auto it = sch.op_fields.find(op_name); it != sch.op_fields.end()) {
                    fields = it->second;
                }
            }
            std::ranges::sort(admissible);
            // Array fields are spelled "name[]" in op_fields and "name" in a pair's slot.
            const auto bare = [](std::string_view f) -> std::string_view {
                return f.ends_with("[]") ? f.substr(0, f.size() - 2) : f;
            };
            int distractor = -1;
            for (int pid = 0; pid < static_cast<int>(sch.pairs.size()); ++pid) {
                if (std::ranges::binary_search(admissible, pid)) continue;
                const std::string_view slot = bare(sch.pairs[static_cast<std::size_t>(pid)].first);
                const bool slot_is_a_field = std::ranges::any_of(
                    fields, [&](const std::string& f) { return bare(f) == slot; });
                if (slot_is_a_field) { distractor = pid; break; }
            }
            if (distractor >= 0) {
                ++injected_rows;
                for (auto& plist : lit_pairs) {
                    if (!std::ranges::contains(plist, distractor)) plist.push_back(distractor);
                    std::ranges::sort(plist);
                }
            } else {
                ++uninjectable_rows;
            }
        }

        // --mask: restrict every literal's pair set to what this operation admits.
        if (apply_mask) {
            bool masked_ok = true;
            for (auto& plist : lit_pairs) {
                auto m = sensen::encoder_reconstruct::maskPairsToOperation(
                    sch, op, std::span<const int>(plist));
                if (!m) {
                    std::printf("{\"row\":%d,\"error\":\"mask: %s\"}\n",
                                row_id, escape_json(m.error()).c_str());
                    masked_ok = false;
                    break;
                }
                plist = std::move(*m);
            }
            if (!masked_ok) continue;
        }

        // Execute reconstruct
        auto res = sensen::encoder_reconstruct::reconstruct(sch, op, *lits_res, lit_pairs, conv);
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
    if (inject) {
        std::fprintf(stderr,
                     "[inject] a same-slot wrong-map distractor was planted in %d rows; "
                     "%d rows admit no such pair (their op_pairs already equals every pair "
                     "on their own fields), so the arm cannot reach them\n",
                     injected_rows, uninjectable_rows);
    }
    return 0;
}
