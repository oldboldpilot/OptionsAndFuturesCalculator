/**
 * Proto-derived CONSTRAINED DECODING for the mortgage assistant.
 *
 * @author Olumuyiwa Oluwasanmi
 *
 * ===========================================================================
 * WHAT THIS IS FOR
 * ===========================================================================
 *
 * The mortgage fine-tune scores 8/30 (27%) params exact-match on raw decode
 * against a 95% bar, and llama.cpp scored the identical 8/30 on the same
 * rollouts -- so the deficit is the model, not the engine. The dominant
 * failure is not hallucinated data. It is SCHEMA CONFUSION across 26
 * operations and 160 fields:
 *
 *     want  ComputeHomeFutureValue: current_property_value, target_years
 *     got   ComputeHomeFutureValue: property_price,         years
 *     want  ComputeMortgageRecast:  lump_sum_payment
 *     got   ComputeMortgageRecast:  extra_payment
 *     want  ComputeDetailedAmortization: annual_tax_rate
 *     got   ComputeDetailedAmortization: monthly_tax_rate
 *     want  ComputeFutureValueDetailed   (the operation itself is wrong)
 *     got   ComputeFutureValue
 *
 * plus operation ids that do not exist at all (`ComputePayoff`,
 * `ComputeRentBuy`, `ComputeRentalRccapRate`) and ~14% of outputs that are not
 * even valid JSON (`"timing"]=`, unquoted keys, unbalanced brackets).
 *
 * `mortgage_verification.cppm` REJECTS every one of those AFTER the fact.
 * This module makes them UNREPRESENTABLE: a decoder driven by the automaton
 * below can only ever place a character that keeps the emitted text on a path
 * to a well-formed params object for a real operation with exactly that
 * operation's field names and value kinds. The failure class is removed at
 * generation time rather than converted into a refusal.
 *
 * The two modules are complements, not substitutes. This one settles SHAPE
 * (which ids, which keys, which value forms, which punctuation). It cannot
 * settle GROUNDING -- whether 5378.63 is the number the user actually said --
 * because that is a property of the utterance, not of the schema, and no
 * grammar over the output alone can decide it. `verify_mortgage_output` stays
 * mandatory.
 *
 * ===========================================================================
 * THE LABEL SPACE IS DERIVED, NOT COPIED
 * ===========================================================================
 *
 * `backend/proto/finance.proto` is the authority. Two places already project
 * it: `agent/dataset/build_mortgage_dataset.py` (`parse_finance_proto()` plus
 * the section-banner scope and the two named exclusions), which produces the
 * labels the model was TRAINED on, and `mortgage_verification.cppm`, whose
 * embedded table is checked against a fresh parse of the .proto by
 * `tests/test_mortgage_verification.cpp`.
 *
 * A third hand-maintained copy would drift. So this module HAS NO TABLE OF ITS
 * OWN: `Schema::build()` calls `mortgage_verification`'s exported
 * `operation_ids()`, `fields_of()` and `classify_slot()` and derives every
 * operation id, every field name, every field order and every value form from
 * what they return. The grammar and the verifier cannot disagree about the
 * label space because there is only one label space, and the .proto drift gate
 * that already guards it guards this module too. `tests/test_mortgage_grammar.cpp`
 * re-parses finance.proto independently and asserts all three agree.
 *
 * ONE unavoidable exception, and it is fully gated. mortgage_verification
 * keeps its enum CONSTANT sets in an unexported `detail` namespace, so they
 * cannot be read through its public API. The ten constants of the four enums
 * this label space reaches are therefore declared here (`kEnumTables`) -- and
 * the test checks them in BOTH directions: against a fresh parse of
 * finance.proto, and against `verify_mortgage_params` itself, which must
 * accept every constant this module permits and reject one it does not.
 *
 * ===========================================================================
 * CONFORMING TO SENSEN RATHER THAN INVENTING A PARALLEL MECHANISM
 * ===========================================================================
 *
 * sensen already has the hook. `sensen::IGrammar` (backend/sensen/src/grammar.cppm)
 * is the per-decode-step contract -- `allowedMask()` -> mask the disallowed
 * logits to -inf -> `accept(token)` -- and `sensen::LlmPipeline::sampleGuided`
 * takes a `const IGrammar*`. `MortgageParamsGrammar` below IS a
 * `sensen::IGrammar`. It is not a parallel mechanism; it is a third
 * implementation of the interface sensen defines, alongside sensen's own
 * `JsonObjectGrammar` and `RegexGrammar`, and it is masked and advanced by
 * exactly the same calls.
 *
 * What sensen does NOT have is an injection point. `GenerationConfig` exposes
 * `grammar_kind` (None / JsonObject / Regex) and `grammar_pattern`, and
 * `LlmPipeline::generate()` builds the grammar from those two fields alone --
 * there is no `IGrammar*` field on the config, so a custom grammar cannot be
 * handed to `generate()`. Stated plainly because it decides the integration:
 *
 *   1. THE PREFERRED PATH is for the service to drive the decode loop itself
 *      and pass `&grammar` to `sampleGuided`, or to consult
 *      `allowed_next_chars()` / `allowedMask()` directly. This is exact, and
 *      it costs one automaton step per candidate token character.
 *
 *   2. THE NO-CODE-CHANGE PATH is `params_regex()`, which emits a pattern in
 *      the subset `sensen::RegexNfa` compiles (literals, classes, `*` `+` `?`,
 *      `|`, groups, escapes) for `GrammarKind::Regex` + `grammar_pattern`.
 *      It goes through the existing hook untouched, and it is exactly as
 *      strict: sensen's own `RegexGrammar`, driven byte by byte over this
 *      pattern, accepts all 554 gold params objects and refuses the same
 *      defects the automaton does (`tests/test_mortgage_grammar.cpp`, gate 8).
 *
 *      It is also 8,881 characters of pattern -- some thousands of NFA states
 *      -- and `RegexGrammar` allocates a `vector<bool>` sized to that state
 *      count for every character of every vocabulary entry on every step.
 *      Measured over a 151,936-entry vocabulary (Qwen3's size, synthetic
 *      texts, mean length ~4.5 bytes) on this machine: 5.39 ms per decode
 *      step for the regex path against 0.253 ms for the automaton, a 21x
 *      difference; the automaton's advantage is the precomputed
 *      `allowed_next_chars()` table, which rejects most of the vocabulary on
 *      its first byte before any state is copied. Both are dwarfed by a
 *      0.6B forward pass on CPU, so path 2 is viable -- but path 1 is the one
 *      to reach for, and the number above is why.
 *
 * Neither path requires editing backend/sensen, which is a read-only
 * submodule here.
 *
 * ===========================================================================
 * WHAT THE GRAMMAR ACCEPTS, AND WHY IT IS THIS TIGHT
 * ===========================================================================
 *
 * Exactly the shape the model was trained to emit, measured over all 554
 * params-bearing rows of `agent/dataset/data_mortgage/val.jsonl`:
 *
 *     <params>{"operation":"<Op>","<f1>":<v1>,...,"<fn>":<vn>}</params>
 *
 *   - no whitespace anywhere                       (554/554 gold rows)
 *   - `operation` first                            (554/554)
 *   - every declared field of that operation present, none absent
 *                                                  (554/554; and a missing
 *                                                  field is `ReasonCode::MissingField`
 *                                                  in the verifier, so a
 *                                                  grammar that permitted one
 *                                                  would only be manufacturing
 *                                                  refusals)
 *   - fields in finance.proto's own declaration order
 *                                                  (554/554; relaxable via
 *                                                  `GrammarOptions::require_declaration_order`,
 *                                                  which turns the key phase
 *                                                  into a set membership over
 *                                                  the fields not yet emitted)
 *
 * Value form per field, derived from the proto type and the verifier's own
 * `classify_slot`:
 *
 *   proto `string`   -> a QUOTED strict decimal   `"(0|[1-9][0-9]*)(\.[0-9]+)?"`
 *   proto enum       -> a quoted constant of THAT enum's closed set
 *   proto `bool`     -> `true` / `false`
 *   otherwise        -> a bare JSON number, same digit grammar
 *   `repeated`       -> `[` one-or-more of the above, comma separated `]`
 *
 * Two refinements, both measured rather than assumed:
 *
 *   - A FRACTION is permitted unless the proto type is `int32` AND the slot
 *     is not a `YearCount`. Keying it on the proto type alone would reject
 *     `"period":33.0` (a `double` the generator writes with a trailing `.0`);
 *     keying it on the slot kind alone would reject `"recovery_period":27.5`
 *     (an `int32` carrying MACRS's 27.5-year residential life). The
 *     conjunction accepts both and still forbids a fractional `term_months`.
 *
 *   - A LEADING MINUS is permitted only on `values`. That is not a guess: it
 *     is precisely the exemption `mortgage_verification`'s `bound_violation`
 *     makes ("a cash-flow series is the one money slot where a negative
 *     number is the contract"), and it is the only field negative in any gold
 *     row. Everything else is refused a sign at generation time instead of
 *     being refused a verdict afterwards.
 *
 * The value grammar is deliberately SHAPE-only past that point. It does not
 * try to bound a rate at 30% or a term at 1200 months: those are G5, they are
 * already enforced by the verifier against an exact `Decimal`, and encoding
 * them as a character-level automaton would double this file's size to
 * duplicate a check that already exists.
 *
 * ===========================================================================
 * THE `<think>` BLOCK, AND WHEN TO SWITCH THE CONSTRAINT ON
 * ===========================================================================
 *
 * Qwen3 emits a `<think>` block on every response, including correct ones, and
 * a legitimate response may be a clarifying question rather than params at all
 * (46 of 600 gold val rows are). A grammar that forced `<params>` from the
 * first token would forbid both. So the constraint is TRIGGER-ACTIVATED: the
 * service decodes unconstrained, and switches the mask on at the point the
 * model has committed to emitting params.
 *
 * `activation_marker()` is that point. With `wrap_in_params_tags` the
 * automaton's own prelude begins at `<params>`, so the service arms the
 * grammar when the emitted text ends with the marker's prefix; with the option
 * off, the automaton covers the bare object and the service arms it on `{`.
 * Either way `feed_text()` lets the already-emitted prefix be replayed into a
 * fresh automaton, so arming late is not a desync.
 */
