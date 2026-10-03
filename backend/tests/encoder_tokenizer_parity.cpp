/**
 * @file encoder_tokenizer_parity.cpp
 * @brief Parity harness proving sensen's WordPiece tokenizer reproduces
 *        the Python trainer's tokenizer exactly.
 *
 * @author Olumuyiwa Oluwasanmi
 *
 * ===========================================================================
 * PURPOSE: WHAT IS BEING PROVEN
 * ===========================================================================
 * In the small encoder assistant architecture, the model does not autoregressively
 * generate token digits. Instead, per-literal heads point at numeric literals
 * discovered by a lexer in the raw input text. This requires sensen's C++
 * WordPiece tokenizer to reproduce the Python trainer's Hugging Face tokenizers
 * implementation (EncoderTokenizer) bit-for-bit and span-for-span.
 *
 * Specifically, this harness proves:
 * 1. Exact token ID sequence parity for every utterance in the evaluation fixture.
 * 2. Exact token span (begin, end byte offset) parity. Special framing tokens
 *    ([CLS] and [SEP]) carry the no-text sentinel TokenSpan::kNone and are
 *    emitted as [-1, -1] rather than [0, 0], ensuring zero-width literals at 0
 *    do not mistakenly claim the [CLS] framing token.
 *
 * ===========================================================================
 * WHY THE FOUR CONFIG VALUES ARE SET EXPLICITLY RATHER THAN DEFAULTED
 * ===========================================================================
 * A vocab.txt file carries ONLY vocabulary strings (one per line, where ID is
 * the 0-based line number). It carries absolutely NO normalizer or WordPiece
 * configuration. An engine that defaults these hyperparameters is running a
 * DIFFERENT tokenizer that will load cleanly without throwing an error, but will
 * answer with plausible, wrong token IDs and corrupted spans.
 *
 * Therefore, all four values are explicitly configured from the trainer's
 * measured tokenizer.json:
 *
 * 1. cfg.lowercase = true
 *    BertNormalizer lowercase: folds ASCII and Unicode case to match the
 *    uncased vocabulary before subword lookup.
 *
 * 2. cfg.strip_accents = true
 *    BertNormalizer strip_accents: strips diacritics and accents (e.g. 'é' -> 'e')
 *    so accented forms match unaccented vocabulary entries.
 *
 * 3. cfg.tokenize_chinese_chars = false
 *    The trainer explicitly sets handle_chinese_chars = false. Sensen defaults
 *    tokenize_chinese_chars to true. Leaving this at sensen's default would split
 *    CJK ideographs into separate tokens rather than treating them as unhandled
 *    or single words, causing divergence on any non-Latin or CJK sequences.
 *
 * 4. cfg.max_input_chars_per_word = 64
 *    The trainer configures WordPiece(max_input_chars_per_word=64), whereas
 *    sensen defaults to 100. Any token or word exceeding 64 characters must
 *    immediately produce [UNK] without attempting subword search. An engine
 *    using 100 would split words between 65 and 100 characters instead of
 *    emitting [UNK], yielding wrong IDs.
 *
 * ===========================================================================
 * COMPLIANCE WITH HOUSE RULES (config/cpp_details.txt)
 * ===========================================================================
 * - C++23 modules: `import std;`, `import sensen.tokenizer;`,
 *   `import sensen.gguf_parser;`, `import fastjson;`
 * - Textual `#include <cstdio>` for stderr/stdout macros and `#include <new>` as ODR anchor.
 * - Trailing return types everywhere (`auto f(...) -> T`).
 * - [[nodiscard]] on all value-returning functions.
 * - Zero raw pointers (uses smart pointers and std::span/std::string_view).
 * - Standard std::print and std::format for program output (no printf to stdout).
 * - std::expected for fallible operations (read_file), no custom exceptions.
 * - Strict adherence to Rule 39: internal test harness without GTest/Catch2.
 * ===========================================================================
 */

// `import std;` exports no macros, and `stderr`/`stdout` require <cstdio>.
// <new> is an ODR anchor required by this repository's module conventions.
#include <cstdio>
#include <new>

