module;
#include <cstdint>
#include <memory>
#include <optional>
#include <string>
#include <string_view>

export module sgee_queue_client;

import sgee.runtime.resilience;

/**
 * @author Olumuyiwa Oluwasanmi
 *
 * Serving-tier mirror client for the Stochastic Graph Execution Engine (SGEE) task queue.
 *
 * Provides non-blocking, mirror-mode enqueue capability parallel to Postgres inference_jobs.
 * Mirror writes land on a bounded in-process ring buffer drained by a background worker thread.
 * Payload cap is 256 KB. If the queue is full or unreachable, mirror payloads are dropped
 * without blocking or erroring the calling RPC. Circuit breaker and exponential backoff with jitter
 * protect downstream nodes from cascading failures.
 */

export enum class SgeeQueueMode : std::uint8_t { Off, Mirror };

export struct MirrorStats {
    std::uint64_t enqueued{0};
    std::uint64_t dropped{0};
    std::uint64_t redirects{0};
    std::string breaker_state;
};

export class SgeeQueueClient {
  public:
    class Impl;

    SgeeQueueClient() = default;
    explicit SgeeQueueClient(std::shared_ptr<Impl> impl);

    static auto create_from_env() -> std::optional<SgeeQueueClient>;
    auto enqueue_mirror(std::string_view payload_json) noexcept -> void;
    [[nodiscard]] auto stats() const -> MirrorStats;

  private:
    std::shared_ptr<Impl> impl_;
};
