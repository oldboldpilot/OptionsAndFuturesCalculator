# Backend code map: grounding, grammar and pricing
@author Olumuyiwa Oluwasanmi

> **How to read a citation in this document.** Every `path:line` here was checked
> mechanically: the file exists and the line is inside it. The line is a pointer to
> a NEIGHBOURHOOD, not a guarantee — a sample of 1,167 citations found the named
> symbol within eight lines of the cited line in about four cases in five, and the
> residue is mostly prose adjacency rather than error. Trust the path and the symbol
> name; re-grep the symbol if the line looks wrong. An attempt to auto-correct the
> residue by moving line numbers made it worse and was reverted, which is why this
> note exists instead of a tighter number.

## Scope

This document provides reference documentation for the verification, grammar, cataloguing, pricing, and unit testing modules in the backend:

- `backend/src/modules/mortgage_verification.cppm`
- `backend/src/modules/mortgage_grammar.cppm`
- `backend/src/modules/assistant_verification.cppm`
- `backend/src/modules/strategy_catalogue.cppm`
- `backend/src/modules/pricing_engine.cppm`
- `backend/src/modules/testing_framework.cppm`

---

## backend/src/modules/mortgage_verification.cppm

### Purpose
Implements mandatory, fail-closed, tri-state verification of mortgage calculation parameters emitted by the fine-tuned language model before RPC dispatch. It enforces schema membership, parameter bounds, and utterance-level value grounding via exact 15-decimal fixed-point arithmetic without linking libz3.

### Verification Gates in Execution Order

The entry point `verify_mortgage_output()` (`backend/src/modules/mortgage_verification.cppm:2850-2870`) composes five verification gates into a strict sequence:

```
[Incoming MortgageParamsInput + Utterance]
                   │
                   ▼
       ┌─────────────────────────┐
       │   G4: Absent Params     │──(No params block emitted)──► Indeterminate (NoParamsEmitted)
       └─────────────────────────┘
                   │ params_emitted == true
                   ▼
       ┌─────────────────────────┐
       │  G1: Operation Identity │──(Unknown operation id)─────► Unsafe (UnknownOperation)
       └─────────────────────────┘
                   │ is_known_operation() == true
                   ▼
       ┌─────────────────────────┐
       │     G2: Field Sets      │──(Unknown / missing field)──► Unsafe (UnknownField / MissingField)
       │ (Shape, Enums, Bools)   │──(Bad enum / boolean)──────► Unsafe (InvalidEnumValue)
       └─────────────────────────┘
                   │
                   ▼
       ┌─────────────────────────┐
       │    G5: Scope Bounds     │──(Value out of range)───────► Unsafe (OutOfRange / MalformedNumber)
       └─────────────────────────┘
                   │ verify_mortgage_params() returns Proven
                   ▼
       ┌─────────────────────────┐
       │  G3: Value Grounding    │──(Unclassifiable slot)──────► Indeterminate (Unclassified)
       │  (M0..M9 Candidate Maps)│──(Ungrounded number)────────► Unsafe (UngroundedValue)
       └─────────────────────────┘
                   │
                   ▼
                 Proven ──► [Caller Dispatches RPC]
```

1. **Gate 4 (G4) — Absent Params Check** (`backend/src/modules/mortgage_verification.cppm:2855-2862`):
   - **Proves**: The model actually emitted a `<params>` block rather than declining, giving prose, or asking a clarifying question.
   - **Enforcement**: If `input.params_emitted` is `false`, returns `Outcome::Indeterminate` with `ReasonCode::NoParamsEmitted`. Also guarded inside `MortgageParamsDomain::translate()` (`backend/src/modules/mortgage_verification.cppm:2502-2507`) and `ground_emitted_values()` (`backend/src/modules/mortgage_verification.cppm:2736-2741`).
   - **Order dependency**: Evaluated before any schema lookup. An absent block has no operation id or field set to inspect. Default values are never fabricated.

2. **Gate 1 (G1) — Closed-Vocabulary Operation Check** (`backend/src/modules/mortgage_verification.cppm:2509-2518`):
   - **Proves**: The emitted `operation` string exactly matches one of the 27 operations declared in `detail::kOperationIds`.
   - **Enforcement**: Fails with `Outcome::Unsafe` and `ReasonCode::UnknownOperation`. No fuzzy matching or renaming is performed.

3. **Gate 2 (G2) — Closed-Vocabulary Field Set per Operation**:
   - **G2a (Unknown, duplicate, or shape-mismatched fields)** (`backend/src/modules/mortgage_verification.cppm:2523-2558`): Every emitted field must be declared in `finance.proto` for that specific operation (`find_field`), must match the scalar vs. repeated declaration, must contain exactly one value if scalar, and cannot appear more than once. Violations yield `Outcome::Unsafe` with `ReasonCode::UnknownField`, `ReasonCode::ShapeMismatch`, or `ReasonCode::DuplicateField`.
   - **G2b (Missing fields)** (`backend/src/modules/mortgage_verification.cppm:2560-2616`): Every declared field of the operation must be present, except fields exempted by `kOperationExcludedFields` or `kVariantInertFields`. Missing fields yield `Outcome::Unsafe` with `ReasonCode::MissingField`.
   - **G2c (Enum and boolean constants)** (`backend/src/modules/mortgage_verification.cppm:2635-2679`): Emitted values for `SlotKind::Enumeration` must match an entry in `detail::kEnumConstants` for that proto type. `SlotKind::Boolean` values must be `"true"` or `"false"`. Invalid constants yield `Outcome::Unsafe` with `ReasonCode::InvalidEnumValue`.

4. **Gate 5 (G5) — Product-Scope Bounds** (`backend/src/modules/mortgage_verification.cppm:2407-2493`, `2681-2696`):
   - **Proves**: All numeric values parse as strict decimals and lie within safe domain limits for mortgage calculations.
   - **Enforcement**: Fails with `Outcome::Unsafe` and `ReasonCode::MalformedNumber` or `ReasonCode::OutOfRange`. Runs inside `MortgageParamsDomain::translate()`.

5. **Gate 3 (G3) — Utterance-Level Value Grounding** (`backend/src/modules/mortgage_verification.cppm:2732-2844`):
   - **Proves**: Every emitted numeric value is directly traceable to a literal in the user's prompt (or prior clarification) via admissible candidate mappings (M1..M9), or matches an exact entry in `kConventionValues`, or matches an ungroundable solver seed (`kUngroundedFields`), or matches an English cadence word (`cadence_word_grounds`).
   - **Enforcement**: Fails with `Outcome::Unsafe` and `ReasonCode::UngroundedValue` (naming the failing field, value, and nearest textual literal), or `Outcome::Indeterminate` with `ReasonCode::Unclassified` if a field's slot kind cannot be classified.
   - **Order dependency**: Runs last in `verify_mortgage_output()` (`backend/src/modules/mortgage_verification.cppm:2869`). It strictly requires G1 and G2 to have passed so that every field has a validated name and an unambiguous `SlotKind`.

### Static Tables