module;
#include <array>
#include <cstddef>
#include <cstdint>
#include <expected>
#include <optional>
#include <span>
#include <string>
#include <string_view>
#include <vector>

export module mortgage_grammar;

import mortgage_verification;
import sensen.grammar;

namespace mortgage_calculator::assistant::grammar {

namespace mv = mortgage_calculator::assistant::verify;

// ===========================================================================
// 1. VALUE FORMS.
// ===========================================================================

/** The five shapes a `<params>` value can take on the wire, as distinguished
 * by finance.proto's declared type. This is a JSON-level distinction, not a
 * numeric one: `DecimalString` and `Number` carry the same digit grammar and
 * differ only in whether it is wrapped in quotes, which is exactly the
 * BigDecimal-versus-double split finance.proto's own numeric-types banner
 * describes. */
export enum class ValueForm : std::uint8_t {
    DecimalString, ///< proto `string` (sensen::BigDecimal) -- a QUOTED decimal
    Number,        ///< proto `int32`/`double` -- a bare JSON number
    Boolean,       ///< proto `bool` -- `true` or `false`
    EnumConstant,  ///< a proto enum -- a quoted constant of that enum's set
};

/** Everything the automaton needs to admit or refuse the next character of one
 * field's value. Derived per field in `Schema::build()`; never hand-written. */
export struct ValueShape {
    ValueForm form = ValueForm::Number;
    bool repeated = false;      ///< the value is a `[...]` list of `form`
    bool allow_sign = false;    ///< a leading '-' is admissible (only `values`)
    bool allow_fraction = true; ///< a '.' fraction is admissible
    /** Index into `Schema::enum_table()`, meaningful only for `EnumConstant`. */
    std::uint8_t enum_index = 0xFFU;
};

/** One field of one operation: the name the key must spell, and the shape its
 * value must take. `name` points into `mortgage_verification`'s own static
 * label-space table, so it outlives every automaton. */
export struct FieldPlan {
    std::string_view name;
    ValueShape shape;
};

/** One enum's closed constant set, keyed by the proto type name that appears
 * in `mv::FieldSpec::proto_type`. */
export struct EnumTable {
    std::string_view type_name;
    std::span<const std::string_view> constants;
};

/** The four enums finance.proto's in-scope request messages reach, with their
 * closed constant sets.
 *
 * See the file banner: this is the ONE thing in this module that is not
 * derived, because `mortgage_verification` keeps its equivalent table in an
 * unexported `detail` namespace and so cannot be read through its public API.
 * It is checked in BOTH directions by `tests/test_mortgage_grammar.cpp` --
 * against a fresh parse of finance.proto, and against `verify_mortgage_params`
 * itself, which must accept every constant permitted here and refuse one that
 * is not. */
export inline constexpr std::array<std::string_view, 2> kAnnuityTimingConstants{
    "END_OF_PERIOD", "BEGINNING_OF_PERIOD"};
export inline constexpr std::array<std::string_view, 2> kComponentConstants{"INTEREST",
                                                                            "PRINCIPAL"};
export inline constexpr std::array<std::string_view, 2> kClosingCostTypeConstants{
    "PAID_IN_CASH", "ROLLED_INTO_LOAN"};
export inline constexpr std::array<std::string_view, 4> kMethodConstants{
    "STRAIGHT_LINE", "SUM_OF_YEARS_DIGITS", "DECLINING_BALANCE", "MACRS"};

export inline constexpr std::array<EnumTable, 4> kEnumTables{{
    {.type_name = "AnnuityTiming", .constants = kAnnuityTimingConstants},
    {.type_name = "Component", .constants = kComponentConstants},
    {.type_name = "ClosingCostType", .constants = kClosingCostTypeConstants},
    {.type_name = "Method", .constants = kMethodConstants},
}};

// ===========================================================================
// 2. OPTIONS.
// ===========================================================================

export struct GrammarOptions {
    /** The automaton covers `<params>` ... `</params>` rather than the bare
     * object. On by default because that is the whole response shape the model
     * was trained to emit, and because it gives the decoder an unambiguous
     * activation marker and an unambiguous stopping point. */
    bool wrap_in_params_tags = true;

