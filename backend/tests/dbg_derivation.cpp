/** Replay tool: print the derivation for one (operation, utterance) pair, or
 *  SWEEP a corpus and report where the solver would corrupt a correct answer.
 *
 *  The sweep is the important mode and it needs no model and no engine. It
 *  feeds the layer the GOLD params as though the model had emitted them
 *  perfectly, and reports every field the layer would then REWRITE. A rewrite
 *  of a correct value is a served regression by construction -- the row scores
 *  raw-exact and serves wrong -- which is a loss no raw metric can see.
 *
 *  @author Olumuyiwa Oluwasanmi */
#include <cstdio>
#include <new>
import std;
import mortgage_derivation;
import fastjson;
namespace md = mortgage_calculator::assistant::derive;

namespace {
/** One TSV field, with the escapes `sweep_corpus.py` writes. */
auto unescape(std::string_view s) -> std::string {
    std::string out;
    for (std::size_t i = 0; i < s.size(); ++i) {
        if (s[i] == '\\' && i + 1 < s.size()) {
            ++i;
            out += (s[i] == 'n') ? '\n' : (s[i] == 't') ? '\t' : s[i];
        } else {
            out += s[i];
        }
    }
    return out;
}

auto split_tabs(const std::string& line) -> std::vector<std::string> {
    std::vector<std::string> out;
    std::size_t start = 0;
    for (std::size_t i = 0; i <= line.size(); ++i) {
        if (i == line.size() || line[i] == '\t') {
            out.push_back(unescape(std::string_view{line}.substr(start, i - start)));
            start = i + 1;
        }
    }
    return out;
}
}  // namespace

auto main(int argc, char** argv) -> int {
    if (argc >= 3 && std::string_view{argv[1]} == "--sweep") {
        std::ifstream in{argv[2]};
        if (!in) { std::println(stderr, "cannot open {}", argv[2]); return 2; }
        int rows = 0;
        int corrupted_rows = 0;
        int corrupted_fields = 0;
        int advisory = 0;
        std::string line;
        while (std::getline(in, line)) {
            if (line.empty()) { continue; }
            const auto f = split_tabs(line);
            if (f.size() < 4) { continue; }
            ++rows;
            const auto parsed = fastjson::parse(f[3]);
            if (!parsed.has_value() || !parsed->is_object()) { continue; }
            std::map<std::string, std::string> gold;
            for (const auto& [k, v] : parsed->as_object()) {
                if (k == "operation") { continue; }
                gold.emplace(k, v.is_string() ? std::string{v.as_string()}
                                              : fastjson::stringify(v));
            }
            const auto cands = md::derive_candidates_in_turns(f[0], f[1], f[2]);
            const auto verdict = md::reconcile(cands, gold);
            bool bad = false;
            for (const auto& r : verdict.replace) {
                // `reconcile` only returns a field when it DIFFERS from what was
                // emitted, and what was emitted here is gold -- so every entry
                // is the layer overwriting a correct value.
                ++corrupted_fields;
                bad = true;
                std::println("CORRUPT\t{}\t{}\tgold={}\tderived={}\tlatest={}", f[0], r.field,
                             gold.count(r.field) ? gold.at(r.field) : std::string{"?"},
                             r.values.front(), f[2]);
            }
            // ADVISORY ONLY. `mortgage_assistant_service.cpp` does not consume
            // `Reconciliation::ambiguous` -- an ambiguous field is simply left
            // for grounding to judge -- so these change no served answer and
            // are reported separately rather than counted as breakage.
            for (const auto& a : verdict.ambiguous) {
                ++advisory;
                std::println("ambig  \t{}\t{}\tgold={}\tlatest={}", f[0], a,
                             gold.count(a) ? gold.at(a) : std::string{"?"}, f[2]);
            }
            if (bad) { ++corrupted_rows; }
        }
        std::println("\n{} rows swept, {} rows the layer would CORRUPT, {} fields "
                     "({} ambiguous, advisory)",
                     rows, corrupted_rows, corrupted_fields, advisory);
        return corrupted_rows == 0 ? 0 : 1;
    }

    if (argc < 3) {
        std::println(stderr, "usage: dbg_derivation <operation> <utterance> [latest-turn]");
        std::println(stderr, "       dbg_derivation --sweep <corpus.tsv>");
        return 2;
    }
    const std::string_view latest = argc > 3 ? argv[3] : "";
    for (const auto& c : md::derive_candidates_in_turns(argv[1], argv[2], latest)) {
        std::print("{}", c.field);
        for (const auto& v : c.values) { std::print("\t{}", v); }
        std::println("");
    }
    return 0;
}
