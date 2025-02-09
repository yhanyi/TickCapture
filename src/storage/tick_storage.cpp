#include "tick_storage.hpp"
#include <fmt/format.h>

namespace tick_capture {

TickStorage::TickStorage(const std::string &base_path) : base_path_(base_path) {
  std::filesystem::create_directories(base_path_);
}

void TickStorage::store(const MarketMessage &msg) {
  try {
    auto &handle = get_file_handle(msg.symbol_id);
    handle.file->write(reinterpret_cast<const char *>(&msg),
                       sizeof(MarketMessage));
    handle.file->flush();

    const auto total = ++total_messages_;

    if (total % 10000 == 0) {
      fmt::print("Successfully stored {} messages\n", total);
    }

  } catch (const std::exception &e) {
    fmt::print(stderr, "Error storing message: {}\n", e.what());
  }
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
  // Validate symbol_id first
  if (symbol_id == 0 || symbol_id > 10000) {
    throw std::runtime_error(fmt::format("Invalid symbol_id: {}", symbol_id));
  }

  FileMap::accessor acc;
  if (!files_.find(acc, symbol_id)) {
    // Create new file handle
    auto filepath = base_path_ / fmt::format("{}.tick", symbol_id);
    fmt::print("Creating new file for symbol {}: {}\n", symbol_id,
               filepath.string());

    auto file = std::make_unique<std::ofstream>(
        filepath,
        std::ios::binary | std::ios::trunc // Start fresh
    );

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
