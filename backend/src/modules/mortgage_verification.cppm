// @author Olumuyiwa Oluwasanmi
//
// Mandatory, fail-closed verification of the MORTGAGE assistant's output,
// built on GP-ARA's DomainPolicy/ReasonerPolicy concepts
// (backend/sensen/src/gp_ara_interfaces.cppm).
//
// Sibling of `assistant_verification.cppm` (the options equivalent). The
// ARCHITECTURE is mirrored deliberately -- same concepts, same tri-state,
// same default-deny, same "policy functions live BESIDE the domain, never
// inside it" split. None of the options-specific RULES are carried over:
// there are no tickers, no strategy catalogue and no asset classes here.
//
// -----------------------------------------------------------------------
// WHY THIS EXISTS, AND WHY IT IS NOT THE SAME MODULE AS THE OPTIONS ONE
//
// The mortgage fine-tune was measured at 31.7% params exact-match (59/186)
// on held-out data. The measured failure classes, in ascending order of how
// hard they are to catch:
//
//   1. Invented operation      wanted ComputePayoffTiming, emitted ComputePayoff
//   2. Wrong operation         wanted ComputeFutureValueDetailed, emitted ComputeFutureValue
//   3. Invented field names    wanted annual_inflation_rate/compound_frequency,
//                              emitted annual_compounding_rate/compounding_periods_per_year
//   4. Wrong field name        wanted target_years, emitted years
//   5. CORRUPTED VALUE         wanted current_monthly_payment 5378.63, emitted 5379.00
//   6. No output               wanted a params block, emitted None
//
// (1)-(4) are closed-vocabulary questions: finance.proto enumerates every
// legal operation id and every legal field name per operation, so a
// membership test settles them. (6) is a structural question. (5) is the
// one that matters, and it is the reason this module exists at all rather
// than a schema validator:
//
//   > 5379.00 is structurally perfect. It names a real operation, sits in a
//   > real field of that operation's request message, parses as a decimal,
//   > satisfies every bound the RPC enforces, and PRICES A DIFFERENT LOAN.
//
// No amount of inspection of the model's own output can catch that, because
// nothing in the output is wrong ON ITS OWN TERMS. The only object that can
// falsify it is the user's own utterance. That is the same blind spot the
// options assistant had -- four adversarial probes produced output its
// verification layer returned Proven on precisely because the output was
// internally self-consistent -- and it is why gate 3 below (`ground_emitted_values`)
// takes the utterance as an explicit argument and is the strongest thing in
// this file.
//
// -----------------------------------------------------------------------
// THE FIVE GATES, IN ASCENDING ORDER OF STRENGTH
//
//   G1  closed-vocabulary OPERATION  -- MortgageParamsDomain::translate()
//   G2  closed-vocabulary FIELD SET, per operation (unknown, duplicated AND
//       missing keys; enum values against their own closed constant set)
//                                    -- MortgageParamsDomain::translate()
//   G3  VALUE GROUNDING against the utterance
//                                    -- ground_emitted_values(), outside the domain
//   G4  ABSENT params                -- verify_mortgage_output(), before anything else
//   G5  product-scope bounds         -- MortgageParamsDomain::translate()
//
// G1/G2/G5 are closed-form comparisons over the emitted fields alone and
// therefore belong inside the DomainPolicy, whose contract (inherited from
// the options module's file banner) is exactly that. G3 needs the user's
// free text, which is per-request and non-categorical, so it lives BESIDE
// the domain as its own exported function -- the identical placement, and
// the identical reason, as `strategy_has_lexical_support` in the options
// module. `verify_mortgage_output()` is the single composed entry point a
// service calls; it runs G4, then G1/G2/G5, then G3, and returns Proven
// only if all four stages do.
//
// -----------------------------------------------------------------------
// WHY THERE IS NO Z3 HERE
//
// Identical to the options module's answer, and for the identical reason:
// `backend/CMakeLists.txt` builds `sensen_slim`, which excludes gp_ara_agent
// (the only consumer of z3++ anywhere in sensen), so libz3 is not linked
// into this binary. `RuleBasedReasoner` below satisfies
// `sensen::gp_ara::ReasonerPolicy` exactly as `Z3Reasoner` would -- same
// ContextType, same signatures, same `std::expected<bool, ReasonerError>` --
// and decides each fact by direct evaluation. Every obligation in this file
// is a finite membership test, a comparison of two exact decimals, or a
// finite disjunction over a candidate set; none of them needs search.
// Nothing here models an SMT-LIB string, so an offline Z3 cross-check
// (spec 2026-08-05-mortgage-agent-misuse-prevention.md section 4.2) is a
// drop-in against this same domain rather than a rewrite of it.
//
// -----------------------------------------------------------------------
// THE TRI-STATE, AND WHAT LANDS IN EACH BUCKET
//
//   Proven         every gate passed. The caller MAY dispatch the RPC.
//   Unsafe         a gate found a DEFINITE contradiction. Refuse, naming it.
//   Indeterminate  a gate COULD NOT BE EVALUATED. Refuse, all the same.
//
// Indeterminate is reached in exactly two places, both of them genuine
// "this verifier was never taught that", never "probably fine":
//
//   - a field whose name matches none of `classify_slot`'s rules, so this
//     file has no opinion on what kind of quantity it holds and therefore
//     cannot ground it (see `SlotKind::Unclassified`);
//   - a model output with no params block at all (G4), which is not a
//     contradiction -- there is nothing to contradict -- but is equally not
//     something that may be served.
//
// Default-deny on Indeterminate is not merely the conservative choice here,
// it is the only one that survives an adversary: Indeterminate is by
// definition the catalogue of this rule table's blind spots, so a
// default-allow verifier reduces an attacker's job to finding one. See the
// misuse spec section 7 for the full argument, and note that
// `VerificationVerdict` default-constructs to Indeterminate so that a future
// early return which forgot to set `outcome` fails closed rather than open.
//
// -----------------------------------------------------------------------
// WHAT THIS MODULE DELIBERATELY DOES NOT DO
//
// NO REPAIR, EVER. `ComputePayoff` is refused, not rewritten to
// `ComputePayoffTiming`; `years` on `ComputeHomeFutureValue` is refused, not
// renamed to `target_years`; `5379.00` is refused, not snapped back to
// `5378.63`. The options module ships `normalize_strategy_alias`, which maps
// a two-token strategy id's transposition onto the one catalogue entry it
// unambiguously means -- that mechanism is NOT reproduced here, and the
// difference is not an oversight. A strategy id names a SHAPE the calculator
// then prices from separately-supplied legs; an operation id names WHICH
// ARITHMETIC RUNS, and `ComputePayoff` -> `ComputePayoffTiming` is a guess
// about which of several plausible mortgage computations the user wanted,
// with a different answer for each guess. Same for a field rename: `years`
// and `target_years` are the same word to a reader and different slots to
// the engine, and picking one is picking a number. A refusal costs the user
// a rephrase. A repair costs them a wrong loan.
//
// NO DUPLICATION OF THE RPC's OWN VALIDATION. `finance_service.cpp` already
// refuses, on the far side of this boundary:
//   - `check_payments_per_year`          payments_per_year in [1, 366]
//   - `check_rate_floor`                 per-period rate <= -100%
//   - `check_decimal_string_magnitude`   > 15 integer digits (BigDecimal range)
//   - `check_compound_growth_safe`       (1+r/n)^periods beyond ~40 nat units
// None of those four is re-implemented here. G5 adds only bounds that are
// PRODUCT SCOPE rather than library integrity -- sensen legitimately computes
// a 45% rate for some other caller, but a mortgage assistant emitting one has
// mis-transcribed, and the honest place to say so is here, before dispatch,
// as a named refusal rather than a gRPC error surfaced mid-render. Where a
// bound is tighter here than there (money at 1e10 versus the RPC's 1e15) that
// is a deliberate narrowing, not a second copy of the same rule.
export module mortgage_verification;
import std;

import sensen.gp_ara_interfaces;

