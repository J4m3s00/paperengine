#pragma once

#include <bit>
#include <cstdint>
#include <span>
#include <vector>

// Bit-packed serialization. Bits are packed LSB-first into bytes, so the
// layout is identical on every platform. The reader never reads out of
// bounds: running past the end sets a sticky error flag and returns zeros, so
// callers can parse untrusted packets and check `ok()` once at the end.
namespace Paper {
class BitWriter {
public:
  // Writes the low `bits` (0..32) bits of `value`.
  void write(std::uint32_t value, int bits) {
    for (int i = 0; i < bits;) {
      if ((m_bitCount & 7U) == 0) {
        m_bytes.push_back(0);
      }
      const int free = 8 - static_cast<int>(m_bitCount & 7U);
      const int take = free < bits - i ? free : bits - i;
      const std::uint32_t chunk = (value >> i) & ((1U << take) - 1U);
      m_bytes.back() = static_cast<std::uint8_t>(m_bytes.back() | (chunk << (m_bitCount & 7U)));
      m_bitCount += static_cast<std::size_t>(take);
      i += take;
    }
  }
  void writeBool(bool v) { write(v ? 1U : 0U, 1); }
  void writeFloat(float v) { write(std::bit_cast<std::uint32_t>(v), 32); }
  // `value` in [min, max] quantized to `bits` bits (clamped).
  void writeQuantized(float value, float min, float max, int bits) {
    write(quantize(value, min, max, bits), bits);
  }

  [[nodiscard]] static std::uint32_t quantize(float value, float min, float max, int bits) {
    const float steps = static_cast<float>((1ULL << bits) - 1ULL);
    float t = (value - min) / (max - min);
    t = t < 0.0F ? 0.0F : (t > 1.0F ? 1.0F : t);
    return static_cast<std::uint32_t>(t * steps + 0.5F);
  }

  [[nodiscard]] std::size_t bitCount() const { return m_bitCount; }
  [[nodiscard]] const std::vector<std::uint8_t> &bytes() const { return m_bytes; }
  [[nodiscard]] std::vector<std::uint8_t> take() { return std::move(m_bytes); }

private:
  std::vector<std::uint8_t> m_bytes;
  std::size_t m_bitCount = 0;
};

class BitReader {
public:
  explicit BitReader(std::span<const std::uint8_t> bytes) : m_bytes(bytes) {}

  [[nodiscard]] std::uint32_t read(int bits) {
    std::uint32_t value = 0;
    if (m_bitPos + static_cast<std::size_t>(bits) > m_bytes.size() * 8) {
      m_ok = false;
      m_bitPos = m_bytes.size() * 8;
      return 0;
    }
    for (int i = 0; i < bits;) {
      const int offset = static_cast<int>(m_bitPos & 7U);
      const int take = 8 - offset < bits - i ? 8 - offset : bits - i;
      const std::uint32_t chunk =
          (static_cast<std::uint32_t>(m_bytes[m_bitPos >> 3]) >> offset) & ((1U << take) - 1U);
      value |= chunk << i;
      m_bitPos += static_cast<std::size_t>(take);
      i += take;
    }
    return value;
  }
  [[nodiscard]] bool readBool() { return read(1) != 0; }
  [[nodiscard]] float readFloat() { return std::bit_cast<float>(read(32)); }
  [[nodiscard]] float readQuantized(float min, float max, int bits) {
    const float steps = static_cast<float>((1ULL << bits) - 1ULL);
    return min + (max - min) * (static_cast<float>(read(bits)) / steps);
  }

  [[nodiscard]] bool ok() const { return m_ok; }

private:
  std::span<const std::uint8_t> m_bytes;
  std::size_t m_bitPos = 0;
  bool m_ok = true;
};
} // namespace Paper
