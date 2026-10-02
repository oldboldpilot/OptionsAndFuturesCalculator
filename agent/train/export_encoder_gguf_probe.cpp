// Does sensen load a GGUF written by the PyTorch exporter, and does it agree?
// text_encoder.cppm's own header says this was never checked.
#include <cstdio>
#include <cstdlib>

import std;
import sensen.text_encoder;
import sensen.gguf_parser;

auto main(int argc, char** argv) -> int {
    if (argc < 2) {
        std::fprintf(stderr, "usage: probe <model.gguf> [hidden.bin]\n");
        return 2;
    }
    auto parser = sensen::GGUFParser::open(argv[1]).loadMetadata().loadTensorIndex().build();
    if (!parser) {
        std::fprintf(stderr, "PARSE FAILED\n");
        return 3;
    }
    std::printf("parsed: arch=%s\n",
                parser->getMetadataString("general.architecture").value_or("?").c_str());

    auto enc = sensen::text_encoder::TextEncoder::fromGguf(
        *parser, {sensen::text_encoder::EncoderPrecision::F32, sensen::text_encoder::EncoderBackend::Cpu});
    if (!enc) {
        std::printf("LOAD FAILED: %s\n", enc.error().message().c_str());
        return 4;
    }
    const auto& c = enc->config();
    std::printf("LOADED ok: vocab=%zu d=%zu layers=%zu heads=%zu ffn=%zu ops=%zu slots=%zu maps=%zu eps=%g rope=%g\n",
                c.vocab, c.d_model, c.n_layers, c.n_heads, c.ffn_dim, c.n_operations,
                c.n_slots, c.n_maps, static_cast<double>(c.norm_eps),
                static_cast<double>(c.rope_base));

    // A deterministic token sequence, so PyTorch can be fed the identical ids.
    std::vector<std::uint32_t> ids;
    for (std::uint32_t i = 0; i < 12U; ++i) ids.push_back((i * 37U + 5U) % 200U + 1U);
    auto trunk = enc->encodeTrunk(ids);
    if (!trunk) {
        std::printf("TRUNK FAILED: %s\n", trunk.error().message().c_str());
        return 5;
    }
    std::printf("trunk: %zu floats for %zu tokens (d=%zu)\n", trunk->size(), ids.size(), c.d_model);
    // dump for the PyTorch comparison
    if (argc >= 3) {
        if (auto* f = std::fopen(argv[2], "wb")) {
            std::fwrite(trunk->data(), sizeof(float), trunk->size(), f);
            std::fclose(f);
            std::printf("wrote %s\n", argv[2]);
        }
    }
    double s = 0.0;
    for (float v : *trunk) s += static_cast<double>(v);
    std::printf("trunk checksum %.8f  first4 % .6f % .6f % .6f % .6f\n",
                s, (*trunk)[0], (*trunk)[1], (*trunk)[2], (*trunk)[3]);
    return 0;
}