    /** Keys must appear in finance.proto's declaration order (554/554 gold
     * rows do). Turning this off makes the key phase a membership test over
     * the fields not yet emitted -- still closed, still complete, just
     * order-free. `params_regex()` requires this to be on, because the
     * order-free language is a permutation blow-up no regex should carry. */
    bool require_declaration_order = true;
};

// ===========================================================================
// 3. THE SCHEMA -- the derived label space, compiled once.
// ===========================================================================

/**
 * The 26 operations and their 160 fields, projected out of
 * `mortgage_verification`'s exported API into the form the automaton walks.
 *
 * Build it once and share it: it is immutable after construction and every
 * automaton and grammar holds a reference to it.
 */
export class Schema {
  public:
    /** Derive the schema. Fails -- rather than guessing -- if the label space
     * contains a proto type this module cannot map to a value form, which is
     * exactly what happens the day finance.proto grows a fifth enum. */
    [[nodiscard]] static auto build(GrammarOptions options = {})
        -> std::expected<Schema, std::string>;

    [[nodiscard]] auto options() const noexcept -> const GrammarOptions& { return options_; }
    [[nodiscard]] auto operation_count() const noexcept -> std::size_t { return ops_.size(); }
    [[nodiscard]] auto field_count() const noexcept -> std::size_t { return field_count_; }

    [[nodiscard]] auto operation_at(std::size_t index) const -> std::string_view {
        return ops_[index].id;
    }
    [[nodiscard]] auto fields_at(std::size_t index) const -> std::span<const FieldPlan> {
        return ops_[index].fields;
    }
    [[nodiscard]] auto index_of_operation(std::string_view id) const -> std::optional<std::size_t>;

    [[nodiscard]] auto enum_table() const noexcept -> std::span<const EnumTable> {
        return std::span<const EnumTable>{kEnumTables};
    }

    /** The literal every accepted string starts with: `<params>{"operation":"`
     * (or the same without the tag). */
    [[nodiscard]] auto prelude() const noexcept -> std::string_view { return prelude_; }
    /** `</params>`, or empty when `wrap_in_params_tags` is off. */
    [[nodiscard]] auto postlude() const noexcept -> std::string_view { return postlude_; }

    /** The text a decoder should watch for to know the model has committed to
     * emitting params, and where a trigger-activated constraint should be
     * armed: `<params>` when wrapped, `{` when not. */
    [[nodiscard]] auto activation_marker() const noexcept -> std::string_view {
        return options_.wrap_in_params_tags ? std::string_view{"<params>"} : std::string_view{"{"};
    }

    /** The widest field list any one operation has. The automaton carries the
     * not-yet-emitted field set as a `std::uint32_t` bitmask, so this being
     * under 32 is a precondition, not a comment -- `build()` refuses a schema
     * that breaks it. */
    static constexpr std::size_t kMaxFieldsPerOperation = 32;

  private:
    struct OpPlan {
        std::string_view id;
        std::vector<FieldPlan> fields;
    };

    Schema() = default;

    GrammarOptions options_{};
    std::vector<OpPlan> ops_;
    std::size_t field_count_ = 0;
    std::string prelude_;
    std::string postlude_;
};

/** The schema every caller should share unless it needs different options:
 * `<params>`-wrapped, declaration order enforced. Returned as the
 * `std::expected` `build()` produced, because a schema that could not be
 * derived is a fact the caller must handle, not one to hide behind a throw. */
export [[nodiscard]] auto default_schema() -> const std::expected<Schema, std::string>&;

/**
 * The drift gate, callable from production and from the test.
 *
 * Re-checks the built schema against `mortgage_verification`'s exported label
 * space: the same operation ids, the same fields in the same order, 160 in
 * total, and an enum table entry for every enum-typed field. Deriving from
 * that module makes agreement structural rather than coincidental, so this
 * catches the residue -- an enum finance.proto grows that `kEnumTables` does
 * not know, a field whose proto type maps to no value form, an operation with
 * more fields than the bitmask holds.
 */
export [[nodiscard]] auto validate_label_space(const Schema& schema)
    -> std::expected<void, std::string>;

// ===========================================================================
// 4. THE CHARACTER-LEVEL AUTOMATON.
// ===========================================================================

/**
 * An incremental, character-level constraint over one `<params>` object.
 *
 * `feed()` advances it, `allowed_next_chars()` reports what may come next, and
 * `complete()` says whether the emitted text is a whole, well-formed params
 * object. Every mutable bit of it lives in one trivially-copyable `State`, so
 * trial-feeding a candidate token is a struct copy and a handful of branches
 * -- which is what makes a per-token mask over a 150k vocabulary affordable.
 *
 * A rejected character leaves the automaton unchanged, so a caller may probe
 * and continue. A rejected character never poisons it.
 */
export class ParamsAutomaton {
  public:
    explicit ParamsAutomaton(const Schema& schema) : schema_(&schema) {}

    /** Consume one character. Returns false and changes nothing if that
     * character cannot keep the output on a valid path. */
    auto feed(char c) -> bool;

