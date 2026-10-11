/**
 * @file encoder_assistant_probe.cpp
 * @brief Run the WHOLE small-encoder chain over a holdout, in process.
 *
 * @author Olumuyiwa Oluwasanmi
 *
 * ===========================================================================
 * This is the first thing that exercises the four gated pieces TOGETHER -- lex, tokenize,
 * encode, mask, reconstruct -- rather than one at a time against a fixture. It deliberately
 * does NOT go through gRPC: that isolates "does the chain work" from "does the RPC plumbing
 * work", and this repository has twice mistaken a harness defect for a model defect (a
 * `llama-cli` holdout scored a deployed model 7/16 and triggered a retrain for a regression
 * that did not exist; an extractor taking the LAST user turn moved 22/27 to 24/27).
 *
 * Takes a GGUF and a JSONL holdout, prints one canonical line per row:
 *     {"row":N,"params":{...}}   or   {"row":N,"params":null}   or   {"row":N,"error":"..."}
 * so the SAME comparator that gated `reconstruct` against Python can score it.
 *
 * It also prints, to stderr, the throughput and the model's own shape -- a served figure is
 * worthless without saying which model produced it.
 * ===========================================================================
 */

#include <cstdio>
#include <new>

import std;
import fastjson;
import sensen.encoder_assistant;

namespace {

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
                    out += std::format("\\u{:04x}", static_cast<unsigned int>(static_cast<unsigned char>(c)));
                } else {
                    out += c;
                }
        }
    }
    return out;
}

/// The canonical one-line form the reconstruct comparator already reads: keys sorted, no
/// insignificant whitespace, the operation carried under the schema's own op key name.
[[nodiscard]] auto render(int row, const sensen::encoder_assistant::Parsed& p) -> std::string {
    std::map<std::string, std::string> all = p.params;
    all["operation"] = p.operation;
    std::string out = std::format("{{\"row\":{},\"params\":{{", row);
    bool first = true;
    for (const auto& [k, v] : all) {
        if (!first) out += ",";
        first = false;
        // A convention boolean is a JSON boolean on the wire, not a quoted string.
        //
        // AN ARRAY FIELD IS A QUOTED STRING HERE, and that is the wire form rather than a
        // convenience. `FinanceParams.params` is a protobuf `map<string, string>`, so a
        // `repeated double` leaves this service as the STRING "[-1000,1000,...]" and the
        // client unwraps it at the one seam that knows the declared wire type. Emitting a
        // bare JSON array instead would also make every element a JSON float, and a float64
        // cannot hold a 38-place decimal -- so the comparison would silently lose precision
        // the chain had computed exactly.
        const bool bare = (v == "true" || v == "false");
        out += std::format("\"{}\":{}", escape_json(k),
                           bare ? v : std::format("\"{}\"", escape_json(v)));
    }
    out += "}}";
    return out;
}

}  // namespace

auto main(int argc, char** argv) -> int {
    const auto args = std::span<char*>(argv, static_cast<std::size_t>(argc));
    if (args.size() < 3) {
        std::fprintf(stderr,
                     "usage: %s <encoder.gguf> <utterances.json>\n"
                     "  utterances.json: a JSON array of {\"text\": \"...\"} objects, in row order\n",
                     args.empty() ? "encoder_assistant_probe" : args[0]);
        return 1;
    }

    auto assistant = sensen::encoder_assistant::EncoderAssistant::fromGguf(args[1]);
    if (!assistant) {
        std::fprintf(stderr, "load failed: %s\n", assistant.error().c_str());
        return 1;
    }
    const auto& a = **assistant;
    std::fprintf(stderr,
                 "[model] %s: %zu operations, %zu pairs, %zu convention fields, vocab %zu\n",
                 args[1], a.operation_count(), a.pair_count(), a.convention_fields(),
                 a.vocab_size());

    std::ifstream in(args[2], std::ios::binary);
    if (!in.is_open()) {
        std::fprintf(stderr, "cannot open %s\n", args[2]);
        return 1;
    }
    std::ostringstream buf;
    buf << in.rdbuf();
    auto parsed = fastjson::parse(buf.str());
    if (!parsed || !parsed->is_array()) {
        std::fprintf(stderr, "%s: expected a JSON array of {\"text\": ...}\n", args[2]);
        return 1;
    }

    int row = 0;
    int none_rows = 0;
    int errors = 0;
    const auto t0 = std::chrono::steady_clock::now();
    for (const auto& e : parsed->as_array()) {
        if (!e.is_object() || !e.contains("text") || !e["text"].is_string()) {
            std::printf("{\"row\":%d,\"error\":\"element has no string 'text'\"}\n", row);
            ++errors;
            ++row;
            continue;
        }
        const auto text = std::string(e["text"].as_string());
        auto res = a.parse(text);
        if (!res) {
            std::printf("{\"row\":%d,\"error\":\"%s\"}\n", row, escape_json(res.error()).c_str());
            ++errors;
        } else if (!res->has_value()) {
            // <NONE>: the model did not recognise an operation. A prediction, not a failure.
            std::printf("{\"row\":%d,\"params\":null}\n", row);
            ++none_rows;
        } else {
            std::printf("%s\n", render(row, **res).c_str());
        }
        ++row;
    }
    const auto ms = std::chrono::duration_cast<std::chrono::milliseconds>(
                        std::chrono::steady_clock::now() - t0).count();
    std::fflush(stdout);
    std::fprintf(stderr,
                 "[run] %d utterances in %lld ms = %.1f utterances/s; %d answered <NONE>, %d errored\n",
                 row, static_cast<long long>(ms),
                 ms > 0 ? (1000.0 * row / static_cast<double>(ms)) : 0.0, none_rows, errors);
    return errors > 0 ? 1 : 0;
}
