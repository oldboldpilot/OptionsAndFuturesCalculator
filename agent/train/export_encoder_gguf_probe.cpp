// Does sensen load a GGUF written by the PyTorch exporter, and does it agree?
// text_encoder.cppm's own header said this was never checked.
//
// It now also exercises the MULTI-LABEL pair head, which is the part that matters: two independent
// single-label softmax heads cap mortgage row accuracy at 76.83% because a literal filling two
// parameters loses one, and `encoder.pair` is what removes that cap. So this dumps the per-literal
// pair logits AND the selected SET, and the Python side compares both -- a trunk comparison alone
// would pass with the selection logic completely wrong.
#include <cstdio>
#include <cstdlib>

import std;
import sensen.text_encoder;
import sensen.gguf_parser;

namespace {

/// [CLS] w w ... [SEP] with 3-character words and a one-character gap, so a literal is a token range.
struct Utterance {
    std::vector<std::uint32_t> ids;
    std::vector<sensen::text_encoder::CharSpan> offsets;
    std::vector<sensen::text_encoder::CharSpan> literals;
};

[[nodiscard]] auto makeUtterance(std::size_t n, std::size_t vocab, std::uint32_t seed,
                                 const std::vector<std::pair<std::size_t, std::size_t>>& ranges) -> Utterance {
    Utterance u;
    std::size_t pos = 0;
    for (std::size_t i = 0; i < n; ++i) {
        u.ids.push_back(static_cast<std::uint32_t>((i * 37U + 5U + seed * 101U) % (vocab - 2U) + 1U));
        if (i == 0 || i + 1 == n) {  // specials carry an empty span
            u.offsets.push_back({0, 0});
            continue;
        }
        u.offsets.push_back({pos, pos + 3});
        pos += 4;
    }
    for (const auto& [a, b] : ranges) u.literals.push_back({u.offsets[a].start, u.offsets[b].end});
    return u;
}

template <class T>
auto put(std::FILE* f, T v) -> void {
    std::fwrite(&v, sizeof(T), 1, f);
}

}  // namespace

auto main(int argc, char** argv) -> int {
    if (argc < 2) {
        std::fprintf(stderr, "usage: probe <model.gguf> [out.bin]\n");
        return 2;
    }
    auto parser = sensen::GGUFParser::open(argv[1]).loadMetadata().loadTensorIndex().build();
    if (!parser) {
        std::fprintf(stderr, "PARSE FAILED\n");
        return 3;
    }
    std::printf("parsed: arch=%s\n", parser->getMetadataString("general.architecture").value_or("?").c_str());

    auto enc = sensen::text_encoder::TextEncoder::fromGguf(
        *parser, {sensen::text_encoder::EncoderPrecision::F32, sensen::text_encoder::EncoderBackend::Cpu});
    if (!enc) {
        std::printf("LOAD FAILED: %s\n", enc.error().message().c_str());
        return 4;
    }
    const auto& c = enc->config();
    std::printf("LOADED ok: vocab=%zu d=%zu layers=%zu heads=%zu ffn=%zu ops=%zu slots=%zu maps=%zu "
                "pairs=%zu threshold=%g eps=%g rope=%g\n",
                c.vocab, c.d_model, c.n_layers, c.n_heads, c.ffn_dim, c.n_operations, c.n_slots, c.n_maps,
                c.n_pairs, static_cast<double>(c.pair_threshold), static_cast<double>(c.norm_eps),
                static_cast<double>(c.rope_base));

    // THE SWEEP. One utterance is one point, and the selection rule is a THRESHOLD over 109 independent
    // sigmoids -- a shape with many ways to be subtly wrong that a single sample cannot see. Each case varies
    // the ids, the length and the literal layout.
    struct Case {
        std::size_t n;
        std::vector<std::pair<std::size_t, std::size_t>> lits;
    };
    const std::vector<Case> cases{
        {14, {{2, 2}, {5, 7}, {10, 11}}},   {8, {{1, 1}}},        {20, {{1, 3}, {5, 5}, {8, 9}, {15, 18}}},
        {6, {{2, 4}}},                      {31, {{1, 1}, {29, 29}}},  {12, {{3, 3}, {4, 4}, {5, 5}}},
    };
    std::FILE* dump = nullptr;
    if (argc >= 3) {
        dump = std::fopen(argv[2], "wb");
        if (dump == nullptr) {
            std::fprintf(stderr, "cannot open %s\n", argv[2]);
            return 7;
        }
        put<std::uint32_t>(dump, static_cast<std::uint32_t>(cases.size() * 4));  // utterances written below
    }
    std::size_t total_multi = 0;
    std::size_t total_empty = 0;
    std::size_t total_lits = 0;
    for (std::uint32_t seed = 0; seed < 4U; ++seed) {
    for (const auto& cs : cases) {
    const auto u = makeUtterance(cs.n, c.vocab, seed, cs.lits);
    sensen::text_encoder::EncoderInput in;
    in.token_ids = u.ids;
    in.token_offsets = u.offsets;
    in.literals = u.literals;

    auto trunk = enc->encodeTrunk(u.ids);
    if (!trunk) {
        std::printf("TRUNK FAILED: %s\n", trunk.error().message().c_str());
        return 5;
    }
    auto out = enc->encode(in);
    if (!out) {
        std::printf("ENCODE FAILED: %s\n", out.error().message().c_str());
        return 6;
    }
    for (const auto& l : out->literals) {
        ++total_lits;
        if (l.pairs.size() > 1) ++total_multi;
        if (l.pairs.empty()) ++total_empty;
    }
    if (dump != nullptr) {
        put<std::uint32_t>(dump, seed);
        put<std::uint32_t>(dump, static_cast<std::uint32_t>(u.ids.size()));
        put<std::uint32_t>(dump, static_cast<std::uint32_t>(c.d_model));
        std::fwrite(trunk->data(), sizeof(float), trunk->size(), dump);
        put<std::uint32_t>(dump, static_cast<std::uint32_t>(out->literals.size()));
        put<std::uint32_t>(dump, static_cast<std::uint32_t>(c.n_pairs));
        put<std::uint32_t>(dump, static_cast<std::uint32_t>(c.n_operations));
        std::fwrite(out->operation_logits.data(), sizeof(float), out->operation_logits.size(), dump);
        for (const auto& l : out->literals) {
            put<std::uint32_t>(dump, static_cast<std::uint32_t>(l.first_token));
            put<std::uint32_t>(dump, static_cast<std::uint32_t>(l.token_count));
            std::fwrite(l.pair_logits.data(), sizeof(float), l.pair_logits.size(), dump);
            put<std::uint32_t>(dump, static_cast<std::uint32_t>(l.pairs.size()));
            std::fwrite(l.pairs.data(), sizeof(std::uint32_t), l.pairs.size(), dump);
        }
    }
    }  // cases
    }  // seeds
    std::printf("SWEEP: %zu utterances, %zu literals; %zu select more than one pair, %zu select none\n",
                cases.size() * 4, total_lits, total_multi, total_empty);
    if (dump != nullptr) {
        std::fclose(dump);
        std::printf("wrote %s\n", argv[2]);
    }
    return 0;
}