| Table | Type & Size | Location | Rules and Contents |
| :--- | :--- | :--- | :--- |
| `kLabelSpace` | `std::array<FieldSpec, 184>` | `mortgage_verification.cppm:197-382` | Projected from `backend/proto/finance.proto` (scoped sections: TVM, Mortgages/HELOC, Cash-Flow, Depreciation, Real Estate, excluding `ConvertInterestRate` and `ComputeFisherRate`). Maps each operation and field to its proto type and repeated flag. |
| `kOperationIds` | `std::array<std::string_view, 27>` | `mortgage_verification.cppm:384-395` | Closed set of valid operation identifiers: `ComputeAmortization`, `ComputeAmortizationBatch`, `ComputeClosingCosts`, `ComputeCumulative`, `ComputeDepreciation`, `ComputeDetailedAmortization`, `ComputeFutureValue`, `ComputeFutureValueDetailed`, `ComputeHeloc`, `ComputeHomeFutureValue`, `ComputeHomeNpv`, `ComputeInterestPayment`, `ComputeIrr`, `ComputeMortgageRecast`, `ComputeNpv`, `ComputePaybackPeriod`, `ComputePayment`, `ComputePayoffTiming`, `ComputePeriods`, `ComputePresentValue`, `ComputePrincipalPayment`, `ComputeRate`, `ComputeRefinance`, `ComputeRentVsBuy`, `ComputeRentalRoi`, `ComputeXirr`, `ComputeXnpv`. |
| `kEnumConstants` | `std::array<EnumSpec, 4>` | `mortgage_verification.cppm:407-418` | Permitted string values for the 4 enum types: `AnnuityTiming` (`END_OF_PERIOD`, `BEGINNING_OF_PERIOD`), `Component` (`INTEREST`, `PRINCIPAL`), `ClosingCostType` (`PAID_IN_CASH`, `ROLLED_INTO_LOAN`), `Method` (`STRAIGHT_LINE`, `SUM_OF_YEARS_DIGITS`, `DECLINING_BALANCE`, `MACRS`). |
| `kOperationExcludedFields` | `std::array<ExcludedField, 5>` | `mortgage_verification.cppm:1291-1303` | Fields declared on shared proto messages that specific operations do not use or where proto documents omitting for engine default seeds: `ComputeXirr.rate`, `ComputeXnpv.guess`, `ComputeRate.guess`, `ComputeXirr.guess`, `ComputeIrr.guess`. G2b does not require these to be emitted. |
| `kUngroundedFields` | `std::array<std::string_view, 1>` | `mortgage_verification.cppm:1281` | Holds `{"guess"}`. Exempt from textual grounding because Newton-Raphson starting seeds are not stated by users. G5 bounds still apply. |
| `kVariantInertFields` | `std::array<VariantInertField, 13>` | `mortgage_verification.cppm:1343-1357` | Fields on `ComputeDepreciation` not read by specific depreciation methods (derived from math signatures in `sensen`): `STRAIGHT_LINE` ignores `period`, `factor`, `recovery_period`, `year`; `SUM_OF_YEARS_DIGITS` ignores `factor`, `recovery_period`, `year`; `DECLINING_BALANCE` ignores `recovery_period`, `year`; `MACRS` ignores `salvage`, `life`, `period`, `factor`. G2b exempts these fields when the governing method matches. |
| `kConventionValues` | `std::array<ConventionValue, 38>` | `mortgage_verification.cppm:1437-1542` | Exact (field, value) pairs exempt from textual grounding: defaults for rent-vs-buy/home-NPV (`monthly_piti_and_maintenance: 0`, `loan_annual_rate: 0`, `loan_term_years: 0`, `loan_amount: 0`, `monthly_taxes_ins_maintenance: 0`, `closing_costs_buy: 0`, `selling_cost_percent: 0.06` & `0`, `annual_inflation_rate: 0`), closing cost lines (`origination_fee_percent: 0`, `discount_points_percent: 0`, `other_lender_fees: 0`, `appraisal_fee: 0`, `inspection_fee: 0`, `recording_fees: 0`, `transfer_tax_percent: 0`, `homeowners_insurance_annual: 0`, `property_tax_annual: 0`, `seller_lender_credits: 0`), prepaid interest days (`15` & `0`), cash-flow epoch (`dates: 0`), monthly cadence (`payments_per_year: 12`, `periods_per_year: 12`, `compound_frequency: 12`), fully-amortizing zero balances (`future_value: 0`, `payment: 0`), base-case zeros (`monthly_overpayment: 0`, `extra_monthly_payment: 0`, `pmi_annual_rate: 0`, `cash_out_amount: 0`, `current_pmi_monthly: 0`, `new_pmi_monthly: 0`, `annual_inflation_rate: 0`, batch elements `extra_payments: 0`, `pmi_rates: 0`), statutory PMI (`pmi_drop_off_ltv: 0.80`), and double-declining factor (`factor: 2.0`). |
| `kUnitCappedRatioFields` | `std::array<std::string_view, 5>` | `mortgage_verification.cppm:1579-1582` | Ratio fields representing proportional shares that cannot exceed 1.0 (enforced ceiling `Decimal::kScale` = 1.0): `down_payment_percent`, `origination_fee_percent`, `discount_points_percent`, `title_settlement_percent`, `transfer_tax_percent`. |
| `kMoneyFields` | `std::array<std::string_view, 48>` | `mortgage_verification.cppm:1783-1808` | Closed inventory of all 48 field names in the label space classified as `SlotKind::Money`. |

### Slot Classification (`classify_slot`) and `SlotKind`

`classify_slot(std::string_view f)` (`backend/src/modules/mortgage_verification.cppm:1739-1811`) maps proto field names to `SlotKind` via an ordered, first-match-wins rule list:

1. `detail::kEnumFields` (`timing`, `component`, `method`, `closing_cost_type`) -> `SlotKind::Enumeration` (`:1743`)
2. `detail::kBoolFields` (`discounted`) -> `SlotKind::Boolean` (`:1744`)
3. `detail::kDayFields` (`dates`, `prepaid_interest_days`) -> `SlotKind::DayOffsets` (`:1745`)
4. `detail::kDimensionlessFields` (`factor`) -> `SlotKind::Dimensionless` (`:1746`)
5. `detail::kFrequencyFields` (`payments_per_year`, `periods_per_year`, `compound_frequency`) -> `SlotKind::Frequency` (`:1751`)
6. `detail::kPeriodIndexFields` (`period`, `start_period`, `end_period`, `year`) -> `SlotKind::PeriodIndex` (`:1757`)
7. `detail::kMonthCountFields` (`periods`, `term_months`) or suffix `_months` -> `SlotKind::MonthCount` (`:1759-1761`)
8. `detail::kYearCountFields` (`years`, `life`, `recovery_period`) or suffix `_years` -> `SlotKind::YearCount` (`:1762-1764`)
9. Contains substring `"ltv"` or suffix `_percent` -> `SlotKind::Ratio` (`:1769-1771`)
10. Suffix `rate`, `rates`, `appreciation`, `_increase`, `_return`, or equal to `"guess"` -> `SlotKind::Rate` (`:1772-1776`)
11. Matches any of the 48 entries in `detail::kMoneyFields` -> `SlotKind::Money` (`:1783-1809`)
12. Fallthrough -> `SlotKind::Unclassified` (`:1810`), which causes `MortgageParamsDomain::translate()` and `ground_emitted_values()` to return `Outcome::Indeterminate`.

