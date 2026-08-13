module;
#include <algorithm>
#include <atomic>
#include <cctype>
#include <chrono>
#include <condition_variable>
#include <cstdint>
#include <cstdlib>
#include <deque>
#include <fstream>
#include <functional>
#include <iterator>
#include <map>
#include <memory>
#include <mutex>
#include <optional>
#include <random>
#include <span>
#include <stop_token>
#include <string>
#include <string_view>
#include <thread>
#include <vector>

#include <grpcpp/grpcpp.h>
#include "task_queue.grpc.pb.h"

module sgee_queue_client;

import logger;
import sgee.runtime.resilience;
import sgee.runtime.task_queue.grpc_client;
import sgee.runtime.task_queue.types;

/**
 * @author Olumuyiwa Oluwasanmi
 *
 * Implementation of SgeeQueueClient mirroring logic.
 */

namespace {

[[nodiscard]] auto env_string_local(const char* name) -> std::optional<std::string> {
    const char* raw = std::getenv(name);
    if (raw == nullptr || *raw == '\0') return std::nullopt;
    return std::string{raw};
}

/** Strict base64 decode. Returns nullopt on ANY malformed input rather than
 *  decoding what it can: a partially-decoded PEM is not a weaker credential, it
 *  is an unusable one, and the caller must be able to tell "not configured" from
 *  "configured wrongly". Whitespace is skipped so a value that picked up a
 *  newline in transit still decodes. */
[[nodiscard]] auto base64_decode(std::string_view in) -> std::optional<std::string> {
    static constexpr std::string_view kAlphabet =
        "ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz0123456789+/";
    std::string out;
    out.reserve(in.size() / 4 * 3);
    std::uint32_t buf = 0;
    int bits = 0;
    std::size_t pad = 0;
    for (const char c : in) {
        if (c == '\n' || c == '\r' || c == ' ' || c == '\t') continue;
        if (c == '=') {
            ++pad;
            continue;
        }
        if (pad != 0) return std::nullopt;  // data after padding
        const auto idx = kAlphabet.find(c);
        if (idx == std::string_view::npos) return std::nullopt;
        buf = (buf << 6) | static_cast<std::uint32_t>(idx);
        bits += 6;
        if (bits >= 8) {
            bits -= 8;
            out.push_back(static_cast<char>((buf >> bits) & 0xFFU));
        }
    }
    if (pad > 2) return std::nullopt;
    return out;
}

/** The PEM behind a `*_B64` content variable, falling back to a path variable.
 *  See channel_credentials() for why both forms exist. */
[[nodiscard]] auto env_pem(const char* b64_name, const char* path_name)
    -> std::optional<std::string> {
    if (auto encoded = env_string_local(b64_name)) {
        auto decoded = base64_decode(*encoded);
        if (!decoded || decoded->empty()) {
            logger::Logger::getInstance().error(
                std::string("sgee_queue_client: ") + b64_name +
                " is set but is not valid base64 -- treating it as unset, which will leave "
                "this client on plaintext");
            return std::nullopt;
        }
        return decoded;
    }
    if (auto path = env_string_local(path_name)) {
        std::ifstream in(*path, std::ios::binary);
        if (!in) return std::nullopt;
        std::string contents((std::istreambuf_iterator<char>(in)),
                             std::istreambuf_iterator<char>());
        if (contents.empty()) return std::nullopt;
        return contents;
    }
    return std::nullopt;
}

[[nodiscard]] auto parse_peers_str(std::string_view raw) -> std::map<std::string, std::string> {
    std::map<std::string, std::string> peers;
    std::size_t start = 0;
    while (start < raw.size()) {
        std::size_t comma = raw.find(',', start);
        std::string_view token = (comma == std::string_view::npos)
                                     ? raw.substr(start)
                                     : raw.substr(start, comma - start);
        start = (comma == std::string_view::npos) ? raw.size() : comma + 1;

        while (!token.empty() && (token.front() == ' ' || token.front() == '\t' || token.front() == '\r' || token.front() == '\n')) {
            token.remove_prefix(1);
        }
        while (!token.empty() && (token.back() == ' ' || token.back() == '\t' || token.back() == '\r' || token.back() == '\n')) {
            token.remove_suffix(1);
        }
        if (token.empty()) continue;

        std::size_t eq = token.find('=');
        if (eq == std::string_view::npos || eq == 0 || eq == token.size() - 1) continue;

        std::string node_id{token.substr(0, eq)};
        std::string addr{token.substr(eq + 1)};
        
        while (!node_id.empty() && (node_id.back() == ' ' || node_id.back() == '\t')) node_id.pop_back();
        while (!addr.empty() && (addr.front() == ' ' || addr.front() == '\t')) addr.erase(addr.begin());

        if (!node_id.empty() && !addr.empty()) {
            peers[node_id] = addr;
        }
    }
    return peers;
}

[[nodiscard]] auto steady_now_ms() -> std::uint64_t {
    return static_cast<std::uint64_t>(
        std::chrono::duration_cast<std::chrono::milliseconds>(
            std::chrono::steady_clock::now().time_since_epoch()).count());
}

} // namespace

