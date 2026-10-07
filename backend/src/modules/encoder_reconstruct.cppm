// @author Olumuyiwa Oluwasanmi
//
// ===========================================================================
// ENCODER RECONSTRUCTION ENGINE: SERVING-SIDE VALUE REBUILDING
// ===========================================================================
//
// A faithful C++23 port of the serving-side reconstruction pipeline defined in
// `agent/train/encoder_corpus.py`.
//
// ---------------------------------------------------------------------------
// WHY THIS EXISTS AND WHY RECONSTRUCTION BEATS DECODER GENERATION
// ---------------------------------------------------------------------------
// A generative decoder (such as fine-tuned Qwen3-0.6B) reproduces financial
// numbers by generating their digits token-by-token during autoregressive
// generation. Measured extensively across this codebase, this is the primary
// source of catastrophic runtime failure in financial parameter extraction:
//
//   1. 55.2% of decoder failures were numeric mis-transcription: the model
//      identified the operation and slot correctly, but hallucinated digits
//      (e.g., pricing a mortgage with payment 5379.00 instead of 5378.63, or
//      computing 5.6/12 instead of 5.64/12).
//   2. The numbers emitted by decoders are structurally indistinguishable from
//      valid numbers -- they parse as valid decimals, satisfy all schema bounds,
//      and price a completely different loan.
//
// An encoder model cannot generate digits; it only POINTS at numeric literals
// already grounded in the user's utterance and NAMES a deterministic
// transformation (the "map"), or selects a low-cardinality convention class.
//
// Therefore, the serving layer is the SOLE place numeric values are computed.
// This design factorises the extraction problem into:
//   - (literal text, admissible map) -> exact calculated value
//   - (convention class index)       -> canonical constant value
//
// ---------------------------------------------------------------------------
// MANDATORY EXACT DECIMAL ARITHMETIC (sensen::BigDecimal)
// ---------------------------------------------------------------------------
// The foundational premise of this architecture is that numbers are COMPUTED
// from literal text. Using IEEE binary floating-point (`double`) would introduce
// rounding errors (e.g. 1.1 becomes 1.100000000000000128 in double, corrupting
// amortization schedules).
//
// Consequently, this module uses `sensen::BigDecimal` for all arithmetic.
// Every division, multiplication, subtraction, and complement is computed in
// exact fixed-point decimal arithmetic.
//
// ---------------------------------------------------------------------------
// THE SCHEMA IS PARSED FROM JSON, NEVER HARD-CODED
// ---------------------------------------------------------------------------
// Hard-coded label tables in C++ modules create the "four-tables drift" defect
// previously documented across this repository. In this architecture, the schema
// is exported by the trainer directly into the GGUF model metadata under key
// 'sensen-encoder.schema_json'.
//
// This module parses the schema at runtime from JSON using the vendored
// `fastjson` library (`backend/sensen/external/fastestjsoninthewest`).
// A hand-written copy of the label space is strictly forbidden.
//
// ---------------------------------------------------------------------------
// THE ADMISSIBLE MAPS: STRICT FAIL-CLOSED REFUSALS
// ---------------------------------------------------------------------------
// Empirical analysis of the mortgage training corpus demonstrated that out of
// 28 candidate maps in the reference extractability study, a small closed set
// of role strings is used by the mortgage label space. It was 11 until
// 2026-10-06, when the visitor-style corpus (agent/dataset/visitor_phrasing.py)
// taught four more that a visitor's own phrasing needs, each deterministic
// arithmetic on one or two literals:
//
//   Unary:
//     1.  "M1 identity"            v
//     2.  "M1 identity#repN"       v repeated N times, 2 <= N <= 64 (payback uses 20; "$12,000
//                                  a year for 3 years" is #rep3)                         [was #rep20 only]
//     3.  "M2 percent/100"         v / 100
//     4.  "M3 annual%->monthly"    (v / 100) / 12
//     5.  "M3 annual%->quarterly"  (v / 100) / 4                                         [NEW]
//     6.  "M5 years->months"       v * 12
//     7.  "M5 years->quarters"     v * 4                                                 [NEW]
//     8.  "M8 negate"              -v
//     9.  "M10 complement%"        1 - (v / 100)
//
//   Binary:
//     10. "M9 a-b#A/#B"            a - b
//     11. "M9 a*(1-p)#A/#B"        a * (1 - b / 100)
//     12. "M9 a*p#A/#B"            a * (b / 100)   ("25% down on a $450,000 rental")     [NEW]
//
// REFUSAL RULE:
// Any other role string encountered during reconstruction or schema processing
// is an immediate REFUSAL (`std::unexpected`), NEVER a silent skip. A silently
// skipped pair would cause the field to fall back to an unstated default or be
// omitted, producing a plausible wrong parameter set -- the exact failure class
// this repository refuses.
// ===========================================================================

module;

export module encoder_reconstruct;

import std;
import sensen.bigdecimal;
import fastjson;

export namespace encoder_reconstruct {

// ===========================================================================
// CONSTANTS
// ===========================================================================

/** Suffix used in schema field names to denote array-typed slots. */
constexpr std::string_view kArraySuffix = "[]";

/** Sentinel operation indicating absence of parameters (e.g. out of scope). */
constexpr std::string_view kNoneOp = "<NONE>";

// ===========================================================================
// 1. ROLE DEFINITIONS AND PARSING
// ===========================================================================

/** Role category distinguishing unary transformations, array repetitions,
 *  and binary operands. */
enum class RoleKind : std::uint8_t {
    Unary,  ///< Unary transformation (e.g. M1 identity, M2 percent/100)
    Rep,    ///< Unary transformation with repetition count (e.g. #rep20)
    A,      ///< Operand A of binary transformation (e.g. M9 a-b#A)
    B       ///< Operand B of binary transformation (e.g. M9 a-b#B)
};

/** Decomposed role specification. */
struct ParsedRole {
    std::string name;                  ///< Base map name (e.g. "M1 identity", "M9 a-b")
    RoleKind kind{RoleKind::Unary};    ///< Role kind
    int rep{1};                        ///< Repetition count (1 for Unary, A, B; 20 for rep20)
    std::string raw_role;              ///< Original role string

    [[nodiscard]] auto kind_str() const noexcept -> std::string_view {
        switch (kind) {
            case RoleKind::Unary: return "unary";
            case RoleKind::Rep:   return "rep";
            case RoleKind::A:     return "A";
            case RoleKind::B:     return "B";
        }
    }

    auto operator==(const ParsedRole& other) const noexcept -> bool = default;
};

/**
 * Parse and validate a role string against the closed set of mortgage maps (the unary ones, the
 * repeated identity and the three binary ones).
 *
 * REFUSES any role string outside that set rather than guessing.
 */
[[nodiscard]] auto parse_role(std::string_view role)
    -> std::expected<ParsedRole, std::string> {
    const auto unary = [&](std::string_view name) {
        return ParsedRole{.name = std::string{name}, .kind = RoleKind::Unary, .rep = 1,
                          .raw_role = std::string{role}};
    };
    for (const std::string_view name : {"M1 identity", "M2 percent/100", "M3 annual%->monthly",
                                        "M3 annual%->quarterly", "M5 years->months",
                                        "M5 years->quarters", "M8 negate", "M10 complement%"}) {
        if (role == name) { return unary(name); }
    }
    // A repeated identity: "M1 identity#repN". N is bounded, and parsed rather than looked up, so
    // a count the corpus teaches (2 for "$300 a month for 2 years", 20 for a payback) needs no
    // code change -- while a count nobody could mean (0, 1, 65, "x") is still refused below.
    constexpr std::string_view kRepPrefix = "M1 identity#rep";
    if (role.starts_with(kRepPrefix)) {
        int n = 0;
        const auto digits = role.substr(kRepPrefix.size());
        const auto [ptr, ec] = std::from_chars(digits.data(), digits.data() + digits.size(), n);
        if (ec == std::errc{} && ptr == digits.data() + digits.size() && n >= 2 && n <= 64) {
            return ParsedRole{.name = "M1 identity", .kind = RoleKind::Rep, .rep = n,
                              .raw_role = std::string{role}};
        }
    }
    for (const std::string_view name : {"M9 a-b", "M9 a*(1-p)", "M9 a*p"}) {
        if (role.size() == name.size() + 2 && role.starts_with(name) && role[name.size()] == '#') {
            const char operand = role.back();
            if (operand == 'A') {
                return ParsedRole{.name = std::string{name}, .kind = RoleKind::A, .rep = 1,
                                  .raw_role = std::string{role}};
            }
            if (operand == 'B') {
                return ParsedRole{.name = std::string{name}, .kind = RoleKind::B, .rep = 1,
                                  .raw_role = std::string{role}};
            }
        }
    }

    return std::unexpected(std::format(
        "Refusing unapproved role string '{}': the mortgage schema allows only its closed set of maps",
        role));
}

// ===========================================================================
// 2. LITERAL DATA STRUCTURES
// ===========================================================================

/** A numeric literal identified in the input utterance. */
struct Literal {
    sensen::BigDecimal value;
    std::string tag{"bare"};
    std::size_t start{0};
    std::size_t end{0};
    std::string text;

