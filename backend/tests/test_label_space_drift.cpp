/**
 * THE FOUR TABLES, ASSERTED AGAINST EACH OTHER INSTEAD OF AGAINST MEMORY.
 *
 * @author Olumuyiwa Oluwasanmi
 *
 * Adding an operation must reach four tables in this repository and a fifth in
 * the client. `mortgage_verification.cppm` carries two of them and is covered.
 * The FOURTH -- `kOperations` in `mortgage_assistant_service.cpp` -- had no
 * check of any kind, and on 2026-09-15 it was the one that drifted:
 *
 *     mortgage_verification.cppm   28 operations, ComputeRentalCashFlow present
 *     mortgage_assistant_service   27 operations, ComputeRentalCashFlow ABSENT
 *
 * `find_operation` returns nullptr for an unknown id, so the service refuses
 * the operation AFTER the verifier has admitted it. The model parses the
 * sentence, grounding proves it safe, and the answer is thrown away at
 * dispatch -- which looks exactly like a bad model and is not one. It is the
 * same failure as the client's `ALLOWED_OPERATIONS` naming fifteen while the
 * engine served twenty-seven.
 *
 * The table lives in a `.cpp`, so a test cannot import it. It is read as TEXT
 * for the same reason `routeTree.gen.ts` is read as text on the other site: a
 * derived check against the real artifact beats a hand-written list that can
 * drift in exactly the way the thing it is checking just did.
 */
#include <cstdio>
#include <new>

import std;
import mortgage_verification;

namespace mv = mortgage_calculator::assistant::verify;

namespace {
int g_checks = 0;
int g_failures = 0;

auto check(bool cond, const std::string& what) -> void {
    ++g_checks;
    std::printf("  %s: %s\n", cond ? "PASS" : "FAIL", what.c_str());
    if (!cond) { ++g_failures; }
}

auto read_file(const std::string& path) -> std::string {
    std::ifstream in(path);
    return std::string{std::istreambuf_iterator<char>(in), std::istreambuf_iterator<char>()};
}

/** The operation ids the SERVICE will dispatch, taken from its `kOperations`
 *  initialiser rather than from a list written beside it. */
auto service_operations(const std::string& src) -> std::vector<std::string> {
    const auto start = src.find("kOperations{{");
    if (start == std::string::npos) { return {}; }
    const auto end = src.find("}};", start);
    if (end == std::string::npos) { return {}; }
    const std::string body = src.substr(start, end - start);

    std::vector<std::string> ids;
    // Each row is `{"ComputeX", "XRequest", kFields_ComputeX},` -- the FIRST
    // string of each row is the id.
    std::size_t pos = 0;
    while ((pos = body.find("{\"", pos)) != std::string::npos) {
        const auto q = body.find('"', pos + 2);
        if (q == std::string::npos) { break; }
        ids.push_back(body.substr(pos + 2, q - pos - 2));
        pos = q;
    }
    return ids;
}
}  // namespace

auto main(int argc, char** argv) -> int {
    const std::string root = argc > 1 ? argv[1] : "..";
    const std::string src = read_file(root + "/src/modules/mortgage_assistant_service.cpp");

    std::printf("1. the extractor found the table (positive control)\n");
    {
        // Without this, a renamed table or a reformatted initialiser makes
        // every assertion below vacuously true -- the failure mode that turns
        // a green suite into decoration.
        check(!src.empty(), "the service source was read");
        const auto ops = service_operations(src);
        check(ops.size() > 20,
              "kOperations parsed, " + std::to_string(ops.size()) + " ids found");
        check(std::ranges::find(ops, "ComputeAmortization") != ops.end(),
              "and it contains an operation we know is there");
    }

    std::printf("\n2. the verifier and the service agree on the label space\n");
    {
        const auto service = service_operations(src);

        std::vector<std::string> verifier;
        for (const auto id : mv::operation_ids()) { verifier.emplace_back(id); }

        std::vector<std::string> missing_from_service;
        for (const auto& id : verifier) {
            if (std::ranges::find(service, id) == service.end()) {
                missing_from_service.push_back(id);
            }
        }
        // The message NAMES them. "expected 1 to be 0" sends a reader to count
        // things rather than to the operation they forgot.
        std::string names;
        for (const auto& m : missing_from_service) { names += " " + m; }
        check(missing_from_service.empty(),
              "every operation the VERIFIER admits, the service can dispatch;"
              " missing:" + (names.empty() ? std::string{" none"} : names));

        std::vector<std::string> unknown_to_verifier;
        for (const auto& id : service) {
            if (std::ranges::find(verifier, id) == verifier.end()) {
                unknown_to_verifier.push_back(id);
            }
        }
        // The other direction matters just as much: a dead entry beside a
        // missing one reads exactly like coverage, which is how the client's
        // allow-list came to name `ComputeRecast` -- not an RPC at all.
        std::string extras;
        for (const auto& m : unknown_to_verifier) { extras += " " + m; }
        check(unknown_to_verifier.empty(),
              "and the service dispatches nothing the verifier has never heard of;"
              " extra:" + (extras.empty() ? std::string{" none"} : extras));

        check(service.size() == verifier.size(),
              "same count: service " + std::to_string(service.size()) +
                  ", verifier " + std::to_string(verifier.size()));
    }

    std::printf("\n3. the operation this test was written for is reachable\n");
    {
        const auto service = service_operations(src);
        check(std::ranges::find(service, "ComputeRentalCashFlow") != service.end(),
              "ComputeRentalCashFlow is dispatchable -- it was admitted by the "
              "verifier and refused by the service for the whole of 2026-09-15");
    }

    std::printf("\n%d checks, %d failures\n", g_checks, g_failures);
    return g_failures == 0 ? 0 : 1;
}