class SgeeQueueClient::Impl {
  public:
    Impl(std::map<std::string, std::string> peer_map,
         std::vector<std::string> peer_ids,
         std::size_t ring_capacity,
         sgee::resilience::CircuitBreakerConfig cb_config)
        : peer_map_(std::move(peer_map)),
          peer_ids_(std::move(peer_ids)),
          max_ring_capacity_(ring_capacity),
          circuit_breaker_(cb_config),
          backoff_(sgee::resilience::BackoffConfig{
              .base_ms = 50,
              .factor = 2.0,
              .max_ms = 400,
              .jitter = sgee::resilience::JitterType::Full
          })
    {
        worker_thread_ = std::thread([this]() { worker_loop(stop_source_.get_token()); });
    }

    ~Impl() {
        stop_source_.request_stop();
        cv_.notify_all();
        if (worker_thread_.joinable()) {
            worker_thread_.join();
        }
    }

    auto enqueue_mirror(std::string_view payload_json) noexcept -> void {
        constexpr std::size_t kMaxMirrorPayloadBytes = 256 * 1024;
        if (payload_json.size() > kMaxMirrorPayloadBytes) {
            dropped_.fetch_add(1, std::memory_order_relaxed);
            return;
        }

        try {
            std::lock_guard lock(mutex_);
            if (ring_buffer_.size() >= max_ring_capacity_) {
                ring_buffer_.pop_front();
                dropped_.fetch_add(1, std::memory_order_relaxed);
            }
            ring_buffer_.push_back(std::string(payload_json));
            cv_.notify_one();
        } catch (...) {
            dropped_.fetch_add(1, std::memory_order_relaxed);
        }
    }

    [[nodiscard]] auto stats() const -> MirrorStats {
        std::lock_guard lock(mutex_);
        std::string b_state = std::string(sgee::resilience::to_string(circuit_breaker_.state()));
        return MirrorStats{
            .enqueued = enqueued_.load(std::memory_order_relaxed),
            .dropped = dropped_.load(std::memory_order_relaxed),
            .redirects = redirects_.load(std::memory_order_relaxed),
            .breaker_state = std::move(b_state)
        };
    }

  private:
    auto get_channel(std::string const& addr) -> std::shared_ptr<grpc::Channel> {
        auto it = channels_.find(addr);
        if (it != channels_.end()) {
            return it->second;
        }
        auto ch = grpc::CreateChannel(addr, channel_credentials());
        channels_[addr] = ch;
        return ch;
    }