### Grounding Candidate Maps (M0..M9)

When checking a numeric emitted value against a lexed literal `v`, `expand_candidates()` (`backend/src/modules/mortgage_verification.cppm:1861-1989`) and `expand_netted_loan()` (`:2024-2082`) admit candidate values according to slot kind and literal tag:

| Map | Transformation | Target SlotKind & Preconditions | Description |
| :--- | :--- | :--- | :--- |
| **M0** | Suppresses candidates | `SlotKind::Rate` when `lit.names_down_payment == true` (`:1881`) | Subtractive rule: prevents a down payment percentage from grounding an interest rate slot. |
| **M1** | $v$ (identity) | Any kind compatible with `tag_admissible(kind, lit.tag)` (`:1898, 1912, 1955, 1962, 1967, 1974`) | Verbatim numeric value. Suppressed for `LiteralTag::Percent` on `Rate`/`Ratio` slots (`:1912`), and suppressed for `LiteralTag::Years` on `MonthCount` (`:1955`). |
| **M2** | $v / 100$ | `SlotKind::Rate`, `SlotKind::Ratio` (`:1914`) | Admitted if `lit.tag == LiteralTag::Percent` or untagged within whole-number percent range $[1, 30)$ (`:1903-1907`). |
| **M3** | $v / n$, $(v/100) / n$ for $n \in \{1, 2, 4, 12, 26, 52\}$ | `SlotKind::Rate` restricted to fields named `rate` or `guess` (`:1918-1928`) | Converts annual percentage rates to per-period rates for specific TVM slots documented as per-period. |
| **M4** | $v \times 10^3$, $v \times 10^6$ | `SlotKind::Money` when literal has suffix scale $10^3$ or $10^6$ (`:1891-1894`) | Replaces the unscaled value when a suffix (`k`, `million`) was parsed. |
| **M5** | $v \times \text{periods\_per\_year}$ | `SlotKind::MonthCount`, tag in `{Years, Untagged}` (`:1956-1958`) | Converts duration in years to period counts using inferred cadence. |
| **M6** | $v / 12$ (exact integer division) | `SlotKind::YearCount`, `lit.tag == LiteralTag::Months` (`:1963`) | Converts whole month counts to years (e.g. 360 months -> 30 years; 361 months is rejected). |
| **M7** | $v \times 365$ | `SlotKind::DayOffsets`, `lit.tag == LiteralTag::Years` (`:1968`) | Converts duration in years to day offsets for cash flows. |
| **M8** | $-v$ (negated) | Restricted to repeated field `values` (`:1984-1988`) | Encodes cash-flow outlays as negative amounts. |
| **M9** | $p - d$ or $p \times (1 - \text{pct}/100)$ | `SlotKind::Money` restricted to fields `present_value` or `loan_amount` (`:2024-2082`) | Binary netting map: pairs a property price $p$ with a literal $d$ or $\text{pct}$ flagged with `names_down_payment`. |

#### Opposing Forces on Down-Payment Percentages: M0 vs M9 and M2
When a prompt contains a phrase like "a $500,000 home with 20% down at 6.5%":
- **M0** acts subtractively on `SlotKind::Rate`: it detects that `20%` is adjacent to "down" (`lit.names_down_payment == true`), and unconditionally purges `0.20` from the candidate pool for rate slots (`backend/src/modules/mortgage_verification.cppm:1881`). This prevents the model from setting `annual_rate = 0.2000` and pricing a 20% loan.
- **M9** and **M2** pull in the opposite direction: M9 consumes that exact `20%` down payment and pairs it with `$500,000` to admit $500{,}000 \times (1 - 0.20) = 400{,}000$ for `loan_amount` (`:2054-2079`), while M2 admits `0.20` for `down_payment_percent` because `down_payment_percent` is classified as `SlotKind::Ratio`, which is exempt from M0 (`:1876-1881`).

### Cadence Inference (`infer_periods_per_year`)

`infer_periods_per_year` (`backend/src/modules/mortgage_verification.cppm:2103-2149`) inspects the emitted per-period rate (`rate` or `guess`) and compares it against annual percentages in the user text divided by $n \in \{1, 2, 4, 12, 26, 52\}$. If the emitted rate matches exactly one cadence $n$ within rate tolerance (5e-7), it returns $n$. If ambiguous or unrecognised, it returns `std::nullopt` (which defaults to 12 at line 2750).

**Why periods are not grounded against a hardcoded cadence**:
In general TVM operations (`ComputeFutureValue`, `ComputePayment`), loans and investments can be annual ($n=1$), quarterly ($n=4$), or monthly ($n=12$). If cadence were hardcoded to 12:
- An annual problem stating "5% for 10 years" has `periods = 10`. A fixed $\times 12$ rule would admit only 120 and reject 10.
- Conversely, an annual rate paired with a hallucinated monthly `periods = 360` would be accepted under a hardcoded $\times 12$ rule. Inferring the cadence directly couples `periods` to the division applied to `rate`.

### TVM Sign Inversion (`tvm_payment_needs_sign_flip`)

`tvm_payment_needs_sign_flip` (`backend/src/modules/mortgage_verification.cppm:1386-1405`):
```cpp
auto tvm_payment_needs_sign_flip(std::string_view operation,
                                 std::string_view present_value,
                                 std::string_view payment,
                                 std::string_view future_value) -> bool;
```
- **Exact Condition** (`:1390, 1401-1404`):
  1. `operation` is exactly `"ComputeRate"` or `"ComputePeriods"`, AND
  2. `sign_of(future_value) == 0` (future value is zero), AND
  3. `pv != 0` and `pmt != 0` and `pv == pmt` (present value and payment share the same sign).
- **Scope**: Restricted to `ComputeRate` and `ComputePeriods` solving $\text{PV}(1+r)^n + \text{PMT} \cdot a(r,n) + \text{FV} = 0$. When $\text{FV} = 0$, a mathematical root exists only when $\text{PV}$ and $\text{PMT}$ have opposite signs. Flipping one sign is a pure translation because the formula is homogeneous, whereas with a non-zero future value (e.g. savings goals), same-signed legs can be mathematically valid.

### Verdict Tri-State (`Outcome`)

The verification engine defines three verdict outcomes (`backend/src/modules/mortgage_verification.cppm:712`):

| Verdict | Definition | Caller Action |
| :--- | :--- | :--- |
| `Outcome::Proven` | All gates passed without contradiction or missing information. | **The ONLY outcome allowing RPC dispatch.** |
| `Outcome::Unsafe` | A gate detected a definite contradiction, malformed number, missing field, or ungrounded value. | Refuse request with specific refusal detail. |
| `Outcome::Indeterminate` | A rule could not be evaluated (e.g. G4 absent params block, or `SlotKind::Unclassified`). | Refuse request. `VerificationVerdict` default-constructs to `Indeterminate` (`:715`) to fail closed on unexpected exits. |

