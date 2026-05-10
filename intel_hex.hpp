#pragma once

#include <cstdint>
#include <string>
#include <vector>

namespace intel_hex {
struct Segment {
  uint32_t start;
  std::vector<std::byte> data;
};
class Segments {
public:
  Segments() = default;
  Segments(std::vector<Segment> &&s) : m_segments{std::move(s)} {}
  std::byte At(uint32_t addr) const;
  const auto &segments() const { return m_segments; }

private:
  const std::vector<Segment> m_segments;
};
Segments ReadFile(const std::string &filename);
} // namespace intel_hex