    constexpr Literal() noexcept = default;

    Literal(sensen::BigDecimal val, std::string t = "bare", std::size_t s = 0,
            std::size_t e = 0, std::string txt = {})
        : value(std::move(val)), tag(std::move(t)), start(s), end(e), text(std::move(txt)) {}

    /** Construct a Literal directly from raw text by parsing with BigDecimal. */
    [[nodiscard]] static auto from_text(std::string_view raw_text, std::string_view t = "bare",
                                        std::size_t s = 0, std::size_t e = 0)
        -> std::expected<Literal, std::string> {
        auto dec = sensen::BigDecimal::try_parse(raw_text);
        if (!dec) {
            return std::unexpected(std::format(
                "Failed to parse literal text '{}' into BigDecimal: {}", raw_text, dec.error()));
        }
        return Literal(*dec, std::string(t), s, e, std::string(raw_text));
    }

    /** Parse a literal from its JSON dictionary representation. */
    [[nodiscard]] static auto from_json(const fastjson::json_value& j)
        -> std::expected<Literal, std::string> {
        if (!j.is_object()) {
            return std::unexpected("Literal JSON must be an object");
        }
        Literal lit;
        if (j.contains("text") && j["text"].is_string()) {
            lit.text = std::string(j["text"].as_string());
        }
        if (j.contains("tag") && j["tag"].is_string()) {
            lit.tag = std::string(j["tag"].as_string());
        }
        if (j.contains("start")) {
            lit.start = static_cast<std::size_t>(j["start"].as_int64());
        }
        if (j.contains("end")) {
            lit.end = static_cast<std::size_t>(j["end"].as_int64());
        }

        if (j.contains("value")) {
            const auto& val_node = j["value"];
            if (val_node.is_string()) {
                auto dec = sensen::BigDecimal::try_parse(val_node.as_string());
                if (!dec) {
                    return std::unexpected(std::format(
                        "Literal 'value' '{}' failed BigDecimal parse: {}",
                        val_node.as_string(), dec.error()));
                }
                lit.value = *dec;
            } else if (val_node.is_number()) {
                lit.value = sensen::BigDecimal(val_node.as_number());
            } else {
                return std::unexpected("Literal 'value' must be a decimal string or number");
            }
        } else if (!lit.text.empty()) {
            auto dec = sensen::BigDecimal::try_parse(lit.text);
            if (dec) {
                lit.value = *dec;
            } else {
                return std::unexpected(std::format(
                    "Literal missing 'value' and 'text' ('{}') is not a clean decimal: {}",
                    lit.text, dec.error()));
            }
        } else {
            return std::unexpected("Literal JSON missing required 'value' field");
        }

        return lit;
    }

    /** Parse an array of literals from JSON. */
    [[nodiscard]] static auto from_json_array(const fastjson::json_value& arr)
        -> std::expected<std::vector<Literal>, std::string> {
        if (!arr.is_array()) {
            return std::unexpected("Literals JSON must be an array");
        }
        std::vector<Literal> lits;
        lits.reserve(arr.size());
        for (const auto& item : arr.as_array()) {
            auto l = from_json(item);
            if (!l) {
                return std::unexpected(l.error());
            }
            lits.push_back(std::move(*l));
        }
        return lits;
    }

    auto operator==(const Literal& other) const noexcept -> bool = default;
};

/** Association between a literal index and a role string assigned to a slot. */
struct SlotEntry {
    int lit_idx{0};
    std::string role;

    auto operator==(const SlotEntry& other) const noexcept -> bool = default;
};

/** Scoring function signature used for literal pair arbitration. */
using ScoreFn = std::function<double(int li, std::string_view slot, std::string_view role)>;

/** Default score function: more recent literals receive higher positional priority. */
[[nodiscard]] inline auto default_score_fn(int li, std::string_view, std::string_view) noexcept
    -> double {
    return static_cast<double>(li);
}

// ===========================================================================
// 3. SCHEMA CLASS
// ===========================================================================

/**
 * Complete encoder-server label contract and lookup tables.
 *
 * Populated dynamically from JSON metadata embedded in the GGUF model.
 */
class Schema {
  public:
    std::string op_key;
    std::string question_mode;
    std::vector<std::string> ops;
    std::unordered_map<std::string, std::vector<std::string>> op_fields;
    std::unordered_map<std::string, std::string> field_kind;
    std::vector<std::pair<std::string, std::string>> pairs;
    std::unordered_map<std::string, std::vector<int>> op_pairs;
    std::vector<std::string> conv_fields;
    std::unordered_map<std::string, std::vector<std::string>> conv_vocab;
    std::unordered_map<std::string, std::unordered_map<std::string, std::vector<int>>> conv_op_mask;
    std::unordered_map<std::string, std::string> const_default;
    std::unordered_map<std::string, std::unordered_map<std::string, std::string>> op_default;
    std::unordered_map<std::string, std::unordered_map<std::string, double>> compat;
    std::unordered_map<std::string, std::string> anchor;
    std::unordered_map<std::string, double> params;

    Schema() = default;

