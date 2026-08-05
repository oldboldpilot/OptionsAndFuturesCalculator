# Finance proto extension: seven sensen home-finance capabilities → `sensen.finance.Finance`

Date: 2026-08-05. Status: SPEC ONLY — nothing below is implemented.
Sensen submodule: `299cc4fc` ("feat(financial): optimize and deploy refinance features with GPU batch simulation") — VERIFIED via `git log` in `backend/sensen`.

Every claim below is labeled VERIFIED (read from the tree) or INFERRED. Sources read:
`backend/sensen/src/financial.cppm` (lines 1839–2255, plus `xnpv`/`xirr` at 866–905),
`backend/proto/finance.proto` (718 lines, 36 RPCs), `backend/src/modules/finance_service.cpp`
(all 36 handlers), `backend/src/modules/quota.cpp` (cost model, lines 419–500),
`backend/src/modules/api_key.cppm` (auth surface), `backend/src/smoke_client.cpp`
(`check_finance`, lines 742–1114), `backend/envoy.yaml`, `backend/CMakeLists.txt`,
`backend/src/main.cpp`.

## Scope correction: six RPCs, not seven

**`get_pmi_drop` is not an exported sensen function.** It is a local lambda *inside*
`calculate_refinance_metrics` (`financial.cppm:1906`) and its two outputs are already
surfaced as `RefinanceSummary.current_loan_pmi_drop_off_months` /
`new_loan_pmi_drop_off_months`. VERIFIED — the only occurrences of `get_pmi_drop` in
`financial.cppm` are the lambda definition and its two call sites. There is nothing to
expose separately; the spec covers **six new RPCs** and the PMI-drop figures ride on the
refinance response.

---

## 1. Exact signatures (VERIFIED, `financial.cppm` 1839–2253)

All functions are in `export namespace sensen` (line 32 opens the export block, line 2255
closes it) — every one is importable via `import sensen.financial;` with no sensen change
needed. VERIFIED.

### 1a. `calculate_refinance_metrics(const RefinanceInput&) noexcept -> RefinanceSummary` (line 1885)

```cpp
enum class RefinanceClosingCostType { PaidInCash, RolledIntoLoan };   // line 1839

struct RefinanceInput {                                               // line 1844
    BigDecimal current_loan_balance;
    BigDecimal current_monthly_payment;   // P&I only
    BigDecimal current_annual_rate;
    int        current_remaining_months;
    BigDecimal property_value;
    BigDecimal new_annual_rate;
    int        new_term_years;
    BigDecimal closing_costs;
    RefinanceClosingCostType closing_cost_type;
    BigDecimal cash_out_amount;
    BigDecimal current_pmi_monthly;
    BigDecimal new_pmi_monthly;
    BigDecimal pmi_drop_off_ltv;
    int        payments_per_year = 12;
};

struct RefinanceSummary {                                             // line 1866
    BigDecimal new_loan_amount;
    BigDecimal new_monthly_payment;       // P&I only
    BigDecimal monthly_savings_initial;   // (Old P&I + PMI) - (New P&I + PMI)
    int        current_loan_pmi_drop_off_months;
    int        new_loan_pmi_drop_off_months;
    int        payoff_date_shift_months;
    int        simple_break_even_months;
    int        cash_flow_break_even_months;
    int        equity_adjusted_break_even_months;
    BigDecimal total_savings_over_life;
};
```

Sentinels (VERIFIED from the implementation): the three break-even fields are **-1** when
break-even never occurs (lines 1924, 1935–1936); PMI-drop months are **0** when the loan
starts at/below the PMI threshold or the respective PMI is zero, **-1** when `nper_fn`
fails (lines 1906–1917).

### 1b. `calculate_payoff_timing(...)` (line 2005)

```cpp
auto calculate_payoff_timing(
    BigDecimal current_loan_balance,
    BigDecimal annual_rate,
    BigDecimal current_monthly_payment,   // P&I only
    BigDecimal extra_monthly_payment,
    int        payments_per_year = 12) noexcept -> PayoffTimingSummary;

struct PayoffTimingSummary {                                          // line 1997
    int        original_months_remaining;
    int        new_months_remaining;
    int        months_saved;
    BigDecimal total_interest_saved;
};
```

**Silent-zero hazard (VERIFIED, lines 2017–2028):** when `nper_fn` has no value (payment
does not cover interest, so the loan never amortizes), `orig_months`/`new_months` stay
**0** and the function returns zeros that look like answers. The handler must refuse this
input (§4) — sensen will not.

### 1c. `calculate_home_future_value(...)` (line 2058)

```cpp
auto calculate_home_future_value(
    BigDecimal current_property_value,
    BigDecimal annual_appreciation_rate,
    BigDecimal current_loan_balance,
    BigDecimal annual_mortgage_rate,
    BigDecimal current_monthly_payment,   // P&I only
    int        target_years,
    int        payments_per_year = 12) noexcept -> FutureValueSummary;

struct FutureValueSummary {                                           // line 2051
    BigDecimal future_property_value;
    BigDecimal future_loan_balance;       // clamped to 0 when negative
    BigDecimal future_equity;
};
```

### 1d. `calculate_mortgage_recast(...)` (line 2094)

```cpp
auto calculate_mortgage_recast(
    BigDecimal current_loan_balance,
    BigDecimal current_monthly_payment,
    BigDecimal lump_sum_payment,          // new_balance clamped to 0 if lump > balance
    BigDecimal annual_rate,
    int        remaining_months,
    int        payments_per_year = 12) noexcept -> RecastSummary;

struct RecastSummary {                                                // line 2088
    BigDecimal new_monthly_payment;
    BigDecimal monthly_savings;
};
```

### 1e. `calculate_rent_vs_buy(...)` (line 2124)

