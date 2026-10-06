#pragma once

#include <cstdint>
#include <optional>
#include <utility>
#include <vector>

namespace Paper {
// Slot map behind the Handle<Tag> types: stable indices, generation bumped on
// destroy so stale handles are detected. Not thread-safe.
template <typename T, typename HandleT> class ResourcePool {
public:
  HandleT create(T value) {
    std::uint32_t index = 0;
    if (!m_free.empty()) {
      index = m_free.back();
      m_free.pop_back();
    } else {
      index = static_cast<std::uint32_t>(m_slots.size());
      m_slots.emplace_back();
    }
    m_slots[index].value.emplace(std::move(value));
    return HandleT{index, m_slots[index].generation};
  }

  [[nodiscard]] T *get(HandleT handle) {
    if (!handle.valid() || handle.index >= m_slots.size()) {
      return nullptr;
    }
    Slot &slot = m_slots[handle.index];
    return slot.value && slot.generation == handle.generation
               ? &*slot.value
               : nullptr;
  }

  // Removes the resource and returns it, or nullopt for a stale handle.
  std::optional<T> destroy(HandleT handle) {
    if (get(handle) == nullptr) {
      return std::nullopt;
    }
    Slot &slot = m_slots[handle.index];
    std::optional<T> taken = std::move(slot.value);
    slot.value.reset();
    ++slot.generation;
    m_free.push_back(handle.index);
    return taken;
  }

  void clear() {
    m_slots.clear();
    m_free.clear();
  }

private:
  struct Slot {
    std::optional<T> value;
    std::uint32_t generation = 0;
  };

  std::vector<Slot> m_slots;
  std::vector<std::uint32_t> m_free;
};
} // namespace Paper