namespace mortgage_calculator::assistant::verify {

// ===========================================================================
// 1. THE LABEL SPACE.
//
// Every operation id and every field name below is GENERATED from
// `backend/proto/finance.proto` -- the same source, read the same way, as
// `agent/dataset/build_mortgage_dataset.py`'s `parse_finance_proto()` /
// `build_operations()`, which is what produces the labels the model was
// trained on. That shared origin is the whole point: the trainer's label
// space and the verifier's label space cannot disagree, because both are
// projections of one .proto file.
//
// Scope is finance.proto's own service-block section banners -- "Time value
// of money", "Mortgages, HELOC", "Cash-flow analysis", "Depreciation",
// "Real estate" -- minus the two rate-theory utilities
// (ConvertInterestRate, ComputeFisherRate) `build_mortgage_dataset.py`
// excludes by name. 26 operations, 160 fields. Fixed income, futures,
// options and portfolio RPCs are OUT of this assistant's label space by
// construction, so a model that emits `AnalyzeBond` is refused by G1 exactly
// like a model that emits `ComputePayoff`: neither is a mortgage operation.
//
// THIS TABLE IS A COPY, AND THE COPY IS CHECKED. Embedding it (rather than
// parsing the .proto at start-up) keeps this module free of any file or
// protobuf dependency, exactly as the options module keeps itself free of
// proto and of assistant_service.cpp. The drift that always threatens a copy
// is closed by `tests/test_mortgage_verification.cpp`, which re-parses
// finance.proto at test time with the same section/exclusion rules and fails
// if this table and that file have diverged in either direction.
// ===========================================================================

/** One (operation, field) pair of the label space, with the proto type the
 * field is declared as. `proto_type` is carried because the string/double
 * distinction is load-bearing in this contract (finance.proto's own numeric-
 * types banner: `string` where sensen computes in BigDecimal, `double` where
 * it genuinely computes in double) and because an enum-typed field's type
 * name is how `kEnumConstants` below is keyed. */
export struct FieldSpec {
    std::string_view operation;
    std::string_view field;
    std::string_view proto_type;
    bool repeated = false;
};

namespace detail {

// GENERATED from backend/proto/finance.proto -- do not hand-edit.
// Regenerate exactly as agent/dataset/build_mortgage_dataset.py derives
// its own OPERATIONS dict (parse_finance_proto + build_operations, the
// same IN_SCOPE_SECTIONS and EXCLUDE_RPCS). The test re-parses the .proto
// and fails if this table has drifted from it in either direction.
constexpr std::array<FieldSpec, 184> kLabelSpace{{
    {.operation = "ComputeAmortization", .field = "loan_amount", .proto_type = "string", .repeated = false},
    {.operation = "ComputeAmortization", .field = "annual_rate", .proto_type = "string", .repeated = false},
    {.operation = "ComputeAmortization", .field = "term_months", .proto_type = "int32", .repeated = false},
    {.operation = "ComputeAmortization", .field = "monthly_overpayment", .proto_type = "string", .repeated = false},
    {.operation = "ComputeAmortization", .field = "pmi_annual_rate", .proto_type = "string", .repeated = false},
    {.operation = "ComputeAmortization", .field = "original_home_value", .proto_type = "string", .repeated = false},
    {.operation = "ComputeAmortizationBatch", .field = "loan_amounts", .proto_type = "double", .repeated = true},
    {.operation = "ComputeAmortizationBatch", .field = "annual_rates", .proto_type = "double", .repeated = true},
    {.operation = "ComputeAmortizationBatch", .field = "term_months", .proto_type = "int32", .repeated = true},
    {.operation = "ComputeAmortizationBatch", .field = "extra_payments", .proto_type = "double", .repeated = true},
    {.operation = "ComputeAmortizationBatch", .field = "pmi_rates", .proto_type = "double", .repeated = true},
    {.operation = "ComputeAmortizationBatch", .field = "home_values", .proto_type = "double", .repeated = true},
    {.operation = "ComputeClosingCosts", .field = "home_price", .proto_type = "string", .repeated = false},
    {.operation = "ComputeClosingCosts", .field = "down_payment_percent", .proto_type = "string", .repeated = false},
    {.operation = "ComputeClosingCosts", .field = "annual_rate", .proto_type = "string", .repeated = false},
    {.operation = "ComputeClosingCosts", .field = "origination_fee_percent", .proto_type = "string", .repeated = false},
    {.operation = "ComputeClosingCosts", .field = "discount_points_percent", .proto_type = "string", .repeated = false},
    {.operation = "ComputeClosingCosts", .field = "other_lender_fees", .proto_type = "string", .repeated = false},
    {.operation = "ComputeClosingCosts", .field = "title_settlement_percent", .proto_type = "string", .repeated = false},
    {.operation = "ComputeClosingCosts", .field = "appraisal_fee", .proto_type = "string", .repeated = false},
    {.operation = "ComputeClosingCosts", .field = "inspection_fee", .proto_type = "string", .repeated = false},
    {.operation = "ComputeClosingCosts", .field = "recording_fees", .proto_type = "string", .repeated = false},
    {.operation = "ComputeClosingCosts", .field = "transfer_tax_percent", .proto_type = "string", .repeated = false},
    {.operation = "ComputeClosingCosts", .field = "homeowners_insurance_annual", .proto_type = "string", .repeated = false},
    {.operation = "ComputeClosingCosts", .field = "property_tax_annual", .proto_type = "string", .repeated = false},
    {.operation = "ComputeClosingCosts", .field = "tax_escrow_months", .proto_type = "int32", .repeated = false},
    {.operation = "ComputeClosingCosts", .field = "seller_lender_credits", .proto_type = "string", .repeated = false},
    {.operation = "ComputeClosingCosts", .field = "prepaid_interest_days", .proto_type = "int32", .repeated = false},
    {.operation = "ComputeCumulative", .field = "component", .proto_type = "Component", .repeated = false},
    {.operation = "ComputeCumulative", .field = "rate", .proto_type = "double", .repeated = false},
    {.operation = "ComputeCumulative", .field = "periods", .proto_type = "int32", .repeated = false},
    {.operation = "ComputeCumulative", .field = "present_value", .proto_type = "double", .repeated = false},
    {.operation = "ComputeCumulative", .field = "start_period", .proto_type = "int32", .repeated = false},
    {.operation = "ComputeCumulative", .field = "end_period", .proto_type = "int32", .repeated = false},
    {.operation = "ComputeCumulative", .field = "timing", .proto_type = "AnnuityTiming", .repeated = false},
    {.operation = "ComputeDepreciation", .field = "method", .proto_type = "Method", .repeated = false},
    {.operation = "ComputeDepreciation", .field = "cost", .proto_type = "double", .repeated = false},
    {.operation = "ComputeDepreciation", .field = "salvage", .proto_type = "double", .repeated = false},
    {.operation = "ComputeDepreciation", .field = "life", .proto_type = "double", .repeated = false},
    {.operation = "ComputeDepreciation", .field = "period", .proto_type = "double", .repeated = false},
    {.operation = "ComputeDepreciation", .field = "factor", .proto_type = "double", .repeated = false},
    {.operation = "ComputeDepreciation", .field = "recovery_period", .proto_type = "int32", .repeated = false},
    {.operation = "ComputeDepreciation", .field = "year", .proto_type = "int32", .repeated = false},
    {.operation = "ComputeDetailedAmortization", .field = "loan_amount", .proto_type = "string", .repeated = false},
    {.operation = "ComputeDetailedAmortization", .field = "annual_rate", .proto_type = "string", .repeated = false},
    {.operation = "ComputeDetailedAmortization", .field = "term_months", .proto_type = "int32", .repeated = false},
    {.operation = "ComputeDetailedAmortization", .field = "monthly_overpayment", .proto_type = "string", .repeated = false},
    {.operation = "ComputeDetailedAmortization", .field = "pmi_annual_rate", .proto_type = "string", .repeated = false},
    {.operation = "ComputeDetailedAmortization", .field = "original_home_value", .proto_type = "string", .repeated = false},
    {.operation = "ComputeDetailedAmortization", .field = "annual_tax_rate", .proto_type = "string", .repeated = false},
    {.operation = "ComputeFutureValue", .field = "rate", .proto_type = "string", .repeated = false},
    {.operation = "ComputeFutureValue", .field = "periods", .proto_type = "int32", .repeated = false},
    {.operation = "ComputeFutureValue", .field = "payment", .proto_type = "string", .repeated = false},
    {.operation = "ComputeFutureValue", .field = "present_value", .proto_type = "string", .repeated = false},
    {.operation = "ComputeFutureValue", .field = "timing", .proto_type = "AnnuityTiming", .repeated = false},
    {.operation = "ComputeFutureValueDetailed", .field = "annual_rate", .proto_type = "string", .repeated = false},
    {.operation = "ComputeFutureValueDetailed", .field = "years", .proto_type = "int32", .repeated = false},
    {.operation = "ComputeFutureValueDetailed", .field = "annual_contribution", .proto_type = "string", .repeated = false},
    {.operation = "ComputeFutureValueDetailed", .field = "current_principal", .proto_type = "string", .repeated = false},
    {.operation = "ComputeFutureValueDetailed", .field = "annual_inflation_rate", .proto_type = "string", .repeated = false},
    {.operation = "ComputeFutureValueDetailed", .field = "compound_frequency", .proto_type = "int32", .repeated = false},
    {.operation = "ComputeHeloc", .field = "home_value", .proto_type = "string", .repeated = false},
    {.operation = "ComputeHeloc", .field = "current_mortgage_balance", .proto_type = "string", .repeated = false},
    {.operation = "ComputeHeloc", .field = "max_ltv_rate", .proto_type = "string", .repeated = false},
    {.operation = "ComputeHeloc", .field = "drawn_amount", .proto_type = "string", .repeated = false},
    {.operation = "ComputeHeloc", .field = "annual_rate", .proto_type = "string", .repeated = false},
    {.operation = "ComputeHeloc", .field = "repayment_term_years", .proto_type = "int32", .repeated = false},
    {.operation = "ComputeHeloc", .field = "payments_per_year", .proto_type = "int32", .repeated = false},
    {.operation = "ComputeHomeFutureValue", .field = "current_property_value", .proto_type = "string", .repeated = false},
    {.operation = "ComputeHomeFutureValue", .field = "annual_appreciation_rate", .proto_type = "string", .repeated = false},
    {.operation = "ComputeHomeFutureValue", .field = "current_loan_balance", .proto_type = "string", .repeated = false},
    {.operation = "ComputeHomeFutureValue", .field = "annual_mortgage_rate", .proto_type = "string", .repeated = false},
    {.operation = "ComputeHomeFutureValue", .field = "current_monthly_payment", .proto_type = "string", .repeated = false},
    {.operation = "ComputeHomeFutureValue", .field = "target_years", .proto_type = "int32", .repeated = false},
    {.operation = "ComputeHomeFutureValue", .field = "payments_per_year", .proto_type = "int32", .repeated = false},
    {.operation = "ComputeHomeNpv", .field = "property_price", .proto_type = "string", .repeated = false},
    {.operation = "ComputeHomeNpv", .field = "down_payment", .proto_type = "string", .repeated = false},
    {.operation = "ComputeHomeNpv", .field = "closing_costs_buy", .proto_type = "string", .repeated = false},
    {.operation = "ComputeHomeNpv", .field = "loan_amount", .proto_type = "string", .repeated = false},
    {.operation = "ComputeHomeNpv", .field = "loan_annual_rate", .proto_type = "string", .repeated = false},
    {.operation = "ComputeHomeNpv", .field = "loan_term_years", .proto_type = "int32", .repeated = false},
    {.operation = "ComputeHomeNpv", .field = "monthly_taxes_ins_hoa", .proto_type = "string", .repeated = false},
    {.operation = "ComputeHomeNpv", .field = "monthly_maintenance", .proto_type = "string", .repeated = false},
    {.operation = "ComputeHomeNpv", .field = "annual_appreciation_rate", .proto_type = "string", .repeated = false},
    {.operation = "ComputeHomeNpv", .field = "selling_closing_cost_percent", .proto_type = "string", .repeated = false},
    {.operation = "ComputeHomeNpv", .field = "monthly_rent_saved", .proto_type = "string", .repeated = false},
    {.operation = "ComputeHomeNpv", .field = "annual_rent_increase", .proto_type = "string", .repeated = false},
    {.operation = "ComputeHomeNpv", .field = "annual_discount_rate", .proto_type = "string", .repeated = false},
    {.operation = "ComputeHomeNpv", .field = "holding_period_years", .proto_type = "int32", .repeated = false},
    {.operation = "ComputeHomeNpv", .field = "annual_inflation_rate", .proto_type = "string", .repeated = false},
    {.operation = "ComputeInterestPayment", .field = "rate", .proto_type = "string", .repeated = false},
    {.operation = "ComputeInterestPayment", .field = "period", .proto_type = "int32", .repeated = false},
    {.operation = "ComputeInterestPayment", .field = "periods", .proto_type = "int32", .repeated = false},
    {.operation = "ComputeInterestPayment", .field = "present_value", .proto_type = "string", .repeated = false},
    {.operation = "ComputeInterestPayment", .field = "future_value", .proto_type = "string", .repeated = false},
    {.operation = "ComputeInterestPayment", .field = "timing", .proto_type = "AnnuityTiming", .repeated = false},
    {.operation = "ComputeIrr", .field = "values", .proto_type = "double", .repeated = true},
    {.operation = "ComputeIrr", .field = "guess", .proto_type = "double", .repeated = false},
    {.operation = "ComputeMortgageRecast", .field = "current_loan_balance", .proto_type = "string", .repeated = false},
    {.operation = "ComputeMortgageRecast", .field = "current_monthly_payment", .proto_type = "string", .repeated = false},
    {.operation = "ComputeMortgageRecast", .field = "lump_sum_payment", .proto_type = "string", .repeated = false},
    {.operation = "ComputeMortgageRecast", .field = "annual_rate", .proto_type = "string", .repeated = false},
    {.operation = "ComputeMortgageRecast", .field = "remaining_months", .proto_type = "int32", .repeated = false},
    {.operation = "ComputeMortgageRecast", .field = "payments_per_year", .proto_type = "int32", .repeated = false},
    {.operation = "ComputeNpv", .field = "rate", .proto_type = "double", .repeated = false},
    {.operation = "ComputeNpv", .field = "values", .proto_type = "double", .repeated = true},
    {.operation = "ComputePaybackPeriod", .field = "values", .proto_type = "double", .repeated = true},
    {.operation = "ComputePaybackPeriod", .field = "discounted", .proto_type = "bool", .repeated = false},
    {.operation = "ComputePaybackPeriod", .field = "rate", .proto_type = "double", .repeated = false},
    {.operation = "ComputePayment", .field = "rate", .proto_type = "string", .repeated = false},
    {.operation = "ComputePayment", .field = "periods", .proto_type = "int32", .repeated = false},
    {.operation = "ComputePayment", .field = "present_value", .proto_type = "string", .repeated = false},
    {.operation = "ComputePayment", .field = "future_value", .proto_type = "string", .repeated = false},
    {.operation = "ComputePayment", .field = "timing", .proto_type = "AnnuityTiming", .repeated = false},
    {.operation = "ComputePayoffTiming", .field = "current_loan_balance", .proto_type = "string", .repeated = false},
    {.operation = "ComputePayoffTiming", .field = "annual_rate", .proto_type = "string", .repeated = false},
    {.operation = "ComputePayoffTiming", .field = "current_monthly_payment", .proto_type = "string", .repeated = false},
    {.operation = "ComputePayoffTiming", .field = "extra_monthly_payment", .proto_type = "string", .repeated = false},
    {.operation = "ComputePayoffTiming", .field = "payments_per_year", .proto_type = "int32", .repeated = false},
    {.operation = "ComputePeriods", .field = "rate", .proto_type = "string", .repeated = false},
    {.operation = "ComputePeriods", .field = "payment", .proto_type = "string", .repeated = false},
    {.operation = "ComputePeriods", .field = "present_value", .proto_type = "string", .repeated = false},
    {.operation = "ComputePeriods", .field = "future_value", .proto_type = "string", .repeated = false},
    {.operation = "ComputePeriods", .field = "timing", .proto_type = "AnnuityTiming", .repeated = false},
    {.operation = "ComputePresentValue", .field = "rate", .proto_type = "string", .repeated = false},
    {.operation = "ComputePresentValue", .field = "periods", .proto_type = "int32", .repeated = false},
    {.operation = "ComputePresentValue", .field = "payment", .proto_type = "string", .repeated = false},
    {.operation = "ComputePresentValue", .field = "future_value", .proto_type = "string", .repeated = false},
    {.operation = "ComputePresentValue", .field = "timing", .proto_type = "AnnuityTiming", .repeated = false},
    {.operation = "ComputePrincipalPayment", .field = "rate", .proto_type = "string", .repeated = false},
    {.operation = "ComputePrincipalPayment", .field = "period", .proto_type = "int32", .repeated = false},
    {.operation = "ComputePrincipalPayment", .field = "periods", .proto_type = "int32", .repeated = false},
    {.operation = "ComputePrincipalPayment", .field = "present_value", .proto_type = "string", .repeated = false},
    {.operation = "ComputePrincipalPayment", .field = "future_value", .proto_type = "string", .repeated = false},
    {.operation = "ComputePrincipalPayment", .field = "timing", .proto_type = "AnnuityTiming", .repeated = false},
    {.operation = "ComputeRate", .field = "periods", .proto_type = "int32", .repeated = false},
    {.operation = "ComputeRate", .field = "payment", .proto_type = "string", .repeated = false},
    {.operation = "ComputeRate", .field = "present_value", .proto_type = "string", .repeated = false},
    {.operation = "ComputeRate", .field = "future_value", .proto_type = "string", .repeated = false},
    {.operation = "ComputeRate", .field = "timing", .proto_type = "AnnuityTiming", .repeated = false},
    {.operation = "ComputeRate", .field = "guess", .proto_type = "string", .repeated = false},
    {.operation = "ComputeRefinance", .field = "current_loan_balance", .proto_type = "string", .repeated = false},
    {.operation = "ComputeRefinance", .field = "current_monthly_payment", .proto_type = "string", .repeated = false},
    {.operation = "ComputeRefinance", .field = "current_annual_rate", .proto_type = "string", .repeated = false},
    {.operation = "ComputeRefinance", .field = "current_remaining_months", .proto_type = "int32", .repeated = false},
    {.operation = "ComputeRefinance", .field = "property_value", .proto_type = "string", .repeated = false},
    {.operation = "ComputeRefinance", .field = "new_annual_rate", .proto_type = "string", .repeated = false},
    {.operation = "ComputeRefinance", .field = "new_term_years", .proto_type = "int32", .repeated = false},
    {.operation = "ComputeRefinance", .field = "closing_costs", .proto_type = "string", .repeated = false},
    {.operation = "ComputeRefinance", .field = "closing_cost_type", .proto_type = "ClosingCostType", .repeated = false},
    {.operation = "ComputeRefinance", .field = "cash_out_amount", .proto_type = "string", .repeated = false},
    {.operation = "ComputeRefinance", .field = "current_pmi_monthly", .proto_type = "string", .repeated = false},
    {.operation = "ComputeRefinance", .field = "new_pmi_monthly", .proto_type = "string", .repeated = false},
    {.operation = "ComputeRefinance", .field = "pmi_drop_off_ltv", .proto_type = "string", .repeated = false},
    {.operation = "ComputeRefinance", .field = "payments_per_year", .proto_type = "int32", .repeated = false},
    {.operation = "ComputeRentVsBuy", .field = "property_price", .proto_type = "string", .repeated = false},
    {.operation = "ComputeRentVsBuy", .field = "down_payment", .proto_type = "string", .repeated = false},
    {.operation = "ComputeRentVsBuy", .field = "monthly_piti_and_maintenance", .proto_type = "string", .repeated = false},
    {.operation = "ComputeRentVsBuy", .field = "annual_home_appreciation", .proto_type = "string", .repeated = false},
    {.operation = "ComputeRentVsBuy", .field = "current_monthly_rent", .proto_type = "string", .repeated = false},
    {.operation = "ComputeRentVsBuy", .field = "annual_rent_increase", .proto_type = "string", .repeated = false},
    {.operation = "ComputeRentVsBuy", .field = "annual_investment_return", .proto_type = "string", .repeated = false},
    {.operation = "ComputeRentVsBuy", .field = "years", .proto_type = "int32", .repeated = false},
    {.operation = "ComputeRentVsBuy", .field = "loan_annual_rate", .proto_type = "string", .repeated = false},
    {.operation = "ComputeRentVsBuy", .field = "loan_term_years", .proto_type = "int32", .repeated = false},
    {.operation = "ComputeRentVsBuy", .field = "loan_amount", .proto_type = "string", .repeated = false},
    {.operation = "ComputeRentVsBuy", .field = "monthly_taxes_ins_maintenance", .proto_type = "string", .repeated = false},
    {.operation = "ComputeRentVsBuy", .field = "closing_costs_buy", .proto_type = "string", .repeated = false},
    {.operation = "ComputeRentVsBuy", .field = "selling_cost_percent", .proto_type = "string", .repeated = false},
    {.operation = "ComputeRentVsBuy", .field = "annual_inflation_rate", .proto_type = "string", .repeated = false},
    {.operation = "ComputeRentalRoi", .field = "property_value", .proto_type = "string", .repeated = false},
    {.operation = "ComputeRentalRoi", .field = "total_cash_invested", .proto_type = "string", .repeated = false},
    {.operation = "ComputeRentalRoi", .field = "periodic_gross_rent", .proto_type = "string", .repeated = false},
    {.operation = "ComputeRentalRoi", .field = "periodic_operating_expenses", .proto_type = "string", .repeated = false},
    {.operation = "ComputeRentalRoi", .field = "periodic_mortgage_payment", .proto_type = "string", .repeated = false},
    {.operation = "ComputeRentalRoi", .field = "periods_per_year", .proto_type = "int32", .repeated = false},
    {.operation = "ComputeXirr", .field = "rate", .proto_type = "double", .repeated = false},
    {.operation = "ComputeXirr", .field = "values", .proto_type = "double", .repeated = true},
    {.operation = "ComputeXirr", .field = "dates", .proto_type = "double", .repeated = true},
    {.operation = "ComputeXirr", .field = "guess", .proto_type = "double", .repeated = false},
    {.operation = "ComputeXnpv", .field = "rate", .proto_type = "double", .repeated = false},
    {.operation = "ComputeXnpv", .field = "values", .proto_type = "double", .repeated = true},
    {.operation = "ComputeXnpv", .field = "dates", .proto_type = "double", .repeated = true},
    {.operation = "ComputeXnpv", .field = "guess", .proto_type = "double", .repeated = false},
}};

constexpr std::array<std::string_view, 27> kOperationIds{
    "ComputeAmortization", "ComputeAmortizationBatch", "ComputeClosingCosts",
    "ComputeCumulative",
    "ComputeDepreciation", "ComputeDetailedAmortization", "ComputeFutureValue",
    "ComputeFutureValueDetailed", "ComputeHeloc", "ComputeHomeFutureValue",
    "ComputeHomeNpv", "ComputeInterestPayment", "ComputeIrr",
    "ComputeMortgageRecast", "ComputeNpv", "ComputePaybackPeriod",
    "ComputePayment", "ComputePayoffTiming", "ComputePeriods",
    "ComputePresentValue", "ComputePrincipalPayment", "ComputeRate",
    "ComputeRefinance", "ComputeRentVsBuy", "ComputeRentalRoi",
    "ComputeXirr", "ComputeXnpv",
};

/** Closed constant sets for every enum type reachable from the label space,
 * read from finance.proto alongside the fields. An enum field is a
 * closed-vocabulary check just like an operation id, so it is settled the
 * same way -- membership, never repair. */
struct EnumSpec {
    std::string_view type_name;
    std::array<std::string_view, 4> constants;
    std::size_t count;
};

constexpr std::array<EnumSpec, 4> kEnumConstants{{
    {.type_name = "AnnuityTiming",
     .constants = {"END_OF_PERIOD", "BEGINNING_OF_PERIOD", "", ""},
     .count = 2},
    {.type_name = "Component", .constants = {"INTEREST", "PRINCIPAL", "", ""}, .count = 2},
    {.type_name = "ClosingCostType",
     .constants = {"PAID_IN_CASH", "ROLLED_INTO_LOAN", "", ""},
     .count = 2},
    {.type_name = "Method",
     .constants = {"STRAIGHT_LINE", "SUM_OF_YEARS_DIGITS", "DECLINING_BALANCE", "MACRS"},
     .count = 4},
}};

}  // namespace detail

/** Every operation id this assistant may emit, for a caller that wants to
 * enumerate the label space (the test's drift check does). */
export [[nodiscard]] auto operation_ids() -> std::vector<std::string_view>;

/** The declared field set of `operation`'s request message, in the proto's
 * own declaration order, or an empty vector if `operation` is not in the
 * label space at all. */
export [[nodiscard]] auto fields_of(std::string_view operation) -> std::vector<FieldSpec>;

/** True iff `operation` is one of the 26 ids finance.proto declares under
 * this assistant's in-scope sections. This is G1 in one line. */
export [[nodiscard]] auto is_known_operation(std::string_view operation) -> bool;

/** The spec for one (operation, field) pair, or `nullptr` if `field` is not
 * declared on `operation`'s request message. This is G2 in one line -- and
 * note it is keyed on BOTH, not on the field name alone: `years` is a real
 * field of `ComputeRentVsBuy` and is not a field of `ComputeHomeFutureValue`
 * (which spells the same idea `target_years`), so a check that asked only
 * "is `years` a field name anywhere" would pass exactly the defect row this
 * gate exists to catch. */
export [[nodiscard]] auto find_field(std::string_view operation, std::string_view field)
    -> const FieldSpec*;

// ===========================================================================
// 2. EXACT DECIMAL ARITHMETIC.
//
// Everything numeric in this file is compared as an exact fixed-point
// decimal, never as a `double`. Two reasons, both concrete:
//
//   - The obligation this module's most important gate discharges is
//     "5378.63 was said and 5379.00 was emitted." A float comparison with a
//     tolerance chosen by feel is exactly how a 37-cent discrepancy becomes
//     a rounding artefact somebody argues about. `Decimal` makes the
//     tolerance an explicit, documented number of decimal places.
//   - The values arriving here are DECIMAL STRINGS off the wire, carrying
//     sensen's BigDecimal semantics (finance.proto's numeric-types banner).
//     Parsing them to double to compare them re-introduces the exact loss
//     that contract exists to avoid.
//
// Scale is 1e15 -- fifteen places. That is three orders finer than the
// finest tolerance any slot below uses (a rate at 1e-6) and comfortably
// inside `__int128` for every magnitude G5 admits (1e10 money x 1e15 = 1e25,
// against an `__int128` ceiling near 1.7e38, leaving room for the x12 and
// x1e6 candidate maps to run without overflow).
// ===========================================================================

export class Decimal {
  public:
    static constexpr int kPlaces = 15;
    static constexpr __int128 kScale = static_cast<__int128>(1'000'000'000'000'000LL);

    constexpr Decimal() noexcept = default;
    explicit constexpr Decimal(__int128 units) noexcept : units_(units) {}

    [[nodiscard]] constexpr auto units() const noexcept -> __int128 { return units_; }

    [[nodiscard]] static constexpr auto from_integer(std::int64_t v) noexcept -> Decimal {
        return Decimal{static_cast<__int128>(v) * kScale};
    }

    [[nodiscard]] constexpr auto negated() const noexcept -> Decimal { return Decimal{-units_}; }

    [[nodiscard]] constexpr auto is_negative() const noexcept -> bool { return units_ < 0; }
    [[nodiscard]] constexpr auto is_zero() const noexcept -> bool { return units_ == 0; }

    [[nodiscard]] constexpr auto operator==(const Decimal& other) const noexcept -> bool {
        return units_ == other.units_;
    }

    /** |this - other| <= tolerance_units. The ONLY comparison this file uses
     * for a value that went through a candidate map, and the only place a
     * tolerance is ever applied -- see `slot_tolerance_units` for where each
     * tolerance comes from and why it is not a free parameter. */
    [[nodiscard]] constexpr auto within(const Decimal& other, __int128 tolerance_units) const noexcept
        -> bool {
        const __int128 diff = units_ > other.units_ ? units_ - other.units_ : other.units_ - units_;
        return diff <= tolerance_units;
    }

    /** Exact multiply by a small positive integer, or `nullopt` on overflow.
     * Used by the x12 (years -> months) and x1e3/x1e6 (k/million) maps. */
    [[nodiscard]] constexpr auto scaled_by(std::int64_t factor) const noexcept
        -> std::optional<Decimal> {
        constexpr __int128 kCeiling = static_cast<__int128>(1) << 120;
        if (factor <= 0) return std::nullopt;
        const __int128 magnitude = units_ < 0 ? -units_ : units_;
        if (magnitude != 0 && magnitude > kCeiling / factor) return std::nullopt;
        return Decimal{units_ * factor};
    }

    /** Truncating divide by a small positive integer. Truncation loses at
     * most 1e-15, which is five orders below the tightest tolerance any slot
     * uses, so it can never flip a verdict; `divided_exactly_by` is the
     * variant used where exactness is required instead. */
    [[nodiscard]] constexpr auto divided_by(std::int64_t divisor) const noexcept
        -> std::optional<Decimal> {
        if (divisor <= 0) return std::nullopt;
        return Decimal{units_ / divisor};
    }

    /** Divide by a small positive integer ONLY if it divides exactly.
     * Used where a lossy conversion would be a guess rather than a
     * transcription -- months -> years is admissible for 360 months and is
     * NOT admissible for 361. */
    [[nodiscard]] constexpr auto divided_exactly_by(std::int64_t divisor) const noexcept
        -> std::optional<Decimal> {
        if (divisor <= 0) return std::nullopt;
        if (units_ % divisor != 0) return std::nullopt;
        return Decimal{units_ / divisor};
    }

    /** Human-readable, for refusal messages only -- never for comparison. */
    [[nodiscard]] auto to_string() const -> std::string;

  private:
    __int128 units_ = 0;
};

/**
 * The strict decimal grammar every numeric value must satisfy before any
 * arithmetic exists to overflow: `-?[0-9]{1,15}(\.[0-9]{1,18})?`, total
 * length <= 34, nothing else.
 *
 * Rejects, deliberately and by construction rather than by blocklist: NaN
 * and Inf in every spelling, exponent notation (`1e309`), hex (`0x1p4`), a
 * leading `+`, internal or surrounding whitespace, thousands separators,
 * currency symbols, and the empty string. The model is trained to emit bare
 * decimals (`build_mortgage_dataset.py`'s `money_str`/`rate_str`), so
 * anything else is already off-distribution before it is dangerous.
 *
 * Returns `nullopt` on any violation -- the caller turns that into a
 * `MalformedNumber` refusal naming the field, never into a zero.
 */
export [[nodiscard]] auto parse_strict_decimal(std::string_view text) -> std::optional<Decimal>;

// ===========================================================================
// 3. SLOT KINDS.
//
// finance.proto types a money field and a rate field identically (both are
// `string`, both are BigDecimal), so the proto type alone cannot say whether
// `0.0468` in a given slot is four and two thirds cents or four and
// two-thirds percent. The grounding gate needs that distinction to know
// which candidate maps are admissible, so this file derives it from the
// field NAME through the ordered rule list below.
//
// The rules are ordered, first-match-wins, and every one of them is either
// an exact name or a suffix -- no substring matching, because a substring
// rule that fired on "rent" would classify `annual_rent_increase` as money.
// Checked exhaustively against all 85 distinct field names in the label
// space; `test_mortgage_verification.cpp` re-checks that exhaustiveness on
// every run, so a field finance.proto grows later that matches no rule turns
// the test red rather than silently defaulting.
//
// A name that matches nothing classifies as `Unclassified`, which is an
// Indeterminate, which is a refusal. Absence of a rule is never treated as
// permission.
// ===========================================================================

export enum class SlotKind {
    Money,         ///< an amount of currency (BigDecimal string or double)
    Rate,          ///< an interest/growth rate as a fraction of 1 (0.0468 == 4.68%)
    Ratio,         ///< a non-rate proportion: LTV caps, cost percentages
    MonthCount,    ///< an integer count of months / of monthly periods
    YearCount,     ///< a count of years (may be fractional: a 27.5-year MACRS life)
    PeriodIndex,   ///< a 1-based period ordinal ("payment 45", "year 3")
    Frequency,     ///< periods per year
    DayOffsets,    ///< day offsets for an irregular cash-flow series
    Dimensionless, ///< a bare multiplier (declining-balance `factor`)
    Enumeration,   ///< a proto enum constant; checked by membership, not grounded
    Boolean,       ///< a proto bool; checked by membership, not grounded
    Unclassified,  ///< no rule matched -> Indeterminate -> refusal
};

/** The slot kind for a proto field name, by the ordered rules described
 * above. Exported because the test asserts totality over the label space and
 * spot-checks the rules that are easy to get backwards. */
export [[nodiscard]] auto classify_slot(std::string_view field_name) -> SlotKind;

// ===========================================================================
// 4. THE MODEL'S OUTPUT, AS GP-ARA's InputDataType.
// ===========================================================================

/** One field of the emitted `<params>` object. `values` holds one entry for
 * a scalar and N for a repeated field, so the two cases share one shape and
 * one code path. Values are carried as the TEXT the model emitted, never
 * pre-parsed: the strict grammar above is part of the verification, and a
 * caller that parsed first would have already decided what a malformed
 * number means. */
export struct EmittedField {
    std::string name;
    std::vector<std::string> values;
    bool repeated = false;
};

/** Mirrors what a mortgage `ParseResponse.params` carries. A plain struct,
 * not a proto type, so this module has no protobuf dependency -- mapping
 * to and from the wire message belongs to the service, one layer up, which
 * this module does not touch and must not depend on. */
export struct MortgageParamsInput {
    /** false == the model emitted no `<params>` block at all. G4. This is
     * NOT modelled as "an input with zero fields": a params block that
     * exists and is empty is a different defect from no params block, and
     * collapsing them would let a caller construct the absent case by
     * accident. */
    bool params_emitted = false;
    std::string operation;
    std::vector<EmittedField> fields;
};

/** Why a verification failed. Deliberately finer-grained than the options
 * module's three codes, because this contract is NEW (`mortgage_assistant.proto`
 * does not exist yet, and is another agent's file) and so is not constrained
 * to reasons an already-frozen proto happens to have -- the misuse spec
 * section 2.2 makes the same observation about `ADVICE_REQUESTED`. A service
 * mapping these onto its own wire enum may collapse them; it must not
 * silently drop the distinction between a definite refusal and an
 * Indeterminate one. */
export enum class ReasonCode {
    None,             ///< only valid alongside Outcome::Proven
    NoParamsEmitted,  ///< G4: no `<params>` block
    UnknownOperation, ///< G1: operation not in finance.proto's in-scope set
    UnknownField,     ///< G2: field not declared on that operation's request
    MissingField,     ///< G2: a declared field of that operation was not emitted
    DuplicateField,   ///< G2: the same field emitted twice
    ShapeMismatch,    ///< G2: scalar emitted for a repeated field, or vice versa
    InvalidEnumValue, ///< G2: enum constant not in that enum's closed set
    MalformedNumber,  ///< strict decimal grammar violated
    UngroundedValue,  ///< G3: value is not derivable from anything the user said
    OutOfRange,       ///< G5: product-scope bound violated
    Unclassified,     ///< verifier gap -> Indeterminate
};

/** GP-ARA's LogicalConstraintType for this domain: not an SMT-LIB formula
 * (see the file banner), just the outcome of evaluating every closed-form
 * rule against one `MortgageParamsInput`. */
export struct VerificationFacts {
    bool violated = false;   ///< a definite contradiction -> Unsafe
    bool incomplete = false; ///< a rule could not be evaluated -> Indeterminate
    ReasonCode reason = ReasonCode::None;
    std::string detail;
};

export enum class Outcome { Proven, Unsafe, Indeterminate };

export struct VerificationVerdict {
    Outcome outcome = Outcome::Indeterminate; ///< fail-closed default construction
    ReasonCode reason = ReasonCode::None;
    std::string message;
};

// ===========================================================================
// 5. THE DOMAIN -- G1, G2 and G5.
// ===========================================================================

/**
 * GP-ARA `DomainPolicy` over the emitted params ALONE.
 *
 * `translate()` is total and explicit: every branch returns either a
 * definite violation, a definite pass, or an explicit "incomplete". There is
 * no branch that falls through to "assume fine", and in particular an
 * unclassifiable field is reported incomplete rather than skipped.
 *
 * It is deliberately BLIND to the utterance. That is not a limitation to be
 * fixed later -- it is what makes this half of the verification cheap,
 * testable in isolation, and reusable by the offline dataset gate, which has
 * labels but is scoring the label space rather than any one user's words.
 * The utterance-dependent obligation is G3, immediately below this class.
 */
export class MortgageParamsDomain {
  public:
    using InputDataType = MortgageParamsInput;
    using LogicalConstraintType = VerificationFacts;