```cpp
auto calculate_rent_vs_buy(
    BigDecimal property_price,
    BigDecimal down_payment,
    BigDecimal monthly_piti_and_maintenance,
    BigDecimal annual_home_appreciation,
    BigDecimal current_monthly_rent,
    BigDecimal annual_rent_increase,
    BigDecimal annual_investment_return,
    int        years) noexcept -> RentVsBuySummary;

struct RentVsBuySummary {                                             // line 2116
    BigDecimal total_cost_of_buying;
    BigDecimal total_cost_of_renting;
    bool       is_buying_better;
    BigDecimal buying_advantage;          // rent cost - buy cost; positive => buy
};
```

### 1f. `calculate_home_npv(const HomeNPVInput&) noexcept -> HomeNPVSummary` (line 2196)

```cpp
struct HomeNPVInput {                                                 // line 2166
    BigDecimal property_price;
    BigDecimal down_payment;
    BigDecimal closing_costs_buy;
    BigDecimal loan_amount;
    BigDecimal loan_annual_rate;
    int        loan_term_years;
    BigDecimal monthly_taxes_ins_hoa;
    BigDecimal monthly_maintenance;
    BigDecimal annual_appreciation_rate;
    BigDecimal selling_closing_cost_percent;
    BigDecimal monthly_rent_saved;        // imputed rent / alternative housing cost
    BigDecimal annual_rent_increase;
    BigDecimal annual_discount_rate;
    int        holding_period_years;
};

struct HomeNPVSummary {                                               // line 2188
    BigDecimal net_present_value;
    BigDecimal internal_rate_of_return;
    BigDecimal future_sale_price;
    BigDecimal future_equity;
};
```

**Silent-zero hazard (VERIFIED, lines 2243–2244):** `npv_opt.value_or(0.0)` /
`irr_opt.value_or(0.0)` — an `xnpv`/`xirr` failure becomes a silent 0.0. The handler
cannot distinguish this post-hoc; §4 prescribes pre-validation that removes the known
failure modes, plus a documented residual.

---

## 2. Numeric type mapping — per field, from the arithmetic actually read

The proto's own rule (`finance.proto:20–38`, VERIFIED): `string` where sensen computes in
BigDecimal (exact `__int128` × 1e18), `double` where sensen genuinely computes in double —
"widening it to a string would imply a precision the engine never had."

The subtlety in these seven: **several struct fields are declared `BigDecimal` but the
value inside them was computed entirely in `double` and wrapped at the end.** The struct
declaration is not the computation. Per field:

### Inputs — all decimal inputs are `string`

Every decimal input parameter is `BigDecimal` in the sensen signature, so it is parsed
exactly and the wire type is `string` (parse via the existing strict `parse_decimal`,
empty = 0). This holds even for `calculate_home_npv`, which immediately narrows most
inputs to double — precedent: `RateRequest`/`PeriodsRequest` carry string inputs and the
handler narrows, with a comment saying so (`finance_service.cpp:284–289`, VERIFIED).
`int` params → `int32`. `RefinanceClosingCostType` → proto enum (§3).

### `RefinanceResponse`

| field | C++ | computation (VERIFIED line) | wire | why |
| --- | --- | --- | --- | --- |
| new_loan_amount | BigDecimal | `add()` chain, 1888–1895 | string | exact BigDecimal arithmetic |
| new_monthly_payment | BigDecimal | BigDecimal `pmt()`, 1902 | string | exact |
| monthly_savings_initial | BigDecimal | `add`/`subtract`, 1919–1922 | string | exact |
| current_loan_pmi_drop_off_months | int | `nper_fn` (double) → ceil, 1906–1914 | int32 | integer |
| new_loan_pmi_drop_off_months | int | same | int32 | integer |
| payoff_date_shift_months | int | int arithmetic, 1981 | int32 | integer |
| simple_break_even_months | int | double division → ceil, 1926 | int32 | integer; -1 = never |
| cash_flow_break_even_months | int | double month loop, 1968 | int32 | integer; -1 = never |
| equity_adjusted_break_even_months | int | double month loop, 1975 | int32 | integer; -1 = never |
| total_savings_over_life | BigDecimal | **double accumulation over the whole month loop** (`total_old_paid`/`total_new_paid` are `double`, 1932–1980), wrapped `BigDecimal(double)` at 1980 | **double** | the engine genuinely computes this in double; an 18-place string here would be the false-precision claim the proto header forbids |

### `PayoffTimingResponse`

| field | C++ | computation | wire | why |
| --- | --- | --- | --- | --- |
| original_months_remaining | int | `nper_fn` double → ceil (2017–2021) | int32 | integer |
| new_months_remaining | int | same (2024–2028) | int32 | integer |
| months_saved | int | int subtraction | int32 | integer |
| total_interest_saved | BigDecimal | BigDecimal `multiply`/`subtract` on the integer month counts (2032–2041) | string | the arithmetic producing the figure is exact BigDecimal (the months are integers by the time they enter it) |

### `HomeFutureValueResponse`

| field | C++ | computation | wire | why |
| --- | --- | --- | --- | --- |
| future_property_value | BigDecimal | `std::pow` in double, wrapped (2067–2068) | **double** | wholly double-computed |
| future_loan_balance | BigDecimal | BigDecimal `fv()` (2074) | string | exact BigDecimal closed form |
| future_equity | BigDecimal | BigDecimal `subtract` (2079), but the property leg is double-derived | string, **with a comment** stating the appreciation leg was computed in double | the final arithmetic is exact; the comment keeps the precision claim honest without splitting the identity `equity = value − balance` across types in a way clients cannot reconcile |

### `MortgageRecastResponse`

