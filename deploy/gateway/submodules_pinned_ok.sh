#!/usr/bin/env bash
#
# Is EVERY submodule of the deploy source at the commit its superproject records? One predicate,
# so it can be GATED.
#
# @author Olumuyiwa Oluwasanmi
#
# WHY IT IS DERIVED AND NOT A LIST OF PATHS. `redeploy.sh` checked ONE submodule by name --
# `external/tbqwf` -- and on 2026-10-11 the deploy source was built with `external/
# fastestjsoninthewest` at `e8c970c` while the superproject recorded `25ffd2e6`. The binary that
# came out was therefore not the recorded tree, and nothing said so: fastjson was not the
# submodule anybody had thought to name. A hand-kept list of paths cannot notice the submodule
# that was added after the list was written, which is the defect this tree records against the
# four label-space tables and `ALLOWED_OPERATIONS`. `git submodule status` enumerates them for
# us, so there is nothing to keep.
#
# THE ONE-CHARACTER PREFIX IS THE WHOLE CONTRACT. `git submodule status` writes it in column 1:
#   ' '  the checkout IS the recorded commit
#   '+'  the checkout is a DIFFERENT commit -- newer or older, both wrong for a release
#   '-'  not initialised, so there is no checkout to build from
#   'U'  merge conflicts in the submodule
# Only ' ' is a release. '+' is refused in BOTH directions deliberately: a submodule ahead of its
# pin is the case that actually happened here, and it is indistinguishable from a release of code
# that was never recorded.
submodules_pinned_ok() {
    local text line
    text="$(cat)"
    # A SUBMODULE-LESS OR SILENT ANSWER IS REFUSED, NOT PASSED OVER. An empty string is what a
    # `git submodule status` that failed prints, and the deploy source has five submodules -- so
    # "nothing to check" here means the check did not run. Same rule as `admin show`'s
    # `schema_version=-1`: a surface that answers "I do not know" must not read as "yes".
    [ -n "$text" ] || return 1
    while IFS= read -r line; do
        [ -n "$line" ] || continue
        case "$line" in
            " "*) ;;
            *) return 1;;
        esac
    done <<< "$text"
    return 0
}

# The offending lines, for a refusal message. Prints nothing when everything is pinned. A
# submodule path is not a secret and carries no credential, unlike a remote URL.
submodules_unpinned_lines() {
    grep -vE '^ ' || true
}