    /**
     * Parse the complete schema from a JSON string view using fastjson.
     */
    [[nodiscard]] static auto from_json(std::string_view json_text)
        -> std::expected<Schema, std::string> {
        auto parse_res = fastjson::parse(json_text);
        if (!parse_res.has_value()) {
            return std::unexpected(std::format(
                "Schema JSON parse failed: {}", parse_res.error().to_string()));
        }
        const auto& root = *parse_res;
        if (!root.is_object()) {
            return std::unexpected("Schema JSON root must be an object");
        }

        Schema s;

        // 1. op_key
        if (!root.contains("op_key") || !root["op_key"].is_string()) {
            return std::unexpected("Schema JSON missing required string field 'op_key'");
        }
        s.op_key = std::string(root["op_key"].as_string());

        // 2. question_mode
        if (!root.contains("question_mode") || !root["question_mode"].is_string()) {
            return std::unexpected("Schema JSON missing required string field 'question_mode'");
        }
        s.question_mode = std::string(root["question_mode"].as_string());

        // 3. ops
        if (!root.contains("ops") || !root["ops"].is_array()) {
            return std::unexpected("Schema JSON missing required array field 'ops'");
        }
        for (const auto& item : root["ops"].as_array()) {
            if (!item.is_string()) {
                return std::unexpected("Schema JSON 'ops' array elements must be strings");
            }
            s.ops.push_back(std::string(item.as_string()));
        }

        // 4. op_fields
        if (!root.contains("op_fields") || !root["op_fields"].is_object()) {
            return std::unexpected("Schema JSON missing required object field 'op_fields'");
        }
        for (const auto& [k, v] : root["op_fields"].as_object()) {
            if (!v.is_array()) {
                return std::unexpected(std::format(
                    "Schema JSON 'op_fields' for op '{}' must be an array", k));
            }
            std::vector<std::string> flds;
            for (const auto& f : v.as_array()) {
                if (!f.is_string()) {
                    return std::unexpected(std::format(
                        "Schema JSON 'op_fields' element for op '{}' must be string", k));
                }
                flds.push_back(std::string(f.as_string()));
            }
            s.op_fields[k] = std::move(flds);
        }

        // 5. field_kind
        if (!root.contains("field_kind") || !root["field_kind"].is_object()) {
            return std::unexpected("Schema JSON missing required object field 'field_kind'");
        }
        for (const auto& [k, v] : root["field_kind"].as_object()) {
            if (!v.is_string()) {
                return std::unexpected(std::format(
                    "Schema JSON 'field_kind' for field '{}' must be a string", k));
            }
            s.field_kind[k] = std::string(v.as_string());
        }

        // 6. pairs
        if (!root.contains("pairs") || !root["pairs"].is_array()) {
            return std::unexpected("Schema JSON missing required array field 'pairs'");
        }
        for (std::size_t i = 0; i < root["pairs"].as_array().size(); ++i) {
            const auto& p = root["pairs"].as_array()[i];
            if (!p.is_array() || p.as_array().size() < 2 ||
                !p.as_array()[0].is_string() || !p.as_array()[1].is_string()) {
                return std::unexpected(std::format(
                    "Schema JSON 'pairs' at index {} must be a 2-element string array [slot, role]", i));
            }
            s.pairs.emplace_back(
                std::string(p.as_array()[0].as_string()),
                std::string(p.as_array()[1].as_string()));
        }

        // 7. op_pairs
        if (root.contains("op_pairs") && root["op_pairs"].is_object()) {
            for (const auto& [k, v] : root["op_pairs"].as_object()) {
                if (v.is_array()) {
                    std::vector<int> pids;
                    for (const auto& pid : v.as_array()) {
                        pids.push_back(static_cast<int>(pid.as_int64()));
                    }
                    s.op_pairs[k] = std::move(pids);
                }
            }
        }

        // 8. conv_fields
        if (root.contains("conv_fields") && root["conv_fields"].is_array()) {
            for (const auto& item : root["conv_fields"].as_array()) {
                if (item.is_string()) {
                    s.conv_fields.push_back(std::string(item.as_string()));
                }
            }
        }

        // 9. conv_vocab
        if (root.contains("conv_vocab") && root["conv_vocab"].is_object()) {
            for (const auto& [k, v] : root["conv_vocab"].as_object()) {
                if (v.is_array()) {
                    std::vector<std::string> vocab;
                    for (const auto& item : v.as_array()) {
                        if (item.is_string()) {
                            vocab.push_back(std::string(item.as_string()));
                        } else {
                            vocab.push_back(item.to_string());
                        }
                    }
                    s.conv_vocab[k] = std::move(vocab);
                }
            }
        }

        // 10. conv_op_mask
        if (root.contains("conv_op_mask") && root["conv_op_mask"].is_object()) {
            for (const auto& [field, ops_obj] : root["conv_op_mask"].as_object()) {
                if (ops_obj.is_object()) {
                    std::unordered_map<std::string, std::vector<int>> op_mask;
                    for (const auto& [op, mask_arr] : ops_obj.as_object()) {
                        if (mask_arr.is_array()) {
                            std::vector<int> ids;
                            for (const auto& id_val : mask_arr.as_array()) {
                                ids.push_back(static_cast<int>(id_val.as_int64()));
                            }
                            op_mask[op] = std::move(ids);
                        }
                    }
                    s.conv_op_mask[field] = std::move(op_mask);
                }
            }
        }

        // 11. const_default
        if (root.contains("const_default") && root["const_default"].is_object()) {
            for (const auto& [k, v] : root["const_default"].as_object()) {
                if (v.is_string()) {
                    s.const_default[k] = std::string(v.as_string());
                } else {
                    s.const_default[k] = v.to_string();
                }
            }
        }

        // 12. op_default
        if (root.contains("op_default") && root["op_default"].is_object()) {
            for (const auto& [op, f_obj] : root["op_default"].as_object()) {
                if (f_obj.is_object()) {
                    std::unordered_map<std::string, std::string> defaults;
                    for (const auto& [f, v] : f_obj.as_object()) {
                        if (v.is_string()) {
                            defaults[f] = std::string(v.as_string());
                        } else {
                            defaults[f] = v.to_string();
                        }
                    }
                    s.op_default[op] = std::move(defaults);
                }
            }
        }

        // 13. compat
        if (root.contains("compat") && root["compat"].is_object()) {
            for (const auto& [field, map_obj] : root["compat"].as_object()) {
                if (map_obj.is_object()) {
                    std::unordered_map<std::string, double> shares;
                    for (const auto& [tm, share_val] : map_obj.as_object()) {
                        shares[tm] = share_val.as_number();
                    }
                    s.compat[field] = std::move(shares);
                }
            }
        }

        // 14. anchor
        if (root.contains("anchor") && root["anchor"].is_object()) {
            for (const auto& [op, anc] : root["anchor"].as_object()) {
                if (anc.is_string()) {
                    s.anchor[op] = std::string(anc.as_string());
                }
            }
        }

        // 15. params
        if (root.contains("params") && root["params"].is_object()) {
            for (const auto& [k, v] : root["params"].as_object()) {
                s.params[k] = v.as_number();
            }
        }

        s.reindex();
        return s;
    }

    /**
     * Compute derived indexing lookups from raw schema arrays.
     */
    auto reindex() -> void {
        pid_.clear();
        for (std::size_t i = 0; i < pairs.size(); ++i) {
            pid_[pairs[i]] = static_cast<int>(i);
        }

        oidx_.clear();
        for (std::size_t i = 0; i < ops.size(); ++i) {
            oidx_[ops[i]] = static_cast<int>(i);
        }

        cset_.clear();
        for (const auto& [n, v] : conv_vocab) {
            cset_[n] = std::unordered_set<std::string>(v.begin(), v.end());
        }
        for (const auto& [n, v] : const_default) {
            if (!cset_.contains(n)) {
                cset_[n] = std::unordered_set<std::string>{v};
            }
        }
    }

    /** Lookup index of a (slot, role) pair. */
    [[nodiscard]] auto pair_id(std::string_view slot, std::string_view role) const
        -> std::optional<int> {
        auto it = pid_.find({std::string(slot), std::string(role)});
        if (it != pid_.end()) {
            return it->second;
        }
        return std::nullopt;
    }

    /** Return the complete (slot, role) -> pair_id mapping. */
    [[nodiscard]] auto pair_id() const
        -> const std::map<std::pair<std::string, std::string>, int>& {
        return pid_;
    }

    /** Lookup index of an operation name. */
    [[nodiscard]] auto op_index(std::string_view op) const -> std::optional<int> {
        auto it = oidx_.find(std::string(op));
        if (it != oidx_.end()) {
            return it->second;
        }
        return std::nullopt;
    }

    /** Return the complete op_name -> op_index mapping. */
    [[nodiscard]] auto op_index() const -> const std::unordered_map<std::string, int>& {
        return oidx_;
    }

    /** Return the set of convention string values for a field. */
    [[nodiscard]] auto conv_values(std::string_view name) const
        -> const std::unordered_set<std::string>& {
        auto it = cset_.find(std::string(name));
        if (it != cset_.end()) {
            return it->second;
        }
        return kEmptySet_;
    }

    /** Return the anchor array field name for an operation, if defined. */
    [[nodiscard]] auto get_anchor(std::string_view op_name) const
        -> std::optional<std::string_view> {
        auto it = anchor.find(std::string(op_name));
        if (it != anchor.end()) {
            return std::string_view(it->second);
        }
        return std::nullopt;
    }

    [[nodiscard]] auto n_ops() const noexcept -> std::size_t {
        return ops.size();
    }

    [[nodiscard]] auto n_pairs() const noexcept -> std::size_t {
        return pairs.size();
    }

    [[nodiscard]] auto n_conv() const noexcept -> std::size_t {
        return conv_fields.size();
    }

  private:
    std::map<std::pair<std::string, std::string>, int> pid_;
    std::unordered_map<std::string, int> oidx_;
    std::unordered_map<std::string, std::unordered_set<std::string>> cset_;
    inline static const std::unordered_set<std::string> kEmptySet_{};
};

// ===========================================================================
// 4. VALUE AND PARAMETER REPRESENTATIONS
// ===========================================================================

/** Sentinel type representing a field that could not be produced by any route. */
struct Missing {
    [[nodiscard]] auto to_string() const -> std::string { return "MISSING"; }
    auto operator==(const Missing&) const noexcept -> bool = default;
};

/**
 * Type-safe variant holding a reconstructed parameter value:
 * Decimal (numeric), String (categorical/enum), Array (vector of Decimals),
 * or Missing (unfulfilled).
 */
class FieldValue {
  public:
    using ValueVariant = std::variant<
        Missing,
        sensen::BigDecimal,
        std::string,
        std::vector<sensen::BigDecimal>>;

