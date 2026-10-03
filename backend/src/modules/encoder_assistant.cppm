/**
 * encoder_assistant -- the small-encoder mortgage assistant, end to end, in one place.
 *
 * @author Olumuyiwa Oluwasanmi
 *
 * ===========================================================================
 * WHAT THIS IS
 * ===========================================================================
 * The whole serving chain for the ~1M-parameter bidirectional encoder that replaces a
 * fine-tuned Qwen3-0.6B on the mortgage surface:
 *
 *     lex -> tokenize -> encode -> mask -> reconstruct -> FinanceParams
 *
 * THE MODEL NEVER EMITS A DIGIT. It names an operation, points at numeric spans in the
 * user's own text and names a MAP; every parameter VALUE is then computed here, in
 * 256-bit `BigDecimal`. That is what makes the documented dangerous failure --
 * `present_value = 304000.00` against a 495,000 utterance -- unrepresentable rather than
 * merely refused: there is no channel through which the model could say it.
 *
 * ---------------------------------------------------------------------------
 * ONE ARTEFACT, AND EVERY TABLE DERIVED FROM IT
 * ---------------------------------------------------------------------------
 * A single GGUF carries the weights, the label SCHEMA (`sensen-encoder.schema_json`) and
 * the TOKENIZER (`tokenizer.ggml.*`, vocabulary and normaliser). Nothing here holds a
 * second copy of the label space, the vocabulary or the normaliser, because this
 * repository has paid for that four times -- the label space in four tables plus
 * `ALLOWED_OPERATIONS` in a fifth repository, in another project.
 *
 * ---------------------------------------------------------------------------
 * FOUR GATES STAND BEHIND IT, each measured rather than argued
 * ---------------------------------------------------------------------------
 *   lex        the deployed `lex_numeric_literals` finds the trainer's literals on
 *              754/754 utterances -- 3,381 literals, order, span start, span END and value
 *   tokenize   sensen's WordPiece matches the trainer on 754/754, ids AND spans, and the
 *              vocab.txt route and the in-GGUF route are BYTE-IDENTICAL to each other
 *   mask       restricting a literal's pairs to the operation's admissible set: 0 rows
 *              changed on a correct fixture, 123 recovered when a wrong-map distractor
 *              is planted
 *   reconstruct the C++ port matches Python's on 600/600 rows, 5,280 of 5,352 decimals
 *              byte-exact and the other 72 within one BigDecimal ulp of 1e-38
 *
 * ---------------------------------------------------------------------------
 * WHAT IS THIS LAYER'S OWN, AND WHY IT IS HERE RATHER THAN IN sensen
 * ---------------------------------------------------------------------------
 * sensen's encoder is deliberately SCHEMA-FREE: it decodes the pair head mask-free and
 * emits the convention logits FLAT, because masking and slicing need the schema and
 * `encoder_model.py` says in as many words that masks are the CALLER's. So two things
 * belong to this module and to nothing below it:
 *
 *   - the per-operation PAIR MASK (`encoder_reconstruct::maskPairsToOperation`)
 *   - slicing `convention_logits` into its per-field classifiers and taking an argmax
 *     of each, by `conv_fields` order and `conv_vocab` widths
 *
 * THE TAG IS NOT SUPPLIED, and that is measured rather than an omission. `reconstruct`
 * applies a map BY NAME; the tag predicate in the trainer's `MAPS` gates CANDIDATE
 * GENERATION during training only. Rewriting all 3,381 literal tags to "bare" leaves both
 * languages byte-identical, so the lexer's tag is not forwarded and nothing downstream
 * reads one.
 * ===========================================================================
 */

module;
#include <new>  // ODR anchor: see the import-std notes in this project's CLAUDE.md

export module encoder_assistant;

import std;
import fastjson;
import sensen.gguf_parser;
import sensen.tokenizer;
import sensen.text_encoder;
import sensen.bigdecimal;
import encoder_reconstruct;
import mortgage_verification;

