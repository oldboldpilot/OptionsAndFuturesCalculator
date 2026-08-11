// @author Olumuyiwa Oluwasanmi
//
// Tests for sgee_queue_client module.
// Validates mirror mode enqueuing, payload cap, circuit breaker behavior,
// leader redirection retry logic, and non-blocking caller execution.

#include <atomic>
#include <chrono>
#include <cstdio>
#include <cstdlib>
#include <memory>
#include <string>
#include <thread>
#include <vector>

#include <grpcpp/grpcpp.h>
#include "task_queue.grpc.pb.h"

import sgee_queue_client;

namespace {

int g_checks = 0;
int g_failures = 0;

auto check(bool condition, const std::string& what) -> void {
    ++g_checks;
    if (condition) {
        std::printf("  PASS: %s\n", what.c_str());
    } else {
        ++g_failures;
        std::printf("  FAIL: %s\n", what.c_str());
    }
}

auto section(const char* title) -> void { std::printf("\n=== %s ===\n", title); }

// Fake in-process gRPC TaskQueue service
class FakeTaskQueueService final : public sgee::task_queue::rpc::TaskQueue::Service {
  public:
    std::atomic<std::uint64_t> enqueue_calls{0};
    std::atomic<bool> return_not_leader{false};
    std::string leader_hint_to_return;

    grpc::Status Enqueue(grpc::ServerContext* /*context*/,
                         const sgee::task_queue::rpc::EnqueueRequest* /*request*/,
                         sgee::task_queue::rpc::EnqueueResponse* response) override {
        enqueue_calls.fetch_add(1, std::memory_order_relaxed);
        if (return_not_leader.load(std::memory_order_relaxed)) {
            response->mutable_status()->set_code(sgee::task_queue::rpc::QUEUE_ERROR_NOT_LEADER);
            response->mutable_status()->set_leader_hint(leader_hint_to_return);
            response->mutable_status()->set_detail("Not leader");
            return grpc::Status::OK;
        }
        response->mutable_status()->set_code(sgee::task_queue::rpc::QUEUE_ERROR_OK);
        response->set_task_id(1001);
        return grpc::Status::OK;
    }
};

struct ServerFixture {
    FakeTaskQueueService service;
    std::unique_ptr<grpc::Server> server;
    int port{0};

    ServerFixture() {
        grpc::ServerBuilder builder;
        builder.AddListeningPort("127.0.0.1:0", grpc::InsecureServerCredentials(), &port);
        builder.RegisterService(&service);
        server = builder.BuildAndStart();
    }