### Exported Symbol Inventory

| Symbol | Signature / Type | Description |
| :--- | :--- | :--- |
| `FieldSpec` | `struct` (`:183-188`) | Represents an operation field, its proto type name, and whether it is repeated. |
| `operation_ids` | `() -> std::vector<std::string_view>` (`:424`) | Returns all 27 valid operation identifiers. |
| `fields_of` | `(std::string_view) -> std::vector<FieldSpec>` (`:429`) | Returns field specifications for an operation in proto declaration order. |
| `is_known_operation` | `(std::string_view) -> bool` (`:433`) | Gate G1: true if operation is in `detail::kOperationIds`. |
| `find_field` | `(std::string_view, std::string_view) -> const FieldSpec*` (`:442-443`) | Gate G2: looks up a field on a specific operation. |
| `Decimal` | `class` (`:468-538`) | Exact 15-decimal fixed-point arithmetic class backed by `__int128`. |
| `parse_strict_decimal` | `(std::string_view) -> std::optional<Decimal>` (`:555`) | Parses decimals conforming to `-?[0-9]{1,15}(\.[0-9]{1,18})?`. |
| `SlotKind` | `enum class` (`:580-593`) | Category of numeric slot (`Money`, `Rate`, `Ratio`, `MonthCount`, etc.). |
| `classify_slot` | `(std::string_view) -> SlotKind` (`:598`) | Classifies field name into `SlotKind`. |
| `EmittedField` | `struct` (`:610-614`) | Name and raw string values of an emitted parameter field. |
| `MortgageParamsInput` | `struct` (`:620-629`) | Container for model output: `params_emitted`, `operation`, `fields`. |
| `ReasonCode` | `enum class` (`:639-652`) | Granular refusal reason codes (12 values). |
| `operation_excludes_field` | `(std::string_view, std::string_view) -> bool` (`:683-684`) | True if field is in `kOperationExcludedFields`. |
| `variant_governing_field` | `(std::string_view) -> std::string_view` (`:688`) | Returns enum field governing method variant (e.g. `"method"`). |
| `tvm_payment_needs_sign_flip` | `(std::string_view, std::string_view, std::string_view, std::string_view) -> bool` (`:693-696`) | Checks if PV and PMT have identical signs with FV=0. |
| `field_is_inert_for_variant` | `(std::string_view, std::string_view, std::string_view) -> bool` (`:701-703`) | Checks if variant ignores field in `kVariantInertFields`. |
| `VerificationFacts` | `struct` (`:705-710`) | Constraint status: `violated`, `incomplete`, `reason`, `detail`. |
| `Outcome` | `enum class` (`:712`) | Tri-state: `Proven`, `Unsafe`, `Indeterminate`. |
| `VerificationVerdict` | `struct` (`:714-718`) | Composite result: `outcome`, `reason`, `message`. |
| `MortgageParamsDomain` | `class` (`:738-744`) | Implements GP-ARA `DomainPolicy` for structural parameters. |
| `RuleBasedReasoner` | `class` (`:752-785`) | Implements GP-ARA `ReasonerPolicy` via direct evaluation. |
| `verify_mortgage_params` | `(const MortgageParamsInput&) -> VerificationVerdict` (`:804-805`) | Runs structural gates G1 + G2 + G5. |
| `LiteralTag` | `enum class` (`:1041`) | Adjacency tag: `Untagged`, `Money`, `Percent`, `Years`, `Months`, `Days`. |
| `NumericLiteral` | `struct` (`:1047-1076`) | Parsed literal text, `Decimal` value, `LiteralTag`, scale, offset, `names_down_payment`. |
| `lex_numeric_literals` | `(std::string_view) -> std::vector<NumericLiteral>` (`:1092`) | Extracts tagged numeric literals from free-form prompt text. |
| `ground_emitted_values` | `(const MortgageParamsInput&, std::string_view) -> VerificationVerdict` (`:1114-1115`) | Gate G3: grounds emitted numbers against prompt text. |
| `verify_mortgage_output` | `(const MortgageParamsInput&, std::string_view) -> VerificationVerdict` (`:1139-1140`) | Composed entry point running G4, then G1/G2/G5, then G3. |
| `to_string(ReasonCode)` | `(ReasonCode) -> std::string_view` (`:1143`) | String representation of reason code. |
| `to_string(Outcome)` | `(Outcome) -> std::string_view` (`:1146`) | String representation of outcome. |

### Invariants, Refusals and Failure Modes

- `params_emitted == false` returns `Outcome::Indeterminate` with `ReasonCode::NoParamsEmitted` (`:2503-2506, 2737-2740, 2856-2861`).
- Unknown operations return `Outcome::Unsafe` with `ReasonCode::UnknownOperation` (`:2510-2518`).
- Unknown field names return `Outcome::Unsafe` with `ReasonCode::UnknownField` (`:2525-2532`).
- Repeated scalar fields or empty repeated fields return `Outcome::Unsafe` with `ReasonCode::ShapeMismatch` (`:2534-2547`).
- Duplicate field emissions return `Outcome::Unsafe` with `ReasonCode::DuplicateField` (`:2548-2557`).
- Undeclared missing fields return `Outcome::Unsafe` with `ReasonCode::MissingField` (`:2608-2615`).
- Unrecognised enum values return `Outcome::Unsafe` with `ReasonCode::InvalidEnumValue` (`:2658-2665`).
- Malformed decimals return `Outcome::Unsafe` with `ReasonCode::MalformedNumber` (`:2684-2688`).
- Numbers violating magnitude bounds return `Outcome::Unsafe` with `ReasonCode::OutOfRange` (`:2691-2695`):
  - Money $> 10^{10}$ units (`kMaxMoneyUnits`, `:1552`) or negative for fields other than `values` (`:2424-2426`).
  - Rates $> 30\%$ (`kMaxRateUnits`, `:1554`) or negative (`:2429-2430`).
  - General ratios $> 150\%$ (`kMaxRatioUnits`, `:1556`) or negative (`:2432`).
  - Unit-capped ratios $> 1.0$ (`kUnitCappedRatioFields`, `:2436-2440`).
  - Month counts $> 1200$ or non-positive (except `tax_escrow_months`, which allows 0) (`:2455-2458`).
  - Year counts $> 100$ or non-positive (except `loan_term_years`, which allows 0) (`:2473-2476`).
  - Period index $\le 0$ or $> 1200$ (`:2478-2479`).
  - Day offsets $< 0$ or $> 36,525$ (`:2481-2482`).
- Values not derived from prompt literals return `Outcome::Unsafe` with `ReasonCode::UngroundedValue` (`:2830-2837`).
- Unclassified field names return `Outcome::Indeterminate` with `ReasonCode::Unclassified` (`:2628-2633, 2756-2760`).

### Gotchas
- **No repair policy**: The verifier never converts aliases, fixes spelling, or adjusts numbers. Any deviation from the schema is rejected (`:111-127`).
- **Precision bounds**: Tolerances are fixed at 0.005 for Money, 5e-7 for Rate/Ratio, and 0 for integer counts/indices (`:1817-1827`).
- **Trailing zeroes in Decimal**: `Decimal` compares values numerically; `"0.00"` equals `"0"`. However, string formatting removes trailing zeroes after the decimal point (`:1650`).
- **Sign preservation**: The lexer preserves negative signs. An emitted positive loan amount cannot ground against a negative literal (`:2308-2325`).

