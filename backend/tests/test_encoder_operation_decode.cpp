/**
 * Gates for `encoder_reconstruct::OperationDecode` -- the per-operation mask both model
 * heads are decoded through.
 *
 * @author Olumuyiwa Oluwasanmi
 *
 * NO EXTERNAL TEST FRAMEWORK (rule 39): a `check()` and two counters, and a non-zero exit
 * is the failure signal ctest reads.
 *
 * WHY THIS FILE EXISTS. The pair head got its per-operation mask and the CONVENTION heads
 * did not, because they are decoded in a different file: `encoder_assistant.cpp` referenced
 * `conv_op_mask` ZERO times while the schema had carried it the whole time. Measured through
 * the real `ParseStrategy` RPC on 300 holdout rows, that cost 45 refusals reading
 * `"futures_short" is a futures-only strategy, but asset_class is "EQUITY"` -- an unmasked
 * argmax preferring EQUITY on an operation whose admissible set is exactly {FUTURES}, so
 * GP-ARA refused a self-contradictory parse the model had never made. The repair is one
 * object both heads go through, and this file pins its rules.
 *
 * THE SCHEMA HERE IS A MINIATURE OF THE REAL ONE, not a copy of it. It reproduces the one
 * SHAPE that matters -- a field whose admissible set is a strict subset for some operations
 * and not others -- with logits chosen so the unmasked argmax is deliberately WRONG. A test
 * built from the real 47-operation schema would pass whether or not the mask were applied
 * on most rows, which is exactly how the defect survived.
 *
 * WHY THE MORTGAGE SCHEMA COULD NOT HAVE CAUGHT IT, measured and recorded because it is the
 * fifth instance in one day of a corpus that cannot exhibit a failure being read as a
 * control for it: every mask the mortgage schema carries is FULL (all classes admissible)
 * for the single operation that has an entry, and 135 (field, operation) pairs have no entry
 * at all -- for ZERO of which does `op_fields` name the field. So the mask is provably a
 * no-op there, and mortgage served 560/560 through this code with the defect present.
 */
#include <cstdio>

import std;
import encoder_reconstruct;

namespace {

int g_checks = 0;
int g_failures = 0;

auto check(bool condition, const std::string& what) -> void {
    ++g_checks;
    if (condition) {
        std::printf("  PASS: %s\n", what.c_str());
    } else {
        std::printf("  FAIL: %s\n", what.c_str());
        ++g_failures;
    }
}

/// op 0 is <NONE>, as in every real schema. `futures_only` admits one asset class;
/// `equity_or_crypto` admits two of three; `unmasked_field` is FULL for both, so it stands
/// in for the strategy schema's `expiration_days` where the mask is a genuine no-op.
[[nodiscard]] auto miniature() -> encoder_reconstruct::Schema {
    encoder_reconstruct::Schema s;
    s.ops = {"<NONE>", "futures_only", "equity_or_crypto"};
    s.conv_fields = {"asset_class", "unmasked_field"};
    s.conv_vocab["asset_class"] = {"\"EQUITY\"", "\"FUTURES\"", "\"CRYPTO\""};
    s.conv_vocab["unmasked_field"] = {"7", "30"};
    s.conv_op_mask["asset_class"] = {{"futures_only", {1}}, {"equity_or_crypto", {0, 2}}};
    s.conv_op_mask["unmasked_field"] = {{"futures_only", {0, 1}}, {"equity_or_crypto", {0, 1}}};
    return s;
}

auto class_of(const std::unordered_map<std::string, int>& m, const std::string& k) -> int {
    const auto it = m.find(k);
    return it == m.end() ? -1 : it->second;
}

} // namespace

