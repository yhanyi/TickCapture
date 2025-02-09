#pragma once
#include <chrono>
#include <cstdint>
#include <string>
#include <vector>

namespace tick_capture {

enum class MessageType : uint8_t {
  Trade = 1,
  Quote = 2,
  OrderAdd = 3,
  OrderModify = 4,
  OrderCancel = 5
};

// Fixed-size message structure with explicit padding, alignment and checksum
struct alignas(8) MarketMessage {
  // Header (24 bytes)
  uint64_t sequence_number; // 8 bytes
  uint64_t timestamp;       // 8 bytes
  uint32_t checksum;        // 4 bytes (New!)
  uint32_t reserved;        // 4 bytes (for future use)

  // Identifiers (8 bytes)
  uint32_t symbol_id; // 4 bytes
  MessageType type;   // 1 byte
  uint8_t padding[3]; // 3 bytes explicit padding

  // Data section (32 bytes)
  union {
    struct {
      double price;
      uint32_t size;
      uint8_t flags;
      uint8_t padding[3];
    } trade;

    uint8_t raw[32]; // Ensure fixed size
  };

  // Initialize with default values
  MarketMessage()
      : sequence_number(0), timestamp(0), checksum(0), reserved(0),
        symbol_id(0), type(MessageType::Trade) {
    std::memset(&trade, 0, sizeof(trade));
  }

  // Calculate checksum
  uint32_t calculate_checksum() const {
    uint32_t sum = 0;
    const uint32_t *ptr = reinterpret_cast<const uint32_t *>(this);
    // Skip the checksum field itself
    for (size_t i = 2; i < sizeof(MarketMessage) / 4; ++i) {
      sum ^= ptr[i];
    }
    return sum;
  }

  // Validate message
  bool is_valid() const {
    return sequence_number > 0 && symbol_id > 0 && symbol_id <= 10000 &&
           type == MessageType::Trade && trade.price > 0 &&
           trade.price < 1000000 && trade.size > 0 &&
           checksum == calculate_checksum();
  }

  // Update checksum before sending
  void update_checksum() { checksum = calculate_checksum(); }
};

static_assert(sizeof(MarketMessage) == 64, "MarketMessage must be 64 bytes");
static_assert(alignof(MarketMessage) == 8,
              "MarketMessage must be 8-byte aligned");

struct CaptureConfig {
  // Network settings
  std::string multicast_addr = "239.255.0.1";
  uint16_t port = 12345;

  // Buffer sizes
  size_t ring_buffer_size = 131072;     // Increased to 128K entries
  size_t udp_buffer_size = 262144;      // Increased to 256KB
  size_t socket_buffer_size = 33554432; // 32MB

  // Batch sizes
  size_t max_batch_size = 256; // Maximum messages to process in one batch

  // Storage settings
  std::string output_dir;

  // Feature flags
  bool enable_timestamps = false;
  bool verify_checksums = true; // New option

  // Coordinator settings (optional)
  std::string coordinator_address;
  std::vector<std::string> peer_addresses;
};

struct CaptureStats {
  uint64_t messages_received = 0;
  uint64_t messages_processed = 0;
  uint64_t messages_dropped = 0;
  uint64_t messages_invalid = 0;
  uint64_t checksum_errors = 0; // New counter
  std::chrono::nanoseconds avg_latency{0};
  std::chrono::nanoseconds max_latency{0};
};

} // namespace tick_capture