---

## backend/src/modules/mortgage_grammar.cppm

### Purpose
Provides a proto-derived constrained decoding grammar and finite state automaton that guarantees the language model can only generate syntactically well-formed JSON matching the exact operation schema inside `<params>` tags, rendering structural defects unrepresentable.

### Exported Symbol Inventory

| Symbol | Signature / Type | Description |
| :--- | :--- | :--- |
| `ValueForm` | `enum class` (`:212-217`) | Wire representation: `DecimalString` (quoted), `Number` (bare), `Boolean`, `EnumConstant`. |
| `ValueShape` | `struct` (`:221-228`) | Field shape constraints: form, repeated, allow_sign, allow_fraction, enum_index. |
| `FieldPlan` | `struct` (`:233-236`) | Tuple of field name and its corresponding `ValueShape`. |
| `EnumTable` | `struct` (`:240-243`) | Type name and array of allowed constants for an enum. |
| `kAnnuityTimingConstants` | `array<string_view, 2>` (`:255-256`) | `"END_OF_PERIOD"`, `"BEGINNING_OF_PERIOD"`. |
| `kComponentConstants` | `array<string_view, 2>` (`:257-258`) | `"INTEREST"`, `"PRINCIPAL"`. |
| `kClosingCostTypeConstants` | `array<string_view, 2>` (`:259-260`) | `"PAID_IN_CASH"`, `"ROLLED_INTO_LOAN"`. |
| `kMethodConstants` | `array<string_view, 4>` (`:261-262`) | `"STRAIGHT_LINE"`, `"SUM_OF_YEARS_DIGITS"`, `"DECLINING_BALANCE"`, `"MACRS"`. |
| `kEnumTables` | `array<EnumTable, 4>` (`:264-269`) | Aggregate of all four enum constant tables. |
| `GrammarOptions` | `struct` (`:275-288`) | Flags: `wrap_in_params_tags` (default true), `require_declaration_order` (default true). |
| `Schema` | `class` (`:301-357`) | Compiled representation of operations, field sequences, and value shapes derived from `mortgage_verification`. |
| `default_schema` | `() -> const expected<Schema, string>&` (`:363`) | Singleton accessor for default wrapped, ordered schema. |
| `validate_label_space` | `(const Schema&) -> expected<void, string>` (`:376-377`) | Drift check verifying schema against `mortgage_verification`'s label space. |
| `ParamsAutomaton` | `class` (`:395-486`) | Character-level state machine tracking valid transitions through JSON parameter blocks. |
| `MortgageParamsGrammar` | `class` (`:506-542`) | Implementation of `sensen::IGrammar` driving token masking during LLM sampling. |
| `params_regex` | `(const Schema&) -> expected<string, string>` (`:564`) | Compiles schema into a regex string supported by `sensen::RegexNfa`. |

### Automaton Structure and Phases

`ParamsAutomaton` (`backend/src/modules/mortgage_grammar.cppm:395-486`) maintains a lightweight, trivially copyable `State` struct (`:457-470`). Character transitions advance through distinct phases (`Phase`, `:436-448`):

1. `Phase::Prelude`: Matches `<params>{"operation":"` (or `{"operation":"` when unwrapped).
2. `Phase::Name`: Consumes characters of operation ids, field keys, or enum constants.
3. `Phase::KeyOpen`: Consumes the opening `"` of a field key.
4. `Phase::KeyColon`: Consumes the `:` separating key and value.
5. `Phase::ValueStart`: Determines value format and opening delimiter (`[`, `"`, digits, sign, or boolean start).
6. `Phase::Number`: Consumes numeric digits, signs, and decimals through sub-phases (`NumPhase`: `Start`, `Sign`, `Zero`, `Int`, `Dot`, `Frac`, `:450`).
7. `Phase::BoolLiteral`: Consumes `true` or `false`.
8. `Phase::AfterElement`: Consumes `,` or `]` inside array lists.
9. `Phase::SepOrClose`: Consumes `,` between fields or `}` closing the JSON object.
10. `Phase::Postlude`: Matches `</params>`.
11. `Phase::Done`: Terminal state; no further characters admitted.

### Numeric Literal Lexer, Tags, and Downstream Decisions

While `mortgage_grammar.cppm` enforces the output JSON syntax during model generation, the free-text numeric literal lexer that feeds verification resides in `backend/src/modules/mortgage_verification.cppm:2242-2399` (`lex_numeric_literals`). The downstream decisions governed by these tags are:

1. **Literal Tags** (`LiteralTag`, `mortgage_verification.cppm:1041`):
   - `Untagged`: Raw number without explicit units or currency symbols.
   - `Money`: Prefixed with `$` (`:2255-2260`) or followed by currency words (`dollars`, `dollar`, `usd`, `:2355`), or scaled by `k`/`thousand` ($10^3$, `:2337, 2350`) or `m`/`million` ($10^6$, `:2343, 2353`).
   - `Percent`: Followed immediately by `%` (`:2333`) or unit words `percent`/`pct` (`:2347-2348`).
   - `Years`: Followed by `year`, `years`, `yr`, `yrs` (`:2358-2359`).
   - `Months`: Followed by `month`, `months`, `mo`, `mos` (`:2360-2361`).
   - `Days`: Followed by `day`, `days` (`:2362-2363`).
2. **Tag Admissibility (`tag_admissible`)** (`mortgage_verification.cppm:1834-1856`):
   - `SlotKind::Money` admits only `Money` and `Untagged` literals.
   - `SlotKind::Rate` and `SlotKind::Ratio` admit only `Percent` and `Untagged` literals.
   - `SlotKind::MonthCount` and `PeriodIndex` admit `Months`, `Years`, and `Untagged`.
   - `SlotKind::YearCount` admits `Years`, `Months`, and `Untagged`.
   - `SlotKind::Frequency` and `Dimensionless` admit only `Untagged`.
   - `SlotKind::DayOffsets` admits `Days`, `Years`, and `Untagged`.
3. **Down-Payment Adjacency Detection**:
   `lex_numeric_literals` inspects words following a literal (`:2373-2393`). If followed by `down`, `downpayment`, `deposit`, or filler phrases like `as a down payment`, sets `lit.names_down_payment = true`. This flag triggers the subtractive refusal M0 (`:1881`) and activates binary netting M9 (`:2044`).

### Invariants, Refusals and Failure Modes

- `Schema::build()` returns an error string if `mortgage_verification` returns an empty operation set, more than 64 operations, more than 32 fields for any operation (`kMaxFieldsPerOperation`, `:342`), or an unsupported proto type (`:638-642, 654-672`).
- Negative signs are permitted solely for fields named `values` (`:608`). Any negative sign on other fields causes `step_number()` to reject the character (`:1003`).
- Floating point fractions are disallowed on `int32` fields unless classified as `YearCount` (such as `recovery_period` at 27.5 years) (`:633-635`).
- `params_regex()` returns an error if `require_declaration_order` is disabled, refusing to generate a permutation blow-up regex (`:1212-1216`).

