#include "coordinator.hpp"
#include <fmt/format.h>

namespace tick_capture {

Coordinator::Coordinator(const std::string &bind_address,
                         const std::vector<std::string> &peer_addresses) {
  // Initialize ZMQ context
  context_ = zmq_ctx_new();
  if (!context_) {
    throw std::runtime_error("Failed to create ZMQ context");
  }

  // Create publisher socket
  publisher_ = zmq_socket(context_, ZMQ_PUB);
  if (!publisher_) {
    zmq_ctx_destroy(context_);
    throw std::runtime_error("Failed to create publisher socket");
  }

  if (zmq_bind(publisher_, bind_address.c_str()) != 0) {
    zmq_close(publisher_);
    zmq_ctx_destroy(context_);
    throw std::runtime_error(
        fmt::format("Failed to bind publisher: {}", zmq_strerror(errno)));
  }

  // Create subscriber socket
  subscriber_ = zmq_socket(context_, ZMQ_SUB);
  if (!subscriber_) {
    zmq_close(publisher_);
    zmq_ctx_destroy(context_);
    throw std::runtime_error("Failed to create subscriber socket");
  }

  // Subscribe to all messages
  zmq_setsockopt(subscriber_, ZMQ_SUBSCRIBE, "", 0);

  // Connect to peers
  for (const auto &addr : peer_addresses) {
    if (zmq_connect(subscriber_, addr.c_str()) != 0) {
      fmt::print(stderr, "Warning: Failed to connect to {}: {}\n", addr,
                 zmq_strerror(errno));
    }
  }
}

Coordinator::~Coordinator() {
  stop();

  if (publisher_)
    zmq_close(publisher_);
  if (subscriber_)
    zmq_close(subscriber_);
  if (context_)
    zmq_ctx_destroy(context_);
}

void Coordinator::start() {
  if (running_)
    return;
  running_ = true;

  heartbeat_thread_ = std::thread([this] { run_heartbeat(); });
  message_thread_ = std::thread([this] { handle_messages(); });
}

void Coordinator::stop() {
  if (!running_)
    return;
  running_ = false;

  if (heartbeat_thread_.joinable())
    heartbeat_thread_.join();
  if (message_thread_.joinable())
    message_thread_.join();
}

void Coordinator::run_heartbeat() {
  using namespace std::chrono;
  auto next_heartbeat = system_clock::now();

  while (running_) {
    // Create heartbeat message
    std::string heartbeat =
        fmt::format(R"({{"type":"heartbeat","timestamp":{}}})",
                    system_clock::now().time_since_epoch().count());

    // Send heartbeat
    zmq_send(publisher_, heartbeat.data(), heartbeat.size(), 0);

    // Schedule next heartbeat
    next_heartbeat += heartbeat_interval_;
    std::this_thread::sleep_until(next_heartbeat);
  }
}

void Coordinator::handle_messages() {
  zmq_pollitem_t items[] = {{subscriber_, 0, ZMQ_POLLIN, 0}};

  // Buffer for receiving messages
  std::vector<char> buffer(1024 * 1024); // 1MB buffer

  while (running_) {
    if (zmq_poll(items, 1, 100) > 0) { // 100ms timeout
      if (items[0].revents & ZMQ_POLLIN) {
        // Receive message
        int size = zmq_recv(subscriber_, buffer.data(), buffer.size(), 0);
        if (size > 0) {
          // Process message
          std::string_view msg(buffer.data(), size);

          try {
            if (msg.find("\"type\":\"status\"") != std::string_view::npos) {
              std::lock_guard<std::mutex> lock(nodes_mutex_);
              auto &node = nodes_["node1"]; // TODO: extract real address
              node.last_heartbeat = std::chrono::system_clock::now();
            }
          } catch (const std::exception &e) {
            fmt::print(stderr, "Error parsing message: {}\n", e.what());
          }
        }
      }
    }

    check_node_health();
  }
}

void Coordinator::check_node_health() {
  using namespace std::chrono;
  const auto now = system_clock::now();

  std::lock_guard<std::mutex> lock(nodes_mutex_);
  for (auto &[addr, node] : nodes_) {
    node.is_healthy = (now - node.last_heartbeat) <= health_check_interval_;
  }
}

std::unordered_map<std::string, Coordinator::NodeInfo>
Coordinator::get_node_status() const {
  std::lock_guard<std::mutex> lock(nodes_mutex_);
  return nodes_;
}

void Coordinator::publish_status(const std::string &status) {
  zmq_send(publisher_, status.data(), status.size(), 0);
}

} // namespace tick_capture
