/**
 * The output-driven explainer: empty columns are not explained, and every
 * sentence can be traced to the figure it describes.
 *
 * @author Olumuyiwa Oluwasanmi
 *
 * Each section pins one way this could produce a plausible, wrong explanation.
 * None of them is a crash; all of them read fine to someone who has not got
 * the result open beside the prose, which is why they are asserted.
 */
#include <cstdio>
#include <new>

import std;
import mortgage_explanation;

namespace ex = mortgage_calculator::assistant::explain;

namespace {
int g_checks = 0;
int g_failures = 0;

auto check(bool cond, const std::string& what) -> void {
    ++g_checks;
    std::printf("  %s: %s\n", cond ? "PASS" : "FAIL", what.c_str());
    if (!cond) { ++g_failures; }
}

auto mentions(const std::vector<ex::ExplanationPoint>& pts, std::string_view field) -> bool {
    return std::ranges::any_of(pts, [field](const auto& p) { return p.field == field; });
}

/** A rental that was bought with cash: no loan, no HELOC, no management. */
auto rental() -> ex::ResultView {
    return ex::ResultView{
        .operation = "ComputeRentalCashFlow",
        .fields = {
            {.name = "effective_gross_income", .label = "rent collected",
             .value = "22080.00", .unit = ex::Unit::Money, .zero = ex::ZeroMeaning::Real},
            {.name = "vacancy_loss", .label = "vacancy loss",
             .value = "1920.00", .unit = ex::Unit::Money, .zero = ex::ZeroMeaning::Real},
            {.name = "annual_repairs", .label = "repairs",
             .value = "2400.00", .unit = ex::Unit::Money, .zero = ex::ZeroMeaning::Absent},
            {.name = "monthly_hoa", .label = "HOA",
             .value = "0.00", .unit = ex::Unit::Money, .zero = ex::ZeroMeaning::Absent},
            {.name = "management_fee_rate", .label = "management fee",
             .value = "0.0000", .unit = ex::Unit::Share, .zero = ex::ZeroMeaning::Absent},
            {.name = "total_profit", .label = "total profit",
             .value = "0.00", .unit = ex::Unit::Money, .zero = ex::ZeroMeaning::Real},
        }};
}
}  // namespace

