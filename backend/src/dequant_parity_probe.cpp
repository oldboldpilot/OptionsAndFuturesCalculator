// THROWAWAY: bit-parity check of sensen's GGUF dequantizer against ggml's own
// to_float for the REAL tensors of a real model file (not synthetic rows).
//
// For every tensor in the GGUF: read the raw bytes once, dequantize with
// sensen::GGUFParser::dequantizeTensor and with ggml_get_type_traits(t)->to_float,
// and report exact-bit equality / max ulp difference.
//
// Usage: ./dequant_parity_probe <model.gguf>

#include <cmath>
#include <cstdint>
#include <cstdio>
#include <cstring>
#include <string>
#include <vector>

extern "C" {
#include "ggml.h"
}

import sensen.gguf_parser;

int main(int argc, char** argv) {
    if (argc < 2) { std::fprintf(stderr, "usage: %s <model.gguf>\n", argv[0]); return 2; }
    auto parser = sensen::GGUFParser::open(argv[1]).loadMetadata().loadTensorIndex().build();
    const auto tensors = parser->getAllTensors();

    std::printf("tensors: %zu\n", tensors.size());
    std::size_t n_checked = 0, n_bit_identical = 0, n_type_skipped = 0;
    double global_max_abs = 0.0;
    std::string worst_tensor;
    std::vector<std::string> mismatched;

    for (const auto& t : tensors) {
        const int gt = static_cast<int>(t.type);
        const ggml_type gg = static_cast<ggml_type>(gt);
        const ggml_type_traits* tr = ggml_get_type_traits(gg);
        if (tr == nullptr || tr->to_float == nullptr) { ++n_type_skipped; continue; }

        auto raw = parser->getTensorData(t);
        // ggml to_float works per contiguous run of blck_size elements; the whole
        // tensor is a flat run for every row-major GGUF tensor.
        std::vector<float> ref(t.num_elements);
        tr->to_float(static_cast<const void*>(raw.data()), ref.data(),
                     static_cast<int64_t>(t.num_elements));

        std::vector<float> mine = parser->dequantizeTensor(t);
        if (mine.size() != ref.size()) {
            std::printf("SIZE MISMATCH %s: sensen=%zu ggml=%zu\n", t.name.c_str(), mine.size(), ref.size());
            mismatched.push_back(t.name);
            ++n_checked;
            continue;
        }
        const bool bits = std::memcmp(mine.data(), ref.data(), ref.size() * sizeof(float)) == 0;
        double maxabs = 0.0;
        if (!bits) {
            for (std::size_t i = 0; i < ref.size(); ++i)
                maxabs = std::max(maxabs, std::abs(double(ref[i]) - double(mine[i])));
            mismatched.push_back(t.name);
        }
        if (maxabs > global_max_abs) { global_max_abs = maxabs; worst_tensor = t.name; }
        ++n_checked;
        if (bits) ++n_bit_identical;
    }

    std::printf("checked=%zu  bit_identical=%zu  skipped(no ggml to_float)=%zu\n",
                n_checked, n_bit_identical, n_type_skipped);
    std::printf("worst max|diff| = %.9g  (tensor %s)\n", global_max_abs,
                worst_tensor.empty() ? "-" : worst_tensor.c_str());
    if (!mismatched.empty()) {
        std::printf("NON-BIT-IDENTICAL tensors (%zu), first 20:\n", mismatched.size());
        for (std::size_t i = 0; i < mismatched.size() && i < 20; ++i)
            std::printf("  %s\n", mismatched[i].c_str());
    }
    std::printf("VERDICT: %s\n", mismatched.empty() ? "BIT-IDENTICAL" : "DIFFERS");
    return mismatched.empty() ? 0 : 1;
}