    /** Consume every character of `text`, stopping at the first rejection.
     * On rejection the automaton is left where the last accepted character put
     * it. Returns true iff all of `text` was accepted. */
    auto feed_text(std::string_view text) -> bool;

    /** Whether `text` would be accepted in full from the CURRENT state,
     * without advancing it. */
    [[nodiscard]] auto would_accept(std::string_view text) const -> bool;

    /** One entry per ASCII code point: true iff `feed(c)` would succeed. The
     * automaton emits only ASCII, so bytes >= 0x80 are never permitted. */
    [[nodiscard]] auto allowed_next_chars() const -> std::array<bool, 128>;

    /** A whole, closed params object (including `</params>` when wrapped) has
     * been emitted and nothing further is admissible. */
    [[nodiscard]] auto complete() const noexcept -> bool;

    /** The operation the emitted text has already committed to, once its id
     * has been closed. Empty before that. */
    [[nodiscard]] auto resolved_operation() const -> std::optional<std::string_view>;

    /** The fields of the resolved operation not yet emitted, in declaration
     * order. Empty before the operation resolves. */
    [[nodiscard]] auto pending_fields() const -> std::vector<std::string_view>;

    auto reset() noexcept -> void;

    [[nodiscard]] auto schema() const noexcept -> const Schema& { return *schema_; }

    /** Whole-string membership: does `text` spell a complete params object? */
    [[nodiscard]] static auto accepts(const Schema& schema, std::string_view text) -> bool;

  private:
    enum class Phase : std::uint8_t {
        Prelude,     ///< matching `<params>{"operation":"`
        Name,        ///< matching an operation id / field key / enum constant
        KeyOpen,     ///< expecting the `"` that opens a field key
        KeyColon,    ///< expecting the `:` after a field key
        ValueStart,  ///< expecting the first character of a value or element
        Number,      ///< inside a (possibly quoted) decimal
        BoolLiteral, ///< inside `true` / `false`
        AfterElement,///< an array element closed: expecting `,` or `]`
        SepOrClose,  ///< a field value closed: expecting `,` or `}`
        Postlude,    ///< matching `</params>`
        Done,        ///< nothing further is admissible
    };

    enum class NumPhase : std::uint8_t { Start, Sign, Zero, Int, Dot, Frac };

    static constexpr std::uint8_t kNoIndex = 0xFFU;

    /** All of the automaton's mutable state, deliberately trivially copyable:
     * no strings, no vectors, no pointers. Everything that varies is an index
     * into the (immutable, shared) schema. */
    struct State {
        Phase phase = Phase::Prelude;
        NumPhase num = NumPhase::Start;
        std::uint16_t lit_pos = 0;  ///< position within prelude/postlude/bool literal
        std::uint16_t name_pos = 0; ///< characters of the name matched so far
        std::uint32_t cand = 0;     ///< candidate names still consistent with name_pos chars
        std::uint32_t remaining = 0;///< fields of the operation not yet emitted
        std::uint8_t op = kNoIndex;
        std::uint8_t field = kNoIndex;
        std::uint8_t name_target = 0; ///< 0 operation, 1 field key, 2 enum constant
        std::uint8_t bool_lit = 0;    ///< 1 `true`, 2 `false`
        bool in_array = false;
        bool quoted = false;
    };

    [[nodiscard]] auto step(State& st, char c) const -> bool;
    [[nodiscard]] auto step_name(State& st, char c) const -> bool;
    [[nodiscard]] auto step_value_start(State& st, char c) const -> bool;
    [[nodiscard]] auto step_number(State& st, char c) const -> bool;
    [[nodiscard]] auto step_bool(State& st, char c) const -> bool;
    [[nodiscard]] auto shape_of(const State& st) const -> const ValueShape&;
    [[nodiscard]] auto name_candidate(const State& st, std::size_t bit) const -> std::string_view;
    [[nodiscard]] auto name_candidate_count(const State& st) const -> std::size_t;
    auto close_value(State& st) const -> void;
    auto close_array(State& st) const -> void;
    auto finish_object(State& st) const -> void;

    const Schema* schema_;
    State state_{};
};

// ===========================================================================
// 5. THE TOKEN-LEVEL GRAMMAR -- a sensen::IGrammar.
// ===========================================================================

/**
 * `ParamsAutomaton` lifted to a vocabulary, as the interface sensen's sampler
 * already speaks.
 *
 * Construction takes the id -> text table once (the same table
 * `sensen::JsonObjectGrammar` and `sensen::RegexGrammar` take) and, optionally,
 * the EOS id, which is permitted exactly when the automaton is complete -- so
 * generation can stop on a closed params object and cannot stop inside one.
 *
 * Cost per step is O(vocab * token length), the same order as sensen's own two
 * grammars, but with a far cheaper inner step: a first-character reject using
 * the precomputed `allowed_next_chars()` table eliminates the great majority
 * of a 150k vocabulary before any state is copied.
 */
export class MortgageParamsGrammar final : public sensen::IGrammar {
  public:
    MortgageParamsGrammar(const Schema& schema, std::vector<std::string> vocab_text,
                          std::optional<std::uint32_t> eos = std::nullopt);

    [[nodiscard]] auto allowedMask() const -> const std::vector<bool>& override { return mask_; }
    [[nodiscard]] auto accept(std::uint32_t token_id) -> bool override;
    [[nodiscard]] auto isComplete() const -> bool override { return automaton_.complete(); }
    auto reset() -> void override;

    /** The allowed set as ids rather than a mask, for a caller that wants to
     * inspect or log it rather than multiply it into logits. */
    [[nodiscard]] auto allowed_token_ids() const -> std::vector<std::uint32_t>;

    /** Everything accepted so far, which for a completed grammar is the whole
     * `<params>...</params>` string ready to hand to the verifier. */
    [[nodiscard]] auto text() const noexcept -> std::string_view { return text_; }

    /** Replay an already-emitted prefix into the grammar, for the
     * trigger-activated integration described in the file banner: decode
     * unconstrained until the model commits to params, then arm the constraint
     * with the text it has already produced. Returns false if that prefix is
     * not on a valid path, in which case the caller must not proceed as though
     * it were. */
    [[nodiscard]] auto prime(std::string_view emitted) -> bool;

    [[nodiscard]] auto automaton() const noexcept -> const ParamsAutomaton& { return automaton_; }

  private:
    auto recompute() -> void;

