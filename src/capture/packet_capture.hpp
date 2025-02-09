#pragma once
#include "../../include/tick_capture/types.hpp"
#include "ring_buffer.hpp"
#include <atomic>
#include <boost/asio.hpp>
#include <memory>
#include <thread>

namespace tick_capture {

class PacketCapture {
public:
  explicit PacketCapture(const CaptureConfig &config);
  ~PacketCapture();

  // Non-copyable
  PacketCapture(const PacketCapture &) = delete;
  PacketCapture &operator=(const PacketCapture &) = delete;

  // Start/stop capture
  void start();
  void stop();

  // Get statistics
  CaptureStats get_stats() const;

  // Access the packet buffer
  RingBuffer<MarketMessage> &get_buffer() { return buffer_; }

private:
  void setup_socket();
  void capture_loop();
  bool validate_message(const MarketMessage &msg);

  CaptureConfig config_;
  std::atomic<bool> running_{false};
  std::thread capture_thread_;

  // Network resources
  boost::asio::io_context io_context_;
  boost::asio::ip::udp::socket socket_;
  boost::asio::ip::udp::endpoint listen_endpoint_;

  // Statistics
  std::atomic<uint64_t> messages_received_{0};
  std::atomic<uint64_t> messages_dropped_{0};
  std::atomic<uint64_t> messages_invalid_{0};

  // Buffers
  std::vector<char> recv_buffer_;
  RingBuffer<MarketMessage> buffer_;
};

} // namespace tick_capture
