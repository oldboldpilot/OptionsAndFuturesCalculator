/**
 * encoder_assistant implementation. See encoder_assistant.cppm for what this chain is and
 * which four gates stand behind each link of it.
 *
 * @author Olumuyiwa Oluwasanmi
 */

module;
#include <new>

module encoder_assistant;

namespace encoder_assistant {

namespace {

/// JSON string escaping, shared by `Parsed::to_json` and the chain's array rendering.
[[nodiscard]] auto escape_json(std::string_view s) -> std::string {
    std::string out;
    out.reserve(s.size() + 8);
    for (const char c : s) {
        switch (c) {
            case '"':  out += "\\\""; break;
            case '\\': out += "\\\\"; break;
            case '\n': out += "\\n";  break;
            case '\r': out += "\\r";  break;
            case '\t': out += "\\t";  break;
            default:
                if (static_cast<unsigned char>(c) < 0x20) {
                    out += std::format("\\u{:04x}",
                                       static_cast<unsigned int>(static_cast<unsigned char>(c)));
                } else {
                    out += c;
                }
        }
    }
    return out;
}

constexpr std::string_view kSchemaKey = "sensen-encoder.schema_json";

/// `[offset, end)` per literal, which is the span the trainer labelled: `offset` is the
/// first DIGIT (a leading "$" excluded) and `end` covers a trailing "%". See
/// mortgage_verification's NumericLiteral::end for why `offset + text.size()` is not it.
[[nodiscard]] auto literal_spans(const std::vector<mv::NumericLiteral>& lits)
    -> std::vector<sensen::text_encoder::CharSpan> {
    std::vector<sensen::text_encoder::CharSpan> out;
    out.reserve(lits.size());
    for (const auto& l : lits) out.push_back(sensen::text_encoder::CharSpan{l.offset, l.end});
    return out;
}

/**
 * A token with no source text carries `TokenSpan::kNone`, and `CharSpan`'s own "no text" is
 * an EMPTY span. Mapping kNone to {0, 0} would be wrong in the one direction that matters:
 * a zero-width span at 0 passes a containment test whenever a literal starts at character
 * 0, so [CLS] would be mistaken for text -- which is exactly why tokenizer.cppm chose a
 * sentinel other than (0, 0) in the first place.
 */
[[nodiscard]] auto token_spans(const std::vector<sensen::TokenSpan>& offsets)
    -> std::vector<sensen::text_encoder::CharSpan> {
    std::vector<sensen::text_encoder::CharSpan> out;
    out.reserve(offsets.size());
    for (const auto& t : offsets) {
        out.push_back(t.hasSource() ? sensen::text_encoder::CharSpan{t.begin, t.end} : sensen::text_encoder::CharSpan{0, 0});
    }
    return out;
}

/// Index of the largest element; the LOWEST index on a tie, matching sensen's own
/// `argmaxIndex` and therefore `std::ranges::max_element`, and therefore PyTorch's argmax.
[[nodiscard]] auto argmax(std::span<const float> v) -> std::size_t {
    std::size_t best = 0;
    for (std::size_t i = 1; i < v.size(); ++i) {
        if (v[i] > v[best]) best = i;
    }
    return best;
}


}  // namespace

/**
 * Tokenize an utterance that may contain a LITERAL "[SEP]", reproducing HuggingFace.
 *
 * WHY THIS IS NOT JUST `tokenizer_->encode(utterance)`. The trainer renders a multi-turn row
 * by joining its turns with the string " [SEP] " and then tokenizes the join -- and
 * HuggingFace matches added tokens in the INPUT TEXT with a trie BEFORE WordPiece runs, so
 * that "[SEP]" becomes the single id 3. sensen's WordPiece does NOT pre-match specials in
 * text: it splits them into "[", "sep", "]" -- ids 28, 130, 29. Measured over the 600
 * rendered holdout utterances, 154 contain a literal "[SEP]" and ALL 154 tokenized
 * differently.
 *
 * IT WAS INVISIBLE TO THE TOKENIZER GATE, which is the part worth recording. That gate scored
 * 754/754 on ids and spans -- over the USER TURNS, which contain no "[SEP]" at all. A corpus
 * that cannot exhibit a failure is not a control for it, and this is the third instance of
 * that shape in one day (strategy's 100% could not show the single-label ceiling; the holdout
 * has no `weeks`/`quarters` literal to show the six-against-eight tag gap).
 *
 * IT COST EXACTLY ONE ROW OF 600, AND THAT IS NOT REASSURANCE. Only row 227 changed answer --
 * the literal SPANS are unaffected by the mangling, so the extra tokens are usually just
 * noise the model shrugs off. A 599/600 resting on a tokenizer that disagrees on a quarter of
 * its inputs is luck, not correctness.
 *
 * FIXED HERE RATHER THAN IN sensen, deliberately. The real service does not receive a joined
 * string at all: `ParseRequest` carries the turns in separate fields, so segment boundaries
 * are something it KNOWS rather than something it has to recover from text. Splitting on the
 * marker is the same operation, and it keeps a general-purpose tokenizer free of a convention
 * that belongs to one corpus's rendering.
 *
 * The framing matches the trainer exactly: `[CLS]` and the FINAL `[SEP]` carry no text (span
 * (0,0), which `token_spans` also uses for sensen's kNone), while an in-text `[SEP]` carries
 * the span of the five characters it was written as -- verified against the trainer, which
 * gives (43,48) and (50,55) on row 227.
 */
[[nodiscard]] auto EncoderAssistant::tokenize_with_specials(std::string_view utterance) const
    -> std::expected<EncoderAssistant::Framed, std::string> {
    static constexpr std::string_view kSep = "[SEP]";
    const auto& specials = tokenizer_->getSpecialTokens();

    sensen::TokenizeOptions opt;
    opt.add_bos = false;  // the framing is assembled here, so each SEGMENT is unframed
    opt.add_eos = false;
    opt.pad = false;
    opt.truncate = true;
    opt.max_length = 0;
    opt.return_offsets = true;  // without this `offsets` comes back empty and every span fails

    EncoderAssistant::Framed out;
    out.ids.push_back(specials.cls);
    out.spans.push_back({0, 0});

    std::size_t at = 0;
    bool first = true;
    while (at <= utterance.size()) {
        const auto hit = utterance.find(kSep, at);
        const auto seg_end = hit == std::string_view::npos ? utterance.size() : hit;
        const auto segment = utterance.substr(at, seg_end - at);
        if (!segment.empty()) {
            const auto enc = tokenizer_->encode(segment, opt);
            if (enc.offsets.size() != enc.token_ids.size()) {
                return std::unexpected(std::format(
                    "tokenizer returned {} ids and {} offsets for a segment; return_offsets was "
                    "requested, so this is a tokenizer defect rather than a caller error",
                    enc.token_ids.size(), enc.offsets.size()));
            }
            for (std::size_t i = 0; i < enc.token_ids.size(); ++i) {
                out.ids.push_back(enc.token_ids[i]);
                const auto& t = enc.offsets[i];
                // Shift into the WHOLE utterance's coordinates: the literal spans the lexer
                // produced are in those, and a per-segment offset would point at the wrong
                // characters for every segment after the first.
                out.spans.push_back(t.hasSource()
                                        ? sensen::text_encoder::CharSpan{at + t.begin, at + t.end}
                                        : sensen::text_encoder::CharSpan{0, 0});
            }
        }
        if (hit == std::string_view::npos) break;
        out.ids.push_back(specials.sep);
        out.spans.push_back({hit, hit + kSep.size()});
        at = hit + kSep.size();
        first = false;
    }
    (void)first;

    out.ids.push_back(specials.sep);  // the post-processor's own [SEP]: no text
    out.spans.push_back({0, 0});
    return out;
}

namespace {
}  // namespace

auto Parsed::to_json() const -> std::string {
    std::string out = "{\"operation\":\"" + escape_json(operation) + "\"";
    for (const auto& [k, v] : params) {
        out += ",\"" + escape_json(k) + "\":";
        // A boolean is a JSON boolean and an array is a JSON array: the service's reader
        // asks `is_string()` / `is_boolean()` per field and refuses a mismatch, so quoting a
        // convention boolean here would be refused as a type error rather than read as false.
        const bool bare = (v == "true" || v == "false" || (!v.empty() && v.front() == '['));
        out += bare ? v : ("\"" + escape_json(v) + "\"");
    }
    out += "}";
    return out;
}

auto EncoderAssistant::fromGguf(const std::filesystem::path& path)
    -> std::expected<std::unique_ptr<EncoderAssistant>, std::string> {
    auto self = std::unique_ptr<EncoderAssistant>(new EncoderAssistant());

    self->parser_ = sensen::GGUFParser::open(path).loadMetadata().loadTensorIndex().build();
    if (!self->parser_) {
        return std::unexpected(std::format("could not open GGUF '{}'", path.string()));
    }

    // The SCHEMA, out of the same file as the weights. Refused when absent rather than
    // defaulted: every table below is derived from it, so a file without it is not a model
    // this chain can serve, and guessing would mean inventing a label space.
    const auto blob = self->parser_->getMetadataString(std::string(kSchemaKey));
    if (!blob.has_value() || blob->empty()) {
        return std::unexpected(std::format(
            "{} is absent from '{}'. The operation vocabulary, the (slot, map) pair table, the "
            "per-operation pair mask and the convention class vocabularies are all derived from "
            "it -- there is nothing to fall back to.",
            kSchemaKey, path.string()));
    }
    auto sch = encoder_reconstruct::Schema::from_json(*blob);
    if (!sch) return std::unexpected(std::format("{}: {}", kSchemaKey, sch.error()));
    self->schema_ = std::move(*sch);

    try {
        auto builder = sensen::Tokenizer::fromParser(*self->parser_);
        self->tokenizer_ = std::move(builder).build();
    } catch (const std::exception& e) {
        return std::unexpected(std::format("tokenizer: {}", e.what()));
    }
    if (!self->tokenizer_) return std::unexpected("tokenizer: builder produced nothing");
    self->vocab_size_ = self->tokenizer_->getVocabSize();

    auto enc = sensen::text_encoder::TextEncoder::fromGguf(*self->parser_);
    if (!enc) {
        return std::unexpected(std::format("encoder: {}", enc.error().message()));
    }
    self->encoder_ = std::make_unique<sensen::text_encoder::TextEncoder>(std::move(*enc));

    // The convention head arrives FLAT -- every field's classes end to end in conv_fields
    // order -- because sensen does not read a schema. The slice table is built ONCE here,
    // from conv_vocab, and asserted against the head's own width: a disagreement would read
    // one field's logits as another's, silently, and produce a plausible convention value.
    std::size_t at = 0;
    for (const auto& f : self->schema_.conv_fields) {
        const auto it = self->schema_.conv_vocab.find(f);
        if (it == self->schema_.conv_vocab.end()) {
            return std::unexpected(std::format(
                "schema conv_fields names '{}' but conv_vocab has no entry for it, so its class "
                "count is unknown and the flat convention logits cannot be sliced", f));
        }
        self->conv_offset_.push_back(at);
        self->conv_width_.push_back(it->second.size());
        at += it->second.size();
    }
    if (at != self->encoder_->config().n_conventions) {
        return std::unexpected(std::format(
            "conv_vocab totals {} classes across {} fields but the model's convention head is {} "
            "wide. The caller slices by conv_vocab, so a disagreement reads past the end of one "
            "field into the next.",
            at, self->schema_.conv_fields.size(), self->encoder_->config().n_conventions));
    }

    return self;
}

auto EncoderAssistant::parse(std::string_view utterance) const
    -> std::expected<std::optional<Parsed>, std::string> {
    // 1. LEX. The deployed lexer, gated at 754/754 against the trainer's.
    const auto lits = mv::lex_numeric_literals(utterance);

    // 2. TOKENIZE.
    auto enc = tokenize_with_specials(utterance);
    if (!enc) return std::unexpected(enc.error());

    sensen::text_encoder::EncoderInput in;
    in.token_ids = std::move(enc->ids);
    in.token_offsets = std::move(enc->spans);
    in.literals = literal_spans(lits);

    // 3. ENCODE.
    auto out = encoder_->encode(in);
    if (!out) {
        return std::unexpected(std::format("encode: {}", out.error().message()));
    }
    if (!out->has_operation) {
        return std::unexpected("the model has no operation head, so it cannot name an operation");
    }
    const int op = static_cast<int>(out->operation);

    // <NONE>. A prediction, not a failure: 40 of the 600 holdout rows are this.
    if (op == 0) return std::optional<Parsed>{std::nullopt};

    // 4. CONVENTION CLASSES. Slice the flat logits and take an argmax per field. A field
    //    whose argmax the schema cannot name is REFUSED rather than dropped -- dropping it
    //    would silently fall back to const_default and look like a correct answer.
    std::unordered_map<std::string, int> conv;
    if (out->has_conventions) {
        for (std::size_t f = 0; f < schema_.conv_fields.size(); ++f) {
            const std::span<const float> slice{out->convention_logits.data() + conv_offset_[f],
                                               conv_width_[f]};
            conv[schema_.conv_fields[f]] = static_cast<int>(argmax(slice));
        }
    }

    // 5. MASK. sensen decodes the pair head mask-free by design, so restricting each
    //    literal's pair set to what this operation admits is this layer's job.
    std::vector<std::vector<int>> lit_pairs;
    lit_pairs.reserve(out->literals.size());
    for (const auto& lp : out->literals) {
        std::vector<int> selected;
        selected.reserve(lp.pairs.size());
        for (const auto p : lp.pairs) selected.push_back(static_cast<int>(p));
        auto masked = encoder_reconstruct::maskPairsToOperation(schema_, op,
                                                               std::span<const int>(selected));
        if (!masked) return std::unexpected(std::format("pair mask: {}", masked.error()));
        lit_pairs.push_back(std::move(*masked));
    }

    // 6. RECONSTRUCT. The suffix multiplier is applied here: the lexer keeps `value` BEFORE
    //    it with `scale` beside it, deliberately, and the trainer's literal value has it
    //    baked in -- so this is the conversion between the two representations, not a choice.
    //    The TAG is deliberately left at its default: reconstruct applies a map by NAME and
    //    never reads one (proven by rewriting all 3,381 fixture tags to "bare" with
    //    byte-identical results).
    //
    //    THE TWO DECIMAL TYPES ARE DIFFERENT AND THE CONVERSION GOES THROUGH A STRING.
    //    `mortgage_verification::Decimal` is `__int128` at 15 decimal places; BigDecimal is
    //    `Int256` at 38. There is no widening operator between them and there must not be a
    //    `double` in the path -- this project has already published a double as a 38-place
    //    decimal and called it precision. `to_string()` is exact and carries the SIGN, which
    //    is why the lexer's `text` is not used instead: `text` holds unsigned digits while a
    //    negative literal's sign lives only in `value`.
    std::vector<encoder_reconstruct::Literal> rlits;
    rlits.reserve(lits.size());
    for (const auto& l : lits) {
        auto value = sensen::BigDecimal::try_parse(l.value.to_string());
        if (!value) {
            return std::unexpected(std::format(
                "literal at offset {} ('{}') does not survive Decimal -> BigDecimal: {}",
                l.offset, l.text, value.error()));
        }
        auto scaled = *value;
        if (l.scale != 1) scaled = scaled * sensen::BigDecimal(static_cast<std::int64_t>(l.scale));
        rlits.emplace_back(std::move(scaled), std::string{"bare"}, l.offset, l.end, l.text);
    }

    auto rec = encoder_reconstruct::reconstruct(schema_, op, std::span<const encoder_reconstruct::Literal>(rlits),
                                               lit_pairs, conv);
    if (!rec) return std::unexpected(std::format("reconstruct: {}", rec.error()));
    if (!rec->has_value()) return std::optional<Parsed>{std::nullopt};

    Parsed parsed;
    parsed.operation = (*rec)->operation;
    for (const auto& [key, fval] : (*rec)->fields) {
        if (fval.is_missing()) continue;
        if (fval.is_string()) {
            parsed.params[key] = *fval.as_string();
        } else if (fval.is_decimal()) {
            parsed.params[key] = fval.as_decimal()->to_string();
        } else if (fval.is_array()) {
            std::string arr = "[";
            // BY VALUE, not by reference. `as_array()` returns
            // `std::optional<std::span<const BigDecimal>>` BY VALUE, so `const auto& v =
            // *fval.as_array()` binds a reference INTO a temporary optional that dies at the
            // end of that statement -- every later `v[i]` then reads through a dangling span.
            // It segfaulted inside BigDecimal::to_string() on the first array-valued row
            // (ComputePaybackPeriod's `values`), two rows into the holdout. The span is a
            // view: copying it is free and it still points at the vector the FieldValue owns.
            const auto v = *fval.as_array();
            for (std::size_t i = 0; i < v.size(); ++i) {
                if (i > 0) arr += ",";
                arr += v[i].to_string();
            }
            arr += "]";
            parsed.params[key] = arr;
        }
    }
    return std::optional<Parsed>{std::move(parsed)};
}

}  // namespace encoder_assistant