    std::vector<std::string> vocab_;
    std::optional<std::uint32_t> eos_;
    ParamsAutomaton automaton_;
    std::vector<bool> mask_;
    std::string text_;
};

// ===========================================================================
// 6. THE REGEX PROJECTION -- the no-code-change path through GenerationConfig.
// ===========================================================================

/**
 * The same language as a pattern `sensen::RegexNfa::compile` accepts, for
 * `GenerationConfig{.grammar_kind = GrammarKind::Regex, .grammar_pattern = ...}`.
 *
 * Uses only the documented subset: literals, `[a-z]` classes, `*` `+` `?`,
 * alternation, groups and backslash-escaped literals. No `{n,m}`, which that
 * parser does not have.
 *
 * Refused (rather than silently approximated) when
 * `require_declaration_order` is off: the order-free language is every
 * permutation of up to fourteen keys, and a regex spelling that out is not a
 * thing to generate. Use the automaton for that mode.
 *
 * Read the file banner's cost note before choosing this path over the
 * automaton.
 */
export [[nodiscard]] auto params_regex(const Schema& schema) -> std::expected<std::string, std::string>;

}  // namespace mortgage_calculator::assistant::grammar

// ===========================================================================
// IMPLEMENTATION
// ===========================================================================

namespace mortgage_calculator::assistant::grammar {

namespace detail {

[[nodiscard]] inline auto enum_index_of(std::string_view proto_type) -> std::optional<std::uint8_t> {
    for (std::size_t i = 0; i < kEnumTables.size(); ++i) {
        if (kEnumTables[i].type_name == proto_type) {
            return static_cast<std::uint8_t>(i);
        }
    }
    return std::nullopt;
}

/** finance.proto's scalar numeric and string types, as they appear in
 * `mv::FieldSpec::proto_type`. Anything outside this set and the enum table is
 * a type this module has no value form for, and `Schema::build()` says so
 * rather than defaulting. */
[[nodiscard]] inline auto is_bare_number_type(std::string_view t) -> bool {
    return t == "int32" || t == "int64" || t == "uint32" || t == "uint64" || t == "sint32" ||
           t == "sint64" || t == "double" || t == "float";
}

/**
 * The value shape of one field.
 *
 * The two judgement calls here are the ones the file banner justifies from the
 * gold data: fractions, and the sign.
 */
[[nodiscard]] inline auto shape_for(const mv::FieldSpec& f)
    -> std::expected<ValueShape, std::string> {
    ValueShape shape;
    shape.repeated = f.repeated;

    // `values` is the one slot where a negative is the contract rather than an
    // error -- the same exemption mortgage_verification's bound_violation
    // makes for a cash-flow series, and the only field negative in any gold row.
    shape.allow_sign = (f.field == "values");

    if (const auto idx = enum_index_of(f.proto_type); idx.has_value()) {
        shape.form = ValueForm::EnumConstant;
        shape.enum_index = *idx;
        shape.allow_fraction = false;
        shape.allow_sign = false;
        return shape;
    }
    if (f.proto_type == "bool") {
        shape.form = ValueForm::Boolean;
        shape.allow_fraction = false;
        shape.allow_sign = false;
        return shape;
    }
    if (f.proto_type == "string") {
        shape.form = ValueForm::DecimalString;
        shape.allow_fraction = true;
        return shape;
    }
    if (is_bare_number_type(f.proto_type)) {
        shape.form = ValueForm::Number;
        // A fraction is admissible unless the wire type is a 32-bit integer
        // AND the slot is not a year count. `recovery_period` is exactly that
        // exception: an int32 carrying MACRS's 27.5-year residential life.
        const bool integral_wire = (f.proto_type == "int32");
        const bool year_count = (mv::classify_slot(f.field) == mv::SlotKind::YearCount);
        shape.allow_fraction = !integral_wire || year_count;
        return shape;
    }
    return std::unexpected(std::string{f.operation} + "." + std::string{f.field} +
                           " has proto type \"" + std::string{f.proto_type} +
                           "\", which this module has no value form for -- if finance.proto grew "
                           "an enum, add it to Schema::kEnumTables");
}

}  // namespace detail

auto Schema::build(GrammarOptions options) -> std::expected<Schema, std::string> {
    Schema schema;
    schema.options_ = options;
    schema.prelude_ = options.wrap_in_params_tags ? R"(<params>{"operation":")"
                                                  : R"({"operation":")";
    schema.postlude_ = options.wrap_in_params_tags ? "</params>" : "";

    const auto ids = mv::operation_ids();
    if (ids.empty()) {
        return std::unexpected("mortgage_verification reports an empty operation set");
    }
    if (ids.size() > 64) {
        return std::unexpected("more operations than the candidate bitmask holds");
    }

    schema.ops_.reserve(ids.size());
    for (const auto id : ids) {
        const auto fields = mv::fields_of(id);
        if (fields.empty()) {
            return std::unexpected(std::string{id} + " has no declared fields");
        }
        if (fields.size() > kMaxFieldsPerOperation) {
            return std::unexpected(std::string{id} + " declares " + std::to_string(fields.size()) +
                                   " fields, more than the " +
                                   std::to_string(kMaxFieldsPerOperation) +
                                   " the automaton's field bitmask holds");
        }
        OpPlan plan;
        plan.id = id;
        plan.fields.reserve(fields.size());
        for (const auto& f : fields) {
            auto shape = detail::shape_for(f);
            if (!shape.has_value()) {
                return std::unexpected(shape.error());
            }
            plan.fields.push_back(FieldPlan{.name = f.field, .shape = *shape});
        }
        schema.field_count_ += plan.fields.size();
        schema.ops_.push_back(std::move(plan));
    }
    return schema;
}

auto Schema::index_of_operation(std::string_view id) const -> std::optional<std::size_t> {
    for (std::size_t i = 0; i < ops_.size(); ++i) {
        if (ops_[i].id == id) {
            return i;
        }
    }
    return std::nullopt;
}

auto default_schema() -> const std::expected<Schema, std::string>& {
    static const std::expected<Schema, std::string> instance = Schema::build();
    return instance;
}