### Gotchas
- **Trigger-activation required**: The grammar starts matching from `<params>`. The decoding pipeline must run unconstrained through `<think>` blocks until `<params>` is encountered, then prime and attach the grammar (`:178-190, 524-530`).
- **Performance difference**: Driving sampling through `ParamsAutomaton::allowed_next_chars()` takes $\approx 0.25$ ms per step, whereas compiling to `params_regex()` and using `sensen::RegexGrammar` takes $\approx 5.39$ ms per step due to dynamic bitset allocations across large vocabularies (`:107-113`).

---

## backend/src/modules/assistant_verification.cppm

### Purpose
Implements mandatory, fail-closed cross-field verification for options and futures strategy assistant parameters. It ensures mutual consistency across symbol, asset class, strategy catalogue identity, and expiration structure, and provides input filtering for prompt injection, financial advice, and bare futures order recovery.

### Exported Symbol Inventory

| Symbol | Signature / Type | Description |
| :--- | :--- | :--- |
| `AssistantParamsInput` | `struct` (`:98-107`) | Input container: `symbol`, `asset_class`, `strategy`, `expiration_days`, `quantity`, `far_expiration_days`. |
| `ReasonCode` | `enum class` (`:114-119`) | Refusal mapping: `None`, `UnsupportedStrategy`, `UnknownSymbol`, `OutOfScope`. |
| `VerificationFacts` | `struct` (`:124-129`) | Evaluation facts: `violated`, `incomplete`, `reason`, `detail`. |
| `AssetClassSignal` | `enum class` (`:234`) | Trader signal: `None`, `Futures`, `Equity`. |
| `AmbiguousRootInfo` | `struct` (`:244-249`) | Ambiguity details: `symbol`, `futures_label`, `equity_label`, `equity_keyword`. |
| `find_ambiguous_root_info` | `(string_view) -> const AmbiguousRootInfo*` (`:394`) | Checks if root is in `detail::kAmbiguousRootDetails` (`"ES"`, `"CL"`). |
| `detect_asset_class_signal` | `(string_view, string_view) -> AssetClassSignal` (`:434-435`) | Resolves ambiguous root asset class from utterance keywords. |
| `build_ambiguity_clarification` | `(string_view, string_view) -> optional<string>` (`:490-491`) | Generates user clarification prompt for unresolved ambiguous roots. |
| `normalize_strategy_alias` | `(string_view) -> optional<string>` (`:609`) | Normalises transposed two-token strategy names (e.g. `long_futures` -> `futures_long`). |
| `AssistantParamsDomain` | `class` (`:631-799`) | GP-ARA `DomainPolicy` evaluating cross-field rules over 5 parameters. |
| `RuleBasedReasoner` | `class` (`:812-850`) | Direct evaluation `ReasonerPolicy` returning `expected<bool, ReasonerError>`. |
| `Outcome` | `enum class` (`:861`) | Tri-state: `Proven`, `Unsafe`, `Indeterminate`. |
| `VerificationVerdict` | `struct` (`:863-867`) | Output verdict: `outcome`, `reason`, `message`. |
| `verify_assistant_params` | `(const AssistantParamsInput&) -> VerificationVerdict` (`:882-883`) | Main verification entry point. |
| `strategy_has_lexical_support` | `(string_view, string_view) -> bool` (`:1001-1002`) | True if prompt contains at least one non-generic token from strategy id. |
| `is_unsupported_bare_direction_guess` | `(string_view, string_view) -> bool` (`:1044-1045`) | Detects hallucinated `long_call`/`long_put` without prompt lexical support. |
| `infer_ambiguous_root_asset_class` | `(string_view, string_view, int64_t, int64_t, string_view) -> optional<string>` (`:1121-1124`) | Infers `"FUTURES"` via strategy category constraint and lexical support. |
| `looks_like_prompt_injection` | `(string_view) -> bool` (`:1239`) | Detects system override and prompt extraction attacks. |
| `looks_like_advice_request` | `(string_view) -> bool` (`:1253`) | Detects requests asking for market predictions or trade recommendations. |
| `recover_bare_futures_directive` | `(string_view) -> optional<string>` (`:1361-1362`) | Deterministically reconstructs parameters for directional futures orders. |
| `ExerciseType` | `enum class` (`:1436`) | `EUROPEAN = 0`, `AMERICAN = 1`, `BERMUDAN = 2`. |
| `AsianType` | `enum class` (`:1440`) | `NOT_ASIAN = 0`, `AVERAGE_PRICE = 1`, `AVERAGE_STRIKE = 2`. |
| `ExerciseAsianExtraction` | `struct` (`:1447-1450`) | Container holding extracted `exercise_type` and `asian_type`. |
| `extract_exercise_and_asian` | `(string_view) -> ExerciseAsianExtraction` (`:1697`) | Extracts exercise style and Asian averaging options from prompt. |

### Invariants, Refusals and Failure Modes

- `symbol` not matching alphanumeric ticker grammar yields `Outcome::Unsafe` with `ReasonCode::UnknownSymbol` (`:640-645`).
- `asset_class` not in `{"EQUITY", "FUTURES", "CRYPTO"}` yields `Outcome::Unsafe` with `ReasonCode::UnknownSymbol` (`:651-656`).
- Strategy matching `kAssistantForbiddenStrategies` (`"crack_321"`) yields `Outcome::Unsafe` with `ReasonCode::UnsupportedStrategy` (`:659-666`).
- Unknown strategy yields `Outcome::Unsafe` with `ReasonCode::UnsupportedStrategy` (`:669-676`).
- Multi-expiry strategy with `far_expiration_days <= expiration_days` (and non-zero) yields `Outcome::Unsafe` with `ReasonCode::UnsupportedStrategy` (`:697-705`).
- Single-expiry strategy with non-zero `far_expiration_days` yields `Outcome::Unsafe` with `ReasonCode::UnsupportedStrategy` (`:709-715`).
- Cross-field category mismatches yield `Outcome::Unsafe` with `ReasonCode::UnsupportedStrategy`:
  - `category == "Futures"` paired with `asset_class != "FUTURES"` (`:719-727`).
  - Options category (`Bullish`, `Bearish`, `Neutral`, `Volatility`, `Income & Hedge`) paired with `asset_class == "FUTURES"` (`:728-737`).
- Unmapped strategy category in catalogue yields `Outcome::Indeterminate` with `ReasonCode::OutOfScope` (`:746-750`).
- Known non-ambiguous futures root paired with non-futures asset class, or known crypto symbol paired with non-crypto asset class, yields `Outcome::Unsafe` with `ReasonCode::UnknownSymbol` (`:762-777`). Ambiguous roots (`"ES"`, `"CL"`) are excluded from this static check.
- Quantity $< 1$ or $> 100{,}000$ yields `Outcome::Unsafe` with `ReasonCode::OutOfScope` (`:780-785`).
- Expiration days $< 1$ or $> 3{,}650$ yields `Outcome::Unsafe` with `ReasonCode::OutOfScope` (`:788-795`).

