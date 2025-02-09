#pragma once
#include "../../include/tick_capture/types.hpp"
#include <atomic>
#include <chrono>
#include <memory>
#include <mutex>
#include <thread>
#include <unordered_map>
#include <vector>
#include <zmq.h>

namespace tick_capture {

class Coordinator {
public:
  struct NodeInfo {
    std::string address;
    CaptureStats stats;
    std::chrono::system_clock::time_point last_heartbeat;
    bool is_healthy{true};
  };

  Coordinator(const std::string &bind_address,
              const std::vector<std::string> &peer_addresses);
  ~Coordinator();

  // Non-copyable
  Coordinator(const Coordinator &) = delete;
  Coordinator &operator=(const Coordinator &) = delete;

  // Start/stop coordinator
  void start();
  void stop();

  // Get aggregated stats from all nodes
  std::unordered_map<std::string, NodeInfo> get_node_status() const;

  // Publish node status
  void publish_status(const std::string &status);

private:
  void run_heartbeat();
  void handle_messages();
  void check_node_health();

  void *context_;
  void *publisher_;  // For broadcasting configs/commands
  void *subscriber_; // For receiving node status/heartbeats

  std::unordered_map<std::string, NodeInfo> nodes_;
  mutable std::mutex nodes_mutex_;

  std::atomic<bool> running_{false};
  std::thread heartbeat_thread_;
  std::thread message_thread_;

  // Configuration
  std::chrono::seconds heartbeat_interval_{1};
  std::chrono::seconds health_check_interval_{5};
};

} // namespace tick_capture