    [[nodiscard]] auto translate(const MortgageParamsInput& in) const -> VerificationFacts;
};

/**
 * GP-ARA `ReasonerPolicy` for `MortgageParamsDomain`, discharging each
 * `VerificationFacts` by direct evaluation rather than an SMT solver -- see
 * the file banner. Every path is default-deny: the only way `prove_safety`
 * returns `true` is the explicit `!violated && !incomplete` fallthrough.
 */
export class RuleBasedReasoner {
  public:
    struct Context {
        std::uint64_t queries_processed = 0;
    };
    using ContextType = Context;

    [[nodiscard]] auto prove_safety(Context& ctx, const VerificationFacts& formula)
        -> std::expected<bool, sensen::gp_ara::ReasonerError> {
        ++ctx.queries_processed;
        if (formula.incomplete) {
            return std::unexpected(sensen::gp_ara::ReasonerError{
                .code = sensen::gp_ara::ReasonerErrorCode::Indeterminate, .message = formula.detail});
        }
        if (formula.violated) return false;
        return true;
    }

    /**
     * Implemented so `RuleBasedReasoner` satisfies `ReasonerPolicy` in full
     * (the concept requires both methods). This domain has no goal state
     * distinct from its constraint -- proving the constraint safe IS the
     * goal -- so nothing in this file calls it, but it stays default-deny
     * for any future caller: both sides must independently pass, and an
     * Indeterminate on either propagates rather than being discarded.
     */
    [[nodiscard]] auto prove_goal(Context& ctx, const VerificationFacts& constraint,
                                  const VerificationFacts& goal)
        -> std::expected<bool, sensen::gp_ara::ReasonerError> {
        auto c = prove_safety(ctx, constraint);
        if (!c.has_value() || !*c) return c;
        return prove_safety(ctx, goal);
    }
};

static_assert(sensen::gp_ara::DomainPolicy<MortgageParamsDomain>,
              "MortgageParamsDomain must satisfy sensen::gp_ara::DomainPolicy");
static_assert(sensen::gp_ara::ReasonerPolicy<RuleBasedReasoner, MortgageParamsDomain>,
              "RuleBasedReasoner must satisfy sensen::gp_ara::ReasonerPolicy<MortgageParamsDomain>");

/**
 * G1 + G2 + G5. Runs `MortgageParamsDomain::translate()` through
 * `RuleBasedReasoner::prove_safety()` and returns a verdict the caller
 * cannot mistake for a boolean.
 *
 * A `Proven` here means ONLY "structurally sound": a real operation, exactly
 * its declared fields, well-formed decimals inside product-scope bounds. It
 * says nothing whatsoever about whether the numbers came from the user --
 * that is `ground_emitted_values`, and `verify_mortgage_output` is what runs
 * both. A caller that dispatches on this function's Proven alone has
 * reproduced precisely the blind spot documented in the file banner.
 */
export [[nodiscard]] auto verify_mortgage_params(const MortgageParamsInput& input)
    -> VerificationVerdict;

// ===========================================================================
// 6. G3 -- VALUE GROUNDING.
//
// THE OBLIGATION, stated once, precisely:
//
//   For every numeric field the model emitted, the emitted value must be a
//   member of the candidate set produced by applying the ADMISSIBLE MAPS
//   below to some numeric literal that appears in the user's own text.
//
// The model's job is TRANSCRIPTION, never computation -- that is the design
// rule the training set is built on (`build_mortgage_dataset.py` derives
// every label from a value the template also puts into the user turn), and
// this is its serving-time contrapositive. A number the user never said and
// that no admissible map produces from something they did say is, by
// definition, a number the model invented.
//
// ---------------------------------------------------------------------------
// THE ADMISSIBLE MAPS -- the complete list, deliberately short
//
// A literal `v` lexed from the text, with an adjacency TAG, expands to
// candidates for a slot of kind K as follows and in no other way:
//
//  M1 identity          v                    every kind, when the tag is
//                                            compatible with K (see the
//                                            tag/kind matrix below)
//  M2 percent->decimal  v / 100              K in {Rate, Ratio}, when the tag
//                                            is PERCENT, or the tag is
//                                            UNTAGGED and 1 <= v < 30 (the
//                                            whole-number-percent signature)
//  M3 annual->per-period
//                       v / n, (v/100) / n   K == Rate AND the field is named
//                       for n in              exactly `rate` or `guess`
//                       {1,2,4,12,26,52}
//  M4 magnitude         v x 1e3, v x 1e6     K == Money, when the literal
//                                            carries a k/thousand or
//                                            m/million suffix. When a suffix
//                                            is present the UNSCALED value is
//                                            NOT a candidate -- "300k" means
//                                            300000, and 300 for a money slot
//                                            is the magnitude slip this gate
//                                            exists to catch.
//  M5 years->months     v x 12               K in {MonthCount}, tag in
//                                            {YEARS, UNTAGGED}
//  M6 months->years     v / 12, exact only   K == YearCount, tag == MONTHS.
//                                            361 months does not become a
//                                            year count; 360 does.
//  M7 years->days       v x 365              K == DayOffsets, tag == YEARS
//  M8 negation          -v                   ONLY the repeated field named
//                                            `values`, whose contract is a
//                                            cash-flow series in which an
//                                            outlay is negative ("I invest
//                                            $60,000 today" -> -60000)
//
// Thousands separators and currency symbols are NOT maps: `$1,356,200` is
// LEXED as the literal 1356200, so `1356200.00` matches it under M1. That is
// the deliberate reading of the brief's "must not be refused" example -- the
// separators are notation, not arithmetic.
//
// ---------------------------------------------------------------------------
// TAG/KIND ADMISSIBILITY -- which literals may feed which slots at all
//
//   Money         MONEY, UNTAGGED
//   Rate, Ratio   PERCENT, UNTAGGED
//   MonthCount    MONTHS, YEARS, UNTAGGED
//   YearCount     YEARS, MONTHS, UNTAGGED
//   PeriodIndex   MONTHS, YEARS, UNTAGGED
//   Frequency     UNTAGGED
//   DayOffsets    DAYS, YEARS, UNTAGGED
//   Dimensionless UNTAGGED
//
// A PERCENT literal can therefore never feed a money slot, which is the
// "20% down" -> `down_payment: 20` unit confusion, refused structurally
// rather than by a magnitude heuristic. A MONEY literal can never feed a
// term slot, so "$360,000" cannot become a 360-month term.
//
// ---------------------------------------------------------------------------
// TOLERANCE -- the one place a value may differ from its candidate, and why
//
// A candidate is computed EXACTLY (M2 and M3 are exact rational division at
// fifteen places). The emitted value is then accepted if it is a correct
// rounding of that exact candidate AT THE SLOT'S OWN SERIALIZATION
// PRECISION, and at no coarser precision:
//
//   Money                       2 places  -> tolerance 0.005
//   Rate, Ratio                 6 places  -> tolerance 0.0000005
//   every count/index/offset    exact     -> tolerance 0
//
// Those two precisions are not chosen by feel. They are the precisions the
// training labels are serialized at: `money_str` is `f"{v:.2f}"` and
// `rate_str` defaults to `f"{v:.6f}"` in `build_mortgage_dataset.py`. So the
// rule is "the model may round to the precision it was taught to write, and
// not one digit further".
//
// This is exactly what separates the two cases the brief puts side by side:
//
//   4.68% -> 0.0468      M2 gives 0.0468 exactly. PASSES.
//   6.5% -> 0.005417     M3 with n=12 gives 0.00541666..., whose correct
//                        6-place rounding is 0.005417, |diff| = 3.3e-7 <
//                        5e-7. PASSES -- and it must, because every
//                        per-period TVM label in the training set is
//                        produced this way (finance.proto: "per-period rate,
//                        decimal (0.004166... monthly)").
//   5378.63 -> 5379.00   M1 gives 5378.63 exactly; |diff| = 0.37, which is
//                        74x the money tolerance. REFUSED. Rounding a
//                        payment to the whole dollar is not a serialization
//                        precision this contract has, and the loan it prices
//                        is a different loan.
//
// M3 is the widest map here and is therefore the most tightly fenced: it
// applies ONLY to a field named exactly `rate` or `guess`, which
// finance.proto documents as per-period, and NEVER to a field whose own name
// says `annual_` (`annual_rate`, `current_annual_rate`, `new_annual_rate`,
// ...). So the per-period-versus-annual confusion the misuse spec calls the
// "12x error" is still refused wherever the slot's own name settles the
// question, while the slots that genuinely are per-period still verify.
//
// ---------------------------------------------------------------------------
// CONVENTION VALUES -- the escape hatch, and why it cannot be used to launder
//
// A few fields are emitted from a stated CONVENTION rather than from the
// text: `payments_per_year: 12` when nobody said "monthly", `future_value: 0`
// for a fully-amortizing loan, `monthly_overpayment: 0` when no overpayment
// was mentioned, `guess` as a solver seed, `pmi_drop_off_ltv: 0.80` as the
// statutory PMI threshold. Each is exempt from grounding ONLY at ONE EXACT
// VALUE, listed by (field name, value) in `kConventionValues`. `payments_per_year: 12`
// is exempt; `payments_per_year: 26` must be grounded in the text like any
// other number. The exemption therefore cannot carry an arbitrary value: it
// is a whitelist of constants, not a whitelist of fields.
//
// ---------------------------------------------------------------------------
// HONEST LIMITS -- what grounding does NOT prove
//
//   - SLOT ASSIGNMENT. "300k loan on a 400k house" grounds `loan_amount`
//     400000 and `property_value` 300000 exactly as well swapped. Both
//     literals are MONEY and both slots are Money; nothing here can tell
//     them apart. G5's bounds catch a swap only when it makes a ratio
//     absurd.
//   - COMPLETENESS OF MEANING. Grounding is existential per field. A stated
//     PMI the model dropped is caught by G2's missing-field rule only
//     because this contract requires the full key set; a stated figure put
//     into the wrong one of two same-kind slots is not caught at all.
//   - OPERATION CHOICE. `ComputeFutureValue` and `ComputePresentValue` can
//     be identically grounded. G1 proves the operation EXISTS; nothing here
//     proves it is the one the user meant. That is a training-and-holdout
//     question, and the brief's row 2 (`ComputeFutureValueDetailed` emitted
//     as `ComputeFutureValue`) is caught here only because the two request
//     messages have disjoint field sets, so G2 fires. Had they shared a
//     shape, this layer would have passed it, and saying so is more useful
//     than pretending otherwise.
//   - ENGLISH ONLY. The lexer reads English unit words. A non-English
//     utterance yields no tagged literals, so any emitted params fail
//     grounding -- fail-closed, which is the right direction, but the
//     refusal message will mislead.
//
// ---------------------------------------------------------------------------
// MEASURED: WHAT THIS GATE REFUSES IN THE CURRENT TRAINING SET, AND WHY THAT
// IS A GENERATOR FINDING RATHER THAN A TOLERANCE TO LOOSEN
//
// The misuse spec's WU-M2 acceptance criterion is that every extraction
// label in the generated training set grounds against its own utterance, and
// that a failure is "either a generator bug or a missing admissible map".
// Run against `agent/dataset/data_mortgage/train.jsonl` (11,400 rows) on
// 2026-08-05, four families do not ground, and every one of them is the
// first case -- a label the generator produces that its own user turn never
// states. Recorded with counts so nobody has to re-derive them, and so that
// nobody "fixes" this gate by widening a map to admit a number that was
// genuinely invented:
//
//   642 rows  `pmi_annual_rate` emitted non-zero (e.g. 0.0112) from a user
//             turn that says only "so PMI applies" and states NO PMI rate.
//             `make_amortization_extraction` samples the rate and never puts
//             it in the text. This teaches the model to hallucinate a PMI
//             rate, which is precisely the defect class this file exists to
//             refuse -- so refusing it is correct, and the fix belongs in
//             the generator's template, not here.
//   448 rows  `ComputeRefinance.current_remaining_months` against a user
//             turn that rounds the term to one decimal place ("20.8 years
//             left" for 250 months). 20.8 x 12 = 249.6, and a term slot is
//             exact by design, so 250 does not ground. The generator is
//             discarding information the label needs.
//   224 rows  `ComputeXnpv`/`ComputeXirr` `dates`, a cumulative day grid the
//             generator derives at 30.44 days per month from text that says
//             "after 14 months". No admissible map reproduces it, and adding
//             one would mean teaching this file an average-month constant
//             the user never wrote.
//    39 rows  `ComputeDepreciation.recovery_period`, emitted as int(life),
//             so a stated "27.5-year life" becomes 27.
//
// ~11.5% of the corpus, all four fixable at the generator. Left as refusals
// on purpose: a value the user did not state and cannot be derived from what
// they did state is exactly the thing this gate is for, and the fact that
// the training set contains 1,314 examples teaching the model to produce
// them is an argument for the gate, not against it.
// ===========================================================================

/** How a numeric literal was written in the user's text -- the adjacency tag
 * that decides which slots it may feed at all. */
export enum class LiteralTag { Untagged, Money, Percent, Years, Months, Days };

/** One numeric literal lexed out of the user's utterance. `scale` is the
 * k/million multiplier the suffix carried (1, 1000 or 1000000); `value` is
 * the bare number BEFORE that multiplier, so M4 can be applied explicitly
 * rather than baked in where it cannot be reasoned about. */
export struct NumericLiteral {
    std::string text;
    Decimal value;
    LiteralTag tag = LiteralTag::Untagged;
    std::int64_t scale = 1;
    std::size_t offset = 0;
};

/**
 * Lexes every numeric literal out of `text`, with its adjacency tag.
 *
 * Handles the notation a person actually types: `$1,356,200`, `4.68%`,
 * `30-year`, `$300k`, `1.2 million`, `246 months`, `6.5 percent`. Thousands
 * separators and the currency symbol are consumed as NOTATION, so the
 * literal that comes out of `$1,356,200` is the number 1356200 and needs no
 * map to match `1356200.00`.
 *
 * Exported because it is independently testable and because the offline
 * dataset gate needs the same lexer the serving path uses -- two lexers is
 * exactly the drift `build_mortgage_dataset.py`'s own single-source rule
 * exists to prevent.
 */
export [[nodiscard]] auto lex_numeric_literals(std::string_view text) -> std::vector<NumericLiteral>;

/**
 * G3. Every numeric field of `input` must be traceable to `user_text`
 * through the admissible maps above.
 *
 * `user_text` is expected to be the user's utterance and, on a second turn,
 * the prior clarification exchange concatenated -- the same shape the
 * options module's utterance-taking functions accept, and for the same
 * reason: a number the user supplied in answer to a clarifying question is
 * as grounded as one they supplied first time.
 *
 * Returns `Unsafe` naming the first field that cannot be grounded (with its
 * value and the nearest literal in the text, so the refusal is actionable),
 * `Indeterminate` if a field's slot kind is unclassifiable, and `Proven`
 * only when every numeric field is grounded or is an exact convention value.
 *
 * Assumes -- does not re-check -- that `verify_mortgage_params` has already
 * returned Proven for the same input: it needs a real operation and real
 * field names to know what kind of quantity each value is.
 * `verify_mortgage_output` enforces that ordering.
 */
export [[nodiscard]] auto ground_emitted_values(const MortgageParamsInput& input,
                                                std::string_view user_text) -> VerificationVerdict;

// ===========================================================================
// 7. THE COMPOSED ENTRY POINT -- G4, then G1/G2/G5, then G3.
// ===========================================================================

/**
 * The single mandatory gate. A service calls THIS, not the pieces.
 *
 * Order is load-bearing:
 *   G4 first  -- an absent params block has no operation to look up and no
 *                fields to ground, and must never be repaired into one. It
 *                returns Indeterminate: there is nothing to contradict, and
 *                there is equally nothing that may be served. The service's
 *                correct responses are a refusal or a clarifying question --
 *                never a params block assembled from defaults, which is
 *                fabrication wearing a schema.
 *   G1/G2/G5  -- structure, which G3 depends on for slot kinds.
 *   G3 last   -- the utterance-dependent obligation.
 *
 * Nothing in this function returns `Proven` on an error path: the verdict
 * default-constructs to Indeterminate, so a future early return that forgot
 * to set `outcome` fails closed.
 */
export [[nodiscard]] auto verify_mortgage_output(const MortgageParamsInput& input,
                                                 std::string_view user_text) -> VerificationVerdict;

/** Human-readable name of a reason code, for logs and for test output. */
export [[nodiscard]] auto to_string(ReasonCode code) -> std::string_view;

/** Human-readable name of an outcome, for logs and for test output. */
export [[nodiscard]] auto to_string(Outcome outcome) -> std::string_view;

}  // namespace mortgage_calculator::assistant::verify

// ###########################################################################
// IMPLEMENTATION
// ###########################################################################

namespace mortgage_calculator::assistant::verify {

namespace detail {

[[nodiscard]] constexpr auto to_lower_char(char c) noexcept -> char {
    return (c >= 'A' && c <= 'Z') ? static_cast<char>(c - 'A' + 'a') : c;
}

[[nodiscard]] inline auto to_lower_copy(std::string_view s) -> std::string {
    std::string out(s.size(), '\0');
    for (std::size_t i = 0; i < s.size(); ++i) out[i] = to_lower_char(s[i]);
    return out;
}

[[nodiscard]] constexpr auto is_digit(char c) noexcept -> bool { return c >= '0' && c <= '9'; }
[[nodiscard]] constexpr auto is_alpha(char c) noexcept -> bool {
    return (c >= 'a' && c <= 'z') || (c >= 'A' && c <= 'Z');
}
[[nodiscard]] constexpr auto is_alnum(char c) noexcept -> bool { return is_digit(c) || is_alpha(c); }

[[nodiscard]] inline auto ends_with(std::string_view s, std::string_view suffix) noexcept -> bool {
    return s.size() >= suffix.size() && s.substr(s.size() - suffix.size()) == suffix;
}

template <std::size_t N>
[[nodiscard]] constexpr auto is_one_of(const std::array<std::string_view, N>& set,
                                       std::string_view needle) noexcept -> bool {
    for (const auto entry : set) {
        if (entry == needle) return true;
    }
    return false;
}

// --- the ordered slot-kind rules; see section 3's banner ------------------

constexpr std::array<std::string_view, 4> kEnumFields{"timing", "component", "method",
                                                      "closing_cost_type"};
constexpr std::array<std::string_view, 1> kBoolFields{"discounted"};
constexpr std::array<std::string_view, 2> kDayFields{"dates", "prepaid_interest_days"};
constexpr std::array<std::string_view, 1> kDimensionlessFields{"factor"};
constexpr std::array<std::string_view, 3> kFrequencyFields{"payments_per_year", "periods_per_year",
                                                           "compound_frequency"};
constexpr std::array<std::string_view, 4> kPeriodIndexFields{"period", "start_period", "end_period",
                                                             "year"};
constexpr std::array<std::string_view, 2> kMonthCountFields{"periods", "term_months"};
constexpr std::array<std::string_view, 3> kYearCountFields{"years", "life", "recovery_period"};
/** The two field names finance.proto documents as PER-PERIOD rates ("rate --
 * per-period rate, decimal (0.004166... monthly)" on PaymentRequest, and
 * RateRequest's `guess`, which seeds a solve for that same per-period rate).
 * These are the ONLY slots map M3 may fire on. */
constexpr std::array<std::string_view, 2> kPerPeriodRateFields{"rate", "guess"};

/** Fields exempt from grounding, at ONE exact value each -- see the section
 * 6 banner. Compared NUMERICALLY, so "0", "0.00" and "0.000000" are the same
 * exemption and "0.01" is not any of them. */
struct ConventionValue {
    std::string_view field;
    std::string_view value;
};
constexpr std::array<ConventionValue, 37> kConventionValues{{
    // ComputeRentVsBuy / ComputeHomeNpv: the optional inputs added with the
    // amortising model. An utterance that never mentions closing costs, selling
    // costs or inflation grounds none of them, and without an exemption the
    // whole parse is refused rather than read as "the caller did not say" --
    // the same failure the closing-cost fields hit when sixteen slots all had
    // to be filled.
    // EVERY new field needs one. translate() requires each declared field to be
    // emitted, so a model trained on the eight legacy fields is refused on the
    // first of the seven it has never heard of -- exactly the failure the
    // sixteen closing-cost fields hit. These exemptions are what let the
    // DEPLOYED model keep parsing rent-vs-buy unchanged, with no retrain: it
    // omits all seven, they default, and the service takes the legacy path.
    // The legacy composite is exempt at 0 for the mirror-image reason: a
    // full-shape request states the P&I split instead and has nothing to put
    // here, yet G2 still demands the field.
    {.field = "monthly_piti_and_maintenance", .value = "0"},
    {.field = "loan_annual_rate", .value = "0"},
    {.field = "loan_term_years", .value = "0"},
    {.field = "loan_amount", .value = "0"},
    {.field = "monthly_taxes_ins_maintenance", .value = "0"},
    {.field = "closing_costs_buy", .value = "0"},
    {.field = "selling_cost_percent", .value = "0.06"},
    {.field = "selling_cost_percent", .value = "0"},
    {.field = "annual_inflation_rate", .value = "0"},
    // ComputeClosingCosts: the lines a closing may genuinely not have.
    // Sixteen fields must all be emitted, so without these a request
    // that never mentions an inspection is refused rather than read as
    // "no inspection fee". Same trade as the payment/future_value zeros
    // below: it also stops catching a STATED figure dropped to zero.
    {.field = "origination_fee_percent", .value = "0"},
    {.field = "discount_points_percent", .value = "0"},
    {.field = "other_lender_fees", .value = "0"},
    {.field = "appraisal_fee", .value = "0"},
    {.field = "inspection_fee", .value = "0"},
    {.field = "recording_fees", .value = "0"},
    {.field = "transfer_tax_percent", .value = "0"},
    {.field = "homeowners_insurance_annual", .value = "0"},
    {.field = "property_tax_annual", .value = "0"},
    {.field = "seller_lender_credits", .value = "0"},
    // The day count a caller never names. 15 is the half-month convention the
    // proto documents as the ABSENT behaviour, so a request that says nothing
    // about prepaid interest must be able to emit it; 0 is the deliberate
    // "closing on the last day of the month, none owed".
    {.field = "prepaid_interest_days", .value = "15"},
    {.field = "prepaid_interest_days", .value = "0"},

    // The monthly cadence, when the user said nothing about frequency.
    {.field = "payments_per_year", .value = "12"},
    {.field = "periods_per_year", .value = "12"},
    {.field = "compound_frequency", .value = "12"},
    // A fully-amortizing loan has no balloon; the training set states this
    // convention rather than sampling it.
    {.field = "future_value", .value = "0"},
    // The mirror of that on the other side of the annuity: a LUMP-SUM present
    // or future value question states no periodic contribution, and the model
    // fills the slot with 0. Without this, "the future value of 1000 at 5% for
    // 10 years" -- the most ordinary TVM question there is -- is refused on
    // `payment` even once `periods` grounds correctly. Same trade the zeros
    // above already accept: an unconditional exemption cannot also catch a user
    // who DID state a payment the model then dropped to zero.
    {.field = "payment", .value = "0"},
    // "not stated -> base case" zeros.
    {.field = "monthly_overpayment", .value = "0"},
    {.field = "extra_monthly_payment", .value = "0"},
    {.field = "pmi_annual_rate", .value = "0"},
    {.field = "cash_out_amount", .value = "0"},
    {.field = "current_pmi_monthly", .value = "0"},
    {.field = "new_pmi_monthly", .value = "0"},
    {.field = "annual_inflation_rate", .value = "0"},
    // Solver seeds, not user quantities.
    {.field = "guess", .value = "0.005"},
    {.field = "guess", .value = "0.1"},
    // The statutory PMI drop-off threshold and the declining-balance factor.
    {.field = "pmi_drop_off_ltv", .value = "0.80"},
    {.field = "factor", .value = "2.0"},
}};

// --- product-scope bounds (G5); see the file banner on non-duplication ----

/** Ten billion. No residential mortgage, HELOC draw, rental or home-NPV
 * figure approaches it; a larger number is a transcription defect or an
 * overflow probe. Tighter than `check_decimal_string_magnitude`'s 15
 * integer digits ON PURPOSE -- that bound protects BigDecimal's range for
 * every caller of the general-purpose library, this one states what a
 * mortgage assistant is for. */
constexpr __int128 kMaxMoneyUnits = static_cast<__int128>(10'000'000'000LL) * Decimal::kScale;
/** 30%. Above this a "rate" is a magnitude slip, not a mortgage. */
constexpr __int128 kMaxRateUnits = (static_cast<__int128>(30) * Decimal::kScale) / 100;
/** 150%. LTV caps and cost percentages legitimately exceed 100% of nothing
 * useful, but a 1.5 ceiling still refuses a percent-as-whole-number slip. */
constexpr __int128 kMaxRatioUnits = (static_cast<__int128>(150) * Decimal::kScale) / 100;

/** Ratio fields the ENGINE refuses above 1.0, listed so the verifier refuses
 * them too.
 *
 * The global ceiling above is 1.5 because an LTV legitimately exceeds 1.0 on an
 * underwater loan. These are shares of a price or a loan, where above 1.0 is
 * always a mistake -- and `sensen::validate_closing_costs` says so, returning
 * INVALID_ARGUMENT.
 *
 * Listed here because a verifier LOOSER than the engine it guards is not a
 * safety property: it proves a parse admissible and the engine then refuses it,
 * so the caller gets a transport error instead of the honest "I could not
 * ground that" this layer exists to produce.
 *
 * Keep in step with `sensen::validate_closing_costs`. The two sides are
 * asserted SEPARATELY, because this module is deliberately free of sensen,
 * proto and gRPC and so cannot call the engine to compare: the verifier half
 * is in test_mortgage_verification.cpp ("a share above 1.0 is refused"), the
 * engine half in test_finance_service_validation.cpp section 23 ("a fee share
 * above 100%"). Both must move together; neither test can notice on its own
 * that the other side changed. */
constexpr std::array<std::string_view, 5> kUnitCappedRatioFields{
    "down_payment_percent", "origination_fee_percent", "discount_points_percent",
    "title_settlement_percent", "transfer_tax_percent",
};
constexpr __int128 kMaxUnitRatioUnits = Decimal::kScale;
/** 1200 months / 100 years -- the horizon the misuse spec fixes for this
 * product, and the same figure `finance_service.cpp`'s own guard header
 * cites for its compounding check. */
constexpr __int128 kMaxMonthUnits = static_cast<__int128>(1200) * Decimal::kScale;
constexpr __int128 kMaxYearUnits = static_cast<__int128>(100) * Decimal::kScale;
/** A cash-flow day grid: 100 years out, in days. */
constexpr __int128 kMaxDayUnits = static_cast<__int128>(36'525) * Decimal::kScale;

}  // namespace detail

// ---------------------------------------------------------------------------
// Label-space queries.
// ---------------------------------------------------------------------------

auto operation_ids() -> std::vector<std::string_view> {
    return {detail::kOperationIds.begin(), detail::kOperationIds.end()};
}

auto is_known_operation(std::string_view operation) -> bool {
    for (const auto id : detail::kOperationIds) {
        if (id == operation) return true;
    }
    return false;
}

auto fields_of(std::string_view operation) -> std::vector<FieldSpec> {
    std::vector<FieldSpec> out;
    for (const auto& spec : detail::kLabelSpace) {
        if (spec.operation == operation) out.push_back(spec);
    }
    return out;
}

auto find_field(std::string_view operation, std::string_view field) -> const FieldSpec* {
    for (const auto& spec : detail::kLabelSpace) {
        if (spec.operation == operation && spec.field == field) return &spec;
    }
    return nullptr;
}

// ---------------------------------------------------------------------------
// Decimal.
// ---------------------------------------------------------------------------

auto Decimal::to_string() const -> std::string {
    const bool negative = units_ < 0;
    __int128 magnitude = negative ? -units_ : units_;
    const __int128 whole = magnitude / kScale;
    __int128 frac = magnitude % kScale;

    std::string whole_text;
    if (whole == 0) {
        whole_text = "0";
    } else {
        __int128 w = whole;
        while (w > 0) {
            whole_text.insert(whole_text.begin(), static_cast<char>('0' + static_cast<int>(w % 10)));
            w /= 10;
        }
    }

    std::string frac_text(kPlaces, '0');
    for (int i = kPlaces - 1; i >= 0; --i) {
        frac_text[static_cast<std::size_t>(i)] = static_cast<char>('0' + static_cast<int>(frac % 10));
        frac /= 10;
    }
    while (!frac_text.empty() && frac_text.back() == '0') frac_text.pop_back();

    std::string out;
    if (negative) out += '-';
    out += whole_text;
    if (!frac_text.empty()) {
        out += '.';
        out += frac_text;
    }
    return out;
}

auto parse_strict_decimal(std::string_view text) -> std::optional<Decimal> {
    // Grammar: -?[0-9]{1,15}(\.[0-9]{1,18})?  -- total length <= 34, nothing
    // else. Everything rejected here is rejected by CONSTRUCTION rather than
    // by a blocklist: "NaN", "1e309", "0x1p4", "+1", " 1", "1,000" and ""
    // all fail because none of them is that grammar.
    constexpr std::size_t kMaxTotalLength = 34;
    constexpr std::size_t kMaxIntegerDigits = 15;
    constexpr std::size_t kMaxFractionDigits = 18;

    if (text.empty() || text.size() > kMaxTotalLength) return std::nullopt;

    std::size_t i = 0;
    bool negative = false;
    if (text[i] == '-') {
        negative = true;
        ++i;
    }

    const std::size_t int_start = i;
    while (i < text.size() && detail::is_digit(text[i])) ++i;
    const std::size_t int_digits = i - int_start;
    if (int_digits == 0 || int_digits > kMaxIntegerDigits) return std::nullopt;

    std::string_view frac;
    if (i < text.size()) {
        if (text[i] != '.') return std::nullopt;
        ++i;
        const std::size_t frac_start = i;
        while (i < text.size() && detail::is_digit(text[i])) ++i;
        const std::size_t frac_digits = i - frac_start;
        if (frac_digits == 0 || frac_digits > kMaxFractionDigits) return std::nullopt;
        frac = text.substr(frac_start, frac_digits);
    }
    if (i != text.size()) return std::nullopt;

    __int128 units = 0;
    for (std::size_t d = int_start; d < int_start + int_digits; ++d) {
        units = units * 10 + (text[d] - '0');
    }
    units *= Decimal::kScale;

    // Fraction digits beyond `kPlaces` are truncated. They cannot change a
    // verdict: the tightest tolerance in this file is 5e-7, eight orders
    // coarser than the 1e-15 this discards.
    __int128 frac_units = 0;
    __int128 place = Decimal::kScale;
    for (std::size_t d = 0; d < frac.size() && d < static_cast<std::size_t>(Decimal::kPlaces); ++d) {
        place /= 10;
        frac_units += static_cast<__int128>(frac[d] - '0') * place;
    }
    units += frac_units;

    return Decimal{negative ? -units : units};
}

// ---------------------------------------------------------------------------
// Slot classification.
// ---------------------------------------------------------------------------

auto classify_slot(std::string_view f) -> SlotKind {
    // Ordered, first match wins. Exact names and suffixes only -- never a
    // bare substring, which would misclassify `annual_rent_increase` on
    // "rent". The order matters in exactly two places, both noted inline.
    if (detail::is_one_of(detail::kEnumFields, f)) return SlotKind::Enumeration;
    if (detail::is_one_of(detail::kBoolFields, f)) return SlotKind::Boolean;
    if (detail::is_one_of(detail::kDayFields, f)) return SlotKind::DayOffsets;
    if (detail::is_one_of(detail::kDimensionlessFields, f)) return SlotKind::Dimensionless;

    // Before the year rules: `payments_per_year` ends in "_year", not
    // "_years", so it would not collide -- but stating the order here means
    // a future `*_years` frequency field cannot silently become a term.
    if (detail::is_one_of(detail::kFrequencyFields, f)) return SlotKind::Frequency;

    // Before the month/year rules: `period`, `start_period`, `end_period`
    // and `year` are ORDINALS, not durations. Exact names only, so
    // `recovery_period` and `holding_period_years` fall through to the
    // duration rules below where they belong.
    if (detail::is_one_of(detail::kPeriodIndexFields, f)) return SlotKind::PeriodIndex;

    if (detail::is_one_of(detail::kMonthCountFields, f) || detail::ends_with(f, "_months")) {
        return SlotKind::MonthCount;
    }
    if (detail::is_one_of(detail::kYearCountFields, f) || detail::ends_with(f, "_years")) {
        return SlotKind::YearCount;
    }

    // Ratios BEFORE rates: `max_ltv_rate` ends in "rate" but is a
    // loan-to-value proportion, and `pmi_drop_off_ltv` (0.80) would fail the
    // 30% rate band if it were classified as a rate.
    if (f.find("ltv") != std::string_view::npos || detail::ends_with(f, "_percent")) {
        return SlotKind::Ratio;
    }
    if (detail::ends_with(f, "rate") || detail::ends_with(f, "rates") ||
        detail::ends_with(f, "appreciation") || detail::ends_with(f, "_increase") ||
        detail::ends_with(f, "_return") || f == "guess") {
        return SlotKind::Rate;
    }

    // Everything the label space declares that is not one of the above is an
    // amount of money. This is the ONLY catch-all in this function, and it
    // is deliberately last; a name the rules above do not reach and that is
    // not a money field would be an unclassified slot, which the test's
    // totality assertion is what actually rules out.
    static constexpr std::array<std::string_view, 48> kMoneyFields{
        "monthly_taxes_ins_maintenance",
        // ComputeClosingCosts. Absent, these classify Unclassified ->
        // Indeterminate and every closing-cost parse is refused.
        "appraisal_fee", "home_price", "homeowners_insurance_annual", "inspection_fee", "other_lender_fees", "property_tax_annual", "recording_fees", "seller_lender_credits",
        "annual_contribution",          "cash_out_amount",
        "closing_costs",                "closing_costs_buy",
        "cost",                         "current_loan_balance",
        "current_monthly_payment",      "current_monthly_rent",
        "current_mortgage_balance",     "current_pmi_monthly",
        "current_principal",            "current_property_value",
        "down_payment",                 "drawn_amount",
        "extra_monthly_payment",        "extra_payments",
        "future_value",                 "home_value",
        "home_values",                  "loan_amount",
        "loan_amounts",                 "lump_sum_payment",
        "monthly_maintenance",          "monthly_overpayment",
        "monthly_piti_and_maintenance", "monthly_rent_saved",
        "monthly_taxes_ins_hoa",        "new_pmi_monthly",
        "original_home_value",          "payment",
        "periodic_gross_rent",          "periodic_mortgage_payment",
        "periodic_operating_expenses",  "present_value",
        "property_price",               "property_value",
        "salvage",                      "total_cash_invested",
        "values"};
    if (detail::is_one_of(kMoneyFields, f)) return SlotKind::Money;

    return SlotKind::Unclassified;
}

namespace detail {

/** Tolerance for a slot kind, in `Decimal` units -- see the section 6
 * banner for where the two non-zero figures come from. */
[[nodiscard]] constexpr auto slot_tolerance_units(SlotKind kind) noexcept -> __int128 {
    switch (kind) {
        case SlotKind::Money:
            return Decimal::kScale / 200;  // 0.005, half of the 2-place unit
        case SlotKind::Rate:
        case SlotKind::Ratio:
            return Decimal::kScale / 2'000'000;  // 5e-7, half of the 6-place unit
        default:
            return 0;  // counts, indices and day offsets are exact
    }
}

[[nodiscard]] constexpr auto is_numeric_kind(SlotKind kind) noexcept -> bool {
    return kind != SlotKind::Enumeration && kind != SlotKind::Boolean &&
           kind != SlotKind::Unclassified;
}

[[nodiscard]] inline auto tag_admissible(SlotKind kind, LiteralTag tag) noexcept -> bool {
    switch (kind) {
        case SlotKind::Money:
            return tag == LiteralTag::Money || tag == LiteralTag::Untagged;
        case SlotKind::Rate:
        case SlotKind::Ratio:
            return tag == LiteralTag::Percent || tag == LiteralTag::Untagged;
        case SlotKind::MonthCount:
        case SlotKind::PeriodIndex:
            return tag == LiteralTag::Months || tag == LiteralTag::Years ||
                   tag == LiteralTag::Untagged;
        case SlotKind::YearCount:
            return tag == LiteralTag::Years || tag == LiteralTag::Months ||
                   tag == LiteralTag::Untagged;
        case SlotKind::Frequency:
        case SlotKind::Dimensionless:
            return tag == LiteralTag::Untagged;
        case SlotKind::DayOffsets:
            return tag == LiteralTag::Days || tag == LiteralTag::Years ||
                   tag == LiteralTag::Untagged;
        default:
            return false;
    }
}

/** The candidate set for one literal against one slot -- the complete
 * expansion of maps M1..M8, and nothing outside them. */
[[nodiscard]] inline auto expand_candidates(const NumericLiteral& lit, SlotKind kind,
                                            std::string_view field_name,
                                            std::int64_t periods_per_year = 12)
    -> std::vector<Decimal> {
    std::vector<Decimal> out;
    if (!tag_admissible(kind, lit.tag)) return out;

    const auto push = [&out](std::optional<Decimal> d) {
        if (d.has_value()) out.push_back(*d);
    };

    // M4 first, because when a magnitude suffix is present it REPLACES the
    // identity candidate for a money slot rather than adding to it: "300k"
    // means 300000, and admitting 300 as well would pass the very
    // magnitude slip this gate exists to catch.
    if (kind == SlotKind::Money && lit.scale != 1) {
        push(lit.value.scaled_by(lit.scale));
        return out;
    }

    switch (kind) {
        case SlotKind::Money:
            push(lit.value);  // M1
            break;

        case SlotKind::Rate:
        case SlotKind::Ratio: {
            const bool percent_signature =
                lit.tag == LiteralTag::Percent ||
                (lit.tag == LiteralTag::Untagged &&
                 lit.value.units() >= Decimal::kScale &&
                 lit.value.units() < static_cast<__int128>(30) * Decimal::kScale);

            // M1 -- a rate already written as a fraction ("0.0468"). Not
            // admissible when the literal is explicitly PERCENT-tagged:
            // "4.68%" is four and two-thirds percent, never 468%.
            if (lit.tag != LiteralTag::Percent) push(lit.value);
            // M2
            if (percent_signature) push(lit.value.divided_by(100));

            // M3 -- annual to per-period, only for the two field names
            // finance.proto documents as per-period.
            if (kind == SlotKind::Rate && is_one_of(kPerPeriodRateFields, field_name)) {
                constexpr std::array<std::int64_t, 6> kCadences{1, 2, 4, 12, 26, 52};
                for (const auto n : kCadences) {
                    if (lit.tag != LiteralTag::Percent) push(lit.value.divided_by(n));
                    if (percent_signature) {
                        if (auto as_fraction = lit.value.divided_by(100); as_fraction.has_value()) {
                            push(as_fraction->divided_by(n));
                        }
                    }
                }
            }
            break;
        }

        case SlotKind::MonthCount:
            // M5 -- a duration in years becomes a COUNT OF PERIODS, and the
            // period is whatever the rate's period is. `periods_per_year` is
            // inferred from the emitted rate (see infer_periods_per_year); it
            // is 12 when the rate was divided by 12, and 1 when the rate is
            // annual, so one rule covers both.
            //
            // This used to be a hardcoded x12. That is right for a mortgage
            // and wrong for the rest of the TVM surface, which shares the same
            // generic `rate` + `periods` pair: "future value of 1000 at 5% for
            // 10 years" with an ANNUAL rate has ten periods, and the fixed x12
            // admitted only 120, so a correct parse was refused with the
            // self-contradicting "10 does not correspond to anything in the
            // request (the nearest figure you gave is 10)".
            //
            // Note what did NOT change: the identity candidate is still
            // suppressed for a Years-tagged literal. With a monthly rate,
            // "30 years" still grounds ONLY to 360, so `periods = 30` against
            // a monthly rate -- a thirty-MONTH loan answered as thirty years --
            // is still refused. Inferring the cadence tightens the gate as
            // well as loosening it: an ANNUAL rate paired with `periods = 360`
            // now grounds against 30 and is refused too, which the fixed x12
            // accepted.
            if (lit.tag != LiteralTag::Years) push(lit.value);  // M1
            if (lit.tag == LiteralTag::Years || lit.tag == LiteralTag::Untagged) {
                push(lit.value.scaled_by(periods_per_year));  // M5
            }
            break;

        case SlotKind::YearCount:
            if (lit.tag != LiteralTag::Months) push(lit.value);  // M1
            if (lit.tag == LiteralTag::Months) push(lit.value.divided_exactly_by(12));  // M6
            break;

        case SlotKind::DayOffsets:
            if (lit.tag != LiteralTag::Years) push(lit.value);  // M1
            if (lit.tag == LiteralTag::Years) push(lit.value.scaled_by(365));  // M7
            break;

        case SlotKind::PeriodIndex:
        case SlotKind::Frequency:
        case SlotKind::Dimensionless:
            push(lit.value);  // M1 only
            break;

        default:
            break;
    }

    // M8 -- a cash-flow series encodes an outlay as a negative number, and
    // the user says "I invest $60,000", not "-60000". Scoped to the one
    // repeated field whose proto contract is that series.
    if (field_name == "values") {
        const std::size_t n = out.size();
        for (std::size_t i = 0; i < n; ++i) out.push_back(out[i].negated());
    }
    return out;
}

/**
 * Which cadence the model actually used, read off the rate it emitted.
 *
 * `finance.proto` documents `rate` as a PER-PERIOD rate, and `periods` as a
 * count of those same periods. Nothing in the field names says which period --
 * the pair is only meaningful together, and until this existed the two were
 * grounded independently: the rate grounded against any cadence in M3, and
 * `periods` grounded against months by convention. A pair that disagreed
 * therefore passed, and a pair that agreed annually was refused.
 *
 * Returns the number of periods per year (1 annual, 12 monthly, ...) when the
 * emitted rate matches exactly ONE cadence of some percentage in the user's
 * text, and `nullopt` otherwise -- ambiguous or unrecognised, in which case the
 * caller keeps the conservative monthly default and refuses rather than guesses.
 *
 * Deliberately reuses M3's cadence list, so the set of period lengths this can
 * infer is exactly the set the rate is allowed to be divided by. Adding one in
 * a single place would let the two disagree again.
 */
[[nodiscard]] inline auto infer_periods_per_year(const std::vector<EmittedField>& fields,
                                                 const std::vector<NumericLiteral>& literals)
    -> std::optional<std::int64_t> {
    constexpr std::array<std::int64_t, 6> kCadences{1, 2, 4, 12, 26, 52};
    const __int128 tolerance = slot_tolerance_units(SlotKind::Rate);

    for (const auto& f : fields) {
        if (!is_one_of(kPerPeriodRateFields, f.name)) continue;
        if (f.values.size() != 1) continue;
        const auto emitted_rate = parse_strict_decimal(f.values.front());
        if (!emitted_rate.has_value()) continue;

        std::optional<std::int64_t> found;
        for (const auto& lit : literals) {
            // The annual figure the user actually said, as a fraction. A
            // percent-tagged literal is always /100; an untagged one is only
            // treated as a percentage inside the same band M2 uses, so "0.06"
            // and "6%" both reach 0.06 and "300000" reaches nothing.
            std::vector<Decimal> annuals;
            const bool percent_signature =
                lit.tag == LiteralTag::Percent ||
                (lit.tag == LiteralTag::Untagged && lit.value.units() >= Decimal::kScale &&
                 lit.value.units() < static_cast<__int128>(30) * Decimal::kScale);
            if (percent_signature) {
                if (auto as_fraction = lit.value.divided_by(100); as_fraction.has_value()) {
                    annuals.push_back(*as_fraction);
                }
            }
            if (lit.tag != LiteralTag::Percent) annuals.push_back(lit.value);

            for (const auto& annual : annuals) {
                for (const auto n : kCadences) {
                    const auto per_period = annual.divided_by(n);
                    if (!per_period.has_value()) continue;
                    if (!emitted_rate->within(*per_period, tolerance)) continue;
                    // Ambiguous: a zero rate matches every cadence, and so
                    // would two literals that happen to line up. Refuse to
                    // infer rather than pick one.
                    if (found.has_value() && *found != n) return std::nullopt;
                    found = n;
                }
            }
        }
        if (found.has_value()) return found;
    }
    return std::nullopt;
}

[[nodiscard]] inline auto is_convention_value(std::string_view field, const Decimal& value) -> bool {
    for (const auto& c : kConventionValues) {
        if (c.field != field) continue;
        const auto parsed = parse_strict_decimal(c.value);
        if (parsed.has_value() && *parsed == value) return true;
    }
    return false;
}

}  // namespace detail

// ---------------------------------------------------------------------------
// The lexer.
// ---------------------------------------------------------------------------

namespace detail {

/** Reads the word immediately following `pos` (skipping spaces and a single
 * hyphen, so "30-year" and "30 years" both find "year"), lower-cased. */
[[nodiscard]] inline auto next_word(std::string_view text, std::size_t pos) -> std::string {
    std::size_t i = pos;
    bool hyphen_used = false;
    while (i < text.size() && (text[i] == ' ' || (text[i] == '-' && !hyphen_used))) {
        if (text[i] == '-') hyphen_used = true;
        ++i;
    }
    const std::size_t start = i;
    while (i < text.size() && is_alpha(text[i])) ++i;
    return to_lower_copy(text.substr(start, i - start));
}

}  // namespace detail

auto lex_numeric_literals(std::string_view text) -> std::vector<NumericLiteral> {
    std::vector<NumericLiteral> out;

    for (std::size_t i = 0; i < text.size();) {
        if (!detail::is_digit(text[i])) {
            ++i;
            continue;
        }
        const std::size_t start = i;

        // A '$' immediately before (allowing one space) tags the literal as
        // money regardless of what follows it -- "$1,200 a year" is twelve
        // hundred dollars, not twelve hundred years.
        bool currency_prefix = false;
        {
            std::size_t back = start;
            if (back > 0 && text[back - 1] == ' ') --back;
            if (back > 0 && text[back - 1] == '$') currency_prefix = true;
        }

        // Integer part, consuming thousands separators as NOTATION: a comma
        // counts only when it sits between a digit and exactly three more
        // digits, so "1,356,200" is one literal and "2, 3" is two.
        std::string digits;
        while (i < text.size()) {
            if (detail::is_digit(text[i])) {
                digits += text[i];
                ++i;
                continue;
            }
            if (text[i] == ',' && i + 3 < text.size() && detail::is_digit(text[i + 1]) &&
                detail::is_digit(text[i + 2]) && detail::is_digit(text[i + 3]) &&
                (i + 4 >= text.size() || !detail::is_digit(text[i + 4]))) {
                i += 1;
                continue;
            }
            break;
        }
        // Fractional part.
        if (i + 1 < text.size() && text[i] == '.' && detail::is_digit(text[i + 1])) {
            digits += '.';
            ++i;
            while (i < text.size() && detail::is_digit(text[i])) {
                digits += text[i];
                ++i;
            }
        }

        const auto value = parse_strict_decimal(digits);
        if (!value.has_value()) {
            // Longer than the grammar admits (a 20-digit run, say). Skipping
            // it means it grounds nothing, which is the fail-closed
            // direction.
            continue;
        }

        // A leading '-' belongs to the literal, exactly as '$' does above.
        //
        // Without this the lexer produced a MAGNITUDE and threw the sign away,
        // so "-$250,000" and "$250,000" were the same literal. A model that
        // silently dropped the minus -- which is what the deployed one does --
        // then emitted `loan_amount = 250000.00`, and the gate grounded it
        // against the digits it had lexed and returned Proven. The user asked
        // about one thing and was priced another, with every gate satisfied.
        //
        // That is a REPAIR, and §5.1 of the pipeline doc is explicit that
        // nothing is repaired. Carrying the sign makes the repair visible:
        // every map below now operates on a signed value, so a positive
        // emission no longer grounds against a negative literal. M8 is
        // unaffected -- it adds `-v` for the `values` field only, which for a
        // negative literal yields the positive back, which is the one place a
        // cash-flow sign legitimately flips.
        bool negative_prefix = false;
        {
            std::size_t back = start;
            if (back > 0 && text[back - 1] == ' ') --back;
            if (back > 0 && text[back - 1] == '$') --back;
            if (back > 0 && text[back - 1] == ' ') --back;
            if (back > 0 && text[back - 1] == '-') negative_prefix = true;
        }

        NumericLiteral lit;
        lit.text = digits;
        lit.value = negative_prefix ? value->negated() : *value;
        lit.offset = start;
        lit.tag = currency_prefix ? LiteralTag::Money : LiteralTag::Untagged;

        // Suffix, then unit word. A '%' bound directly to the number wins
        // over everything, including the currency prefix, because "$" and
        // "%" never legitimately co-occur on one literal.
        if (i < text.size() && text[i] == '%') {
            lit.tag = LiteralTag::Percent;
            ++i;
        } else if (i < text.size() && (text[i] == 'k' || text[i] == 'K') &&
                   (i + 1 >= text.size() || !detail::is_alnum(text[i + 1]))) {
            lit.tag = LiteralTag::Money;
            lit.scale = 1'000;
            ++i;
        } else if (i < text.size() && (text[i] == 'm' || text[i] == 'M') &&
                   (i + 1 >= text.size() || !detail::is_alnum(text[i + 1]))) {
            lit.tag = LiteralTag::Money;
            lit.scale = 1'000'000;
            ++i;
        } else {
            const std::string word = detail::next_word(text, i);
            if (word == "percent" || word == "pct") {
                lit.tag = LiteralTag::Percent;
            } else if (word == "k" || word == "thousand") {
                lit.tag = LiteralTag::Money;
                lit.scale = 1'000;
            } else if (word == "m" || word == "mm" || word == "million" || word == "millions") {
                lit.tag = LiteralTag::Money;
                lit.scale = 1'000'000;
            } else if (word == "dollars" || word == "dollar" || word == "usd") {
                lit.tag = LiteralTag::Money;
            } else if (!currency_prefix) {
                if (word == "year" || word == "years" || word == "yr" || word == "yrs") {
                    lit.tag = LiteralTag::Years;
                } else if (word == "month" || word == "months" || word == "mo" || word == "mos") {
                    lit.tag = LiteralTag::Months;
                } else if (word == "day" || word == "days") {
                    lit.tag = LiteralTag::Days;
                }
            }
        }

        out.push_back(std::move(lit));
    }

    return out;
}

// ---------------------------------------------------------------------------
// G1 + G2 + G5.
// ---------------------------------------------------------------------------

namespace detail {

/** G5 for one already-parsed value. Bounds only, and only the ones
 * `finance_service.cpp` does NOT already make -- see the file banner's
 * non-duplication note for the four it does. */
[[nodiscard]] inline auto bound_violation(SlotKind kind, std::string_view field,
                                          const Decimal& v) -> std::optional<std::string> {
    const auto too_big = [&](const __int128 ceiling, const char* what) -> std::optional<std::string> {
        if (v.units() > ceiling || v.units() < -ceiling) {
            return std::string{field} + " = " + v.to_string() + " is outside this assistant's " +
                   what + " range";
        }
        return std::nullopt;
    };

    switch (kind) {
        case SlotKind::Money:
            // A cash-flow series is the one money slot where a negative
            // number is the contract (an outlay), not an error.
            if (v.is_negative() && field != "values") {
                return std::string{field} + " = " + v.to_string() + " is negative";
            }
            return too_big(kMaxMoneyUnits, "money");
        case SlotKind::Rate:
            if (v.is_negative()) return std::string{field} + " = " + v.to_string() + " is negative";
            return too_big(kMaxRateUnits, "interest-rate");
        case SlotKind::Ratio:
            if (v.is_negative()) return std::string{field} + " = " + v.to_string() + " is negative";
            // Shares of a price or a loan cannot exceed the whole. Refused
            // HERE as well as in the engine so the verdict is a grounded
            // refusal rather than an INVALID_ARGUMENT from the far side.
            if (detail::is_one_of(detail::kUnitCappedRatioFields, field) &&
                v.units() > detail::kMaxUnitRatioUnits) {
                return std::string{field} + " = " + v.to_string() +
                       " exceeds 1.0, and it is a decimal fraction (0.0075 for 0.75%)";
            }
            return too_big(kMaxRatioUnits, "proportion");
        case SlotKind::MonthCount:
            // Zero months of TAX ESCROW is a real closing -- plenty of loans
            // collect no escrow reserve at all -- so it is carved out of the
            // positivity rule the same way `values` is carved out of Money's
            // negativity rule above. A zero TERM or zero `periods` remains an
            // error; this exemption is by field name, not by kind.
            //
            // It has to live HERE rather than in kConventionValues: bounds run
            // in translate() (G5), before ground_emitted_values (G3) ever
            // consults a convention, so a convention entry would never be
            // reached. The engine accepts 0..24 for this field, and a verifier
            // stricter than the RPC it guards refuses requests the service
            // would have answered.
            if (v.units() < 0 || (v.units() == 0 && field != "tax_escrow_months")) {
                return std::string{field} + " = " + v.to_string() + " is not positive";
            }
            return too_big(kMaxMonthUnits, "term");
        case SlotKind::YearCount:
            // `loan_term_years` is carved out of the positivity rule the same way
            // `tax_escrow_months` is carved out above, and for the same structural
            // reason spelled out there: bounds run in translate() (G5) BEFORE
            // ground_emitted_values (G3) consults kConventionValues, so its
            // convention entry of 0 would never be reached and every legacy-shape
            // ComputeRentVsBuy parse would be refused on a field the utterance
            // never mentions.
            //
            // Zero here means "this request does not describe a loan" -- the
            // all-cash purchase and the legacy composite shape both need it. The
            // engine already refuses a zero term on the amortising path, so this
            // does not make the verifier looser than the RPC it guards; it stops
            // it being STRICTER, which is the failure mode this file warns about.
            if (v.units() < 0 || (v.units() == 0 && field != "loan_term_years")) {
                return std::string{field} + " = " + v.to_string() + " is not positive";
            }
            return too_big(kMaxYearUnits, "horizon");
        case SlotKind::PeriodIndex:
            if (v.units() <= 0) return std::string{field} + " = " + v.to_string() + " is not a 1-based period";
            return too_big(kMaxMonthUnits, "period-ordinal");
        case SlotKind::DayOffsets:
            if (v.is_negative()) return std::string{field} + " = " + v.to_string() + " is negative";
            return too_big(kMaxDayUnits, "cash-flow date");
        case SlotKind::Frequency:
            // Bounds on payments_per_year belong to
            // `finance_service.cpp::check_payments_per_year` ([1, 366]) and
            // are deliberately NOT repeated here. Grounding still applies:
            // 12 is a convention value, anything else must appear in the
            // user's text.
            return std::nullopt;
        default:
            return std::nullopt;
    }
}

}  // namespace detail

auto MortgageParamsDomain::translate(const MortgageParamsInput& in) const -> VerificationFacts {
    VerificationFacts f;

    // ---- G4 is the caller's (verify_mortgage_output's) job, but a domain
    //      handed an absent-params input must not silently pass it. ----
    if (!in.params_emitted) {
        f.incomplete = true;
        f.reason = ReasonCode::NoParamsEmitted;
        f.detail = "the model emitted no <params> block; there is nothing to verify";
        return f;
    }

    // ---- G1: closed-vocabulary operation ----
    if (!is_known_operation(in.operation)) {
        f.violated = true;
        f.reason = ReasonCode::UnknownOperation;
        f.detail = "\"" + in.operation +
                   "\" is not an operation this assistant serves. It is refused, not repaired: an "
                   "operation id names which arithmetic runs, and guessing which one was meant "
                   "prices a different loan.";
        return f;
    }

    const auto declared = fields_of(in.operation);

    // ---- G2a: every emitted field must be declared on THIS operation ----
    for (const auto& emitted : in.fields) {
        const auto* spec = find_field(in.operation, emitted.name);
        if (spec == nullptr) {
            f.violated = true;
            f.reason = ReasonCode::UnknownField;
            f.detail = "\"" + emitted.name + "\" is not a field of " + in.operation +
                       "'s request message. It is refused, not renamed: a rename is a guess about "
                       "which slot was meant.";
            return f;
        }
        if (spec->repeated != emitted.repeated) {
            f.violated = true;
            f.reason = ReasonCode::ShapeMismatch;
            f.detail = "\"" + emitted.name + "\" on " + in.operation + " is declared " +
                       (spec->repeated ? "repeated" : "scalar") + " but was emitted " +
                       (emitted.repeated ? "repeated" : "scalar");
            return f;
        }
        if (!emitted.repeated && emitted.values.size() != 1) {
            f.violated = true;
            f.reason = ReasonCode::ShapeMismatch;
            f.detail = "scalar field \"" + emitted.name + "\" carries " +
                       std::to_string(emitted.values.size()) + " values";
            return f;
        }
        std::size_t seen = 0;
        for (const auto& other : in.fields) {
            if (other.name == emitted.name) ++seen;
        }
        if (seen != 1) {
            f.violated = true;
            f.reason = ReasonCode::DuplicateField;
            f.detail = "\"" + emitted.name + "\" is emitted " + std::to_string(seen) + " times";
            return f;
        }
    }

    // ---- G2b: every declared field must be emitted ----
    //
    // The training format emits the COMPLETE key set for an operation
    // (`build_mortgage_dataset.py`'s `params_block` asserts exactly that),
    // so a missing key is a model defect, not an abbreviation. Refusing it
    // is what keeps gate 4's rule -- never a fabricated default -- true at
    // field granularity as well as at block granularity: the alternative is
    // a service filling `annual_rate` with a proto zero and pricing an
    // interest-free mortgage.
    for (const auto& spec : declared) {
        bool present = false;
        for (const auto& emitted : in.fields) {
            if (emitted.name == spec.field) {
                present = true;
                break;
            }
        }
        if (!present) {
            f.violated = true;
            f.reason = ReasonCode::MissingField;
            f.detail = std::string{"\""} + std::string{spec.field} + "\" is declared on " +
                       in.operation + "'s request message but was not emitted; it will not be "
                       "defaulted";
            return f;
        }
    }

    // ---- G2c: enum constants, and G5: bounds ----
    for (const auto& emitted : in.fields) {
        const auto* spec = find_field(in.operation, emitted.name);
        const SlotKind kind = classify_slot(emitted.name);

        if (kind == SlotKind::Unclassified) {
            // A field finance.proto declares that this file's rule list has
            // never been taught. Every decidable rule above still ran and
            // passed; this one genuinely cannot be evaluated, and the honest
            // answer is "cannot decide", not "assume fine".
            f.incomplete = true;
            f.reason = ReasonCode::Unclassified;
            f.detail = "\"" + emitted.name + "\" on " + in.operation +
                       " has no slot-kind rule in this verifier, so its value cannot be checked";
            return f;
        }

        if (kind == SlotKind::Enumeration) {
            const detail::EnumSpec* enum_spec = nullptr;
            for (const auto& e : detail::kEnumConstants) {
                if (e.type_name == spec->proto_type) {
                    enum_spec = &e;
                    break;
                }
            }
            if (enum_spec == nullptr) {
                f.incomplete = true;
                f.reason = ReasonCode::Unclassified;
                f.detail = "enum type \"" + std::string{spec->proto_type} + "\" of field \"" +
                           emitted.name + "\" has no constant set in this verifier";
                return f;
            }
            for (const auto& v : emitted.values) {
                bool ok = false;
                for (std::size_t k = 0; k < enum_spec->count; ++k) {
                    if (enum_spec->constants[k] == v) {
                        ok = true;
                        break;
                    }
                }
                if (!ok) {
                    f.violated = true;
                    f.reason = ReasonCode::InvalidEnumValue;
                    f.detail = "\"" + v + "\" is not a constant of " +
                               std::string{spec->proto_type} + " (field \"" + emitted.name + "\")";
                    return f;
                }
            }
            continue;
        }

        if (kind == SlotKind::Boolean) {
            for (const auto& v : emitted.values) {
                if (v != "true" && v != "false") {
                    f.violated = true;
                    f.reason = ReasonCode::InvalidEnumValue;
                    f.detail = "\"" + v + "\" is not a boolean (field \"" + emitted.name + "\")";
                    return f;
                }
            }
            continue;
        }

        for (const auto& v : emitted.values) {
            const auto parsed = parse_strict_decimal(v);
            if (!parsed.has_value()) {
                f.violated = true;
                f.reason = ReasonCode::MalformedNumber;
                f.detail = "\"" + v + "\" in field \"" + emitted.name +
                           "\" is not a bare decimal this contract accepts";
                return f;
            }
            if (auto bad = detail::bound_violation(kind, emitted.name, *parsed); bad.has_value()) {
                f.violated = true;
                f.reason = ReasonCode::OutOfRange;
                f.detail = *bad;
                return f;
            }
        }
    }

    return f;  // violated == false, incomplete == false -> Proven.
}

auto verify_mortgage_params(const MortgageParamsInput& input) -> VerificationVerdict {
    const MortgageParamsDomain domain;
    RuleBasedReasoner reasoner;
    RuleBasedReasoner::ContextType ctx;

    const auto facts = domain.translate(input);
    const auto result = reasoner.prove_safety(ctx, facts);

    VerificationVerdict verdict;
    if (!result.has_value()) {
        verdict.outcome = Outcome::Indeterminate;
        verdict.reason = facts.reason;
        verdict.message = result.error().message;
        return verdict;
    }
    if (!*result) {
        verdict.outcome = Outcome::Unsafe;
        verdict.reason = facts.reason;
        verdict.message = facts.detail;
        return verdict;
    }
    verdict.outcome = Outcome::Proven;
    verdict.reason = ReasonCode::None;
    return verdict;
}

// ---------------------------------------------------------------------------
// G3.
// ---------------------------------------------------------------------------

auto ground_emitted_values(const MortgageParamsInput& input, std::string_view user_text)
    -> VerificationVerdict {
    VerificationVerdict verdict;

    if (!input.params_emitted) {
        verdict.outcome = Outcome::Indeterminate;
        verdict.reason = ReasonCode::NoParamsEmitted;
        verdict.message = "no <params> block to ground";
        return verdict;
    }

    const auto literals = lex_numeric_literals(user_text);

    // Read the rate's cadence ONCE, before grounding any field: `periods` is a
    // count of the RATE's periods, so the two must be grounded together or not
    // at all. Falls back to 12 -- the mortgage convention, and the behaviour
    // this gate had before -- when the rate does not identify a cadence.
    const std::int64_t periods_per_year =
        detail::infer_periods_per_year(input.fields, literals).value_or(12);

    for (const auto& emitted : input.fields) {
        const SlotKind kind = classify_slot(emitted.name);
        if (!detail::is_numeric_kind(kind)) {
            if (kind == SlotKind::Unclassified) {
                verdict.outcome = Outcome::Indeterminate;
                verdict.reason = ReasonCode::Unclassified;
                verdict.message = "\"" + emitted.name +
                                  "\" has no slot-kind rule, so it cannot be grounded";
                return verdict;
            }
            continue;  // enums and booleans are settled by G2, not by grounding
        }

        const __int128 tolerance = detail::slot_tolerance_units(kind);

        for (const auto& raw : emitted.values) {
            const auto parsed = parse_strict_decimal(raw);
            if (!parsed.has_value()) {
                verdict.outcome = Outcome::Unsafe;
                verdict.reason = ReasonCode::MalformedNumber;
                verdict.message = "\"" + raw + "\" in field \"" + emitted.name +
                                  "\" is not a bare decimal this contract accepts";
                return verdict;
            }

            if (detail::is_convention_value(emitted.name, *parsed)) continue;

            bool grounded = false;
            for (const auto& lit : literals) {
                for (const auto& candidate :
                     detail::expand_candidates(lit, kind, emitted.name, periods_per_year)) {
                    if (parsed->within(candidate, tolerance)) {
                        grounded = true;
                        break;
                    }
                }
                if (grounded) break;
            }

            if (!grounded) {
                // Name the closest literal the text does contain, so the
                // refusal is actionable rather than merely correct -- this
                // is what turns "5379.00 is ungrounded" into "you said
                // 5378.63".
                std::string nearest;
                __int128 best = 0;
                bool have_nearest = false;
                for (const auto& lit : literals) {
                    const __int128 a = parsed->units();
                    const __int128 b = lit.value.units();
                    const __int128 diff = a > b ? a - b : b - a;
                    if (!have_nearest || diff < best) {
                        best = diff;
                        nearest = lit.text;
                        have_nearest = true;
                    }
                }
                verdict.outcome = Outcome::Unsafe;
                verdict.reason = ReasonCode::UngroundedValue;
                verdict.message =
                    "\"" + emitted.name + "\" = " + raw +
                    " does not correspond to anything in the request" +
                    (have_nearest ? (" (the nearest figure you gave is " + nearest + ")") : "");
                return verdict;
            }
        }
    }

    verdict.outcome = Outcome::Proven;
    verdict.reason = ReasonCode::None;
    return verdict;
}

// ---------------------------------------------------------------------------
// The composed gate.
// ---------------------------------------------------------------------------

auto verify_mortgage_output(const MortgageParamsInput& input, std::string_view user_text)
    -> VerificationVerdict {
    VerificationVerdict verdict;

    // ---- G4 ----
    if (!input.params_emitted) {
        verdict.outcome = Outcome::Indeterminate;
        verdict.reason = ReasonCode::NoParamsEmitted;
        verdict.message =
            "the model produced no parameters. This is a refusal or a clarifying question, never "
            "a params block assembled from defaults.";
        return verdict;
    }

    // ---- G1 + G2 + G5 ----
    const auto structural = verify_mortgage_params(input);
    if (structural.outcome != Outcome::Proven) return structural;

    // ---- G3 ----
    return ground_emitted_values(input, user_text);
}

// ---------------------------------------------------------------------------
// Names.
// ---------------------------------------------------------------------------

auto to_string(ReasonCode code) -> std::string_view {
    switch (code) {
        case ReasonCode::None: return "None";
        case ReasonCode::NoParamsEmitted: return "NoParamsEmitted";
        case ReasonCode::UnknownOperation: return "UnknownOperation";
        case ReasonCode::UnknownField: return "UnknownField";
        case ReasonCode::MissingField: return "MissingField";
        case ReasonCode::DuplicateField: return "DuplicateField";
        case ReasonCode::ShapeMismatch: return "ShapeMismatch";
        case ReasonCode::InvalidEnumValue: return "InvalidEnumValue";
        case ReasonCode::MalformedNumber: return "MalformedNumber";
        case ReasonCode::UngroundedValue: return "UngroundedValue";
        case ReasonCode::OutOfRange: return "OutOfRange";
        case ReasonCode::Unclassified: return "Unclassified";
    }
    return "?";
}

auto to_string(Outcome outcome) -> std::string_view {
    switch (outcome) {
        case Outcome::Proven: return "Proven";
        case Outcome::Unsafe: return "Unsafe";
        case Outcome::Indeterminate: return "Indeterminate";
    }
    return "?";
}

}  // namespace mortgage_calculator::assistant::verify
