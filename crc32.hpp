#pragma once

#include <cstdint>
#include <span>

namespace crypto {
class CRC32 {
public:
  void Update(std::span<const std::byte> in);
  uint32_t Final();

private:
  uint32_t m_state = 0xffff'ffff;
};
} // namespace crypto