### Gotchas
- **Ambiguous root handling**: For roots in `kAmbiguousRoots` (`"ES"` and `"CL"`), `translate()` does not reject an equity or futures designation because both are valid listings (Eversource Energy / E-mini S&P, and Colgate-Palmolive / Crude Oil) (`:140-167`). Disambiguation must occur via `detect_asset_class_signal()`, prompt clarification, or live market data probing.
- **Asymmetric asset class inference**: `infer_ambiguous_root_asset_class()` only ever infers `"FUTURES"` (`:1074-1089, 1132`). It never infers `"EQUITY"` by process of elimination, because the fine-tuned model frequently defaults to equity option strategies when uncertain.
- **Capitalisation guard in exercise extraction**: When parsing exercise styles ("American"), the parser checks `next_token_is_capitalized` (`:1579-1586`). "American put" extracts as `ExerciseType::AMERICAN`, but "American Airlines put" does not, preventing ticker name collisions.

---

## backend/src/modules/strategy_catalogue.cppm

### Purpose
Contains the authoritative compile-time catalogue of 47 option and futures trading strategies supported by the calculation engine and assistant verification layers.

### Generation Mechanism and Drift Invariants

- **Generator Script**: `scripts/generate_strategy_catalogue.py` (`backend/src/modules/strategy_catalogue.cppm:3-8`).
- **Source of Truth**: `agent/dataset/strategies.json` (47 entries).
- **Drift Hazards**:
  - Hand edits to `strategy_catalogue.cppm` are overwritten whenever `generate_strategy_catalogue.py` runs.
  - Three distinct files register strategy identifiers:
    1. `frontend/src/components/StrategySelector.tsx` (Web UI list, 48 entries).
    2. `agent/dataset/strategies.json` (Fine-tuning dataset, 47 entries).
    3. `backend/src/modules/strategy_catalogue.cppm` (Backend validator, 47 entries).
  - The frontend defines a 48th strategy: `"crack_321"` ("3-2-1 Crack Spread", category Futures). Because `"crack_321"` is absent from `strategies.json`, `strategy_catalogue.cppm` contains 47 entries and rejects `"crack_321"` (`:30-40`). `assistant_verification.cppm` explicitly blocks it via `kAssistantForbiddenStrategies` (`backend/src/modules/assistant_verification.cppm:179`).

### Exported Symbol Inventory

| Symbol | Signature / Type | Description |
| :--- | :--- | :--- |
| `StrategyInfo` | `struct` (`:58-65`) | Metadata: `id`, `name`, `category`, `description`, `leg_count`, `multi_expiry`. |
| `kCatalogue` | `inline constexpr array<StrategyInfo, 47>` (`:67-115`) | Complete internal static array of catalogued strategies. |
| `all` | `() noexcept -> span<const StrategyInfo>` (`:118-120`) | Returns span view over `kCatalogue`. |
| `count` | `() noexcept -> size_t` (`:123-125`) | Returns the number of strategies (47). |
| `find` | `(string_view) noexcept -> const StrategyInfo*` (`:128-132`) | Looks up strategy by exact wire id; returns `nullptr` if unknown. |
| `is_known` | `(string_view) noexcept -> bool` (`:140-142`) | Returns `true` if strategy exists in catalogue. |

### Invariants, Refusals and Failure Modes

- Any strategy id not present in `kCatalogue` causes `is_known()` to return `false` and `find()` to return `nullptr` (`:128-142`).
- Callers must reject unknown strategy ids rather than approximating them to nearest neighbours (`:50-54`).

### Gotchas
- `multi_expiry` is `true` for exactly 5 strategies: `calendar_spread`, `diagonal_spread`, `double_diagonal`, `pmcc`, and `futures_calendar` (`:99-101, 106, 109`). All other 42 strategies have `multi_expiry == false`.

---

## backend/src/modules/pricing_engine.cppm

### Purpose
Exposes strategy calculation and parametric risk simulation entry points for multi-leg option and futures strategies (`calculator.engine`), delegating option pricing and Greeks to `sensen`.

### Exposed Models and Sensen Entry Points

1. **Black-Scholes-Merton Pricing & Greeks**:
   - For option legs (`Type::CALL` and `Type::PUT`), calls `sensen::price_black_scholes(S, K, r, sigma, T, opt_type)` (`backend/src/modules/pricing_engine.cppm:132, 179, 182`).
   - Sensen returns `.value`, `.delta`, `.gamma`, `.theta`, `.vega`, `.rho`. Costs and Greeks are scaled by quantity and standard contract multiplier (100.0).
2. **Linear Asset Valuation**:
   - `Type::STOCK`: Evaluated linearly with $\Delta = 1.0 \times \text{sign} \times q$, cost calculated per share (`:142-146, 185-187`).
   - `Type::FUTURE`: Evaluated linearly with $\Delta = 1.0 \times \text{sign} \times q$, terminal PnL scaled by multiplier 100.0 (`:147-151, 188-192`).
3. **Parametric Value-at-Risk (VaR) & Conditional VaR (CVaR)**:
   - Evaluated by `calculate_risk(total_strategy_cost, implied_volatility)` (`:87-104`).
   - Computes parametric 95% ($Z = 1.64485$) and 99% ($Z = 2.32635$) normal distribution risk:
     $$\text{VaR}_{95} = \text{Cost} \cdot Z_{95} \cdot \sigma, \quad \text{VaR}_{99} = \text{Cost} \cdot Z_{99} \cdot \sigma$$
     $$\text{CVaR}_{95} = \text{Cost} \cdot \left(\frac{\phi(Z_{95})}{0.05}\right) \cdot \sigma, \quad \text{CVaR}_{99} = \text{Cost} \cdot \left(\frac{\phi(Z_{99})}{0.01}\right) \cdot \sigma$$
     where $\phi(Z_{95}) = 0.103135$ and $\phi(Z_{99}) = 0.026652$.
4. **PnL Matrix Simulation**:
   - Simulates strategy terminal payoff across 50 steps spanning $\pm 2$ standard deviations ($S \pm 2 \cdot S \cdot \sigma$) (`:158-200`). Computes `max_profit`, `max_loss`, and Probability of Profit (`pop`).

### Exported Symbol Inventory