| field | C++ | computation | wire | why |
| --- | --- | --- | --- | --- |
| new_monthly_payment | BigDecimal | BigDecimal `pmt()` (2107) | string | exact |
| monthly_savings | BigDecimal | BigDecimal `subtract` (2108) | string | exact |

Money over a recast is exactly the compounding-amortization case the proto header's
string rationale exists for. This message is the purest string case of the six.

### `RentVsBuyResponse`

| field | C++ | computation | wire | why |
| --- | --- | --- | --- | --- |
| total_cost_of_buying | BigDecimal | `add`/`subtract` in BigDecimal, but the equity term embeds `std::pow` double (2135–2139) | **double** | see below |
| total_cost_of_renting | BigDecimal | **entirely double**: rent loop and investment `pow` in double, wrapped at 2153 | **double** | wholly double-computed |
| is_buying_better | bool | `is_positive()` (2156) | bool | — |
| buying_advantage | BigDecimal | subtract of the two above (2155) | **double** | both operands double-limited |

Rationale for making the whole message double: the renting side is genuinely double, and
the buy/advantage figures are differences against it — presenting one side as an
18-place-exact string and the other as double would misstate which digits are real.
Field comments state this. (Contrast `HomeFutureValueResponse`, where one leg — the
amortization — genuinely IS exact and worth preserving as a string.)

### `HomeNpvResponse`

| field | C++ | computation | wire | why |
| --- | --- | --- | --- | --- |
| net_present_value | BigDecimal | `xnpv` — pure double (2240, 866–878) | **double** | precedent is decisive: the existing `ComputeNpv`/`ComputeXnpv` return `DoubleResponse` for the *same* `xnpv` function. A string here would make the wrapped call claim more precision than the primitive it wraps |
| internal_rate_of_return | BigDecimal | `xirr` — double Newton solve (2241) | **double** | same precedent: `ComputeXirr` → `DoubleResponse` |
| future_sale_price | BigDecimal | `std::pow` double (2234) | **double** | wholly double-computed |
| future_equity | BigDecimal | double subtraction (2245) | **double** | wholly double-computed (the balance was simulated month-by-month in double, 2211–2220) |

### Precedent flags (the ones the task asked for)

1. **Naive mapping disagrees hardest on `HomeNpvResponse`.** Naively, BigDecimal struct
   fields → string. But the existing proto returns NPV/IRR as `double`
   (`ComputeNpv`/`ComputeXnpv`/`ComputeXirr` → `DoubleResponse`, VERIFIED
   `finance.proto:60–63`), computed by the *same* sensen functions this wrapper calls.
   Mapping the wrapped call to string while the primitive stays double would be
   incoherent. Recommendation: double, as above.
2. **`AmortizationResponse` precedent supports the string choices.** Every money field in
   the amortization/HELOC/rental-ROI family is a string because the computation is
   BigDecimal end-to-end (VERIFIED). `MortgageRecastResponse`, `PayoffTimingResponse`
   and the exact fields of `RefinanceResponse` are the same situation.
3. **Counter-precedent noted for honesty:** `ComputeRate` returns a *string*
   (`DecimalResponse`) for a value solved in double (`BigDecimal(*r).to_string()`,
   `finance_service.cpp:296`), and `ComputeCumulative` returns a *double* for a value
   computed in BigDecimal (`.to_double()`, line 599). The existing file is not perfectly
   consistent at the boundary. This spec resolves the boundary in the direction the proto
   header's own rule states (mirror the computation), which is also the direction the
   XNPV/XIRR precedent points.
4. **Mixed-type messages are new.** No existing response message mixes string and double
   money fields; `RefinanceResponse` and `HomeFutureValueResponse` will. This is
   permitted by the header's rule ("it mirrors what the engine actually computes in") and
   every mixed field carries a comment saying which domain produced it. The alternative —
   uniform strings per message — was rejected because it asserts exactness the engine
   does not have, which is the precise failure mode the header documents against.

---

## 3. Proto design

Style rules observed from the existing file (VERIFIED): `Compute*` verb prefix; request
messages named for the domain not the RPC (`HelocRequest`, `RentalRoiRequest`); section
banner comments; explanatory comments on any field with a convention or default; enums
with a meaningful zero value and no `_UNSPECIFIED` sentinel anywhere in the file
(`AnnuityTiming.END_OF_PERIOD = 0`, `DepreciationRequest.Method.STRAIGHT_LINE = 0`);
nested enums when scoped to one request (`RateConversionRequest.Direction`).

Name collision check (VERIFIED): `FutureValueRequest` and `FutureValueDetailedRequest`
already exist, so the home-FV pair must be `HomeFutureValue*`. No collisions for the
other five.

### Service block additions

```proto
  // -- Mortgages, HELOC ------------------------------------------------------
  // (append after ComputeHeloc)
  rpc ComputeRefinance (RefinanceRequest) returns (RefinanceResponse);
  rpc ComputePayoffTiming (PayoffTimingRequest) returns (PayoffTimingResponse);
  rpc ComputeMortgageRecast (MortgageRecastRequest) returns (MortgageRecastResponse);

  // -- Real estate -----------------------------------------------------------
  // (append after ComputeRentalRoi)
  rpc ComputeHomeFutureValue (HomeFutureValueRequest) returns (HomeFutureValueResponse);
  rpc ComputeRentVsBuy (RentVsBuyRequest) returns (RentVsBuyResponse);
  rpc ComputeHomeNpv (HomeNpvRequest) returns (HomeNpvResponse);
```

`ComputeHomeNpv` capitalization follows `ComputeNpv`/`ComputeXnpv` (not `NPV`).

### Messages

