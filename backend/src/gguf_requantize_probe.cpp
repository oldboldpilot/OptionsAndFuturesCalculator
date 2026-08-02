// THROWAWAY: re-encode a GGUF at a different ftype using llama.cpp's own
// llama_model_quantize, so the numeric audit can compare sensen vs llama.cpp
// on F16/F32 weights (isolating quantized-kernel rounding from real bugs).
//
// Usage: ./gguf_requantize_probe <in.gguf> <out.gguf> <ftype:0=f32,1=f16>
#include <cstdio>
#include <cstdlib>
#include "llama.h"

int main(int argc, char** argv) {
    if (argc < 4) { std::fprintf(stderr, "usage: %s in out ftype\n", argv[0]); return 2; }
    llama_backend_init();
    llama_model_quantize_params p = llama_model_quantize_default_params();
    p.ftype = (llama_ftype)std::atoi(argv[3]);
    p.nthread = 8;
    p.allow_requantize = true;
    p.pure = true;
    const uint32_t r = llama_model_quantize(argv[1], argv[2], &p);
    std::printf("llama_model_quantize -> %u\n", r);
    llama_backend_free();
    return r == 0 ? 0 : 1;
}
