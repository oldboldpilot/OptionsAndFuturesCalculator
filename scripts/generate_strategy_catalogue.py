#!/usr/bin/env python3
"""
Generates backend/src/modules/strategy_catalogue.cppm from agent/dataset/strategies.json.

This is the mechanism that keeps the C++ backend's set of known strategy
identifiers mechanically derived from the dataset rather than retyped by hand.
The backend needs an authoritative "is this a known strategy id" check so an
LLM assistant RPC can REFUSE any strategy string outside the catalogue instead
of passing an unverified guess through to the pricing engine. Retyping the list
a third time (frontend, dataset, backend) would reintroduce exactly the drift
this generator exists to prevent.

Run this by hand whenever agent/dataset/strategies.json changes, and commit the
regenerated .cppm alongside it -- the same "generate, then check in" pattern
scripts/gen_proto.sh uses for the gRPC-Web client, chosen here for the same
reason: the Railway/Docker build context for backend/Dockerfile is the repo
root, but the Dockerfile only COPYs backend/ into the builder image (see
backend/Dockerfile:75), so agent/dataset/strategies.json is never present
inside the container. A generated, checked-in C++ module needs nothing at
build or run time beyond what backend/ already ships; reading the JSON at
container build or run time would require also teaching the Dockerfile to
COPY agent/ in, which this approach avoids entirely.

Usage:
    python3 scripts/generate_strategy_catalogue.py [--check]

    --check   Do not write the file; exit non-zero if the generated content
              would differ from what is currently checked in. Intended for a
              future CI/pre-commit guard against silent drift.
"""

from __future__ import annotations

import argparse
import json
import pathlib
import sys

REPO_ROOT = pathlib.Path(__file__).resolve().parent.parent
DATASET_PATH = REPO_ROOT / "agent" / "dataset" / "strategies.json"
OUTPUT_PATH = REPO_ROOT / "backend" / "src" / "modules" / "strategy_catalogue.cppm"


def cpp_string_literal(value: str) -> str:
    """Escapes a Python string for embedding in a C++ string literal."""
    out = []
    for ch in value:
        if ch == "\\":
            out.append("\\\\")
        elif ch == '"':
            out.append('\\"')
        elif ch == "\n":
            out.append("\\n")
        else:
            out.append(ch)
    return '"' + "".join(out) + '"'


def render_entry(entry: dict) -> str:
    strategy_id = cpp_string_literal(entry["id"])
    name = cpp_string_literal(entry["name"])
    category = cpp_string_literal(entry["category"])
    description = cpp_string_literal(entry["description"])
    leg_count = int(entry["leg_count"])
    multi_expiry = "true" if entry["multi_expiry"] else "false"
    return (
        f"    {{{strategy_id}, {name}, {category}, {description}, "
        f"{leg_count}, {multi_expiry}}},"
    )