```proto
// ============================================================================
// Refinancing
// ============================================================================

message RefinanceRequest {
  string current_loan_balance = 1;
  string current_monthly_payment = 2;   // P&I only -- PMI goes in its own field
  string current_annual_rate = 3;
  int32 current_remaining_months = 4;
  string property_value = 5;            // PMI drop-off is measured against this
  string new_annual_rate = 6;
  int32 new_term_years = 7;
  string closing_costs = 8;
  // How the closing costs are funded changes the new loan's size, so the
  // caller states it. PAID_IN_CASH is the zero value because proto3 makes the
  // first enumerator the default for an unset field, and "the loan did not
  // silently grow" is the only safe reading of a caller who said nothing.
  enum ClosingCostType {
    PAID_IN_CASH = 0;
    ROLLED_INTO_LOAN = 1;
  }
  ClosingCostType closing_cost_type = 9;
  string cash_out_amount = 10;          // omit for a rate-and-term refinance
  string current_pmi_monthly = 11;
  string new_pmi_monthly = 12;
  string pmi_drop_off_ltv = 13;         // 0.80 for the standard 80% LTV
  int32 payments_per_year = 14;         // 12 monthly; zero is rejected
}

message RefinanceResponse {
  string new_loan_amount = 1;
  string new_monthly_payment = 2;       // P&I only
  string monthly_savings_initial = 3;   // (old P&I + PMI) - (new P&I + PMI)
  // Months until each loan's balance amortizes below the PMI drop-off LTV.
  // 0 = never had PMI (or already below the threshold); -1 = never reached.
  int32 current_loan_pmi_drop_off_months = 4;
  int32 new_loan_pmi_drop_off_months = 5;
  // Positive when the refinance pushes the payoff date out, negative when it
  // pulls it in: new term minus months remaining on the old loan.
  int32 payoff_date_shift_months = 6;
  // Three break-evens, three questions: closing costs / initial savings;
  // cumulative cash outlay parity; cash parity plus the equity difference.
  // Each is -1 when it never occurs over the compared horizons.
  int32 simple_break_even_months = 7;
  int32 cash_flow_break_even_months = 8;
  int32 equity_adjusted_break_even_months = 9;
  // A double, not a decimal string: the engine accumulates this over the whole
  // payment horizon in double (the month-by-month comparison loop), so a
  // decimal string here would claim digits the computation never had.
  double total_savings_over_life = 10;
}

message PayoffTimingRequest {
  string current_loan_balance = 1;
  string annual_rate = 2;
  string current_monthly_payment = 3;   // P&I only
  string extra_monthly_payment = 4;
  int32 payments_per_year = 5;          // 12 monthly; zero is rejected
}

message PayoffTimingResponse {
  int32 original_months_remaining = 1;
  int32 new_months_remaining = 2;
  int32 months_saved = 3;
  string total_interest_saved = 4;
}

message MortgageRecastRequest {
  string current_loan_balance = 1;
  string current_monthly_payment = 2;
  string lump_sum_payment = 3;
  string annual_rate = 4;
  int32 remaining_months = 5;           // term is unchanged by a recast
  int32 payments_per_year = 6;          // 12 monthly; zero is rejected
}

message MortgageRecastResponse {
  string new_monthly_payment = 1;
  string monthly_savings = 2;
}

// (real-estate section)

message HomeFutureValueRequest {
  string current_property_value = 1;
  string annual_appreciation_rate = 2;
  string current_loan_balance = 3;
  string annual_mortgage_rate = 4;
  string current_monthly_payment = 5;   // P&I only
  int32 target_years = 6;
  int32 payments_per_year = 7;          // 12 monthly; zero is rejected
}

message HomeFutureValueResponse {
  // Double: compound appreciation is computed in double.
  double future_property_value = 1;
  // Exact: the remaining balance is a BigDecimal closed form. Clamped to 0
  // once the loan would have retired.
  string future_loan_balance = 2;
  // Exact subtraction, but the property leg above entered in double -- the
  // string carries the arithmetic faithfully, not 18 fresh digits.
  string future_equity = 3;
}

message RentVsBuyRequest {
  string property_price = 1;
  string down_payment = 2;
  string monthly_piti_and_maintenance = 3;
  string annual_home_appreciation = 4;
  string current_monthly_rent = 5;
  string annual_rent_increase = 6;
  // What the down payment would have earned invested instead -- the
  // opportunity cost side of the comparison.
  string annual_investment_return = 7;
  int32 years = 8;
}

// All doubles: the rent escalation, appreciation and investment legs are all
// computed in double, and the buy/rent figures are only meaningful against
// each other -- quoting one side to 18 places would misstate which digits
// are real.
message RentVsBuyResponse {
  double total_cost_of_buying = 1;
  double total_cost_of_renting = 2;
  bool is_buying_better = 3;
  double buying_advantage = 4;          // rent cost minus buy cost
}

message HomeNpvRequest {
  string property_price = 1;
  string down_payment = 2;
  string closing_costs_buy = 3;
  string loan_amount = 4;
  string loan_annual_rate = 5;
  int32 loan_term_years = 6;
  string monthly_taxes_ins_hoa = 7;
  string monthly_maintenance = 8;
  string annual_appreciation_rate = 9;
  string selling_closing_cost_percent = 10;  // 0.06 for 6% of the sale price
  string monthly_rent_saved = 11;            // imputed rent the purchase displaces
  string annual_rent_increase = 12;
  string annual_discount_rate = 13;
  int32 holding_period_years = 14;
}

// All doubles, matching ComputeXnpv/ComputeXirr above: this RPC is a cash-flow
// model over the same xnpv/xirr primitives, which this service already serves
// as doubles because that is what they compute in.
message HomeNpvResponse {
  double net_present_value = 1;
  double internal_rate_of_return = 2;
  double future_sale_price = 3;
  double future_equity = 4;
}
```

