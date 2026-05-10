#include "intel_hex.hpp"

#include <fstream>
#include <print>

namespace {
constexpr uint8_t ReadHexdecimal(const char *hex) {
  auto digit = [](char ch) {
    if (ch <= '9')
      return ch - '0';
    if (ch <= 'F')
      return ch - ('A' - 10);
    return ch - ('a' - 10);
  };
  return (digit(hex[0]) << 4) + digit(hex[1]);
}
static_assert(ReadHexdecimal("00") == 0);
static_assert(ReadHexdecimal("FF") == 0xff);
static_assert(ReadHexdecimal("ff") == 0xff);
static_assert(ReadHexdecimal("05") == 0x05);
static_assert(ReadHexdecimal("50") == 0x50);
} // namespace

namespace intel_hex {
enum class Type : uint8_t {
  Data = 0,
  EndOfFile = 1,
  ExtendedSegmentAddress = 2,
  StartSegmentAddress = 3,
};
std::byte Segments::At(uint32_t addr) const {
  for (const auto &s : m_segments) {
    if (addr < s.start)
      continue;
    auto offset = addr - s.start;
    if (offset >= s.data.size())
      continue;
    return s.data[offset];
  }
  throw std::runtime_error("address not preset");
}
Segments ReadFile(const std::string &filename) {
  std::vector<Segment> hex;
  std::ifstream file(filename, std::ios::binary);
  Type type = Type::Data;
  uint32_t base_address = 0;
  uint16_t current_offset = 0xffff;

  std::vector<uint8_t> bytes;
  for (std::string line; std::getline(file, line);) {
    if (type == Type::EndOfFile)
      throw std::runtime_error("data after EOF");
    if (line.length() < 11)
      return {};
    if (line[0] != ':')
      return {};
    bytes.clear();
    {
      uint8_t checksum = 0;
      for (unsigned i = 0; i < (line.length() - 1) / 2; ++i) {
        auto val = ReadHexdecimal(&line[1 + i * 2]);
        checksum += val;
        bytes.emplace_back(val);
      }
      if (checksum) {
        throw std::runtime_error("invalid checksum");
      }
    }
    const auto len = bytes[0];
    const uint16_t addr = uint16_t(bytes[1]) << 8 | bytes[2];
    type = Type(bytes[3]);
    if (type == Type::Data) {
      if (len + 5u != bytes.size())
        throw std::runtime_error("invalid length");
      if (addr != current_offset) {
        hex.emplace_back(base_address + addr);
        current_offset = addr;
      }
      current_offset += len;
      for (unsigned i = 0; i < len; ++i) {
        hex.back().data.emplace_back(std::byte(bytes[4 + i]));
      }
    } else if (type == Type::ExtendedSegmentAddress) {
      if (len != 2 || bytes.size() != 7 || addr != 0)
        throw std::runtime_error("invalid");
      base_address = uint32_t(bytes[4]) << 12 | uint32_t(bytes[5]) << 4;
      current_offset = 0xffff;
    } else if (type == Type::StartSegmentAddress) {
      if (len != 4 || bytes.size() != 9 || addr != 0)
        throw std::runtime_error("invalid");
      const auto start = uint32_t(bytes[4]) << 24 | uint32_t(bytes[5]) << 16 |
                         uint32_t(bytes[6]) << 8 | uint32_t(bytes[7]);
      std::print("Start Segment Address: 0x{:08x}\n", start);
    } else if (type == Type::EndOfFile) {
    } else {
      throw std::runtime_error("Record type not supported");
    }
  }
  std::print("Segments: \n");
  for (const auto &s : hex) {
    std::print(" 0x{:08x} {}\n", s.start, s.data.size());
  }
  return hex;
}

} // namespace intel_hex