import std;
import sensen.tokenizer;
import sensen.gguf_parser;
import fastjson;

namespace {

/**
 * Escapes characters in a string for safe inclusion in a JSON string literal.
 */
[[nodiscard]] auto escape_json(std::string_view s) -> std::string {
    std::string out;
    out.reserve(s.size() + 8);
    for (const char c : s) {
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
                    std::format_to(std::back_inserter(out), "\\u{:04x}",
                                   static_cast<unsigned int>(static_cast<unsigned char>(c)));
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
[[nodiscard]] auto read_file(const std::filesystem::path& path)
    -> std::expected<std::string, std::string> {
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
 * Formats a single parity row containing row index, token IDs, and offsets
 * into compact JSON: {"row":<idx>,"ids":[...],"offsets":[[b,e],...]}
 */
[[nodiscard]] auto format_parity_row(
    std::size_t row,
    std::span<const std::uint32_t> ids,
    std::span<const sensen::TokenSpan> offsets) -> std::string {
    std::string out;
    out.reserve(64 + ids.size() * 16);
    std::format_to(std::back_inserter(out), "{{\"row\":{},\"ids\":[", row);
    for (std::size_t i = 0; i < ids.size(); ++i) {
        if (i > 0) {
            out += ',';
        }
        std::format_to(std::back_inserter(out), "{}", ids[i]);
    }
    out += "],\"offsets\":[";
    for (std::size_t i = 0; i < offsets.size(); ++i) {
        if (i > 0) {
            out += ',';
        }
        const auto& span = offsets[i];
        if (span.begin == sensen::TokenSpan::kNone || span.end == sensen::TokenSpan::kNone) {
            out += "[-1,-1]";
        } else {
            std::format_to(std::back_inserter(out), "[{},{}]", span.begin, span.end);
        }
    }
    out += "]}\n";
    return out;
}

} // namespace

auto main(int argc, char* argv[]) -> int {
    const std::span<char* const> args(argv, static_cast<std::size_t>(argc));
    if (args.size() != 3) {
        std::fprintf(stderr,
                     "Usage: %s <path/to/vocab.txt> <path/to/fixture.json>\n",
                     args.empty() ? "encoder_tokenizer_parity" : args[0]);
        return 1;
    }

    const std::filesystem::path vocab_path{args[1]};
    const std::filesystem::path fixture_path{args[2]};

    // 1. Load the tokenizer. TWO ROUTES, and running BOTH is the point.
    //
    //   *.gguf      -> Tokenizer::fromParser, i.e. the vocabulary AND the normaliser read
    //                  out of the model file itself. This is the route that serves.
    //   anything else -> Tokenizer::fromVocabTxt with the four values stated below.
    //
    // The vocab.txt route is kept because it is the INDEPENDENT arm: it states the four
    // normaliser values explicitly, so running the same fixture through both and getting
    // the same ids proves the GGUF actually CARRIED them rather than falling back to
    // sensen's defaults. Two of those defaults disagree with this trainer
    // (tokenize_chinese_chars true-vs-false, max_input_chars_per_word 100-vs-64), and
    // neither can bite on an all-ASCII corpus of short words -- so "it worked" on the
    // GGUF route alone would have been evidence of nothing.
    const bool from_gguf = vocab_path.extension() == ".gguf";
    std::unique_ptr<sensen::Tokenizer> tok;
    try {
        if (from_gguf) {
            // loadMetadata() is what populates tokenizer.ggml.*; without it the vocabulary
            // comes back empty and fromParser refuses by name rather than guessing.
            auto parser = sensen::GGUFParser::open(vocab_path).loadMetadata().build();
            if (!parser) {
                std::fprintf(stderr, "Error: GGUFParser could not open '%s'\n",
                             vocab_path.string().c_str());
                return 1;
            }
            auto builder = sensen::Tokenizer::fromParser(*parser);
            tok = std::move(builder).build();
        } else {
            sensen::WordPieceConfig cfg;
            cfg.lowercase = true;               // BertNormalizer lowercase
            cfg.strip_accents = true;           // BertNormalizer strip_accents
            cfg.tokenize_chinese_chars = false; // trainer has handle_chinese_chars FALSE; sensen defaults TRUE
            cfg.max_input_chars_per_word = 64;  // trainer value; sensen defaults 100

            auto builder = sensen::Tokenizer::fromVocabTxt(vocab_path, cfg);
            tok = std::move(builder).build();
        }
    } catch (const std::exception& e) {
        std::fprintf(stderr, "Error loading tokenizer from '%s': %s\n",
                     vocab_path.string().c_str(), e.what());
        return 1;
    }
    std::fprintf(stderr, "[tokenizer] loaded from %s: %s\n",
                 from_gguf ? "GGUF (tokenizer.ggml.*)" : "vocab.txt (config stated here)",
                 vocab_path.string().c_str());

    if (!tok) {
        std::fprintf(stderr, "Error: Tokenizer builder returned null for '%s'\n",
                     vocab_path.string().c_str());
        return 1;
    }

    // 2. Load and parse Fixture JSON file
    const auto fixture_text = read_file(fixture_path);
    if (!fixture_text.has_value()) {
        std::fprintf(stderr, "Error reading fixture file: %s\n",
                     fixture_text.error().c_str());
        return 1;
    }

    const auto fixture_parse = fastjson::parse(*fixture_text);
    if (!fixture_parse.has_value()) {
        std::fprintf(stderr, "Error parsing fixture JSON: %s\n",
                     fixture_parse.error().to_string().c_str());
        return 1;
    }

    if (!fixture_parse->is_array()) {
        std::fprintf(stderr, "Error: Fixture JSON root must be an array\n");
        return 1;
    }

    // 3. Configure tokenization options
    sensen::TokenizeOptions opt;
    opt.add_bos = true;        // [CLS] rides the BOS slot
    opt.add_eos = true;        // [SEP] rides the EOS slot -- Python post_processor is [CLS] A [SEP]
    opt.pad = false;
    opt.truncate = true;
    opt.max_length = 0;
    opt.return_offsets = true; // REQUIRED, or offsets comes back empty

    // 4. Tokenize each fixture row and emit compact JSON
    const auto& arr = fixture_parse->as_array();
    for (std::size_t row_idx = 0; row_idx < arr.size(); ++row_idx) {
        const auto& elem = arr[row_idx];
        if (!elem.is_object()) {
            std::print("{{\"row\":{},\"error\":\"Fixture array element is not an object\"}}\n",
                       row_idx);
            continue;
        }

        std::size_t row_id = row_idx;
        if (elem.contains("row")) {
            const auto& r_val = elem["row"];
            if (r_val.is_number() || r_val.is_int_128() || r_val.is_uint_128()) {
                row_id = static_cast<std::size_t>(r_val.as_int64());
            }
        }

        if (!elem.contains("text")) {
            std::print("{{\"row\":{},\"error\":\"Fixture element missing required 'text' field\"}}\n",
                       row_id);
            continue;
        }

        const auto& text_val = elem["text"];
        if (!text_val.is_string()) {
            std::print("{{\"row\":{},\"error\":\"Fixture 'text' field is not a string\"}}\n",
                       row_id);
            continue;
        }

        const std::string_view text = text_val.as_string();

        try {
            const auto res = tok->encode(text, opt);
            if (res.offsets.size() != res.token_ids.size()) {
                std::print(
                    "{{\"row\":{},\"error\":\"Mismatch between token IDs count ({}) and offsets count ({})\"}}\n",
                    row_id, res.token_ids.size(), res.offsets.size());
                continue;
            }

            std::print("{}", format_parity_row(row_id, res.token_ids, res.offsets));
        } catch (const std::exception& ex) {
            std::print("{{\"row\":{},\"error\":\"{}\"}}\n", row_id, escape_json(ex.what()));
            continue;
        } catch (...) {
            std::print("{{\"row\":{},\"error\":\"Unknown exception during tokenization\"}}\n",
                       row_id);
            continue;
        }
    }

    std::fflush(stdout);
    return 0;
}
