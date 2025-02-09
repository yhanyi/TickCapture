#pragma once
#include <atomic>
#include <optional>
#include <type_traits>
#include <vector>

namespace tick_capture {

template <typename T> class RingBuffer {
  static_assert(std::is_trivially_copyable_v<T>,
                "T must be trivially copyable");
  static_assert(alignof(T) <= 64,
                "T must not require alignment greater than 64 bytes");

  // Ensure indices are on different cache lines
  struct alignas(64) AlignedIndex {
    std::atomic<size_t> value{0};
  };

  AlignedIndex write_idx_;
  AlignedIndex read_idx_;

  // Aligned storage for data
  alignas(64) std::vector<T> buffer_;
  const size_t mask_;

  // Statistics for monitoring
  std::atomic<size_t> total_pushed_{0};
  std::atomic<size_t> total_popped_{0};
  std::atomic<size_t> push_failures_{0};

public:
  explicit RingBuffer(size_t size)
      : buffer_(next_power_of_2(size)), mask_(buffer_.size() - 1) {}

  bool try_push(const T &item) noexcept {
    const auto current_write = write_idx_.value.load(std::memory_order_relaxed);
    const auto next_write = (current_write + 1) & mask_;

    // Check if buffer is full
    if (next_write == read_idx_.value.load(std::memory_order_acquire)) {
      push_failures_++;
      return false;
    }

    // Copy item and update write index with release semantics
    buffer_[current_write] = item;
    write_idx_.value.store(next_write, std::memory_order_release);
    total_pushed_++;
    return true;
  }

  std::optional<T> try_pop() noexcept {
    const auto current_read = read_idx_.value.load(std::memory_order_relaxed);

    // Check if buffer is empty
    if (current_read == write_idx_.value.load(std::memory_order_acquire)) {
      return std::nullopt;
    }

    // Copy item and update read index with release semantics
    T item = buffer_[current_read];
    read_idx_.value.store((current_read + 1) & mask_,
                          std::memory_order_release);
    total_popped_++;
    return item;
  }

  // Batch operations for better performance
  template <typename OutputIt>
  size_t pop_bulk(OutputIt dest, size_t max_items) noexcept {
    size_t items_popped = 0;
    const auto available = size();

    // Optimize if we can pop everything requested
    if (available >= max_items) {
      while (items_popped < max_items) {
        if (auto item = try_pop()) {
          *dest++ = *item;
          ++items_popped;
        } else {
          break;
        }
      }
    } else {
      // Pop what's available
      while (items_popped < available) {
        if (auto item = try_pop()) {
          *dest++ = *item;
          ++items_popped;
        } else {
          break;
        }
      }
    }
    return items_popped;
  }

  bool empty() const noexcept {
    return read_idx_.value.load(std::memory_order_acquire) ==
           write_idx_.value.load(std::memory_order_acquire);
  }

  size_t size() const noexcept {
    const auto read = read_idx_.value.load(std::memory_order_acquire);
    const auto write = write_idx_.value.load(std::memory_order_acquire);
    return write >= read ? write - read : buffer_.size() - (read - write);
  }

  size_t capacity() const noexcept { return buffer_.size(); }

  // Statistics
  size_t total_pushed() const noexcept { return total_pushed_; }
  size_t total_popped() const noexcept { return total_popped_; }
  size_t push_failures() const noexcept { return push_failures_; }

private:
  // Helper function to get next power of 2
  static size_t next_power_of_2(size_t v) {
    v--;
    v |= v >> 1;
    v |= v >> 2;
    v |= v >> 4;
    v |= v >> 8;
    v |= v >> 16;
    v |= v >> 32;
    v++;
    return v;
  }
};

} // namespace tick_capture