def render_module(entries: list[dict]) -> str:
    body = "\n".join(render_entry(e) for e in entries)
    count = len(entries)
    ids_seen = set()
    for e in entries:
        if e["id"] in ids_seen:
            raise ValueError(f"duplicate strategy id in dataset: {e['id']!r}")
        ids_seen.add(e["id"])

    return f"""\
// GENERATED FILE -- DO NOT EDIT BY HAND.
//
// Produced by scripts/generate_strategy_catalogue.py from the single upstream
// source agent/dataset/strategies.json ({count} entries at generation time).
// Regenerate with:
//
//     python3 scripts/generate_strategy_catalogue.py
//
// and commit the result. Hand-editing this file only until the next
// regeneration silently discards the edit, and hand-editing the array without
// re-running the generator is exactly the drift this file exists to prevent.
//
// THREE PLACES CURRENTLY NAME THE 48 STRATEGY IDENTIFIERS THIS PRODUCT
// SUPPORTS. THEY MUST NOT DRIFT.
//   1. frontend/src/components/StrategySelector.tsx  -- the web UI's own list
//   2. agent/dataset/strategies.json                 -- the LLM fine-tuning
//                                                        dataset, and the
//                                                        SOURCE OF TRUTH for
//                                                        this file
//   3. backend/src/modules/strategy_catalogue.cppm   -- this file, the
//                                                        backend's runtime
//                                                        validator
//
// (2) -> (3) is kept honest mechanically: this file is regenerated from (2)
// by scripts/generate_strategy_catalogue.py, never edited by hand, so it
// cannot independently drift from the dataset.
//
// (1) and (2) have NO such mechanism between them; they are edited by hand,
// separately, by design (one is a UI list, the other a training set). At
// generation time they were verified to disagree: the frontend lists a
// 48th strategy, "crack_321" ("3-2-1 Crack Spread", category Futures), that
// does not appear in agent/dataset/strategies.json. This catalogue therefore
// has {count} entries, not 48, and correctly REFUSES "crack_321" if an LLM
// assistant ever emits it -- which is the intended behaviour (refuse rather
// than pass through an id this catalogue cannot vouch for), but it means a
// user selecting that strategy in the UI today is exercising a strategy the
// dataset and this validator do not know about. Resolve by either adding
// crack_321 to agent/dataset/strategies.json (then regenerating this file)
// or removing it from StrategySelector.tsx; this generator deliberately does
// not guess which.
module;
#include <algorithm>
#include <array>
#include <cstddef>
#include <span>
#include <string_view>

export module strategy_catalogue;

/*
 * The authoritative set of strategy identifiers the pricing engine and any
 * LLM assistant built on top of it are allowed to act on.
 *
 * This exists because an LLM assistant RPC returns a `strategy` string that
 * the backend cannot take on faith -- a model can hallucinate a plausible-
 * sounding id ("bull_call_ladder_wide") that was never defined anywhere, and
 * silently accepting it would mean pricing a strategy nobody specified the
 * legs for. is_known() is the gate: anything not in this table must be
 * refused, not guessed at or coerced to the nearest match.
 */
namespace options_calculator::strategy {{

/** One catalogued strategy. Every field here is safe to echo back to a caller. */
export struct StrategyInfo {{
    std::string_view id;           // e.g. "iron_condor" -- the wire identifier
    std::string_view name;         // e.g. "Iron Condor" -- for clarifications
    std::string_view category;     // e.g. "Neutral" -- for clarifications
    std::string_view description;  // one line, from the dataset
    int leg_count;
    bool multi_expiry;
}};

inline constexpr std::array<StrategyInfo, {count}> kCatalogue{{{{
{body}
}}}};

/** The full catalogue, for building refusal/clarification messages. */
export [[nodiscard]] constexpr auto all() noexcept -> std::span<const StrategyInfo> {{
    return kCatalogue;
}}

/** How many strategies are known. Expected to be {count}; see the header comment above. */
export [[nodiscard]] constexpr auto count() noexcept -> std::size_t {{
    return kCatalogue.size();
}}

/** Looks up a strategy by its exact wire id. Returns nullptr if unknown. */
export [[nodiscard]] constexpr auto find(std::string_view id) noexcept -> const StrategyInfo* {{
    const auto it = std::ranges::find_if(
        kCatalogue, [id](const StrategyInfo& s) noexcept {{ return s.id == id; }});
    return it == kCatalogue.end() ? nullptr : &*it;
}}

/**
 * Whether `id` is one of the strategies this backend actually knows how to
 * price. The gate an LLM assistant RPC must apply to its own output before
 * returning it -- ANYTHING outside this set must be refused, never passed
 * through and never approximated to the closest known id.
 */
export [[nodiscard]] constexpr auto is_known(std::string_view id) noexcept -> bool {{
    return find(id) != nullptr;
}}

}}  // namespace options_calculator::strategy
"""


def main() -> int:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument(
        "--check",
        action="store_true",
        help="Verify the checked-in file matches the dataset instead of writing it.",
    )
    args = parser.parse_args()

    with DATASET_PATH.open("r", encoding="utf-8") as f:
        entries = json.load(f)

    if not isinstance(entries, list) or not entries:
        print(f"ERROR: {DATASET_PATH} did not parse to a non-empty JSON array", file=sys.stderr)
        return 1

    rendered = render_module(entries)

    if args.check:
        current = OUTPUT_PATH.read_text(encoding="utf-8") if OUTPUT_PATH.exists() else ""
        if current != rendered:
            print(
                f"DRIFT: {OUTPUT_PATH} does not match what "
                f"{DATASET_PATH} generates. Run without --check to regenerate.",
                file=sys.stderr,
            )
            return 1
        print(f"OK: {OUTPUT_PATH} matches {DATASET_PATH} ({len(entries)} strategies).")
        return 0

    OUTPUT_PATH.write_text(rendered, encoding="utf-8")
    print(f"Wrote {OUTPUT_PATH} ({len(entries)} strategies) from {DATASET_PATH}.")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
