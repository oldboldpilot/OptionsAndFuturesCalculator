// Decides whether to trust what the 0.6B parameter agent produced.
//
// A 0.6B model is small enough to be wrong confidently, and the thing it emits
// goes straight into pricing a real position. So its output is not taken on
// trust: the model is sampled N times and sensen's automated reasoning votes on
// the result, then a deterministic check confirms the winner is something this
// application can actually serve.
//
// The two gates answer different questions and neither replaces the other:
//
//   SELF-CONSISTENCY  "does the model agree with itself?"  Catches ambiguity in
//                     the REQUEST. Five samples that disagree mean the sentence
//                     admitted more than one reading, and the right response is
//                     to ask rather than to pick one.
//
//   VALIDATION        "is this answer serveable?"  Catches confident nonsense.
//                     A unanimous vote for `strategy: "gamma_scalp"` is still
//                     wrong, because no such id exists. Agreement is not truth;
//                     five samples of one model share its mistakes.
//
// Running only the vote ships invented ids that the frontend silently drops.
// Running only the validator accepts a coin-flip between two valid readings,
// which is worse -- it prices something the user did not ask for and looks
// exactly as authoritative as a correct answer.

#include <algorithm>
#include <cstdint>
#include <fstream>
#include <iostream>
#include <optional>
#include <span>
#include <string>
#include <string_view>
#include <unordered_map>
#include <unordered_set>
#include <vector>

import sensen.reasoning;
import fastjson;

namespace {

// ---------------------------------------------------------------------------
// What the application can actually serve. Loaded from the same catalogue the
// UI and the training set are built from, so the three cannot drift.
// ---------------------------------------------------------------------------
struct Catalogue {
    std::unordered_map<std::string, int> leg_count;   // id -> legs
    std::unordered_set<std::string> futures_ids;      // ids in the Futures category

    // Roots the data provider returns a term structure for. Probing the live
    // backend: ES and NQ return eight contracts; RTY, YM, CL, NG, GC, SI, ZB
    // and ZN return none. A request naming one of those is well formed and
    // unserveable, which is precisely the case a validator exists to catch.
    std::unordered_set<std::string> futures_with_data{"ES", "NQ"};

    [[nodiscard]] static auto load(const std::string& path) -> std::optional<Catalogue> {
        std::ifstream in(path);
        if (!in) return std::nullopt;
        const std::string text{std::istreambuf_iterator<char>(in),
                               std::istreambuf_iterator<char>()};
        auto parsed = fastjson::parse(text);
        if (!parsed || !parsed->is_array()) return std::nullopt;

        Catalogue c;
        for (const auto& s : parsed->as_array()) {
            if (!s.is_object() || !s.contains("id")) continue;
            const std::string id{s["id"].as_string()};
            c.leg_count[id] = s.contains("leg_count")
                                  ? static_cast<int>(s["leg_count"].as_number())
                                  : 1;
            if (s.contains("category") && s["category"].as_string() == "Futures") {
                c.futures_ids.insert(id);
            }
        }
        return c;
    }
};

struct Params {
    std::string symbol;
    std::string asset_class;
    std::string strategy;
    int expiration_days = 0;
    int quantity = 1;
};

/// Extracts the JSON inside <params></params>. Empty answer = no params emitted
/// (the model asked a question or declined), which is a legitimate outcome and
/// votes as its own distinct answer.
[[nodiscard]] auto extract_params_text(std::string_view chain) -> std::string {
    const auto open = chain.find("<params>");
    if (open == std::string_view::npos) return {};
    const auto close = chain.find("</params>", open);
    if (close == std::string_view::npos) return {};
    return std::string{chain.substr(open + 8, close - open - 8)};
}

[[nodiscard]] auto parse_params(std::string_view json) -> std::optional<Params> {
    auto v = fastjson::parse(std::string{json});
    if (!v || !v->is_object()) return std::nullopt;
    Params p;
    const auto str = [&](const char* k) -> std::string {
        return v->contains(k) && (*v)[k].is_string() ? std::string{(*v)[k].as_string()} : "";
    };
    p.symbol = str("symbol");
    p.asset_class = str("asset_class");
    p.strategy = str("strategy");
    if (v->contains("expiration_days") && (*v)["expiration_days"].is_number()) {
        p.expiration_days = static_cast<int>((*v)["expiration_days"].as_number());
    }
    if (v->contains("quantity") && (*v)["quantity"].is_number()) {
        p.quantity = static_cast<int>((*v)["quantity"].as_number());
    }
    return p;
}

/**
 * A canonical key for voting.
 *
 * Two samples that differ only in key ORDER or whitespace are the same answer,
 * and tallying the raw string would split their votes and manufacture a
 * disagreement that is purely a serialization artefact. Normalising first is
 * what makes the vote fraction mean what it claims.
 */
[[nodiscard]] auto canonical_key(const std::string& chain) -> std::string {
    const auto body = extract_params_text(chain);
    if (body.empty()) return "<no-params>";
    const auto p = parse_params(body);
    if (!p) return "<unparseable>";
    return p->symbol + "|" + p->asset_class + "|" + p->strategy + "|" +
           std::to_string(p->expiration_days) + "|" + std::to_string(p->quantity);
}

struct Verdict {
    bool accept = false;
    std::string reason;
    std::string params_json;
    float confidence = 0.0F;
    float margin = 0.0F;
};

/// Deterministic serveability check. Every failure names the field, because a
/// verdict an operator cannot act on is only marginally better than none.
[[nodiscard]] auto validate(const Params& p, const Catalogue& cat) -> std::optional<std::string> {
    const auto it = cat.leg_count.find(p.strategy);
    if (it == cat.leg_count.end()) {
        return "strategy '" + p.strategy + "' is not in the catalogue";
    }
    if (p.asset_class != "EQUITY" && p.asset_class != "FUTURES" && p.asset_class != "CRYPTO") {
        return "asset_class '" + p.asset_class + "' is not one of EQUITY/FUTURES/CRYPTO";
    }
    const bool is_futures_strategy = cat.futures_ids.contains(p.strategy);
    if (is_futures_strategy && p.asset_class != "FUTURES") {
        return "futures strategy '" + p.strategy + "' with asset_class " + p.asset_class;
    }
    if (p.asset_class == "FUTURES" && !cat.futures_with_data.contains(p.symbol)) {
        // Well formed and unserveable: the curve would come back empty and
        // every futures strategy would stay blocked with nothing saying why.
        return "no term structure for futures root '" + p.symbol + "' (have ES, NQ)";
    }
    if (p.expiration_days <= 0 || p.expiration_days > 400) {
        return "expiration_days " + std::to_string(p.expiration_days) + " out of range";
    }
    if (p.quantity <= 0 || p.quantity > 1000) {
        return "quantity " + std::to_string(p.quantity) + " out of range";
    }
    return std::nullopt;
}

}  // namespace

