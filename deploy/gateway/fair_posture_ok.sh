#!/usr/bin/env bash
#
# Are migration 13's artefacts IN FORCE on the store this gateway serves? One predicate, so it
# can be GATED.
#
# @author Olumuyiwa Oluwasanmi
#
# WHY A POSTURE CHECK AND NOT A BEHAVIOUR ONE. Migration 13 ('gateway_default_fair_plan') puts an
# org with NO plan row on the default fair plan, and it does that in three statements: a column
# grant of `orgs.id` to `gateway_capacity`, a `select` policy `orgs_capacity` on that table, and a
# `create or replace` of `sensen_gateway.model_fair_tenants`, whose `where` clause becomes
# `op.org_id is null or a.rations_fair`.
#
# `model_fair_tenants` is SECURITY DEFINER and returns one row per tenant competing for a model's
# capacity. Fair rationing divides `max_concurrent` between the rows it returns -- so if the grant
# or the policy were missing, or were dropped later, the function returns FEWER TENANTS and the
# share each one is given is too LARGE. There is no error, no refusal and no log line: the gateway
# serves, admits, and over-admits. That is a wrong answer dressed as an answer, and the only thing
# that can see it is an assertion about what is in force.
#
# OPENABILITY DOES NOT COVER IT, which is the reason this is a second predicate rather than a
# tightening of `store_version_ok`. The binary refuses a store BEHIND it (`migration 13 ... has
# not been applied`) and one AHEAD of it (`the store is at schema version N but this binary only
# knows M`), so opening the store does prove the version row reads 13. It proves nothing about the
# objects: `schema_migrations` keeps saying 13 after a policy is dropped. Same lesson this tree
# records for RLS -- the application's own query returns identical results whether or not the
# policy exists, so the posture has to be asserted directly.
#
# IT READS THE PROBE'S TEXT ON STDIN AND SAYS NOTHING, so no caller can leak anything by echoing
# it, and the probe itself needs no DSN and no password: it runs inside the database container.
fair_posture_ok() {
    local text key
    text="$(cat)"
    # EVERY artefact must be present AND true. Requiring the KEY as well as the value is what
    # makes a silent probe fail: a query that printed nothing, or that errored before reaching
    # the third statement, matches no key and is REFUSED rather than read as a pass. `admin
    # show`'s `schema_version=-1` taught this tree the same thing -- a surface answering "I do
    # not know" must not be taken for "yes".
    for key in capacity_can_select_orgs_id orgs_capacity_policy fair_tenants_includes_planless; do
        case "$text" in
            *"${key}=true"*) ;;
            *) return 1;;
        esac
    done
    return 0
}