    /**
     * Mutual TLS when the queue nodes require it, plaintext when they do not.
     *
     * The queue port (50053) accepts work from anything that can reach it, so
     * when the cluster turns on mTLS the server side becomes
     * GRPC_SSL_REQUEST_AND_REQUIRE_CLIENT_CERTIFICATE_AND_VERIFY -- it demands a
     * client certificate AND checks it. A client still offering
     * InsecureChannelCredentials is not rejected loudly at a layer anyone
     * watches: every mirror write simply fails, and mirror writes are dropped
     * by design (see this module's header), so the symptom is a mirror that
     * silently stops mirroring. Hence this reads the same all-or-nothing trio
     * the nodes do.
     *
     * ALL-OR-NOTHING, for the same reason it is on the node: a half-set trio
     * here would fall back to plaintext against a server that now requires a
     * certificate, which is the configuration most likely to be believed
     * secure. It is loud instead -- an error log and NO channel credentials
     * downgrade -- and unset-entirely stays a supported plaintext deployment.
     */
    [[nodiscard]] static auto channel_credentials()
        -> std::shared_ptr<grpc::ChannelCredentials> {
        // ONE deployment interface for both sides: SGEE_TLS_*_B64 carries the PEM
        // itself, base64'd so it survives the CLI/shell/JSON path to a Railway
        // variable in one line. The queue NODE cannot consume it directly --
        // sgee_queue_node's config takes file PATHS -- so its entrypoint decodes
        // these to files; this client is our own code and reads them straight.
        // The path form is still honoured second, for a local run against PEMs
        // already on disk.
        const auto ca = env_pem("SGEE_TLS_CA_CERT_B64", "SGEE_TLS_CA_CERT");
        const auto crt = env_pem("SGEE_TLS_CERT_B64", "SGEE_TLS_CERT");
        const auto key = env_pem("SGEE_TLS_KEY_B64", "SGEE_TLS_KEY");
        const int set_count = (ca ? 1 : 0) + (crt ? 1 : 0) + (key ? 1 : 0);
        if (set_count == 0) {
            return grpc::InsecureChannelCredentials();
        }
        if (set_count != 3) {
            logger::Logger::getInstance().error(
                "sgee_queue_client: " + std::to_string(set_count) +
                " of 3 TLS paths set (SGEE_TLS_CA_CERT, SGEE_TLS_CERT, SGEE_TLS_KEY are "
                "all-or-nothing) -- staying on plaintext, which will FAIL against a cluster "
                "that requires client certificates");
            return grpc::InsecureChannelCredentials();
        }
        grpc::SslCredentialsOptions opts;
        opts.pem_root_certs = *ca;
        opts.pem_cert_chain = *crt;
        opts.pem_private_key = *key;
        if (opts.pem_root_certs.empty() || opts.pem_cert_chain.empty() ||
            opts.pem_private_key.empty()) {
            logger::Logger::getInstance().error(
                "sgee_queue_client: TLS was configured but at least one PEM was unreadable or "
                "empty -- staying on plaintext");
            return grpc::InsecureChannelCredentials();
        }
        return grpc::SslCredentials(opts);
    }

    auto worker_loop(std::stop_token st) -> void {
        while (!st.stop_requested()) {
            std::string payload;
            {
                std::unique_lock lock(mutex_);
                cv_.wait(lock, [this, &st] { return st.stop_requested() || !ring_buffer_.empty(); });
                if (ring_buffer_.empty()) {
                    if (st.stop_requested()) break;
                    continue;
                }
                payload = std::move(ring_buffer_.front());
                ring_buffer_.pop_front();
            }

            process_item(payload);
        }

        std::deque<std::string> remaining;
        {
            std::lock_guard lock(mutex_);
            remaining = std::move(ring_buffer_);
            ring_buffer_.clear();
        }
        for (auto const& payload : remaining) {
            process_item(payload);
        }
    }

    auto process_item(std::string const& payload) -> void {
        auto const start_time = std::chrono::steady_clock::now();
        std::uint64_t const now_ms = steady_now_ms();

        {
            std::lock_guard lock(mutex_);
            if (!circuit_breaker_.allow(now_ms)) {
                dropped_.fetch_add(1, std::memory_order_relaxed);
                if (!logged_breaker_open_warn_) {
                    logger::Logger::getInstance().warn(
                        "sgee_queue_client: circuit breaker is open -- dropping mirror write");
                    logged_breaker_open_warn_ = true;
                }
                return;
            }
            if (logged_breaker_open_warn_) {
                logged_breaker_open_warn_ = false;
            }
        }

        bool success = false;
        std::size_t attempt = 0;
        constexpr std::size_t kMaxAttempts = 3;
        constexpr auto kMaxTotalBudget = std::chrono::milliseconds(1000);

        static thread_local std::mt19937 rng{std::random_device{}()};
        std::uniform_real_distribution<double> dist(0.0, 1.0);
        auto rng_fn = [&]() { return dist(rng); };

        while (attempt < kMaxAttempts) {
            auto const elapsed = std::chrono::duration_cast<std::chrono::milliseconds>(
                std::chrono::steady_clock::now() - start_time);
            if (elapsed >= kMaxTotalBudget) {
                break;
            }

            std::string target_id;
            std::string target_addr;
            {
                std::lock_guard lock(mutex_);
                if (!cached_leader_id_.empty() && peer_map_.contains(cached_leader_id_)) {
                    target_id = cached_leader_id_;
                } else {
                    if (peer_ids_.empty()) break;
                    target_id = peer_ids_[round_robin_idx_ % peer_ids_.size()];
                }
                target_addr = peer_map_[target_id];
            }

            auto channel = get_channel(target_addr);
            sgee::task_queue::grpc_client::TaskQueueClient client(channel);

            auto payload_bytes = std::as_bytes(std::span(payload.data(), payload.size()));
            auto res = client.enqueue(payload_bytes, sgee::task_queue::PlacementTarget::Cpu);

            if (res.has_value()) {
                std::uint64_t const cur_ms = steady_now_ms();
                std::lock_guard lock(mutex_);
                circuit_breaker_.on_success(cur_ms);
                enqueued_.fetch_add(1, std::memory_order_relaxed);
                success = true;
                break;
            }

            auto const& status = res.error();
            if (status.outcome == sgee::task_queue::grpc_client::ClientOutcome::NotLeader) {
                redirects_.fetch_add(1, std::memory_order_relaxed);
                std::string const& hint = status.leader_hint;
                {
                    std::lock_guard lock(mutex_);
                    if (!hint.empty() && peer_map_.contains(hint)) {
                        cached_leader_id_ = hint;
                    } else {
                        cached_leader_id_.clear();
                    }
                }
            } else if (status.outcome == sgee::task_queue::grpc_client::ClientOutcome::TransportError) {
                {
                    std::lock_guard lock(mutex_);
                    if (cached_leader_id_ == target_id) {
                        cached_leader_id_.clear();
                    }
                    round_robin_idx_++;
                }
            } else {
                break;
            }

            attempt++;
            if (attempt < kMaxAttempts) {
                std::uint64_t delay_ms = backoff_.delay_for_attempt(static_cast<std::uint32_t>(attempt - 1), rng_fn);
                auto remaining_budget = kMaxTotalBudget - std::chrono::duration_cast<std::chrono::milliseconds>(
                    std::chrono::steady_clock::now() - start_time);
                if (remaining_budget.count() <= 0) break;
                if (std::chrono::milliseconds(delay_ms) > remaining_budget) {
                    delay_ms = static_cast<std::uint64_t>(remaining_budget.count());
                }
                if (delay_ms > 0) {
                    std::this_thread::sleep_for(std::chrono::milliseconds(delay_ms));
                }
            }
        }

        if (!success) {
            std::uint64_t const cur_ms = steady_now_ms();
            std::lock_guard lock(mutex_);
            circuit_breaker_.on_failure(cur_ms);
            dropped_.fetch_add(1, std::memory_order_relaxed);
        }
    }