/**
 * Votes on N sampled generations and validates the winner.
 *
 * `min_fraction` is how much of the sample must agree. 0.6 of five samples
 * means at least three; below that the request was ambiguous and the caller
 * should ask rather than guess.
 */
[[nodiscard]] auto verify(std::span<const std::string> chains, const Catalogue& cat,
                          float min_fraction = 0.6F) -> Verdict {
    sensen::reasoning::SelfConsistency<std::string, std::string> sc(
        sensen::reasoning::VoteMode::Majority);

    const auto voted = sc.vote(chains, [](const std::string& c) { return canonical_key(c); });
    if (!voted) {
        return {.accept = false, .reason = "vote failed: " + voted.error().message};
    }
    const auto& v = *voted;

    Verdict out;
    out.confidence = v.vote_fraction;
    out.margin = v.agreement_margin;

    if (v.tie) {
        out.reason = "the samples tie between readings — ask which was meant";
        return out;
    }
    if (v.vote_fraction < min_fraction) {
        out.reason = "only " + std::to_string(v.votes) + "/" +
                     std::to_string(v.total_chains) + " samples agree — ambiguous request";
        return out;
    }
    if (v.answer == "<no-params>") {
        out.reason = "the model asked a question or declined rather than emitting parameters";
        return out;
    }
    if (v.answer == "<unparseable>") {
        out.reason = "the winning sample did not contain parseable parameters";
        return out;
    }

    // Recover the winning chain's params text for the caller.
    for (const auto& c : chains) {
        if (canonical_key(c) == v.answer) {
            out.params_json = extract_params_text(c);
            break;
        }
    }
    const auto parsed = parse_params(out.params_json);
    if (!parsed) {
        out.reason = "winner failed to re-parse";
        return out;
    }
    if (const auto err = validate(*parsed, cat)) {
        // Agreement is not truth: N samples of one model share its mistakes, so
        // a unanimous vote for an invented id still fails here.
        out.reason = "agreed but not serveable: " + *err;
        return out;
    }

    out.accept = true;
    out.reason = "agreed and serveable";
    return out;
}

auto main(int argc, char** argv) -> int {
    if (argc < 2) {
        std::cerr << "usage: param_verifier <strategies.json> [candidates.txt]\n"
                     "  candidates: one sampled generation per line; '---' separates cases\n";
        return 2;
    }
    auto cat = Catalogue::load(argv[1]);
    if (!cat) {
        std::cerr << "could not load catalogue from " << argv[1] << "\n";
        return 1;
    }

    std::istream* in = &std::cin;
    std::ifstream file;
    if (argc > 2) {
        file.open(argv[2]);
        if (!file) { std::cerr << "cannot open " << argv[2] << "\n"; return 1; }
        in = &file;
    }

    std::vector<std::string> chains;
    std::string line;
    int failures = 0;
    const auto flush = [&]() {
        if (chains.empty()) return;
        const auto v = verify(chains, *cat);
        std::cout << (v.accept ? "ACCEPT  " : "REFUSE  ")
                  << "conf=" << v.confidence << " margin=" << v.margin
                  << "  " << v.reason << "\n";
        if (v.accept) std::cout << "        " << v.params_json << "\n";
        if (!v.accept) ++failures;
        chains.clear();
    };
    while (std::getline(*in, line)) {
        if (line == "---") { flush(); continue; }
        if (!line.empty()) chains.push_back(line);
    }
    flush();
    return 0;
}