    FieldValue() noexcept : data_(Missing{}) {}
    FieldValue(Missing m) noexcept : data_(m) {}
    FieldValue(sensen::BigDecimal d) noexcept : data_(std::move(d)) {}
    FieldValue(std::string s) noexcept : data_(std::move(s)) {}
    FieldValue(std::vector<sensen::BigDecimal> arr) noexcept : data_(std::move(arr)) {}

    [[nodiscard]] auto is_missing() const noexcept -> bool {
        return std::holds_alternative<Missing>(data_);
    }

    [[nodiscard]] auto is_decimal() const noexcept -> bool {
        return std::holds_alternative<sensen::BigDecimal>(data_);
    }

    [[nodiscard]] auto is_string() const noexcept -> bool {
        return std::holds_alternative<std::string>(data_);
    }

    [[nodiscard]] auto is_array() const noexcept -> bool {
        return std::holds_alternative<std::vector<sensen::BigDecimal>>(data_);
    }

    [[nodiscard]] auto as_decimal() const -> std::optional<sensen::BigDecimal> {
        if (auto* val = std::get_if<sensen::BigDecimal>(&data_)) return *val;
        return std::nullopt;
    }

    [[nodiscard]] auto as_string() const -> std::optional<std::string_view> {
        if (auto* val = std::get_if<std::string>(&data_)) return *val;
        return std::nullopt;
    }

    [[nodiscard]] auto as_array() const -> std::optional<std::span<const sensen::BigDecimal>> {
        if (auto* val = std::get_if<std::vector<sensen::BigDecimal>>(&data_)) return *val;
        return std::nullopt;
    }

    [[nodiscard]] auto raw_variant() const noexcept -> const ValueVariant& {
        return data_;
    }

    [[nodiscard]] auto to_string() const -> std::string {
        return std::visit([](const auto& v) -> std::string {
            using T = std::decay_t<decltype(v)>;
            if constexpr (std::is_same_v<T, Missing>) {
                return "MISSING";
            } else if constexpr (std::is_same_v<T, sensen::BigDecimal>) {
                return v.to_string();
            } else if constexpr (std::is_same_v<T, std::string>) {
                return v;
            } else if constexpr (std::is_same_v<T, std::vector<sensen::BigDecimal>>) {
                std::string res = "[";
                for (std::size_t i = 0; i < v.size(); ++i) {
                    if (i > 0) res += ", ";
                    res += v[i].to_string();
                }
                res += "]";
                return res;
            }
        }, data_);
    }

    auto operator==(const FieldValue& other) const noexcept -> bool = default;

  private:
    ValueVariant data_;
};

/**
 * Reconstructed parameter payload containing the predicted operation
 * and all resolved slot values.
 */
struct ReconstructedParams {
    std::string operation;
    std::unordered_map<std::string, FieldValue> fields;

    [[nodiscard]] auto is_missing(std::string_view field_name) const -> bool {
        auto it = fields.find(std::string(field_name));
        return it == fields.end() || it->second.is_missing();
    }

    [[nodiscard]] auto missing_fields() const -> std::vector<std::string> {
        std::vector<std::string> missing;
        for (const auto& [k, v] : fields) {
            if (v.is_missing()) missing.push_back(k);
        }
        std::ranges::sort(missing);
        return missing;
    }

    [[nodiscard]] auto has_missing_fields() const -> bool {
        return std::ranges::any_of(fields, [](const auto& kv) {
            return kv.second.is_missing();
        });
    }

    [[nodiscard]] auto get_decimal(std::string_view field_name) const
        -> std::optional<sensen::BigDecimal> {
        auto it = fields.find(std::string(field_name));
        if (it != fields.end()) return it->second.as_decimal();
        return std::nullopt;
    }

    [[nodiscard]] auto get_string(std::string_view field_name) const
        -> std::optional<std::string_view> {
        auto it = fields.find(std::string(field_name));
        if (it != fields.end()) return it->second.as_string();
        return std::nullopt;
    }

    [[nodiscard]] auto get_array(std::string_view field_name) const
        -> std::optional<std::span<const sensen::BigDecimal>> {
        auto it = fields.find(std::string(field_name));
        if (it != fields.end()) return it->second.as_array();
        return std::nullopt;
    }

    [[nodiscard]] auto to_json() const -> std::string {
        std::string json = "{";
        json += "\"operation\":\"" + operation + "\"";
        for (const auto& [k, v] : fields) {
            json += ",\"" + k + "\":";
            if (v.is_missing()) {
                json += "null";
            } else if (v.is_string()) {
                json += "\"" + std::string(*v.as_string()) + "\"";
            } else if (v.is_decimal()) {
                json += "\"" + v.as_decimal()->to_string() + "\"";
            } else if (v.is_array()) {
                json += "[";
                auto arr = *v.as_array();
                for (std::size_t i = 0; i < arr.size(); ++i) {
                    if (i > 0) json += ",";
                    json += "\"" + arr[i].to_string() + "\"";
                }
                json += "]";
            }
        }
        json += "}";
        return json;
    }

