#include "packet_capture.hpp"
#include <fmt/format.h>

namespace tick_capture {

PacketCapture::PacketCapture(const CaptureConfig &config)
    : config_(config), socket_(io_context_),
      recv_buffer_(config.udp_buffer_size), // Configurable UDP buffer size
      buffer_(config.ring_buffer_size)      // Configurable ring buffer size
{
  setup_socket();
}

PacketCapture::~PacketCapture() { stop(); }

void PacketCapture::setup_socket() {
  using namespace boost::asio::ip;

  // Create the UDP socket
  socket_.open(udp::v4());
  socket_.set_option(udp::socket::reuse_address(true));

  // Set larger socket buffers
  socket_.set_option(boost::asio::socket_base::receive_buffer_size(
      config_.socket_buffer_size)); // e.g. 32MB

  // Enable multicast
  udp::endpoint listen_endpoint(address_v4::any(), config_.port);
  socket_.bind(listen_endpoint);

  boost::system::error_code ec;
  auto multicast_addr = make_address(config_.multicast_addr, ec);
  if (ec) {
    throw std::runtime_error(
        fmt::format("Invalid multicast address: {}", ec.message()));
  }

  socket_.set_option(multicast::join_group(multicast_addr));

  // Verify socket buffer size
  boost::asio::socket_base::receive_buffer_size option;
  socket_.get_option(option);
  fmt::print("Socket receive buffer size: {} bytes\n", option.value());
}

void PacketCapture::start() {
  if (running_)
    return;
  running_ = true;
  capture_thread_ = std::thread([this] { capture_loop(); });
}

void PacketCapture::stop() {
  if (!running_)
    return;
  running_ = false;

  // Close socket to interrupt any blocking reads
  boost::system::error_code ec;
  socket_.close(ec);

  if (capture_thread_.joinable()) {
    capture_thread_.join();
  }
}

void PacketCapture::capture_loop() {
  boost::asio::ip::udp::endpoint sender_endpoint;
  const size_t msg_size = sizeof(MarketMessage);

  fmt::print("Starting capture loop. Message size: {} bytes\n", msg_size);

  while (running_) {
    try {
      // Receive data
      boost::system::error_code ec;
      size_t bytes_received = socket_.receive_from(
          boost::asio::buffer(recv_buffer_), sender_endpoint, 0, ec);

      if (ec) {
        if (ec != boost::asio::error::would_block) {
          fmt::print(stderr, "Error receiving data: {}\n", ec.message());
        }
        continue;
      }

      if (bytes_received > 0) {
        fmt::print("Received {} bytes from {}\n", bytes_received,
                   sender_endpoint.address().to_string());
      }

      // Process each complete message
      size_t processed = 0;
      while (processed + msg_size <= bytes_received) {
        const auto *msg = reinterpret_cast<const MarketMessage *>(
            recv_buffer_.data() + processed);

        // Basic validation
        if (validate_message(*msg)) {
          if (!buffer_.try_push(*msg)) {
            const auto dropped = ++messages_dropped_;
            if (dropped % 1000 == 0) {
              fmt::print(stderr, "Ring buffer full, dropped {} messages\n",
                         dropped);
            }
          } else {
            const auto received = ++messages_received_;
            if (received % 1000 == 0) {
              fmt::print("Successfully received {} messages\n", received);
            }
          }
        } else {
          const auto invalid = ++messages_invalid_;
          if (invalid % 1000 == 0) {
            fmt::print(
                stderr,
                "Invalid message: seq={}, sym={}, type={}, price={:.2f}\n",
                msg->sequence_number, msg->symbol_id,
                static_cast<int>(msg->type), msg->trade.price);
          }
        }

        processed += msg_size;
      }

    } catch (const std::exception &e) {
      if (running_) {
        fmt::print(stderr, "Error in capture loop: {}\n", e.what());
      }
    }
  }
}

bool PacketCapture::validate_message(const MarketMessage &msg) {
  if (msg.sequence_number == 0 || msg.symbol_id == 0 ||
      msg.symbol_id > 10000 || // Reasonable max symbol ID
      msg.type != MessageType::Trade || msg.trade.price <= 0 ||
      msg.trade.price > 1000000 || msg.trade.size == 0) {
    return false;
  }
  return true;
}

CaptureStats PacketCapture::get_stats() const {
  CaptureStats stats;
  stats.messages_received = messages_received_.load();
  stats.messages_dropped = messages_dropped_.load();
  stats.messages_invalid = messages_invalid_.load();
  stats.messages_processed = stats.messages_received - stats.messages_dropped;
  return stats;
}

} // namespace tick_capture
