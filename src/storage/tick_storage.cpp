#include "tick_storage.hpp"
#include <fmt/format.h>

namespace tick_capture {

TickStorage::TickStorage(const std::string &base_path) : base_path_(base_path) {
  std::filesystem::create_directories(base_path_);
}

void TickStorage::store(const MarketMessage &msg) {
  auto start_time = std::chrono::high_resolution_clock::now();

  // Get file handle for this symbol
  auto &handle = get_file_handle(msg.symbol_id);

  // Write the complete message as a single unit
  // Since we've properly aligned and padded the struct, this is now safe
  handle.file->write(reinterpret_cast<const char *>(&msg),
                     sizeof(MarketMessage));
  handle.file->flush(); // Ensure data is written to disk

  // Update statistics
  handle.messages_written++;
  handle.bytes_written += sizeof(MarketMessage);

  total_messages_++;
  total_bytes_ += sizeof(MarketMessage);

  // Update timing stats
  auto end_time = std::chrono::high_resolution_clock::now();
  total_write_time_ += std::chrono::duration_cast<std::chrono::nanoseconds>(
                           end_time - start_time)
                           .count();
}

void TickStorage::flush() {
  FileMap::accessor acc;
  for (auto it = files_.begin(); it != files_.end(); ++it) {
    it->second.file->flush();
  }
}

TickStorage::Stats TickStorage::get_stats() const {
  Stats stats;
  stats.messages_stored = total_messages_;
  stats.bytes_written = total_bytes_;
  stats.write_time = std::chrono::nanoseconds(total_write_time_);
  return stats;
}

TickStorage::FileHandle &TickStorage::get_file_handle(uint32_t symbol_id) {
  FileMap::accessor acc;
  if (!files_.find(acc, symbol_id)) {
    // Create new file handle
    auto filepath = base_path_ / fmt::format("{}.tick", symbol_id);
    auto file = std::make_unique<std::ofstream>(filepath, std::ios::binary |
                                                              std::ios::app);

    if (!file->is_open()) {
      throw std::runtime_error(
          fmt::format("Failed to open file: {}", filepath.string()));
    }

    FileHandle handle;
    handle.file = std::move(file);
    files_.insert(acc, {symbol_id, std::move(handle)});
  }

  return acc->second;
}

} // namespace tick_capture