auto validate_label_space(const Schema& schema) -> std::expected<void, std::string> {
    const auto ids = mv::operation_ids();
    if (schema.operation_count() != ids.size()) {
        return std::unexpected("schema has " + std::to_string(schema.operation_count()) +
                               " operations, mortgage_verification has " +
                               std::to_string(ids.size()));
    }
    std::size_t counted = 0;
    for (std::size_t i = 0; i < ids.size(); ++i) {
        if (schema.operation_at(i) != ids[i]) {
            return std::unexpected("operation " + std::to_string(i) + ": schema says \"" +
                                   std::string{schema.operation_at(i)} +
                                   "\", mortgage_verification says \"" + std::string{ids[i]} + "\"");
        }
        const auto declared = mv::fields_of(ids[i]);
        const auto planned = schema.fields_at(i);
        if (declared.size() != planned.size()) {
            return std::unexpected(std::string{ids[i]} + ": schema plans " +
                                   std::to_string(planned.size()) + " fields, " +
                                   "mortgage_verification declares " +
                                   std::to_string(declared.size()));
        }
        for (std::size_t j = 0; j < declared.size(); ++j) {
            if (declared[j].field != planned[j].name) {
                return std::unexpected(std::string{ids[i]} + " field " + std::to_string(j) +
                                       ": schema says \"" + std::string{planned[j].name} +
                                       "\", mortgage_verification says \"" +
                                       std::string{declared[j].field} + "\"");
            }
            if (mv::find_field(ids[i], planned[j].name) == nullptr) {
                return std::unexpected(std::string{ids[i]} + "." + std::string{planned[j].name} +
                                       " is not a field mortgage_verification recognises");
            }
            if (planned[j].shape.form == ValueForm::EnumConstant) {
                if (planned[j].shape.enum_index >= schema.enum_table().size()) {
                    return std::unexpected(std::string{ids[i]} + "." +
                                           std::string{planned[j].name} +
                                           " is enum-typed with no constant table");
                }
                if (schema.enum_table()[planned[j].shape.enum_index].constants.empty()) {
                    return std::unexpected(std::string{ids[i]} + "." +
                                           std::string{planned[j].name} +
                                           " has an empty enum constant set");
                }
            }
        }
        counted += planned.size();
    }
    if (counted != schema.field_count()) {
        return std::unexpected("field count disagrees with the sum of the per-operation plans");
    }
    return {};
}

// ---------------------------------------------------------------------------
// ParamsAutomaton
// ---------------------------------------------------------------------------

auto ParamsAutomaton::reset() noexcept -> void { state_ = State{}; }

auto ParamsAutomaton::complete() const noexcept -> bool { return state_.phase == Phase::Done; }

auto ParamsAutomaton::resolved_operation() const -> std::optional<std::string_view> {
    if (state_.op == kNoIndex) {
        return std::nullopt;
    }
    return schema_->operation_at(state_.op);
}

auto ParamsAutomaton::pending_fields() const -> std::vector<std::string_view> {
    std::vector<std::string_view> out;
    if (state_.op == kNoIndex) {
        return out;
    }
    const auto fields = schema_->fields_at(state_.op);
    for (std::size_t i = 0; i < fields.size(); ++i) {
        if ((state_.remaining & (1U << i)) != 0U) {
            out.push_back(fields[i].name);
        }
    }
    return out;
}

auto ParamsAutomaton::feed(char c) -> bool {
    State probe = state_;
    if (!step(probe, c)) {
        return false;
    }
    state_ = probe;
    return true;
}

auto ParamsAutomaton::feed_text(std::string_view text) -> bool {
    for (const char c : text) {
        if (!feed(c)) {
            return false;
        }
    }
    return true;
}

auto ParamsAutomaton::would_accept(std::string_view text) const -> bool {
    State probe = state_;
    for (const char c : text) {
        if (!step(probe, c)) {
            return false;
        }
    }
    return true;
}

auto ParamsAutomaton::allowed_next_chars() const -> std::array<bool, 128> {
    std::array<bool, 128> allowed{};
    for (std::size_t c = 0; c < allowed.size(); ++c) {
        State probe = state_;
        allowed[c] = step(probe, static_cast<char>(c));
    }
    return allowed;
}

auto ParamsAutomaton::accepts(const Schema& schema, std::string_view text) -> bool {
    ParamsAutomaton a{schema};
    return a.feed_text(text) && a.complete();
}

auto ParamsAutomaton::shape_of(const State& st) const -> const ValueShape& {
    return schema_->fields_at(st.op)[st.field].shape;
}

auto ParamsAutomaton::name_candidate_count(const State& st) const -> std::size_t {
    switch (st.name_target) {
        case 0:
            return schema_->operation_count();
        case 1:
            return schema_->fields_at(st.op).size();
        default:
            return schema_->enum_table()[shape_of(st).enum_index].constants.size();
    }
}

auto ParamsAutomaton::name_candidate(const State& st, std::size_t bit) const -> std::string_view {
    switch (st.name_target) {
        case 0:
            return schema_->operation_at(bit);
        case 1:
            return schema_->fields_at(st.op)[bit].name;
        default:
            return schema_->enum_table()[shape_of(st).enum_index].constants[bit];
    }
}

auto ParamsAutomaton::close_value(State& st) const -> void {
    if (st.in_array) {
        st.phase = Phase::AfterElement;
        return;
    }
    st.remaining &= ~(1U << st.field);
    st.phase = Phase::SepOrClose;
}

auto ParamsAutomaton::close_array(State& st) const -> void {
    st.in_array = false;
    st.remaining &= ~(1U << st.field);
    st.phase = Phase::SepOrClose;
}

auto ParamsAutomaton::finish_object(State& st) const -> void {
    if (schema_->postlude().empty()) {
        st.phase = Phase::Done;
        return;
    }
    st.phase = Phase::Postlude;
    st.lit_pos = 0;
}

auto ParamsAutomaton::step_name(State& st, char c) const -> bool {
    if (c == '"') {
        // A name closes only on an EXACT match. This is what stops
        // `ComputeFutureValue` from closing when the model meant
        // `ComputeFutureValueDetailed` -- and, read the other way, what stops
        // the shorter id being unreachable because the longer one exists.
        std::optional<std::size_t> exact;
        for (std::size_t i = 0; i < name_candidate_count(st); ++i) {
            if ((st.cand & (1U << i)) == 0U) {
                continue;
            }
            if (name_candidate(st, i).size() == st.name_pos) {
                exact = i;
                break;
            }
        }
        if (!exact.has_value()) {
            return false;
        }
        switch (st.name_target) {
            case 0: {
                st.op = static_cast<std::uint8_t>(*exact);
                const auto n = schema_->fields_at(st.op).size();
                st.remaining = (n >= 32) ? 0xFFFFFFFFU : ((1U << n) - 1U);
                st.phase = Phase::SepOrClose;
                return true;
            }
            case 1:
                st.field = static_cast<std::uint8_t>(*exact);
                st.phase = Phase::KeyColon;
                return true;
            default:
                close_value(st);
                return true;
        }
    }

    std::uint32_t next = 0;
    for (std::size_t i = 0; i < name_candidate_count(st); ++i) {
        if ((st.cand & (1U << i)) == 0U) {
            continue;
        }
        const auto name = name_candidate(st, i);
        if (st.name_pos < name.size() && name[st.name_pos] == c) {
            next |= (1U << i);
        }
    }
    if (next == 0U) {
        return false;
    }
    st.cand = next;
    ++st.name_pos;
    return true;
}