### The enum and proto3's zero value

`RefinanceClosingCostType` maps to a nested `RefinanceRequest.ClosingCostType`
(nested because it is scoped to one request — the `RateConversionRequest.Direction`
convention). Proto3 requires the first enum value to be 0 and makes 0 the wire default
for an unset field; there is no "absent" state. So the zero value is a semantic
decision, not a formality: `PAID_IN_CASH = 0` matches the C++ enumerator order
(`PaidInCash` is first, so a straight `static_cast` in the handler is order-correct —
though the handler should still map explicitly by value, §4) and makes the unset-field
behavior the conservative one — the engine does not silently grow the caller's loan by
the closing costs. An `_UNSPECIFIED = 0` sentinel would deviate from every enum already
in this file (VERIFIED: none has one).

---

## 4. Handler wiring

**Where:** `backend/src/modules/finance_service.cpp` — anonymous-namespace
`FinanceServiceImpl final : public sensen::finance::Finance::Service`, registered once in
`RegisterFinanceService(grpc::ServerBuilder&)` which `main.cpp:45` calls (VERIFIED).
Adding RPCs = adding `override` methods to this class. **No registration change, no
CMake change** — `finance.pb.cc`/`finance.grpc.pb.cc` are regenerated from
`proto/finance.proto` by the existing custom command (`CMakeLists.txt:308–311`,
VERIFIED), and the auth layer takes `(service, method)` as free-form string_views
(`api_key.cppm:119–120`, VERIFIED) — methods are not individually enrolled anywhere.
INFERRED from that signature: no per-method allowlist exists to update; confirm by
reading `KeyRegistry::authenticate`'s body before shipping.

**The pattern every existing handler follows** (VERIFIED across all 36):

1. `if (request == nullptr || response == nullptr) return INTERNAL` — transport nulls.
2. `CHARGE("MethodName", quota::cost_...())` — the auth-then-quota macro. Auth first
   (produces the identity quota meters against), quota second; a refused call costs a
   hash lookup, not the computation. One visible line per RPC, deliberately not an
   interceptor (an interceptor cannot see the deserialized request to price it).