| Symbol | Signature / Type | Description |
| :--- | :--- | :--- |
| `Action` | `enum class` (`:22`) | `BUY = 0`, `SELL = 1`. |
| `Type` | `enum class` (`:23`) | `CALL = 0`, `PUT = 1`, `FUTURE = 2`, `STOCK = 3`. |
| `Leg` | `struct` (`:25-31`) | `action`, `type`, `strike`, `expiration_days`, `quantity`. |
| `StrategyRequest` | `struct` (`:33-39`) | `underlying_symbol`, `current_price`, `implied_volatility`, `risk_free_rate`, `legs`. |
| `Greeks` | `struct` (`:41-47`) | `delta`, `gamma`, `theta`, `vega`, `rho`. |
| `PnLPoint` | `struct` (`:49-52`) | `underlying_price`, `pnl`. |
| `RiskMetrics` | `struct` (`:54-59`) | Parametric 95/99 VaR and CVaR metrics. |
| `StrategyResponse` | `struct` (`:61-70`) | Net metrics: `max_profit`, `max_loss`, `break_even`, `expected_value`, `pop`, `net_greeks`, `risk_metrics`, `pnl_matrix`. |
| `ErrorCode` | `enum class` (`:72-79`) | `INVALID_PRICE`, `INVALID_VOLATILITY`, `INVALID_RATE`, `INVALID_DAYS`, `SIMD_COMPUTATION_ERROR`, `UNKNOWN_ERROR`. |
| `EngineError` | `struct` (`:81-84`) | `ErrorCode code`, `std::string message`. |
| `calculate_strategy` | `(const StrategyRequest&) -> expected<StrategyResponse, EngineError>` (`:106-207`) | Evaluates multi-leg strategy pricing, net Greeks, VaR, and PnL matrix. |

### Invariants, Refusals and Failure Modes

- `req.current_price <= 0.0` returns `unexpected` with `ErrorCode::INVALID_PRICE` (`:107`).
- `req.implied_volatility <= 0.0` returns `unexpected` with `ErrorCode::INVALID_VOLATILITY` (`:108`).

### Gotchas
- **Global `operator new` anchor**: The module includes `<new>` in its global module fragment (`:1-12`). This is required to prevent ambiguous `operator new` linker errors caused by Intel TBB imported transitively through `sensen.options`.
- **Test-only linkage**: `pricing_engine.cppm` is linked into `test_runner`, while production services dispatch option pricing directly through `calculator_service.cpp` and `sensen` (`backend/CMakeLists.txt:1194, 1310`).

---

## backend/src/modules/testing_framework.cppm

### Purpose
Defines unit test assertion helpers and self-test harness utilities for the pricing engine (`calculator.testing`).

### Assertion Helpers and Conventions

- `assert_true(bool condition, std::string_view msg) -> TestResult` (`backend/src/modules/testing_framework.cppm:22-28`):
  Returns a `TestResult` struct containing `passed = condition` and a formatted message:
  `"PASS: <msg>"` when true, or `"FAIL: <msg>"` when false.
- `run_all_tests() -> TestResult` (`:30-52`):
  Constructs an AAPL 1-leg Long Call request ($S=150, K=155, \sigma=0.20, r=0.05, T=30$) and calls `calculator::calculate_strategy(req)`.
- **Repo-wide Standalone Test Assertion Convention**:
  Test drivers (`test_mortgage_verification.cpp`, `test_mortgage_grammar.cpp`, `test_assistant_verification.cpp`) do not link GTest or CppUnit. They use file-local `check()` helpers:
  ```cpp
  auto check(bool condition, const std::string& what) -> void {
      ++g_checks;
      if (condition) {
          std::printf("  PASS: %s\n", what.c_str());
      } else {
          ++g_failures;
          std::printf("  FAIL: %s\n", what.c_str());
      }
  }
  ```
  `main()` returns `g_failures == 0 ? 0 : 1`.
- **Skipped Test Return Code Convention**:
  When a test target detects missing dependencies or environment prerequisites, it exits with return code `77`. This matches CTest's standard convention for a skipped test (`backend/tests/test_strategy_store_pg.cpp:96`: `return 77; // ctest's conventional "skipped"`).

### Exported Symbol Inventory

| Symbol | Signature / Type | Description |
| :--- | :--- | :--- |
| `TestResult` | `struct` (`:17-20`) | `bool passed`, `std::string message`. |
| `assert_true` | `(bool, std::string_view) -> TestResult` (`:22-28`) | Formats condition outcome into `TestResult`. |
| `run_all_tests` | `() -> TestResult` (`:30-52`) | Core regression suite evaluating standard option pricing request. |

### Gotchas
- Like `pricing_engine.cppm`, `testing_framework.cppm` carries an `#include <new>` anchor in its global module fragment (`:1-8`) to prevent global allocator collisions from TBB.

---

## Test Coverage

| Test Target / Executable | Test Source File | Exercised Modules | Primary Verified Behaviors |
| :--- | :--- | :--- | :--- |
| `test_mortgage_verification` | `backend/tests/test_mortgage_verification.cpp` | `mortgage_verification.cppm` | Re-parses `finance.proto` to verify `kLabelSpace` and `kOperationIds` have zero drift (`:72-230`). Asserts totality of `classify_slot()` (`:232-260`). Exercises strict decimal parsing and bounds (`:262-350`). Verifies all G1..G5 refusals and controls (`:352-650`). Tests literal lexing and tags (`:660-720`). Tests grounding maps M1..M9, M0 subtractive down-payment suppression, cadence inference, TVM sign flips, and convention exemptions (`:730-1595`). |
| `test_mortgage_grammar` | `backend/tests/test_mortgage_grammar.cpp` | `mortgage_grammar.cppm`, `mortgage_verification.cppm` | Re-parses `finance.proto` to check grammar schema against proto and verifier (`:80-250`). Validates enum tables against proto and `verify_mortgage_params` (`:260-350`). Tests DFA state transitions, prefix rejection, and complete object recognition (`:360-600`). Validates all gold parameters from `agent/dataset/data_mortgage/val.jsonl` are accepted (`:610-750`). Validates `MortgageParamsGrammar` as `sensen::IGrammar` (`:760-880`). Tests `params_regex()` compilation with `sensen::RegexNfa` (`:890-1050`). |
| `test_assistant_verification` | `backend/tests/test_assistant_verification.cpp` | `assistant_verification.cppm`, `strategy_catalogue.cppm` | Verifies cross-field constraints across 5 output fields (`:65-350`). Tests ambiguous root detection and clarification formatting for `"ES"` and `"CL"` (`:360-450`). Tests strategy alias normalisation (`:460-520`). Verifies lexical support rules and bare direction guess suppression (`:530-620`). Tests prompt injection and advice filters (`:630-720`). Tests bare futures directive parameter recovery (`:730-820`). Tests exercise style and Asian option extraction (`:830-960`). |
| `test_runner` (`CoreEngineTest`) | `backend/src/test_main.cpp` | `pricing_engine.cppm`, `testing_framework.cppm` | Verifies end-to-end pricing calculations, Black-Scholes execution, and assertion helper mechanics (`backend/src/test_main.cpp:5-15`). |
| `test_assistant_service` | `backend/tests/test_assistant_service.cpp` | `assistant_verification.cppm`, `strategy_catalogue.cppm` | Tests integration of assistant verification rules within the gRPC service layer. |
| `test_mortgage_assistant_service` | `backend/tests/test_mortgage_assistant_service.cpp` | `mortgage_verification.cppm`, `mortgage_grammar.cppm` | Tests integration of mortgage verification and constrained decoding in service RPC dispatch. |
| `test_finance_service_validation` | `backend/tests/test_finance_service_validation.cpp` | `mortgage_verification.cppm` | Cross-checks RPC validation bounds against verifier gates. |

---

## Open Questions

None. All behaviors, static tables, bounds, candidate mappings, and interfaces have been verified directly against the codebase.
