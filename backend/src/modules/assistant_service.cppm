module;
#include <grpcpp/grpcpp.h>
#include <span>
#include <string_view>

// Both contracts, because the averaging handoff below spans them: the
// assistant parses into sensen.finance.AsianType and the calculator accepts
// its OWN calculator.Leg.AsianType. assistant.pb.h already pulls in
// finance.pb.h (assistant.proto imports finance.proto); calculator.pb.h is
// named explicitly because calculator.proto imports nothing and is imported
// by nothing.
#include "assistant.pb.h"
#include "calculator.pb.h"

export module assistant_service;

export namespace options_calculator::assistant {

/**
 * Registers the LLM-backed strategy assistant on a gRPC server builder.
 *
 * Mirrors RegisterFinanceService's shape exactly, and for the same reason:
 * the builder is taken by reference rather than type-erased, because the
 * implementation needs gRPC regardless (config/cpp_details.txt rule 3).
 *
 * This is a THIRD, independent contract sharing the port with
 * calculator.OptionsCalculator and sensen.finance.Finance. Those two speak in
 * deterministic, auditable arithmetic; this one wraps a fine-tuned language
 * model that turns a trader's free-text utterance into the structured
 * parameters CalculateStrategy actually needs. See proto/assistant.proto's
 * file banner for why a clarifying question and a refusal are both OK
 * outcomes of this RPC rather than errors, and why real-data-only policy
 * extends to intent here just as it does to market quotes everywhere else in
 * this codebase.
 */
auto RegisterAssistantService(grpc::ServerBuilder& builder) -> void;

/**
 * TEST-ONLY registration hook. Builds and registers the SAME
 * StrategyAssistantWorkflow graph as RegisterAssistantService above, but
 * binds only the action names listed in `bound_action_names` instead of
 * always binding the full set ("Admission", "CheckModel", "Generate",
 * "ParseAndVerify").
 *
 * Mirrors RegisterCalculatorServiceForTest (calculator_service.cppm) exactly,
 * and for the identical reason: it lets a test reproduce, through the real
 * ParseStrategy RPC, the silent-halt failure mode a partial or renamed
 * action registry produces, and prove that the postcondition after
 * Interpreter::Run() actually catches it. Production never constructs one of
 * these with a partial set.
 */
auto RegisterAssistantServiceForTest(grpc::ServerBuilder& builder,
                                     std::span<const std::string_view> bound_action_names)
    -> void;

// ---------------------------------------------------------------------------
// The averaging handoff: StrategyParams -> calculator.Leg.
//
// `StrategyParams.asian_type` has existed since the exercise/Asian extractor
// landed, and `assistant_verification.cppm` fills it from the trader's own
// words. Until calculator.proto gained `Leg.asian_type` there was nowhere for
// it to go -- the assistant could parse "Asian call on SPY" into a field the
// calculator had no way to accept. These two functions are that missing edge.
//
// THEY HAVE NO PRODUCTION CALLER TODAY, and that is stated here rather than
// discovered later. Nothing in this repository turns a ParseResponse into a
// StrategyRequest: the assistant answers with parameters and the CALLER
// chooses strikes off a live chain, which is a client concern (the browser's
// StrategySelector does exactly that for a hand-picked strategy) and no client
// here calls ParseStrategy yet. What is provided is the part that cannot be
// got right by guessing -- the enum mapping between two DELIBERATELY separate
// vocabularies, and the rule about which legs may carry a style at all.
//
// The honest caveat, recorded next to the code rather than in a commit
// message: the fine-tuned model was never taught Asian strategies. Its
// training set contains zero instances of the concept, so `asian_type` is
// only ever as good as the deterministic keyword extractor layered in front
// of it -- whole-word "Asian", with "average strike"/"average rate"
// disambiguating the style. Anything subtler than the words themselves is not
// detected, and nothing downstream should read a NOT_ASIAN here as evidence
// the trader did not mean an Asian.
// ---------------------------------------------------------------------------

/**
 * Maps the assistant's averaging style onto the calculator's own enum.
 *
 * Two enums, on purpose: calculator.proto has no imports and gains none, so a
 * cross-package reference would pull finance.proto into the generated browser
 * bundle and into Envoy's grpc_json_transcoder services list -- a deployment
 * change wearing a schema change's clothes. The zero values and the orderings
 * are identical by construction, which is what makes this mapping total and
 * lossless; it is written out case by case anyway, so that a value added to
 * one side is a compile-time question rather than a silent reinterpretation.
 */
[[nodiscard]] auto calculator_asian_type(sensen::finance::AsianType parsed)
    -> calculator::Leg::AsianType;

/**
 * Stamps a parse's averaging style onto the OPTION legs of the request built
 * from it, and returns how many legs were stamped.
 *
 * Option legs only. A future or a share has no averaging window, so marking
 * one Asian would describe an instrument that does not exist; those legs are
 * left at NOT_ASIAN rather than refused, because the leg itself is perfectly
 * valid and it is the STYLE that does not apply to it.
 *
 * The count is returned rather than discarded so a caller can tell "the trader
 * said Asian and it landed on three legs" from "the trader said Asian and it
 * landed on nothing" -- the second is a futures structure described with an
 * options word, and silently returning success there would let an Asian
 * request price as an ordinary one. A NOT_ASIAN parse stamps nothing and
 * returns 0, so a vanilla request is bit-identical to one that never called
 * this at all.
 */
auto apply_averaging_to_legs(const calculator::assistant::StrategyParams& params,
                             calculator::StrategyRequest& request) -> int;

}  // namespace options_calculator::assistant