auto ParamsAutomaton::step_value_start(State& st, char c) const -> bool {
    const ValueShape& shape = shape_of(st);
    if (shape.repeated && !st.in_array) {
        // A repeated field's value opens with '['. Empty lists are not
        // admissible: an element must follow.
        if (c != '[') {
            return false;
        }
        st.in_array = true;
        return true;
    }
    switch (shape.form) {
        case ValueForm::DecimalString:
            if (c != '"') {
                return false;
            }
            st.quoted = true;
            st.num = NumPhase::Start;
            st.phase = Phase::Number;
            return true;
        case ValueForm::Number:
            st.quoted = false;
            st.num = NumPhase::Start;
            st.phase = Phase::Number;
            return step_number(st, c);
        case ValueForm::Boolean:
            if (c == 't') {
                st.bool_lit = 1;
            } else if (c == 'f') {
                st.bool_lit = 2;
            } else {
                return false;
            }
            st.lit_pos = 1;
            st.phase = Phase::BoolLiteral;
            return true;
        case ValueForm::EnumConstant: {
            if (c != '"') {
                return false;
            }
            st.phase = Phase::Name;
            st.name_target = 2;
            st.name_pos = 0;
            const auto n = schema_->enum_table()[shape.enum_index].constants.size();
            st.cand = (1U << n) - 1U;
            return true;
        }
    }
    return false;
}

auto ParamsAutomaton::step_bool(State& st, char c) const -> bool {
    const std::string_view lit = (st.bool_lit == 1) ? std::string_view{"true"}
                                                    : std::string_view{"false"};
    if (st.lit_pos >= lit.size() || lit[st.lit_pos] != c) {
        return false;
    }
    ++st.lit_pos;
    if (st.lit_pos == lit.size()) {
        close_value(st);
    }
    return true;
}

auto ParamsAutomaton::step_number(State& st, char c) const -> bool {
    const ValueShape& shape = shape_of(st);
    const auto is_digit = [](char ch) { return ch >= '0' && ch <= '9'; };

    switch (st.num) {
        case NumPhase::Start:
            if (c == '-' && shape.allow_sign) {
                st.num = NumPhase::Sign;
                return true;
            }
            if (c == '0') {
                st.num = NumPhase::Zero;
                return true;
            }
            if (c >= '1' && c <= '9') {
                st.num = NumPhase::Int;
                return true;
            }
            return false;
        case NumPhase::Sign:
            if (c == '0') {
                st.num = NumPhase::Zero;
                return true;
            }
            if (c >= '1' && c <= '9') {
                st.num = NumPhase::Int;
                return true;
            }
            return false;
        case NumPhase::Zero:
            // JSON forbids a second integer digit after a leading zero, so the
            // only continuation is a fraction; anything else must terminate.
            if (c == '.' && shape.allow_fraction) {
                st.num = NumPhase::Dot;
                return true;
            }
            break;
        case NumPhase::Int:
            if (is_digit(c)) {
                return true;
            }
            if (c == '.' && shape.allow_fraction) {
                st.num = NumPhase::Dot;
                return true;
            }
            break;
        case NumPhase::Dot:
            if (is_digit(c)) {
                st.num = NumPhase::Frac;
                return true;
            }
            return false;
        case NumPhase::Frac:
            if (is_digit(c)) {
                return true;
            }
            break;
    }

    // Not part of the number. It can only be a terminator, and only from a
    // digit-settled state -- `1.` and `-` do not end a value.
    const bool settled = (st.num == NumPhase::Zero || st.num == NumPhase::Int ||
                          st.num == NumPhase::Frac);
    if (!settled) {
        return false;
    }
    if (st.quoted) {
        // A quoted decimal is closed by its own quote, and by nothing else.
        if (c != '"') {
            return false;
        }
        st.quoted = false;
        close_value(st);
        return true;
    }
    // A bare number has no closing character of its own: the separator that
    // follows it both ends it and moves the object on. Close the value, then
    // let the separator phase consume the very same character.
    close_value(st);
    return step(st, c);
}

auto ParamsAutomaton::step(State& st, char c) const -> bool {
    switch (st.phase) {
        case Phase::Prelude: {
            const auto prelude = schema_->prelude();
            if (st.lit_pos >= prelude.size() || prelude[st.lit_pos] != c) {
                return false;
            }
            ++st.lit_pos;
            if (st.lit_pos == prelude.size()) {
                st.phase = Phase::Name;
                st.name_target = 0;
                st.name_pos = 0;
                const auto n = schema_->operation_count();
                st.cand = (n >= 32) ? 0xFFFFFFFFU : ((1U << n) - 1U);
            }
            return true;
        }
        case Phase::Name:
            return step_name(st, c);
        case Phase::KeyOpen: {
            if (c != '"') {
                return false;
            }
            st.phase = Phase::Name;
            st.name_target = 1;
            st.name_pos = 0;
            // In declaration-order mode exactly one key may come next: the
            // lowest field not yet emitted. Otherwise any field not yet
            // emitted may. Both are closed sets; one is a singleton.
            if (schema_->options().require_declaration_order) {
                std::uint32_t lowest = st.remaining & (~st.remaining + 1U);
                st.cand = lowest;
            } else {
                st.cand = st.remaining;
            }
            return st.cand != 0U;
        }
        case Phase::KeyColon:
            if (c != ':') {
                return false;
            }
            st.phase = Phase::ValueStart;
            st.in_array = false;
            st.quoted = false;
            return true;
        case Phase::ValueStart:
            return step_value_start(st, c);
        case Phase::Number:
            return step_number(st, c);
        case Phase::BoolLiteral:
            return step_bool(st, c);
        case Phase::AfterElement:
            if (c == ',') {
                st.phase = Phase::ValueStart;
                return true;
            }
            if (c == ']') {
                close_array(st);
                return true;
            }
            return false;
        case Phase::SepOrClose:
            if (c == ',') {
                // A comma promises another field, so it is admissible only
                // while one is still owed. This is the half of the contract
                // that makes a SHORT object unrepresentable.
                if (st.remaining == 0U) {
                    return false;
                }
                st.phase = Phase::KeyOpen;
                return true;
            }
            if (c == '}') {
                // And this is the half that makes an INCOMPLETE one
                // unrepresentable: the object cannot close while a declared
                // field is still owed.
                if (st.remaining != 0U) {
                    return false;
                }
                finish_object(st);
                return true;
            }
            return false;
        case Phase::Postlude: {
            const auto postlude = schema_->postlude();
            if (st.lit_pos >= postlude.size() || postlude[st.lit_pos] != c) {
                return false;
            }
            ++st.lit_pos;
            if (st.lit_pos == postlude.size()) {
                st.phase = Phase::Done;
            }
            return true;
        }
        case Phase::Done:
            return false;
    }
    return false;
}