auto main() -> int {
    std::printf("1. a column with no data is not explained\n");
    {
        const auto pts = ex::explain_result(rental());
        check(!mentions(pts, "monthly_hoa"),
              "a 0.00 HOA is silence, not 'HOA: $0'");
        check(!mentions(pts, "management_fee_rate"),
              "and so is a 0.0000 management fee");
        check(mentions(pts, "annual_repairs"), "while a real repair budget IS explained");
    }

    std::printf("\n2. a COMPUTED zero is the finding, and survives the same filter\n");
    {
        // The error this excludes: treating every zero as absence. Breaking
        // exactly even is the answer to "is this worth buying?", and dropping
        // it leaves an explanation that silently omits the bottom line.
        const auto pts = ex::explain_result(rental());
        check(mentions(pts, "total_profit"),
              "total profit of 0.00 is REAL and is explained");
        check(mentions(pts, "vacancy_loss"), "and a stated vacancy loss too");

        auto full = rental();
        full.fields[1].value = "0.00";          // fully occupied
        const auto p2 = ex::explain_result(full);
        check(mentions(p2, "vacancy_loss"),
              "a zero vacancy loss still reports -- 'nothing lost to voids' is a result");
    }

    std::printf("\n3. a field whose PREREQUISITE is absent is suppressed\n");
    {
        auto v = rental();
        v.fields.push_back({.name = "heloc_drawn_amount", .label = "HELOC drawn",
                            .value = "0.00", .unit = ex::Unit::Money,
                            .zero = ex::ZeroMeaning::Absent});
        v.fields.push_back({.name = "heloc_annual_rate", .label = "HELOC rate",
                            .value = "0.0850", .unit = ex::Unit::Rate,
                            .zero = ex::ZeroMeaning::Absent});
        v.depends_on = {{"heloc_annual_rate", "heloc_drawn_amount"}};

        const auto pts = ex::explain_result(v);
        check(!mentions(pts, "heloc_annual_rate"),
              "8.5% is a perfectly ordinary non-zero rate, and it belongs to a "
              "HELOC that does not exist -- field-by-field filtering keeps it");
        check(!mentions(pts, "heloc_drawn_amount"), "the absent prerequisite says nothing either");

        // And the same rate IS explained once the draw is real.
        v.fields[6].value = "75000.00";
        const auto live = ex::explain_result(v);
        check(mentions(live, "heloc_annual_rate"),
              "but it IS explained once there is a draw to attach it to");
    }

    std::printf("\n4. suppression is TRANSITIVE, which a loop over pairs would miss\n");
    {
        auto v = rental();
        v.fields.push_back({.name = "heloc_drawn_amount", .label = "HELOC drawn",
                            .value = "0.00", .zero = ex::ZeroMeaning::Absent});
        v.fields.push_back({.name = "heloc_annual_rate", .label = "HELOC rate",
                            .value = "0.0850", .unit = ex::Unit::Rate,
                            .zero = ex::ZeroMeaning::Absent});
        v.fields.push_back({.name = "heloc_debt_service", .label = "HELOC payments",
                            .value = "9300.00", .zero = ex::ZeroMeaning::Absent});
        v.depends_on = {{"heloc_annual_rate", "heloc_drawn_amount"},
                        {"heloc_debt_service", "heloc_annual_rate"}};

        const auto pts = ex::explain_result(v);
        check(!mentions(pts, "heloc_debt_service"),
              "a field two hops behind an absent prerequisite is also suppressed -- "
              "its OWN prerequisite carries data, so only the closure sees it");
    }

    std::printf("\n5. every point is itemised and traceable to its own figure\n");
    {
        const auto pts = ex::explain_result(rental());
        check(!pts.empty(), "there are points to check");

        bool numbered = true;
        bool traceable = true;
        for (std::size_t i = 0; i < pts.size(); ++i) {
            if (pts[i].item != static_cast<int>(i) + 1) { numbered = false; }
            if (pts[i].field.empty() || pts[i].value.empty()) { traceable = false; }
        }
        check(numbered, "items are 1..N with no gaps, in the order shown");
        check(traceable, "and each names the output line and its exact figure");

        // The property that keeps prose and figures from drifting: the number
        // carried beside the sentence is the one the engine produced, so a
        // caller can check the sentence against the result rather than against
        // itself.
        const auto profit = std::ranges::find_if(
            pts, [](const auto& p) { return p.field == "total_profit"; });
        check(profit != pts.end() && profit->value == "0.00",
              "the total-profit point carries the engine's own 0.00, not a word for it");

        bool verbatim = std::ranges::all_of(pts, [&](const auto& p) {
            const auto& f = rental().fields;
            return std::ranges::any_of(f, [&](const auto& g) {
                return g.name == p.field && g.value == p.value;
            });
        });
        check(verbatim, "every carried value is VERBATIM from the result, not re-rendered");
    }

    std::printf("\n6. topics are namespaced by operation, so two results never collide\n");
    {
        const auto pts = ex::explain_result(rental());
        check(std::ranges::all_of(pts, [](const auto& p) {
                  return p.topic.starts_with("ComputeRentalCashFlow.");
              }),
              "each topic is <operation>.<field>");
    }

    std::printf("\n7. a result with nothing in it explains nothing, and does not fail\n");
    {
        ex::ResultView empty{.operation = "ComputeRentalCashFlow", .fields = {
            {.name = "total_profit", .label = "profit", .value = "",
             .zero = ex::ZeroMeaning::Real},
            {.name = "cap_rate", .label = "cap rate", .value = "0.0000",
             .unit = ex::Unit::Share, .zero = ex::ZeroMeaning::Absent, .produced = false},
        }};
        const auto pts = ex::explain_result(empty);
        check(pts.empty(), "an empty value and an unproduced field both say nothing");
    }

    std::printf("\n%d checks, %d failures\n", g_checks, g_failures);
    return g_failures == 0 ? 0 : 1;
}
