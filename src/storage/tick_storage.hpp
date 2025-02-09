#pragma once
#include "../../include/tick_capture/types.hpp"
#include <filesystem>
#include <fstream>
#include <memory>
#include <tbb/concurrent_hash_map.h>

namespace tick_capture {

class TickStorage {
public:
  explicit TickStorage(const std::string &base_path);

  // Store a market message
  void store(const MarketMessage &msg);

  // Flush all buffers to disk
  void flush();

  // Get storage statistics
  struct Stats {
    uint64_t messages_stored{0};
    uint64_t bytes_written{0};
    std::chrono::nanoseconds write_time{0};
  };
  Stats get_stats() const;

private:
  // File handle for each symbol
  struct FileHandle {
    std::unique_ptr<std::ofstream> file;
    size_t messages_written{0};
    size_t bytes_written{0};
  };

  using FileMap = tbb::concurrent_hash_map<uint32_t, FileHandle>;
  FileMap files_;
  std::filesystem::path base_path_;

  // Statistics
  std::atomic<uint64_t> total_messages_{0};
  std::atomic<uint64_t> total_bytes_{0};
  std::atomic<uint64_t> total_write_time_{0};

  // Get or create file handle for symbol
  FileHandle &get_file_handle(uint32_t symbol_id);
};

} // namespace tick_capture
