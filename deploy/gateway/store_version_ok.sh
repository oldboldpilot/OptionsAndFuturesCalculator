#!/usr/bin/env bash
#
# Does this image's binary OPEN the live store? One predicate, so it can be GATED.
#
# @author Olumuyiwa Oluwasanmi
#
# WHY A FILE OF ITS OWN. `redeploy.sh` decides this from the text of `sensen-gateway admin show`,
# and that decision is a DISCRIMINATOR: it has to pass on a success and fail on every refusal,
# including refusals nobody has seen yet. A discriminator needs a positive control, and a `case`
# written inline inside a script that builds images and replaces containers cannot have one --
# which is how the first version of it came to match one of the refusals it exists to catch.
#
# THE RULE IS: MATCH `show`'s OWN OUTPUT, NOT THE REFUSALS. `schema_version=` with the equals sign
# is what the success line prints. The first version also allowed a looser `schema version`, which
# `GatewayStore::verifyLocked` uses in its AHEAD refusal -- "the store is at schema version N but
# this binary only knows M" -- so a store NEWER than the image would have read as a pass and the
# deploy would have gone ahead. Matching the refusals instead is the other wrong answer: the BEHIND
# refusal measured on the live store on 2026-10-11 is "migration 8 ('gateway_hold_principal') has
# not been applied to this store", which names neither spelling, so a refusal-matching rule would
# have missed the very state this was written for.
#
# It reads the text on stdin and says nothing, so no caller can leak a DSN by echoing it: `admin
# show` is given its store as `env:NAME` precisely so the DSN is never an argument, and the one
# place the text is printed (`redeploy.sh`'s refusal arm) filters any line carrying a scheme.
store_version_ok() {
    local text
    text="$(cat)"
    case "$text" in
        *schema_version=*) return 0;;
        *) return 1;;
    esac
}