auto main() -> int {
    const auto sch = miniature();
    // EQUITY carries the HIGHEST logit of the three deliberately: it is what the unmasked
    // argmax returns and what production returned for 45 rows.
    const std::vector<float> logits{9.0F, 1.0F, 2.0F, /* unmasked_field */ 0.5F, 4.0F};

    std::printf("Section 1: the mask overrides a higher inadmissible logit\n");
    {
        const auto got = encoder_reconstruct::OperationDecode::of(sch).forOperation(1)
                             .conventionClasses(logits);
        check(got.has_value(), "futures_only decodes");
        if (got) {
            check(class_of(*got, "asset_class") == 1,
                  "asset_class is FUTURES (1), not the unmasked argmax EQUITY (0)");
            check(class_of(*got, "unmasked_field") == 1,
                  "a FULL mask is a no-op: unmasked_field takes its own argmax (1)");
        }
    }

    std::printf("Section 2: a two-of-three mask still picks the best ADMISSIBLE class\n");
    {
        const auto got = encoder_reconstruct::OperationDecode::of(sch).forOperation(2)
                             .conventionClasses(logits);
        check(got.has_value(), "equity_or_crypto decodes");
        // EQUITY (9.0) is admissible here and is the honest winner -- the mask must not
        // move an answer it does not need to move.
        if (got) check(class_of(*got, "asset_class") == 0, "asset_class is EQUITY (0), admissibly");
    }

    std::printf("Section 3: ties resolve to the LOWEST admissible id, as torch.argmax does\n");
    {
        // 0 and 2 are both admissible under equity_or_crypto and carry the same logit.
        // `torch.argmax`, `std::ranges::max_element` and sensen's `argmaxIndex` all return
        // the FIRST maximum, so masked-logits-then-argmax would answer 0.
        const std::vector<float> tied{5.0F, 1.0F, 5.0F, 0.5F, 4.0F};
        const auto got = encoder_reconstruct::OperationDecode::of(sch).forOperation(2)
                             .conventionClasses(tied);
        check(got.has_value() && class_of(*got, "asset_class") == 0,
              "a tie between admissible 0 and 2 resolves to 0");
    }

    std::printf("Section 4: <NONE> decodes no field, and an unknown operation REFUSES\n");
    {
        const auto none = encoder_reconstruct::OperationDecode::of(sch).forOperation(0)
                              .conventionClasses(logits);
        check(none.has_value() && none->empty(), "<NONE> yields no convention field");
        for (const int bad : {-1, 3, 99}) {
            const auto got = encoder_reconstruct::OperationDecode::of(sch).forOperation(bad)
                                 .conventionClasses(logits);
            check(!got.has_value(),
                  std::format("operation {} is refused, never decoded unmasked", bad));
        }
    }

    std::printf("Section 5: a field with NO mask entry is OMITTED, not guessed\n");
    {
        // The mortgage shape: 135 (field, operation) pairs have no entry, and `op_fields`
        // names the field for none of them, so the operation does not take it. Omitting is
        // provably identical to decoding it there -- and strictly more honest.
        auto s = miniature();
        s.conv_op_mask["asset_class"].erase("futures_only");
        const auto got = encoder_reconstruct::OperationDecode::of(s).forOperation(1)
                             .conventionClasses(logits);
        check(got.has_value(), "a missing mask entry is not an error");
        if (got) {
            check(got->find("asset_class") == got->end(),
                  "asset_class is absent, rather than taking an unmasked argmax");
            check(got->contains("unmasked_field"), "its sibling field is still decoded");
        }
    }

    std::printf("Section 6: the flat layout is DERIVED and self-checked against the buffer\n");
    {
        const auto s = miniature();
        const auto decode = encoder_reconstruct::OperationDecode::of(s).forOperation(1);
        // 3 + 2 = 5 classes. A buffer of any other width means the schema and the model
        // disagree about the flat layout, which would read one field's logits as another's.
        check(!decode.conventionClasses(std::vector<float>{1.0F, 2.0F, 3.0F}).has_value(),
              "a buffer too SHORT for conv_vocab is refused");
        check(!decode.conventionClasses(std::vector<float>(9, 1.0F)).has_value(),
              "a buffer WIDER than conv_vocab is refused");
        check(decode.conventionClasses(std::vector<float>(5, 1.0F)).has_value(),
              "the exact width is accepted");
    }

    std::printf("Section 7: a mask naming a class the field does not have is refused\n");
    {
        auto s = miniature();
        s.conv_op_mask["asset_class"]["futures_only"] = {7};
        check(!encoder_reconstruct::OperationDecode::of(s).forOperation(1)
                   .conventionClasses(logits).has_value(),
              "conv_op_mask admitting class 7 of a 3-class field is refused");
    }

    std::printf("Section 8: both heads go through ONE operation, which is the point\n");
    {
        auto s = miniature();
        s.pairs = {{"a", "m1"}, {"b", "m2"}, {"c", "m3"}};
        s.op_pairs = {{"futures_only", {0, 2}}, {"equity_or_crypto", {1}}};
        const auto decode = encoder_reconstruct::OperationDecode::of(s).forOperation(1);
        const std::vector<int> selected{0, 1, 2};
        const auto pairs = decode.admissiblePairs(selected);
        check(pairs.has_value() && *pairs == std::vector<int>{0, 2},
              "the same decode restricts the pair head to {0, 2}");
        const auto conv = decode.conventionClasses(logits);
        check(conv.has_value() && class_of(*conv, "asset_class") == 1,
              "and the convention head to FUTURES, from one `forOperation`");
        check(decode.operation() == 1, "the bound operation is readable back");
    }

    std::printf("\n%d checks, %d failures\n", g_checks, g_failures);
    return g_failures == 0 ? 0 : 1;
}