3. Scalar validations that **refuse rather than default** → `INVALID_ARGUMENT` with a
   message naming the field and the convention (e.g. "payments_per_year must be positive
   (12 monthly, 26 bi-weekly)").
4. `READ_DECIMAL(var, request->field(), "field")` per string field — strict grammar
   validation before `BigDecimal` parse, because `BigDecimal::parse` silently skips
   non-digits ("12x3" → 123). Malformed → `INVALID_ARGUMENT` naming the field. Empty
   string → 0 (proto3 unset).
5. Call sensen. `std::expected` failure → `FAILED_PRECONDITION` carrying sensen's own
   message (`fail()` helper). Functions that throw are wrapped in try/catch →
   `INVALID_ARGUMENT` (only `PriceOptionTree` needs this today; none of the six new
   functions throw — all are `noexcept`, VERIFIED).
6. Serialize: `.to_string()` for BigDecimal→string fields, `.to_double()` for
   BigDecimal→double fields, direct set for ints/bools.

**Per-RPC validation table (all refusals, in the existing style):**

| RPC | refuse when | why |
| --- | --- | --- |
| all six | `payments_per_year <= 0` (where the message has it) | matches ComputeHeloc/ComputeRentalRoi; the C++ default of 12 is not the wire's to assume |
| ComputeRefinance | `current_remaining_months <= 0` or `> 1200`; `new_term_years <= 0` or `> 100` | the engine loops `max(current_remaining_months, new_term_years*ppy)` months (line 1938) — the same DoS-dressed-as-a-mortgage `check_term` guards against (1200-month ceiling, VERIFIED `finance_service.cpp:1101–1114`); reuse/extend `check_term` |
| ComputeRefinance | `closing_cost_type` outside {0,1} | "unknown closing cost type" — the `default:` refusal arm every enum switch in this file has |
| ComputePayoffTiming | `current_monthly_payment * ppy <= balance * annual_rate` (payment does not cover interest); also `extra_monthly_payment < 0` | sensen silently returns **zeros** here (§1b) — the handler must convert the silent zero into a refusal: "current_monthly_payment does not cover the interest; the loan never retires". This is the "refuse rather than compute a guess" rule with teeth |
| ComputeMortgageRecast | `remaining_months <= 0` or `> 1200`; `lump_sum_payment < 0` | term guard; a negative lump is a payment in the wrong direction (engine would happily grow the balance) |
| ComputeHomeFutureValue | `target_years <= 0` or `> 100` | closed form, but a bounded horizon keeps `pow` sane and matches the term ceiling's spirit |
| ComputeRentVsBuy | `years <= 0` or `> 100` | the engine loops `years` iterations (line 2144) |
| ComputeHomeNpv | `holding_period_years <= 0` or `> 100`; `loan_term_years <= 0` or `> 100`; `loan_annual_rate` such that pmt is computable (`loan_term_years*12` months via `check_term`-style bound) | the engine loops `holding*12` months building cash flows; also removes the known `xnpv/xirr` empty-input failure modes behind the `value_or(0.0)` silent zero (§1f). Residual: `xirr` non-convergence still silently yields 0.0 inside sensen — document on the field; the faithful-wrapper contract stops at what sensen exposes |

Closing-cost enum mapping in the handler: explicit switch (`PAID_IN_CASH →
RefinanceClosingCostType::PaidInCash`, `ROLLED_INTO_LOAN → ...::RolledIntoLoan`,
`default → INVALID_ARGUMENT`), not a `static_cast` — the file's enum handlers all
switch (VERIFIED: depreciation, spread, exercise type).

---

## 5. Test plan — independent checks for `check_finance` (`backend/src/smoke_client.cpp`)

The gate's contract (VERIFIED from its header comment, lines 742–750): every assertion is
checked against something derived independently — a closed form evaluated in the test, or
an identity that holds regardless of how the answer was computed. Never against a figure
the engine produced earlier. Each new RPC gets a block appended inside `check_finance`,
plus refusal probes appended to the existing refusals section.

### 5a. ComputeRefinance

- **No-op refinance identity.** Refinance a loan into *itself*: same rate, term equal to
  the remaining months, zero closing costs, zero cash-out, zero PMI, and
  `current_monthly_payment` set to the closed-form annuity payment the test computes
  (`-PV·r/(1-(1+r)^-n)` — the same formula the existing PMT check writes out, sharing no
  code with the engine). Assert: `new_loan_amount == PV` (exact string compare is safe —
  pure BigDecimal add of zeros), `new_monthly_payment` equals the closed form to 1e-6,
  `monthly_savings_initial ≈ 0`, `payoff_date_shift_months == 0`,
  `total_savings_over_life ≈ 0` (double tolerance ~1e-4 — the month loop is double).
- **Simple break-even against its definition.** Refinance 6%→4.5%, 300k, 300 months
  remaining, new 360-month term, 6,000 closing costs paid in cash. The test computes both
  payments from the closed form, `savings = old − new`, and asserts
  `simple_break_even_months == ceil(6000 / savings)`. Every input to that expectation is
  test-side.
- **Closing-cost funding identity.** Same request twice, `PAID_IN_CASH` vs
  `ROLLED_INTO_LOAN`: `new_loan_amount` must differ by exactly `closing_costs` (exact
  BigDecimal add → exact decimal-string arithmetic in the test), and the rolled variant's
  `new_monthly_payment` must be strictly greater.
- **Never-break-even sentinel.** Refinance to a *higher* rate: `simple_break_even_months
  == -1` (savings are negative; a positive number here means the sentinel logic broke).
- **PMI drop-off zero case.** `pmi_drop_off_ltv` set so the starting balance is already
  below `property_value × ltv`: both drop-off fields must be 0 (line 1907's early
  return).

### 5b. ComputePayoffTiming — the annuity inversion

Construct the loan so the answer is known before the call: pick `PV`, `r`, `n`; set
payment to the exact closed-form annuity payment. Then:
- `original_months_remaining == n` (nper of an exact annuity payment is exactly n; ceil
  cannot move it — allow n or n at tolerance via computing test-side
  `ceil(-ln(1 - PV·r_p/pmt)/ln(1+r_p))`).
- With extra `e`: expected `n' = ceil(-ln(1 - PV·r_p/(pmt+e))/ln(1+r_p))` computed in the
  test; assert `new_months_remaining == n'` and `months_saved == n - n'`.
- `total_interest_saved` equals `(pmt·n − PV) − ((pmt+e)·n' − PV)` computed test-side
  (tolerance: the engine computes this leg in BigDecimal from integer months — 1e-6).
- **Refusal:** a payment below one period's interest must be refused, not answered with
  zeros (this is the probe that pins §4's handler guard — without it the silent-zero
  hazard of §1b ships unguarded).

### 5c. ComputeHomeFutureValue — both legs have closed forms

- Property leg: `future_property_value == P·(1+a)^y`, test-side `std::pow` — this is a
  double==double comparison of the same formula; the check is that the field is not
  garbled/misrouted, plus a strictly-independent sanity leg below.
- Loan leg (the genuinely independent one): remaining balance after m payments has the
  closed form `B·(1+r)^m − pmt·((1+r)^m − 1)/r`, written out in the test. Assert to
  1e-4.
- **Retirement identity (schedule closure's sibling):** with payment = exact annuity
  payment and `target_years·12 == remaining term`, `future_loan_balance == 0` exactly
  (the clamp at line 2075 guarantees the sign; the assert proves the magnitude).
- Cross-field: `future_equity == future_property_value − future_loan_balance` from the
  response's own fields (consistency, weaker — flagged as such in the test comment).

### 5d. ComputeMortgageRecast — pmt linearity

`pmt` is linear in principal, which gives a real identity rather than a re-run:
- `lump = 0` → `new_monthly_payment` equals the test-side closed-form payment on
  `(balance, rate, months)` and `monthly_savings == current_payment − that`.
- `lump = L` → `new_monthly_payment` equals the closed form on `balance − L`.
  Equivalently assert the linearity itself: `payment(balance) − payment(balance−L) ==
  payment(L)` across three calls — an identity no cached constant can fake.
- Full-payoff edge: `lump == balance` → `new_monthly_payment == "0"` exactly.

### 5e. ComputeRentVsBuy — no clean arbitrage identity; say so

There is no put-call-parity-grade identity for a rent-vs-buy heuristic — it is a model,
not a priced instrument. The honest next-best, per the gate's own precedent (the PMT
check *is* "closed form written out in the test"):
- **Independent closed-form agreement.** Every term of the model is elementary:
  `buy = D + M·12y − (P(1+a)^y − (P−D))`, `rent = Σ_{k=0}^{y-1} 12·R·(1+g)^k −
  D((1+i)^y − 1)`. The test evaluates both from the raw inputs and asserts agreement to
  double tolerance (1e-6 relative). Shares no code with the engine.
- **Invariants.** `buying_advantage == total_cost_of_renting − total_cost_of_buying`
  (from the response, definitional); `is_buying_better == (buying_advantage > 0)`;
  monotonicity probe — re-issue with higher `current_monthly_rent`, all else fixed:
  `buying_advantage` must strictly increase.
- **Degenerate control.** Zero appreciation, zero rent growth, zero investment return:
  both sides collapse to hand arithmetic (`buy = 12yM − 0` adjustments, `rent = 12yR`);
  assert exactly (double).

### 5f. ComputeHomeNpv — the IRR definition as the identity

- **NPV(IRR) == 0.** The one genuine identity here: re-evaluating the cash-flow stream at
  the returned IRR must give zero, by definition of IRR, regardless of implementation.
  The test reconstructs the stream independently from the request (the model is
  specified: initial outflow `D + closing`; monthly `rent_saved − (pmt + taxes +
  maintenance)` with the rent bumped each 12 months; terminal `+ sale − selling costs −
  remaining balance`) and discounts it at the returned `internal_rate_of_return`; assert
  `|NPV| < $1` on a six-figure model. This catches a wrong IRR *and* a divergent
  reconstruction in one assertion.
  - **Date-unit trap (VERIFIED so the implementer does not re-derive it wrong):**
    sensen's `xnpv` treats dates as *seconds* on a 365-day year (`/ 31536000.0`,
    `financial.cppm:874`) and `calculate_home_npv` feeds it `seconds_per_month =
    31536000/12` ticks (line 2209). The test's reconstruction must discount with month
    exponent `m/12` — which is exactly what those units reduce to. Note the existing
    `DatedCashFlowRequest` comment says "day offsets"; day offsets also work because
    only ratios enter, but the test must pick ONE convention and derive the exponent,
    not mix them.