    mutable std::mutex mutex_;
    std::condition_variable cv_;
    std::deque<std::string> ring_buffer_;
    std::size_t max_ring_capacity_{1024};

    sgee::resilience::CircuitBreaker circuit_breaker_;
    sgee::resilience::BackoffPolicy backoff_;
    bool logged_breaker_open_warn_{false};

    std::map<std::string, std::string> peer_map_;
    std::vector<std::string> peer_ids_;
    std::string cached_leader_id_;
    std::size_t round_robin_idx_{0};

    std::map<std::string, std::shared_ptr<grpc::Channel>> channels_;

    std::atomic<std::uint64_t> enqueued_{0};
    std::atomic<std::uint64_t> dropped_{0};
    std::atomic<std::uint64_t> redirects_{0};

    std::stop_source stop_source_;
    std::thread worker_thread_;
};

SgeeQueueClient::SgeeQueueClient(std::shared_ptr<Impl> impl) : impl_(std::move(impl)) {}

auto SgeeQueueClient::create_from_env() -> std::optional<SgeeQueueClient> {
    auto env_sgee_queue = env_string_local("SGEE_QUEUE");
    if (!env_sgee_queue.has_value()) return std::nullopt;
    std::string mode_str = *env_sgee_queue;
    for (char& c : mode_str) c = static_cast<char>(std::tolower(static_cast<unsigned char>(c)));
    if (mode_str != "mirror") return std::nullopt;

    auto env_peers = env_string_local("SGEE_PEERS");
    if (!env_peers.has_value()) return std::nullopt;

    auto peer_map = parse_peers_str(*env_peers);
    if (peer_map.empty()) return std::nullopt;

    std::vector<std::string> peer_ids;
    for (auto const& [id, _] : peer_map) {
        peer_ids.push_back(id);
    }

    sgee::resilience::CircuitBreakerConfig cb_config{
        .failure_threshold = 3,
        .open_cooldown_ms = 1000,
        .half_open_max_trials = 1
    };

    auto impl = std::make_shared<Impl>(std::move(peer_map), std::move(peer_ids), 1024, cb_config);
    return SgeeQueueClient(std::move(impl));
}

auto SgeeQueueClient::enqueue_mirror(std::string_view payload_json) noexcept -> void {
    if (impl_) {
        impl_->enqueue_mirror(payload_json);
    }
}

auto SgeeQueueClient::stats() const -> MirrorStats {
    if (impl_) {
        return impl_->stats();
    }
    return MirrorStats{};
}
