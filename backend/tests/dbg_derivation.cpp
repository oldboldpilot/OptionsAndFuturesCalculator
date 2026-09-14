/** Replay tool: print the derivation for one (operation, utterance) pair.
 *  Used by scripts to measure the layer against a recorded failure set.
 *  @author Olumuyiwa Oluwasanmi */
#include <cstdio>
#include <new>
import std;
import mortgage_derivation;
namespace md = mortgage_calculator::assistant::derive;
auto main(int argc, char** argv) -> int {
    if (argc < 3) { std::println(stderr, "usage: dbg_derivation <operation> <utterance>"); return 2; }
    for (const auto& c : md::derive_candidates(argv[1], argv[2])) {
        std::print("{}", c.field);
        for (const auto& v : c.values) { std::print("\t{}", v); }
        std::println("");
    }
    return 0;
}
