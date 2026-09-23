# Session 2026-09-23 — one money formatter, a currency picker, and the gate that was a digit too loose

@author Olumuyiwa Oluwasanmi

Asked to make every amount comma-separated on both sites, across locales, with
thorough regression tests. The mortgagefv half is in that repository's
`session_2026-09-23_money_formatting.md`; this is the calculator's.

## There was no money module

`money()` existed TWICE — copied between `StrategyMetrics` and
`ProbabilityCurve`, each hardcoding a `$` and the browser's default locale with
different decimal rules. **Both grouped, which is why nothing looked broken.**
What did not:

| place | rendered | now |
| --- | --- | --- |
| ticket debit/credit | `$7250.00` | `$7,250.00` |
| P&L matrix cell | `12775` | `12,775` |
| option chain strike | `5900.00` | `5,900.00` |
| term structure forward / basis | ungrouped | grouped |

The ticket figure is premium × contracts × 100, so it is the most likely to
reach five figures and it was the one with no separator at all.

## The picker, and the honest limit on it

A 38-currency picker is in the nav, deliberately parallel to mortgagefv's
module so a divergence shows as a diff rather than hiding behind two designs.

**It converts nothing, and the tooltip says so.** These amounts come from a
live US chain and are denominated in dollars. A symbol swap with no FX is a
display choice; labelling a USD price with a euro sign would be the same class
of defect as the LIVE badge derived from request status — fabrication by
labelling. That limit was stated to the owner before building it and the
decision to proceed was theirs.

## check-export caught the picker, correctly

Rendering all 38 options server-side put the same 38 lines into the static HTML
of all 59 exported pages:

```
✗ calculator-similarity: pairwise median 6-gram Jaccard 0.5172, ceiling 0.5
✗ bear-call-spread vs bull-put-spread 0.5535, ceiling 0.54
```

Those pages sit near the ceiling BY DESIGN — they are the same tool with a
different strategy name — so identical chrome is exactly what they cannot
afford. The option list became client-only. **The fix was to emit less
duplicated text, not to raise the ceiling**; a moved threshold with no reason
behind it is indistinguishable from a moved goalpost, and this repository has
already paid for that lesson once.

## Three layers, and the two defects only the browser found

| gate | checks | blind to |
| --- | --- | --- |
| `money-format.test.ts` | 44 | a component that never calls the formatter |
| `money-source-sweep.test.ts` | 8 | whether the result reaches the screen |
| `e2e/money.spec.ts` | 6 | — |

1. **The currency choice did not survive a reload.** `detectCurrency()` was
   ported and never called — a toggle rather than a preference. Every function
   involved was correct in isolation and nothing invoked them, so no unit test
   could have seen it.
2. **The sweep regex was a digit too loose.** `\d{5,}`, against a ticket that
   rendered `$7250.00` — **four** digits. The gate would have passed the very
   defect it was written to catch. `\d{4,}` now, with an explicit case
   asserting it fires on `$7250.00` and stays quiet on `$7,250.00`.

The second is the one worth carrying: it was written by me, it passed, and only
checking that it could FAIL exposed it.

## Gates

| gate | result |
| --- | --- |
| `tsc --noEmit` | 0 |
| `lint` | 0 errors |
| `test` | **362** (was 310; +52 is exactly the new gates) |
| `build` | OK — check-export and check-indexability both pass |
| `e2e/money.spec.ts` | 6/6 |
| `e2e/regression.spec.ts` | 4/4 — arithmetic identity and Pro gate unchanged |

Mutation-checked: restoring `$${cost.toFixed(2)}` fails the sweep by name.