export namespace encoder_assistant {

namespace mv = mortgage_calculator::assistant::verify;

/** One parsed utterance: the operation the model named and the parameters derived for it. */
struct Parsed {
    std::string operation;
    /**
     * The schema's own name for the operation key -- `operation` on the mortgage surface,
     * `strategy` on the options one.
     *
     * CARRIED RATHER THAN HARDCODED, because hardcoding it worked on one surface and silently
     * failed on the other. `to_json()` emitted `"operation"` unconditionally; the mortgage
     * schema's op_key IS `operation`, so it matched by coincidence, while the strategy
     * service refused every single request with "The assistant did not name a strategy" --
     * its own pre-existing refusal, firing because the key it looks for was absent. The
     * params themselves were perfect in the log. A key that is right for one schema and
     * wrong for another is the four-tables defect at field-name scale.
     */
    std::string op_key{"operation"};
    /// Rendered as the wire renders them: decimal strings for numbers, "true"/"false" for a
    /// boolean convention, and a JSON array FLATTENED INTO A STRING for an array-valued field
    /// -- which is the wire form, because `FinanceParams.params` is a protobuf
    /// `map<string, string>` and a `repeated double` has nowhere else to go.
    std::map<std::string, std::string> params;

    /**
     * The params object as the JSON the SERVICE already knows how to validate.
     *
     * ONE renderer, used by the probe and by the service alike. The service feeds this
     * straight into `validate_and_populate_params`, which is the same function the Qwen3
     * path feeds -- so the unknown-key rejection, the per-operation field drops, the
     * verifier's five gates, the clarifying-question refinement and the derivation layer are
     * all SHARED rather than reimplemented for a second backend. A second renderer would be
     * the four-tables drift at the wire boundary; this project has that scar already.
     *
     * A convention boolean is emitted UNQUOTED because the service's JSON reader expects a
     * boolean there, and an array arrives already bracketed from the chain.
     */
    [[nodiscard]] auto to_json() const -> std::string;
};

/**
 * Loaded once per process and then `const`. `parse()` is therefore safe to call from
 * several threads, UNLIKE the Qwen3 path -- `sensen::LLMPipeline::generate()` cannot be
 * called concurrently because `FeedForwardNetwork` holds mutable scratch per instance. This
 * encoder's `encode()` allocates its own working buffers per call.
 */
class EncoderAssistant {
  public:
    [[nodiscard]] static auto fromGguf(const std::filesystem::path& path)
        -> std::expected<std::unique_ptr<EncoderAssistant>, std::string>;

    /**
     * Parse one utterance.
     *
     * `nullopt` means the model named `<NONE>` -- it did not recognise an operation. That
     * is a PREDICTION, not a failure, and the caller should render it as a refusal or a
     * clarification rather than as an error.
     */
    [[nodiscard]] auto parse(std::string_view utterance) const
        -> std::expected<std::optional<Parsed>, std::string>;

    [[nodiscard]] auto operation_count() const noexcept -> std::size_t { return schema_.ops.size(); }
    [[nodiscard]] auto pair_count() const noexcept -> std::size_t { return schema_.pairs.size(); }
    [[nodiscard]] auto vocab_size() const noexcept -> std::size_t { return vocab_size_; }
    [[nodiscard]] auto convention_fields() const noexcept -> std::size_t {
        return schema_.conv_fields.size();
    }

  private:
    EncoderAssistant() = default;

    /// Token ids with their spans, in the WHOLE utterance's coordinates.
    /// Defined here rather than forward-declared: `std::expected<Framed, ...>` instantiates
    /// type traits on it, so an incomplete type is a hard error rather than a link-time one.
    struct Framed {
        std::vector<std::uint32_t> ids;
        std::vector<sensen::text_encoder::CharSpan> spans;
    };

    [[nodiscard]] auto tokenize_with_specials(std::string_view utterance) const
        -> std::expected<Framed, std::string>;

    std::unique_ptr<sensen::GGUFParser> parser_;
    std::unique_ptr<sensen::Tokenizer> tokenizer_;
    std::unique_ptr<sensen::text_encoder::TextEncoder> encoder_;
    encoder_reconstruct::Schema schema_;
    std::size_t vocab_size_{0};
    // The flat convention layout is DERIVED where it is consumed, by
    // `encoder_reconstruct::OperationDecode`, from the same conv_fields + conv_vocab the
    // exporter writes -- so there is no second offset table here to drift from it. `create`
    // still asserts the total against the model's own head width at LOAD time, which is
    // earlier than the first parse and is the only check a never-parsed model gets.
};

}  // namespace encoder_assistant