// ---------------------------------------------------------------------------
// MortgageParamsGrammar
// ---------------------------------------------------------------------------

MortgageParamsGrammar::MortgageParamsGrammar(const Schema& schema,
                                             std::vector<std::string> vocab_text,
                                             std::optional<std::uint32_t> eos)
    : vocab_(std::move(vocab_text)), eos_(eos), automaton_(schema), mask_(vocab_.size(), false) {
    recompute();
}

auto MortgageParamsGrammar::recompute() -> void {
    const auto allowed_chars = automaton_.allowed_next_chars();
    const bool done = automaton_.complete();
    for (std::size_t i = 0; i < vocab_.size(); ++i) {
        const std::string& text = vocab_[i];
        // An empty-text token (a control token) can never advance the
        // automaton, so it is never allowed -- the same rule sensen's
        // JsonObjectGrammar applies.
        if (text.empty()) {
            mask_[i] = false;
            continue;
        }
        const auto first = static_cast<unsigned char>(text.front());
        if (first >= allowed_chars.size() || !allowed_chars[first]) {
            mask_[i] = false;
            continue;
        }
        mask_[i] = automaton_.would_accept(text);
    }
    if (eos_.has_value() && *eos_ < mask_.size()) {
        // EOS exactly when a complete params object has been emitted: the
        // decode cannot stop mid-object, and cannot continue past a closed one.
        mask_[*eos_] = done;
    }
}

auto MortgageParamsGrammar::accept(std::uint32_t token_id) -> bool {
    if (token_id >= vocab_.size()) {
        return eos_.has_value() && token_id == *eos_ && automaton_.complete();
    }
    if (!mask_[token_id]) {
        return false;
    }
    if (eos_.has_value() && token_id == *eos_) {
        return automaton_.complete();
    }
    if (!automaton_.feed_text(vocab_[token_id])) {
        return false; // mask and automaton disagreed -- defensive, never expected
    }
    text_ += vocab_[token_id];
    recompute();
    return true;
}

auto MortgageParamsGrammar::reset() -> void {
    automaton_.reset();
    text_.clear();
    recompute();
}

auto MortgageParamsGrammar::prime(std::string_view emitted) -> bool {
    if (!automaton_.feed_text(emitted)) {
        return false;
    }
    text_.append(emitted);
    recompute();
    return true;
}

auto MortgageParamsGrammar::allowed_token_ids() const -> std::vector<std::uint32_t> {
    std::vector<std::uint32_t> ids;
    for (std::size_t i = 0; i < mask_.size(); ++i) {
        if (mask_[i]) {
            ids.push_back(static_cast<std::uint32_t>(i));
        }
    }
    return ids;
}

// ---------------------------------------------------------------------------
// params_regex
// ---------------------------------------------------------------------------

namespace detail {

/** Escape the three characters `sensen::RegexNfa`'s parser treats specially in
 * atom position and that this pattern actually contains. Braces, commas,
 * colons and quotes are literals in that grammar and are left alone. */
[[nodiscard]] inline auto escape_regex_literal(std::string_view text) -> std::string {
    std::string out;
    out.reserve(text.size());
    for (const char c : text) {
        if (c == '[' || c == ']' || c == '.' || c == '\\' || c == '(' || c == ')' || c == '|' ||
            c == '*' || c == '+' || c == '?' || c == '{' || c == '}') {
            out.push_back('\\');
        }
        out.push_back(c);
    }
    return out;
}

[[nodiscard]] inline auto number_body(const ValueShape& shape) -> std::string {
    std::string out;
    if (shape.allow_sign) {
        out += "-?";
    }
    out += "(0|[1-9][0-9]*)";
    if (shape.allow_fraction) {
        out += R"((\.[0-9]+)?)";
    }
    return out;
}

[[nodiscard]] inline auto scalar_pattern(const Schema& schema, const ValueShape& shape)
    -> std::string {
    switch (shape.form) {
        case ValueForm::DecimalString:
            return "\"" + number_body(shape) + "\"";
        case ValueForm::Number:
            return "(" + number_body(shape) + ")";
        case ValueForm::Boolean:
            return "(true|false)";
        case ValueForm::EnumConstant: {
            std::string out = "(";
            const auto constants = schema.enum_table()[shape.enum_index].constants;
            for (std::size_t i = 0; i < constants.size(); ++i) {
                if (i != 0) {
                    out += "|";
                }
                out += "\"" + std::string{constants[i]} + "\"";
            }
            out += ")";
            return out;
        }
    }
    return {};
}

}  // namespace detail

auto params_regex(const Schema& schema) -> std::expected<std::string, std::string> {
    if (!schema.options().require_declaration_order) {
        return std::unexpected(
            "params_regex requires require_declaration_order: the order-free language is every "
            "permutation of up to 14 keys, which is not a pattern to generate -- drive "
            "ParamsAutomaton directly for that mode");
    }

    std::string out = "(";
    for (std::size_t op = 0; op < schema.operation_count(); ++op) {
        if (op != 0) {
            out += "|";
        }
        out += detail::escape_regex_literal(schema.prelude());
        out += std::string{schema.operation_at(op)};
        out += "\"";
        for (const auto& field : schema.fields_at(op)) {
            out += ",\"" + std::string{field.name} + "\":";
            const std::string scalar = detail::scalar_pattern(schema, field.shape);
            if (scalar.empty()) {
                return std::unexpected(std::string{schema.operation_at(op)} + "." +
                                       std::string{field.name} + " has no scalar pattern");
            }
            if (field.shape.repeated) {
                out += R"(\[)" + scalar + "(," + scalar + R"()*\])";
            } else {
                out += scalar;
            }
        }
        out += detail::escape_regex_literal("}");
        out += detail::escape_regex_literal(schema.postlude());
    }
    out += ")";
    return out;
}

}  // namespace mortgage_calculator::assistant::grammar