- **Cross-check against the already-served primitive.** Feed the reconstructed stream to
  the *existing* `ComputeXnpv` at the request's discount rate and assert it matches
  `net_present_value` to 1e-6 relative. This pins the wrapper to a primitive the service
  already exposes, through the same RPC surface. (Whether `ComputeXnpv` itself is
  currently asserted in `check_finance` beyond this — INFERRED not, from the sections
  read; this check does not depend on it, since the expectation is test-side.)
- Terminal legs: `future_sale_price == P(1+a)^y` (test-side pow); `future_equity ==
  sale − balance` where balance is the test's own month-by-month double recursion (the
  engine's is double too — tolerance 1e-4).
- **Refusal probes** for the refusals section: zero `holding_period_years`; zero
  `loan_term_years`; `payments`-shaped malformed decimals reuse the existing "12x3"
  probe pattern on one new field (e.g. `annual_discount_rate = "12x3"`).

---

## 6. Quota / cost

VERIFIED: every finance RPC charges via `CHARGE(name, quota::cost_*(...))`; costs live in
`backend/src/modules/quota.cpp:430–500` (declarations `quota.cppm:142–153`). Unit = one
closed-form scalar call; shapes priced by order of magnitude, not benchmark. Existing
charges: `cost_default()=1.0` for all closed forms (pmt, BS, bond, HELOC, rental ROI);
`cost_amortization(term)=1+term/12` for schedule loops; `cost_cash_flow(entries)=
1+entries/10` for NPV/IRR/XNPV/XIRR.

**A new RPC without a `CHARGE` line is unmetered** — the macro is per-handler, not an
interceptor, so omission is silent at runtime (though visible in review; the file's own
comment calls a missing CHARGE "a visible omission rather than an invisible hole" —
visible only to a reader, so the acceptance gate below greps for it). Assignments:

| RPC | cost | shape it prices |
| --- | --- | --- |
| ComputeRefinance | `cost_amortization(std::max(current_remaining_months, new_term_years * 12))` | the month-by-month comparison loop (line 1940) — exactly an amortization walk, twice over; the existing helper's shape fits, no new helper needed |
| ComputePayoffTiming | `cost_default()` | two closed-form npers |
| ComputeMortgageRecast | `cost_default()` | one closed-form pmt |
| ComputeHomeFutureValue | `cost_default()` | closed forms only |
| ComputeRentVsBuy | `cost_default()` | an O(years ≤ 100) trivial double loop — under one unit of real work; the ≤100 bound comes from §4's validation |
| ComputeHomeNpv | `cost_cash_flow(holding_period_years * 12)` | builds `12·years` cash flows then `xirr` Newton-iterates a full NPV per step — the same shape `ComputeXirr` prices with this helper |

No `QUOTA_POLICY` change: the live policy defines per-tier limits, not per-method costs
(costs are code-side helpers). Per CLAUDE.md: do not "fix" the live policy against
`docs/FINANCE_API.md`'s example, and remember unkeyed callers all share the one
`~anonymous` bucket — these RPCs inherit that automatically through `CHARGE`.

---

## 7. Risk

- **Wire compatibility: purely additive.** New messages and new RPCs; zero existing
  field numbers, message names, or RPC signatures touched. Existing gRPC and gRPC-Web
  clients are unaffected (unknown methods simply keep not existing for them). The only
  file-level risk is accidental renumbering/edit of an existing message while editing —
  the acceptance gate diffs the descriptor (below).
- **Service registration: none needed.** Same `FinanceServiceImpl`, same
  `RegisterFinanceService`, same port. VERIFIED `main.cpp:45`.
- **Envoy routing: none needed.** The route is a deliberate catch-all `prefix: "/"` to
  `backend_grpc_service`; `envoy.yaml`'s own comment (lines 69–78, VERIFIED) says
  narrowing it to named prefixes would strand new services/methods — new methods on an
  existing service ride through untouched. `grpc_web` + `cors` filters are
  method-agnostic; the CORS header allowlist needs nothing new (no new metadata).
- **Build: none needed.** `finance.pb.*`/`finance.grpc.pb.*` regenerate from the edited
  proto via the existing custom command (`CMakeLists.txt:308–311`); the generated files
  are already in `calculator_engine`'s sources (lines 367–368). The reproducible-image
  constraint (commit `4f11e29`) is untouched — no new deps.