    [[nodiscard]] auto to_string_fields() const
        -> std::vector<std::pair<std::string, std::vector<std::string>>> {
        std::vector<std::pair<std::string, std::vector<std::string>>> result;
        for (const auto& [name, val] : fields) {
            if (val.is_missing()) continue;
            if (val.is_decimal()) {
                result.emplace_back(name, std::vector<std::string>{val.as_decimal()->to_string()});
            } else if (val.is_string()) {
                result.emplace_back(name, std::vector<std::string>{std::string(*val.as_string())});
            } else if (val.is_array()) {
                std::vector<std::string> arr_strs;
                for (const auto& el : *val.as_array()) {
                    arr_strs.push_back(el.to_string());
                }
                result.emplace_back(name, std::move(arr_strs));
            }
        }
        return result;
    }
};

// ===========================================================================
// 5. CORE MATHEMATICAL MAPS
// ===========================================================================

/**
 * Compute the transformed value of a literal under an approved unary role.
 *
 * Implements: M1 identity, M1 identity#rep20, M2 percent/100,
 *             M3 annual%->monthly, M5 years->months, M8 negate, M10 complement%.
 *
 * REFUSES any other role string.
 */
[[nodiscard]] auto _unary_value(std::string_view role, const Literal& lit)
    -> std::expected<sensen::BigDecimal, std::string> {
    auto parsed = parse_role(role);
    if (!parsed) {
        return std::unexpected(parsed.error());
    }
    if (parsed->kind != RoleKind::Unary && parsed->kind != RoleKind::Rep) {
        return std::unexpected(std::format(
            "Role '{}' is not a unary or rep role (kind: '{}')", role, parsed->kind_str()));
    }

    const auto& name = parsed->name;
    const auto& v = lit.value;

    if (name == "M1 identity") {
        return v;
    }
    if (name == "M2 percent/100") {
        auto res = v.divide(sensen::BigDecimal(100));
        if (!res) {
            return std::unexpected(std::format("M2 percent/100 divide by 100 failed: {}", res.error()));
        }
        return *res;
    }
    if (name == "M3 annual%->monthly") {
        auto res1 = v.divide(sensen::BigDecimal(100));
        if (!res1) {
            return std::unexpected(std::format("M3 annual%->monthly divide by 100 failed: {}", res1.error()));
        }
        auto res2 = res1->divide(sensen::BigDecimal(12));
        if (!res2) {
            return std::unexpected(std::format("M3 annual%->monthly divide by 12 failed: {}", res2.error()));
        }
        return *res2;
    }
    if (name == "M3 annual%->quarterly") {
        auto res1 = v.divide(sensen::BigDecimal(100));
        if (!res1) {
            return std::unexpected(std::format("M3 annual%->quarterly divide by 100 failed: {}", res1.error()));
        }
        auto res2 = res1->divide(sensen::BigDecimal(4));
        if (!res2) {
            return std::unexpected(std::format("M3 annual%->quarterly divide by 4 failed: {}", res2.error()));
        }
        return *res2;
    }
    if (name == "M5 years->months") {
        return v * sensen::BigDecimal(12);
    }
    if (name == "M5 years->quarters") {
        return v * sensen::BigDecimal(4);
    }
    if (name == "M8 negate") {
        return v.negate();
    }
    if (name == "M10 complement%") {
        auto res = v.divide(sensen::BigDecimal(100));
        if (!res) {
            return std::unexpected(std::format("M10 complement% divide by 100 failed: {}", res.error()));
        }
        return sensen::BigDecimal(1) - *res;
    }

    return std::unexpected(std::format("Refusing unapproved unary map '{}'", name));
}

/** Alias for _unary_value conforming to standard naming. */
[[nodiscard]] inline auto unary_value(std::string_view role, const Literal& lit)
    -> std::expected<sensen::BigDecimal, std::string> {
    return _unary_value(role, lit);
}

/**
 * Compute the combined value of two literals under an approved binary map.
 *
 * Implements: M9 a-b, M9 a*(1-p), M9 a*p.
 *
 * REFUSES any other binary map name.
 */
[[nodiscard]] auto _binary_value(
    std::string_view name,
    const sensen::BigDecimal& a,
    const sensen::BigDecimal& b)
    -> std::expected<sensen::BigDecimal, std::string> {
    if (name == "M9 a-b") {
        return a - b;
    }
    if (name == "M9 a*(1-p)") {
        auto b_pct = b.divide(sensen::BigDecimal(100));
        if (!b_pct) {
            return std::unexpected(std::format("M9 a*(1-p) divide by 100 failed: {}", b_pct.error()));
        }
        auto factor = sensen::BigDecimal(1) - *b_pct;
        return a * factor;
    }
    if (name == "M9 a*p") {
        auto b_pct = b.divide(sensen::BigDecimal(100));
        if (!b_pct) {
            return std::unexpected(std::format("M9 a*p divide by 100 failed: {}", b_pct.error()));
        }
        return a * *b_pct;
    }

    return std::unexpected(std::format(
        "Refusing unapproved binary map '{}': only 'M9 a-b', 'M9 a*(1-p)' and 'M9 a*p' are supported", name));
}

/** Alias for _binary_value conforming to standard naming. */
[[nodiscard]] inline auto binary_value(
    std::string_view name,
    const sensen::BigDecimal& a,
    const sensen::BigDecimal& b)
    -> std::expected<sensen::BigDecimal, std::string> {
    return _binary_value(name, a, b);
}

// ===========================================================================
// 6. SCALAR RESOLUTION FROM LITERAL PAIRS
// ===========================================================================

/**
 * Resolve a scalar field's numeric value from its candidate (literal, role) entries.
 *
 * Unary candidates take precedence over binary candidates. If multiple literals
 * claim the slot, the candidate maximizing `(score(li, slot, role), li)` wins.
 *
 * Returns:
 *   - `BigDecimal` on successful calculation
 *   - `nullopt` if no candidate entry applies to this slot
 *   - `unexpected(error)` if role validation fails or arithmetic divides by zero
 */
[[nodiscard]] auto _scalar_from_pairs(
    std::string_view name,
    std::span<const SlotEntry> entries,
    std::span<const Literal> lits,
    const ScoreFn& score = default_score_fn)
    -> std::expected<std::optional<sensen::BigDecimal>, std::string> {

    // First validate all entries fail-closed
    for (const auto& entry : entries) {
        if (entry.lit_idx < 0 || static_cast<std::size_t>(entry.lit_idx) >= lits.size()) {
            return std::unexpected(std::format(
                "Slot '{}' references literal index {} out of range [0, {})",
                name, entry.lit_idx, lits.size()));
        }
        auto parsed = parse_role(entry.role);
        if (!parsed) {
            return std::unexpected(std::format(
                "Slot '{}' entry (lit {}, role '{}') failed role validation: {}",
                name, entry.lit_idx, entry.role, parsed.error()));
        }
    }

    // 1. Check unary entries
    std::vector<SlotEntry> unary_entries;
    for (const auto& entry : entries) {
        auto parsed = parse_role(entry.role);
        if (parsed->kind == RoleKind::Unary) {
            unary_entries.push_back(entry);
        }
    }

    if (!unary_entries.empty()) {
        auto best_it = std::ranges::max_element(
            unary_entries,
            [&](const SlotEntry& a, const SlotEntry& b) -> bool {
                double sa = score(a.lit_idx, name, a.role);
                double sb = score(b.lit_idx, name, b.role);
                if (sa != sb) return sa < sb;
                return a.lit_idx < b.lit_idx;
            });

        auto val = _unary_value(best_it->role, lits[best_it->lit_idx]);
        if (!val) {
            return std::unexpected(std::format(
                "Slot '{}' unary evaluation failed for role '{}' on literal {}: {}",
                name, best_it->role, best_it->lit_idx, val.error()));
        }
        return std::optional<sensen::BigDecimal>(*val);
    }

    // 2. Check binary entries (preserves insertion order)
    struct BinaryGroup {
        std::vector<int> a;
        std::vector<int> b;
    };
    std::vector<std::pair<std::string, BinaryGroup>> by_map;

    auto find_or_add_group = [&](std::string_view mname) -> BinaryGroup& {
        for (auto& [k, g] : by_map) {
            if (k == mname) return g;
        }
        by_map.emplace_back(std::string(mname), BinaryGroup{});
        return by_map.back().second;
    };

    for (const auto& entry : entries) {
        auto parsed = parse_role(entry.role);
        if (parsed->kind == RoleKind::A) {
            find_or_add_group(parsed->name).a.push_back(entry.lit_idx);
        } else if (parsed->kind == RoleKind::B) {
            find_or_add_group(parsed->name).b.push_back(entry.lit_idx);
        }
    }

    for (const auto& [mname, ab] : by_map) {
        if (!ab.a.empty() && !ab.b.empty()) {
            std::string role_a = mname + "#A";
            std::string role_b = mname + "#B";

            auto best_a_it = std::ranges::max_element(
                ab.a,
                [&](int la1, int la2) -> bool {
                    double s1 = score(la1, name, role_a);
                    double s2 = score(la2, name, role_a);
                    if (s1 != s2) return s1 < s2;
                    return la1 < la2;
                });

            auto best_b_it = std::ranges::max_element(
                ab.b,
                [&](int lb1, int lb2) -> bool {
                    double s1 = score(lb1, name, role_b);
                    double s2 = score(lb2, name, role_b);
                    if (s1 != s2) return s1 < s2;
                    return lb1 < lb2;
                });

            int la = *best_a_it;
            int lb = *best_b_it;

            auto val = _binary_value(mname, lits[la].value, lits[lb].value);
            if (!val) {
                return std::unexpected(std::format(
                    "Slot '{}' binary evaluation failed for map '{}': {}",
                    name, mname, val.error()));
            }
            return std::optional<sensen::BigDecimal>(*val);
        }
    }

    // 3. No candidate matched
    return std::optional<sensen::BigDecimal>(std::nullopt);
}

/** Alias for _scalar_from_pairs conforming to standard naming. */
[[nodiscard]] inline auto scalar_from_pairs(
    std::string_view name,
    std::span<const SlotEntry> entries,
    std::span<const Literal> lits,
    const ScoreFn& score = default_score_fn)
    -> std::expected<std::optional<sensen::BigDecimal>, std::string> {
    return _scalar_from_pairs(name, entries, lits, score);
}

// ===========================================================================
// 7. CONVENTION AND DEFAULT VALUE RESOLUTION
// ===========================================================================

/**
 * Resolve a field value from convention classifications, constant defaults,
 * or operation-level modal defaults.
 *
 * Precedence:
 *   1. Model convention prediction (`conv[name]` indexing `sch.conv_vocab[name]`)
 *   2. Constant single-value default (`sch.const_default[name]`)
 *   3. Operation-specific modal default (`sch.op_default[op_name][name]`)
 *   4. Missing (returns `FieldValue(Missing{})`)
 */
[[nodiscard]] auto _class_value(
    const Schema& sch,
    std::string_view op_name,
    std::string_view name,
    const std::unordered_map<std::string, int>& conv)
    -> std::expected<FieldValue, std::string> {

    std::string name_str(name);
    auto kind_it = sch.field_kind.find(name_str);
    if (kind_it == sch.field_kind.end()) {
        return std::unexpected(std::format(
            "Field '{}' not declared in schema field_kind", name));
    }
    const std::string& kind = kind_it->second;

    std::optional<std::string> text;

    auto conv_it = conv.find(name_str);
    auto vocab_it = sch.conv_vocab.find(name_str);
    if (conv_it != conv.end() && vocab_it != sch.conv_vocab.end()) {
        int idx = conv_it->second;
        const auto& vocab = vocab_it->second;
        if (idx < 0 || static_cast<std::size_t>(idx) >= vocab.size()) {
            return std::unexpected(std::format(
                "Conv class index {} for field '{}' out of range [0, {})",
                idx, name, vocab.size()));
        }
        text = vocab[idx];
    } else {
        auto const_it = sch.const_default.find(name_str);
        if (const_it != sch.const_default.end()) {
            text = const_it->second;
        } else {
            std::string op_str(op_name);
            auto op_def_it = sch.op_default.find(op_str);
            if (op_def_it != sch.op_default.end()) {
                auto field_def_it = op_def_it->second.find(name_str);
                if (field_def_it != op_def_it->second.end()) {
                    text = field_def_it->second;
                }
            }
        }
    }

    if (!text.has_value()) {
        return FieldValue(Missing{});
    }

    if (kind == "cat") {
        // Text is JSON-encoded (e.g. "\"ANNUITY_DUE\"") per canon_cat
        auto parsed = fastjson::parse(*text);
        if (parsed.has_value()) {
            if (parsed->is_string()) {
                return FieldValue(std::string(parsed->as_string()));
            } else if (parsed->is_boolean()) {
                return FieldValue(parsed->as_boolean() ? std::string("true") : std::string("false"));
            } else {
                return FieldValue(parsed->to_string());
            }
        }
        // Fallback to literal text if unquoted
        return FieldValue(*text);
    } else {
        auto dec = sensen::BigDecimal::try_parse(*text);
        if (!dec) {
            return std::unexpected(std::format(
                "Failed to parse convention decimal '{}' for field '{}': {}",
                *text, name, dec.error()));
        }
        return FieldValue(*dec);
    }
}

/** Alias for _class_value conforming to standard naming. */
[[nodiscard]] inline auto class_value(
    const Schema& sch,
    std::string_view op_name,
    std::string_view name,
    const std::unordered_map<std::string, int>& conv)
    -> std::expected<FieldValue, std::string> {
    return _class_value(sch, op_name, name, conv);
}

// ===========================================================================
// 8. ARRAY RECONSTRUCTION
// ===========================================================================

/**
 * Reconstruct array elements:
 *   (a) Anchor array: matched left-to-right from claimed literals, supporting
 *       repeated runs via role repetition counts (e.g. M1 identity#rep20).
 *   (b) Non-anchor arrays: aligned to the anchor's literals by positional text
 *       group windows; groups with no claimed literal fill with BigDecimal(0).
 */
[[nodiscard]] auto _build_array(
    const Schema& sch,
    std::string_view name,
    std::optional<std::string_view> anchor,
    const std::unordered_map<std::string, std::vector<SlotEntry>>& by_slot,
    std::span<const int> groups,
    std::span<const Literal> lits,
    std::span<const int> reps = {})
    -> std::expected<std::vector<sensen::BigDecimal>, std::string> {

    std::string name_str(name);
    std::vector<SlotEntry> entries;
    auto slot_it = by_slot.find(name_str);
    if (slot_it != by_slot.end()) {
        entries = slot_it->second;
    }
    std::ranges::sort(entries, [](const SlotEntry& a, const SlotEntry& b) {
        if (a.lit_idx != b.lit_idx) return a.lit_idx < b.lit_idx;
        return a.role < b.role;
    });

    if (anchor.has_value() && name == *anchor) {
        std::vector<sensen::BigDecimal> out;
        for (const auto& entry : entries) {
            if (entry.lit_idx < 0 || static_cast<std::size_t>(entry.lit_idx) >= lits.size()) {
                return std::unexpected(std::format(
                    "Anchor array '{}' references literal index {} out of range [0, {})",
                    name, entry.lit_idx, lits.size()));
            }
            auto parsed = parse_role(entry.role);
            if (!parsed) {
                return std::unexpected(std::format(
                    "Anchor array '{}' role '{}' failed parse: {}",
                    name, entry.role, parsed.error()));
            }
            auto v = _unary_value(entry.role, lits[entry.lit_idx]);
            if (!v) {
                return std::unexpected(std::format(
                    "Anchor array '{}' unary evaluation failed for role '{}' on lit {}: {}",
                    name, entry.role, entry.lit_idx, v.error()));
            }
            out.insert(out.end(), static_cast<std::size_t>(parsed->rep), *v);
        }
        return out;
    }

    std::vector<int> bounds;
    bounds.reserve(groups.size() + 1);
    for (int g : groups) bounds.push_back(g);
    bounds.push_back(1'000'000'000); // 10^9

    // A group whose anchor literal is REPEATED (`#repN`) owns N elements, a plain group one. A single
    // repeated anchor literal ("$350,000 ... against ...") may sit on either side of the offers, so
    // its window is the whole utterance. Mirrors `_build_array` in agent/train/encoder_corpus.py.
    std::vector<int> reps_v(reps.begin(), reps.end());
    reps_v.resize(groups.size(), 1);

    std::vector<sensen::BigDecimal> out;
    out.reserve(groups.size());

    for (std::size_t k = 0; k < groups.size(); ++k) {
        const int lo = (groups.size() == 1 && reps_v[0] > 1) ? -1 : bounds[k];
        const int hi = bounds[k + 1];
        std::vector<SlotEntry> hit;
        for (const auto& e : entries) {
            if (e.lit_idx >= lo && e.lit_idx < hi) {
                hit.push_back(e);
            }
        }

        for (int c = 0; c < reps_v[k]; ++c) {
            if (static_cast<std::size_t>(c) < hit.size()) {
                const auto& pick = hit[static_cast<std::size_t>(c)];
                if (pick.lit_idx < 0 || static_cast<std::size_t>(pick.lit_idx) >= lits.size()) {
                    return std::unexpected(std::format(
                        "Array '{}' references literal index {} out of range [0, {})",
                        name, pick.lit_idx, lits.size()));
                }
                auto v = _unary_value(pick.role, lits[pick.lit_idx]);
                if (!v) {
                    return std::unexpected(std::format(
                        "Array '{}' group {} evaluation failed for role '{}' on lit {}: {}",
                        name, k, pick.role, pick.lit_idx, v.error()));
                }
                out.push_back(*v);
            } else {
                out.push_back(sensen::BigDecimal(0));
            }
        }
    }

    return out;
}

/** Alias for _build_array conforming to standard naming. */
[[nodiscard]] inline auto build_array(
    const Schema& sch,
    std::string_view name,
    std::optional<std::string_view> anchor,
    const std::unordered_map<std::string, std::vector<SlotEntry>>& by_slot,
    std::span<const int> groups,
    std::span<const Literal> lits,
    std::span<const int> reps = {})
    -> std::expected<std::vector<sensen::BigDecimal>, std::string> {
    return _build_array(sch, name, anchor, by_slot, groups, lits, reps);
}

// ===========================================================================
// 9. RECONSTRUCT ENTRY POINT
// ===========================================================================

/**
 * Rebuild the financial parameters object from model outputs:
 *   - op: predicted operation index
 *   - lits: literals extracted from the user text
 *   - lit_pairs: per-literal predicted pair indices
 *   - conv: predicted convention class indices per field
 *   - lit_scores: optional probability scores per literal and pair for arbitration
 *
 * Returns:
 *   - `nullopt` if op == 0 (<NONE>)
 *   - `ReconstructedParams` containing all resolved fields
 *   - `unexpected(error)` if role validation fails, bounds are exceeded, or
 *     arithmetic encounters division by zero
 */
[[nodiscard]] auto reconstruct(
    const Schema& sch,
    int op,
    std::span<const Literal> lits,
    const std::vector<std::vector<int>>& lit_pairs,
    const std::unordered_map<std::string, int>& conv,
    std::optional<std::span<const std::unordered_map<int, double>>> lit_scores = std::nullopt)
    -> std::expected<std::optional<ReconstructedParams>, std::string> {

    if (op == 0) {
        return std::optional<ReconstructedParams>{std::nullopt};
    }

    if (op < 0 || static_cast<std::size_t>(op) >= sch.ops.size()) {
        return std::unexpected(std::format(
            "Operation index {} out of range [0, {})", op, sch.ops.size()));
    }

    const std::string& op_name = sch.ops[op];
    ReconstructedParams out;
    out.operation = op_name;

    std::unordered_map<std::string, std::vector<SlotEntry>> by_slot;
    for (std::size_t li = 0; li < lit_pairs.size(); ++li) {
        for (int p : lit_pairs[li]) {
            if (p < 0 || static_cast<std::size_t>(p) >= sch.pairs.size()) {
                return std::unexpected(std::format(
                    "Literal {} specifies pair id {} out of range [0, {})",
                    li, p, sch.pairs.size()));
            }
            const auto& [slot, role] = sch.pairs[p];
            auto parsed = parse_role(role);
            if (!parsed) {
                return std::unexpected(std::format(
                    "Refusing reconstruction: pair id {} (slot '{}', role '{}') failed validation: {}",
                    p, slot, role, parsed.error()));
            }
            by_slot[slot].push_back(SlotEntry{.lit_idx = static_cast<int>(li), .role = role});
        }
    }

    auto score_fn = [&](int li, std::string_view slot, std::string_view role) -> double {
        if (!lit_scores.has_value()) {
            return static_cast<double>(li);
        }
        auto pid = sch.pair_id(slot, role);
        if (!pid.has_value()) return 0.0;
        if (li < 0 || static_cast<std::size_t>(li) >= lit_scores->size()) return 0.0;
        const auto& m = (*lit_scores)[li];
        auto it = m.find(*pid);
        return it != m.end() ? it->second : 0.0;
    };

    auto anchor = sch.get_anchor(op_name);
    std::vector<int> groups;
    std::vector<int> reps;
    if (anchor.has_value()) {
        auto it = by_slot.find(std::string(*anchor));
        if (it != by_slot.end()) {
            std::set<int> unique_lis;
            std::map<int, int> rep_at;
            for (const auto& entry : it->second) {
                unique_lis.insert(entry.lit_idx);
                if (auto parsed = parse_role(entry.role); parsed) {
                    rep_at[entry.lit_idx] = parsed->rep;
                }
            }
            groups.assign(unique_lis.begin(), unique_lis.end());
            for (int g : groups) {
                const auto at = rep_at.find(g);
                reps.push_back(at != rep_at.end() ? at->second : 1);
            }
        }
    }

    auto fields_it = sch.op_fields.find(op_name);
    if (fields_it == sch.op_fields.end()) {
        return std::unexpected(std::format(
            "Operation '{}' not found in schema op_fields", op_name));
    }

    for (const auto& name : fields_it->second) {
        auto kind_it = sch.field_kind.find(name);
        if (kind_it == sch.field_kind.end()) {
            return std::unexpected(std::format(
                "Field '{}' for op '{}' not found in schema field_kind", name, op_name));
        }
        const std::string& kind = kind_it->second;

        if (kind == "arr") {
            auto arr_val = _build_array(sch, name, anchor, by_slot, groups, lits, reps);
            if (!arr_val) {
                return std::unexpected(arr_val.error());
            }
            std::string output_key = name;
            if (output_key.ends_with(kArraySuffix)) {
                output_key.resize(output_key.size() - kArraySuffix.size());
            }
            out.fields[output_key] = FieldValue(*arr_val);
            continue;
        }

        std::span<const SlotEntry> entries;
        auto slot_it = by_slot.find(name);
        if (slot_it != by_slot.end()) {
            entries = slot_it->second;
        }

        auto val = _scalar_from_pairs(name, entries, lits, score_fn);
        if (!val) {
            return std::unexpected(val.error());
        }

        if (val->has_value()) {
            out.fields[name] = FieldValue(**val);
        } else {
            auto cls_val = _class_value(sch, op_name, name, conv);
            if (!cls_val) {
                return std::unexpected(cls_val.error());
            }
            out.fields[name] = *cls_val;
        }
    }

    return std::optional<ReconstructedParams>{std::move(out)};
}

// NO five-argument "convenience overload" of reconstruct(). One was written and
// it made the five-argument CALL ILL-FORMED: the primary overload above already
// defaults `lit_scores`, so `reconstruct(sch, op, lits, pairs, conv)` matched
// both and clang reported `call to 'reconstruct' is ambiguous` -- a declaration
// added to make the common call easier was the only thing preventing it. The
// default argument IS the convenience overload; a second one cannot coexist
// with it.

/** Convenience overload of reconstruct accepting vector of score maps directly. */
[[nodiscard]] inline auto reconstruct(
    const Schema& sch,
    int op,
    std::span<const Literal> lits,
    const std::vector<std::vector<int>>& lit_pairs,
    const std::unordered_map<std::string, int>& conv,
    const std::vector<std::unordered_map<int, double>>& lit_scores)
    -> std::expected<std::optional<ReconstructedParams>, std::string> {
    return reconstruct(
        sch, op, lits, lit_pairs, conv,
        std::optional<std::span<const std::unordered_map<int, double>>>(lit_scores));
}

/**
 * Restrict one literal's predicted pair set to the pairs the OPERATION admits.
 *
 * WHY THIS IS NOT IN sensen. `text_encoder.cppm` decodes the pair head mask-free, which
 * matches the trainer: `encoder_model.py` applies no mask and its docstring says masks are
 * the CALLER's. So the encoder reports what the head said and this layer says what the
 * schema allows -- the same split as `reconstruct()` doing the arithmetic the model refuses
 * to do.
 *
 * WHY IT IS WORTH DOING. The schema admits a MEDIAN OF 5 pairs of 109 per operation (min 2,
 * max 16), so the mask removes about 95% of the label space once the operation is known.
 * Measured on the holdout: 600/600 rows with the mask and 524/600 without it -- a 12.67%
 * error rate bought back by a set intersection.
 *
 * AN INTERSECTION IS EXACTLY MASK-THEN-THRESHOLD, and that is why no sigmoid appears here.
 * The pair head is MULTI-LABEL: an independent sigmoid per pair, with no softmax coupling
 * them. Masking a logit to -inf therefore cannot change any OTHER pair's probability, and a
 * masked pair can never clear the threshold. So intersecting the set sensen already selected
 * with the admissible set gives bit-for-bit what masking the logits first would have given,
 * and this module needs no second copy of `sigmoid` to drift from text_encoder's.
 *
 * AN EMPTY RESULT IS A PREDICTION, NOT A FAILURE. 165 of the mortgage holdout's 3,381
 * literals are labelled with no pair at all -- a number the user stated that fills no
 * parameter of the named operation. Returning an empty vector is the correct answer there.
 *
 * An operation the schema does not describe is REFUSED rather than passed through
 * unmasked: admitting all 109 pairs for an unknown operation would restore exactly the
 * 12.67% this function exists to remove, and would do it silently.
 */
[[nodiscard]] inline auto maskPairsToOperation(const Schema& sch, int op,
                                               std::span<const int> selected)
    -> std::expected<std::vector<int>, std::string> {
    if (op == 0) {
        // <NONE>: no operation was named, so no pair is admissible and there is nothing
        // for reconstruct() to build. Not an error -- 40 of 600 holdout rows are this.
        return std::vector<int>{};
    }
    if (op < 0 || static_cast<std::size_t>(op) >= sch.ops.size()) {
        return std::unexpected(std::format(
            "Operation index {} out of range [0, {})", op, sch.ops.size()));
    }
    const std::string& op_name = sch.ops[static_cast<std::size_t>(op)];
    const auto it = sch.op_pairs.find(op_name);
    if (it == sch.op_pairs.end()) {
        return std::unexpected(std::format(
            "the schema's op_pairs names no entry for operation \"{}\", so there is no "
            "admissible pair set to restrict to; refusing rather than admitting all {} "
            "pairs, which would silently reinstate the 12.67% the mask removes",
            op_name, sch.pairs.size()));
    }
    // Sorted lookup rather than a hash set: the admissible sets are tiny (median 5), so a
    // flat sorted vector beats a hash and keeps the result in ASCENDING pair order, which
    // is the order text_encoder emits and the order reconstruct's arbitration expects.
    std::vector<int> admissible(it->second);
    std::ranges::sort(admissible);
    std::vector<int> out;
    out.reserve(selected.size());
    for (const int pid : selected) {
        if (std::ranges::binary_search(admissible, pid)) out.push_back(pid);
    }
    std::ranges::sort(out);
    return out;
}

/**
 * A decode bound to ONE (schema, operation) pair: the single place that knows how to
 * restrict a head's output to what the named operation admits.
 *
 * WHY THIS TYPE EXISTS AT ALL, rather than a second free function beside
 * `maskPairsToOperation`. The per-literal pair head got its mask on 2026-10-02 and the
 * per-field CONVENTION heads did not, because they are decoded in a different file. The
 * schema has carried `conv_op_mask` the whole time and `encoder_assistant.cpp` referenced
 * it ZERO times, so every convention field was a raw argmax over its full class list.
 * Measured through the real `ParseStrategy` RPC on 300 holdout rows: 45 refused with
 * `"futures_short" is a futures-only strategy, but asset_class is "EQUITY"` -- the
 * unmasked argmax preferring EQUITY on an operation whose admissible set is exactly
 * {FUTURES}. That is this project's "sweep the class, not the instance" scar at register
 * level, so the repair is ONE object both heads go through: a third head cannot be added
 * while quietly forgetting the mask, because there is nowhere else to add it.
 *
 * MASKING IS EXACT FOR BOTH HEADS, FOR TWO DIFFERENT REASONS, and neither needs a second
 * copy of an activation that could drift from `text_encoder.cppm`'s:
 *   - the pair head is MULTI-LABEL (an independent sigmoid per pair, no softmax coupling),
 *     so masking a logit to -inf cannot move any other pair's probability and a masked pair
 *     can never clear the threshold: mask-then-threshold IS a set intersection.
 *   - a convention head is SINGLE-LABEL, and argmax is invariant to softmax's
 *     normalisation, so an argmax over the admissible SUBSET is bit-for-bit the argmax of
 *     -inf-masked logits.
 *
 * TIE-BREAKING IS PART OF THE CONTRACT. `torch.argmax` returns the FIRST maximum and so do
 * `std::ranges::max_element` and sensen's `argmaxIndex`. The admissible class ids are
 * therefore visited in ASCENDING order, so a tie resolves to the lowest admissible id --
 * which is what masking the logits and arg-maxing the whole row would have done.
 *
 * WHY MORTGAGE COULD NOT HAVE CAUGHT THIS, stated because it is the fifth time in one day
 * that a corpus which cannot exhibit a failure was read as a control for it. Measured on
 * both schemas: strategy's `asset_class` admits 1 class for 8 operations and 2 of 3 for the
 * other 39 -- it NEVER admits the full vocabulary, so the mask always constrains. Every
 * mask the MORTGAGE schema carries is FULL (all classes admissible) for the one operation
 * that has an entry, so applying it or not is provably identical there. Mortgage served
 * 560/560 through this same code path with the defect present.
 */
class OperationDecode {
  public:
    /// Bind to a schema. The operation is not chosen yet; `forOperation` does that.
    [[nodiscard]] static auto of(const Schema& schema) noexcept -> OperationDecode {
        return OperationDecode{schema, kNoOperation};
    }

    /// Chain: name the operation every subsequent restriction is relative to.
    [[nodiscard]] auto forOperation(int op) const noexcept -> OperationDecode {
        return OperationDecode{schema_, op};
    }

    [[nodiscard]] auto operation() const noexcept -> int { return op_; }

    /**
     * Decode every convention field from the FLAT convention logits, each restricted to the
     * classes this operation admits.
     *
     * The slice table is DERIVED here from `conv_fields` + `conv_vocab` -- the same source
     * of truth the exporter writes -- and the total is checked against the span actually
     * handed over, so a disagreement is a refusal rather than one field reading another's
     * logits. Deriving it in the one place that consumes it is why there is no second
     * hand-maintained offset table to drift (the `ALLOWED_OPERATIONS` lesson).
     *
     * A FIELD WITH NO MASK ENTRY IS OMITTED, NOT GUESSED. Measured on the mortgage schema:
     * 135 (field, operation) pairs have no entry, and for ZERO of them does `op_fields`
     * name that field -- so the operation does not take it and `reconstruct()` would
     * discard any value decoded for it. Omitting is therefore provably identical to today's
     * behaviour there, and strictly more honest: an inapplicable field gets no argmax.
     */
    [[nodiscard]] auto conventionClasses(std::span<const float> flat_logits) const
        -> std::expected<std::unordered_map<std::string, int>, std::string> {
        std::unordered_map<std::string, int> out;
        if (op_ == 0) return out;  // <NONE>: no operation named, so no field is admissible.
        const auto op_name = operationName();
        if (!op_name) return std::unexpected(op_name.error());

        std::size_t at = 0;
        for (const auto& field : schema_.conv_fields) {
            const auto vocab = schema_.conv_vocab.find(field);
            if (vocab == schema_.conv_vocab.end()) {
                return std::unexpected(std::format(
                    "schema conv_fields names \"{}\" but conv_vocab has no entry for it, so its "
                    "class count is unknown and the flat convention logits cannot be sliced",
                    field));
            }
            const std::size_t width = vocab->second.size();
            if (at + width > flat_logits.size()) {
                return std::unexpected(std::format(
                    "conv_vocab needs at least {} classes to reach the end of field \"{}\" but the "
                    "convention head is {} wide; slicing further would read past the end of one "
                    "field into the next",
                    at + width, field, flat_logits.size()));
            }
            const auto slice = flat_logits.subspan(at, width);
            at += width;

            const auto admissible = admissibleClasses(field, *op_name);
            if (!admissible) return std::unexpected(admissible.error());
            if (admissible->empty()) continue;  // not applicable to this operation
            const auto best = restrictedArgmax(slice, *admissible);
            if (!best) return std::unexpected(best.error());
            out.emplace(field, *best);
        }
        if (at != flat_logits.size()) {
            return std::unexpected(std::format(
                "conv_vocab totals {} classes across {} fields but the model's convention head is "
                "{} wide, so the two disagree about the flat layout",
                at, schema_.conv_fields.size(), flat_logits.size()));
        }
        return out;
    }

    /// Restrict one literal's predicted pair set to the pairs this operation admits.
    [[nodiscard]] auto admissiblePairs(std::span<const int> selected) const
        -> std::expected<std::vector<int>, std::string> {
        return maskPairsToOperation(schema_, op_, selected);
    }

  private:
    static constexpr int kNoOperation = -1;

    OperationDecode(const Schema& schema, int op) noexcept : schema_{schema}, op_{op} {}

    [[nodiscard]] auto operationName() const -> std::expected<std::string, std::string> {
        if (op_ < 0 || static_cast<std::size_t>(op_) >= schema_.ops.size()) {
            return std::unexpected(std::format(
                "Operation index {} out of range [0, {}); an operation the schema does not "
                "describe is refused rather than decoded unmasked, which is exactly the "
                "unmasked-argmax defect this type exists to remove",
                op_, schema_.ops.size()));
        }
        return schema_.ops[static_cast<std::size_t>(op_)];
    }

    /// The class ids `field` admits under `op_name`, ASCENDING. Empty means "not applicable".
    [[nodiscard]] auto admissibleClasses(const std::string& field, const std::string& op_name) const
        -> std::expected<std::vector<int>, std::string> {
        const auto per_field = schema_.conv_op_mask.find(field);
        if (per_field == schema_.conv_op_mask.end()) return std::vector<int>{};
        const auto per_op = per_field->second.find(op_name);
        if (per_op == per_field->second.end()) return std::vector<int>{};
        std::vector<int> ids{per_op->second};
        std::ranges::sort(ids);
        return ids;
    }

    /// Argmax over `admissible` only, visited ascending so a tie takes the lowest id.
    [[nodiscard]] static auto restrictedArgmax(std::span<const float> logits,
                                                std::span<const int> admissible)
        -> std::expected<int, std::string> {
        int best = -1;
        float best_score = 0.0F;
        for (const int id : admissible) {
            if (id < 0 || static_cast<std::size_t>(id) >= logits.size()) {
                return std::unexpected(std::format(
                    "conv_op_mask admits class {} but the field has only {} classes", id,
                    logits.size()));
            }
            const float score = logits[static_cast<std::size_t>(id)];
            if (best < 0 || score > best_score) {
                best = id;
                best_score = score;
            }
        }
        if (best < 0) {
            return std::unexpected(std::string{
                "restrictedArgmax was given an empty admissible set; an empty set means \"not "
                "applicable\" and must be handled by the caller, never arg-maxed"});
        }
        return best;
    }

    const Schema& schema_;
    int op_;
};

} // namespace encoder_reconstruct

