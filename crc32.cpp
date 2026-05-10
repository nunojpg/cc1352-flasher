#include "crc32.hpp"

namespace crypto {
void CRC32::Update(std::span<const std::byte> in) {
  for (auto byte : in) {
    m_state ^= (uint8_t)byte;
    for (unsigned i = 0; i < 8; ++i) {
      if (m_state & 1u) {
        m_state = (m_state >> 1) ^ 0xEDB88320u;
      } else {
        m_state >>= 1;
      }
    }
  }
}
uint32_t CRC32::Final() { return ~m_state; }
} // namespace crypto