- **Sensen surface: none needed.** All six functions are exported from
  `sensen.financial` already (§1); `finance_service.cpp` already does
  `import sensen.financial;`.
- **Auth/entitlement:** `authenticate(ctx, "finance", method, id)` takes the method as a
  string — INFERRED no per-method registry to update (§4); `PRO_GATE_MODE=warn` means
  even a scope misconfiguration logs would-denies rather than breaking callers. Verify
  the INFERRED claim by reading `KeyRegistry::authenticate` before merging.
- **The silent-zero hazards are the real defect surface.** §1b (payoff timing zeros) and
  §1f (`value_or(0.0)`) are places sensen answers wrongly-but-plausibly on bad input.
  The handlers' refusals (§4) plus the refusal probes (§5b, §5f) are the containment;
  skipping them ships handlers that "compute a guess", violating the file's founding
  contract ("No RPC invents a figure... or silently substitutes a default").
- **Production verification must go through gRPC-Web.** Native gRPC does not survive the
  Railway ingress — `smoke_client` against `api.optionsandfuturescalculator.com:443`
  fails with `Stream removed` and no request reaches the container (CLAUDE.md,
  VERIFIED-as-documented). Verify new RPCs in production via (a) `smoke_client` run
  locally against the container / the Railway TCP proxy, and (b) a browser gRPC-Web call
  (or `curl` with `content-type: application/grpc-web-text` through the custom domain),
  confirmed in `railway logs`. A "works over native gRPC locally" result proves nothing
  about the production path.

---

## Work units

### WU-1: Extend `backend/proto/finance.proto`
- **Files:** `backend/proto/finance.proto` only.
- **Change:** add the 6 `rpc` lines (3 under Mortgages/HELOC, 3 under Real estate) and
  the 12 messages of §3, verbatim in style: section banners, field comments carrying the
  conventions (P&I-only, sentinel meanings, the double-vs-string precision comments),
  nested `ClosingCostType` enum with `PAID_IN_CASH = 0`.
- **Risks:** accidental edits to existing messages; a name collision (`FutureValue*` is
  taken — use `HomeFutureValue*`).
- **Acceptance gate:** `protoc --descriptor_set_out` before/after; diff shows only
  additions. Existing message descriptors byte-identical. `smoke_client` (unmodified)
  still passes against a rebuilt engine — proves wire compat for the existing 36.

### WU-2: Handlers in `backend/src/modules/finance_service.cpp`
- **Files:** `backend/src/modules/finance_service.cpp` only (no `.cppm` change — the
  module interface exports `RegisterFinanceService`, which is unchanged).
- **Change:** six `override` methods on `FinanceServiceImpl` following the §4 pattern
  exactly: null-check → `CHARGE` (§6 costs) → scalar refusals (§4 table) →
  `READ_DECIMAL` per string field → sensen call → serialize (`.to_string()` /
  `.to_double()` per §2). Extend `check_term`-style guards for the month/year ceilings.
  Explicit enum switch for `ClosingCostType` with a `default:` refusal.
- **Risks:** a missing `CHARGE` (unmetered RPC); a missing refusal shipping the §1b/§1f
  silent zeros; `static_cast`ing the enum instead of switching.
- **Acceptance gate:** grep gate — every new method body contains exactly one
  `CHARGE(`; build clean; the WU-3 smoke additions pass, including all refusal probes.

### WU-3: Extend `check_finance` in `backend/src/smoke_client.cpp`
- **Files:** `backend/src/smoke_client.cpp` only.
- **Change:** append the §5 blocks — refinance (no-op identity, break-even definition,
  funding identity, -1 sentinel, PMI-zero), payoff timing (annuity inversion +
  interest-coverage refusal), home FV (closed-form legs + retirement identity), recast
  (pmt linearity across three calls + full-payoff edge), rent-vs-buy (independent closed
  form + invariants + monotonicity + degenerate control), home NPV (NPV(IRR)==0 with the
  seconds/365-day date convention of `financial.cppm:874`, cross-check via the existing
  `ComputeXnpv`, terminal legs), and the new refusal probes in the refusals section.
- **Risks:** date-unit mix-up in the NPV reconstruction (§5f trap); asserting exact
  string equality on double-domain fields (only the pure-BigDecimal fields may be
  compared as exact decimal strings).
- **Acceptance gate:** `smoke_client` passes against a locally running engine; then
  against the Railway TCP proxy; production verified via gRPC-Web only (§7), with the
  requests visible in `railway logs`.

### WU-4: Documentation
- **Files:** `backend/docs/` finance API doc (`docs/FINANCE_API.md` — INFERRED location
  from CLAUDE.md's reference to it; confirm path), CLAUDE.md's RPC-count sentence
  ("roughly fifty functions" absorbs +6; update the coverage list to name refinance,
  recast, payoff timing, home FV, rent-vs-buy, home NPV).
- **Risks:** none functional. Do not copy doc examples into `QUOTA_POLICY` (CLAUDE.md
  warning).
- **Acceptance gate:** doc lists the six RPCs with the same string/double annotations as
  the proto comments — a reader must not have to read the .proto to learn which fields
  are exact.

## Open items (could not determine from the tree)

1. Whether `KeyRegistry::authenticate` has any per-method behavior beyond logging —
   INFERRED not, from its signature; read the body before merging (WU-2).
2. Whether `ComputeXnpv`/`ComputeXirr` are already asserted inside `check_finance`
   (only lines 742–1114 were read in full) — §5f does not depend on it either way.
3. `xirr` non-convergence inside `calculate_home_npv` still yields a silent 0.0 that no
   handler validation can fully remove (§4 residual) — if this must be a refusal, it
   needs an upstream sensen change (return `expected` from `calculate_home_npv`), which
   is out of scope for a faithful wrapper and would need its own spec.