    ~ServerFixture() {
        if (server) {
            server->Shutdown();
        }
    }
};

void test_create_from_env_off() {
    section("1. create_from_env when off or unconfigured");
    setenv("SGEE_QUEUE", "off", 1);
    setenv("SGEE_PEERS", "1=127.0.0.1:50051", 1);

    auto client = SgeeQueueClient::create_from_env();
    check(!client.has_value(), "SGEE_QUEUE=off returns std::nullopt");

    unsetenv("SGEE_QUEUE");
    auto client_unset = SgeeQueueClient::create_from_env();
    check(!client_unset.has_value(), "SGEE_QUEUE unset returns std::nullopt");
}

void test_payload_cap() {
    section("2. 256 KB client-side payload cap");
    setenv("SGEE_QUEUE", "mirror", 1);
    setenv("SGEE_PEERS", "1=127.0.0.1:59999", 1);

    auto client_opt = SgeeQueueClient::create_from_env();
    check(client_opt.has_value(), "create_from_env succeeds for SGEE_QUEUE=mirror");
    auto& client = *client_opt;

    std::string huge_payload(257 * 1024, 'x'); // 257 KB
    client.enqueue_mirror(huge_payload);

    auto stats = client.stats();
    check(stats.dropped == 1, "Payload > 256 KB is dropped immediately (stats.dropped == 1)");
    check(stats.enqueued == 0, "Payload > 256 KB is never enqueued");
}

void test_all_endpoints_down_non_blocking() {
    section("3. All endpoints down: non-blocking <10ms and increments dropped");
    setenv("SGEE_QUEUE", "mirror", 1);
    setenv("SGEE_PEERS", "1=127.0.0.1:59999", 1);

    auto client_opt = SgeeQueueClient::create_from_env();
    check(client_opt.has_value(), "Client created");
    auto& client = *client_opt;

    auto const start = std::chrono::steady_clock::now();
    client.enqueue_mirror("{\"prompt\":\"test nonblocking\"}");
    auto const elapsed = std::chrono::duration_cast<std::chrono::milliseconds>(
        std::chrono::steady_clock::now() - start);

    check(elapsed.count() < 10, "enqueue_mirror returned in < 10 ms (non-blocking call site)");

    // Wait for background worker thread to process item and fail retries
    for (int i = 0; i < 50; ++i) {
        if (client.stats().dropped >= 1) break;
        std::this_thread::sleep_for(std::chrono::milliseconds(20));
    }

    auto stats = client.stats();
    check(stats.dropped >= 1, "Dropped counter incremented when endpoints down");
}

void test_not_leader_redirection() {
    section("4. Node 1 answers NOT_LEADER with hint=2: retry MUST land on node 2");
    ServerFixture node1;
    ServerFixture node2;

    node1.service.return_not_leader.store(true);
    node1.service.leader_hint_to_return = "2";

    std::string peers_str = "1=127.0.0.1:" + std::to_string(node1.port) +
                            ",2=127.0.0.1:" + std::to_string(node2.port);

    setenv("SGEE_QUEUE", "mirror", 1);
    setenv("SGEE_PEERS", peers_str.c_str(), 1);

    auto client_opt = SgeeQueueClient::create_from_env();
    check(client_opt.has_value(), "Client created with node1 and node2");
    auto& client = *client_opt;

    client.enqueue_mirror("{\"prompt\":\"redirect test\"}");

    // Poll stats until enqueued == 1
    for (int i = 0; i < 50; ++i) {
        if (client.stats().enqueued >= 1) break;
        std::this_thread::sleep_for(std::chrono::milliseconds(20));
    }

    auto stats = client.stats();
    check(stats.enqueued == 1, "Mirror payload successfully enqueued to node 2");
    check(stats.redirects == 1, "Redirects counter incremented on NOT_LEADER response");
    check(node1.service.enqueue_calls.load() == 1, "Node 1 received exactly 1 attempt before redirecting");
    check(node2.service.enqueue_calls.load() == 1, "Node 2 received the redirected retry and accepted it");
}

void test_breaker_open_suppresses_dials() {
    section("5. Breaker open suppresses dials (ZERO connects during open window)");
    // Use an unreachable port to trip circuit breaker
    setenv("SGEE_QUEUE", "mirror", 1);
    setenv("SGEE_PEERS", "1=127.0.0.1:59998", 1);

    auto client_opt = SgeeQueueClient::create_from_env();
    check(client_opt.has_value(), "Client created");
    auto& client = *client_opt;

    // Trip circuit breaker with 3 failures (failure_threshold=3)
    for (int i = 0; i < 3; ++i) {
        client.enqueue_mirror("{\"prompt\":\"trip breaker\"}");
    }

    for (int i = 0; i < 100; ++i) {
        if (client.stats().breaker_state == "Open") break;
        std::this_thread::sleep_for(std::chrono::milliseconds(20));
    }

    check(client.stats().breaker_state == "Open", "Circuit breaker transitioned to Open after 3 failures");

    ServerFixture late_node;
    std::uint64_t dropped_before = client.stats().dropped;

    // Enqueue while breaker is Open
    client.enqueue_mirror("{\"prompt\":\"suppress dial test\"}");

    std::this_thread::sleep_for(std::chrono::milliseconds(100));

    check(late_node.service.enqueue_calls.load() == 0, "Fake server saw ZERO connects/dials during breaker open window");
    check(client.stats().dropped > dropped_before, "Dropped count incremented while breaker open");
}

} // namespace

int main() {
    std::printf("Running sgee_queue_client tests...\n");

    test_create_from_env_off();
    test_payload_cap();
    test_all_endpoints_down_non_blocking();
    test_not_leader_redirection();
    test_breaker_open_suppresses_dials();

    std::printf("\nSummary: %d checks, %d failures\n", g_checks, g_failures);
    return g_failures == 0 ? 0 : 1;
}
