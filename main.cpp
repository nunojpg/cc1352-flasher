#include <cassert>
#include <cmath>
#include <filesystem>
#include <print>
#include <span>
#include <string>
#include <thread>

#include <fcntl.h>
#include <termios.h>

#include "crc32.hpp"
#include "intel_hex.hpp"

void Bootloader(const intel_hex::Segments &data, const uint32_t FLASH_SIZE) {
  const uint32_t LAST_PAGE = FLASH_SIZE - 8192;
  const uint32_t BL_CONFIG_offset = LAST_PAGE + 0x1FD8;
  const auto BACKDOOR_ENABLE = data.At(BL_CONFIG_offset + 0);
  const auto BL_PIN_NUMBER = data.At(BL_CONFIG_offset + 1);
  const auto BL_LEVEL = data.At(BL_CONFIG_offset + 2);
  const auto BOOTLOADER_ENABLE = data.At(BL_CONFIG_offset + 3);
  if (BOOTLOADER_ENABLE == std::byte(0xc5) &&        //
      BACKDOOR_ENABLE == std::byte(0xc5) &&          //
      (BL_LEVEL & std::byte(0x1)) == std::byte(0) && //
      BL_PIN_NUMBER == std::byte(15)) {
    return;
  }
  std::print(stderr, "Invalid Bootloader config in firmware file:\n");
  std::print(stderr, "{:02x}\n", (uint8_t)BOOTLOADER_ENABLE);
  std::print(stderr, "{:02x}\n", (uint8_t)BL_LEVEL);
  std::print(stderr, "{:02x}\n", (uint8_t)BL_PIN_NUMBER);
  std::print(stderr, "{:02x}\n", (uint8_t)BACKDOOR_ENABLE);
  throw std::runtime_error("Invalid bootloader config");
}

void Write(int fd, const void *ptr, uint16_t len) {
  if (write(fd, ptr, len) != len) {
    throw std::runtime_error("error");
  }
}
uint8_t Read(int fd) {
  char ch;
  for (int i = 0; i < 1000; ++i) {
    if (read(fd, &ch, 1) == 1) {
      return ch;
    }
    std::this_thread::sleep_for(std::chrono::microseconds(i * 10));
  }
  throw std::runtime_error("timeout");
}
void SendACK(int fd) {
  uint8_t buf[2]{0x00, 0xcc};
  Write(fd, buf, 2);
}
void WaitACKOnly(int fd) {
  if (Read(fd) != 0x00) {
    throw std::runtime_error("ACK error");
  }
  if (Read(fd) != 0xcc) {
    throw std::runtime_error("ACK error");
  }
}
uint8_t GetStatus(int fd) {
  uint8_t buf[3]{3, 0x23, 0x23};
  Write(fd, buf, 3);
  WaitACKOnly(fd);
  if (Read(fd) != 3) {
    throw std::runtime_error("unexpected len");
  }
  const auto ret = Read(fd);
  if (Read(fd) != ret) {
    throw std::runtime_error("unexpected checksum");
  }
  SendACK(fd);
  return ret;
}
void WaitACK(int fd) {
  WaitACKOnly(fd);
  if (GetStatus(fd) != 0x40) {
    throw std::runtime_error("cmd failed");
  }
}
void Sync(int fd) {
  uint8_t buf[2]{0x55, 0x55};
  Write(fd, buf, 2);
  WaitACKOnly(fd);
}
void Reset(int fd) {
  uint8_t buf[3]{3, 0x25, 0x25};
  Write(fd, buf, 3);
  WaitACKOnly(fd);
}
void Ping(int fd) {
  uint8_t buf[3]{3, 0x20, 0x20};
  Write(fd, buf, 3);
  WaitACK(fd);
}
void GetChipID(int fd) {
  uint8_t buf[3]{3, 0x28, 0x28};
  Write(fd, buf, 3);
  WaitACKOnly(fd);
  std::print("Chip ID: {:02x} ", Read(fd)); // len 0x06
  std::print("{:02x} ", Read(fd));          // checksum 0x84
  std::print("{:02x} ", Read(fd));          // 0x12
  std::print("{:02x} ", Read(fd));          // 0x82
  std::print("{:02x} ", Read(fd));          // 0xf0
  std::print("{:02x}\n", Read(fd));         // 0x00
  SendACK(fd);
  if (GetStatus(fd) != 0x40) {
    throw std::runtime_error("cmd failed");
  }
}
void EraseAllMemory(int fd) {
  uint8_t buf[3]{3, 0x2C, 0x2C};
  Write(fd, buf, 3);
  WaitACK(fd);
}
void Download(int fd, uint32_t from, uint32_t size) {
  uint8_t buf[11]{11,
                  0,
                  0x21,
                  uint8_t(from >> 24),
                  uint8_t(from >> 16),
                  uint8_t(from >> 8),
                  uint8_t(from >> 0),
                  uint8_t(size >> 24),
                  uint8_t(size >> 16),
                  uint8_t(size >> 8),
                  uint8_t(size >> 0)};
  for (auto b : std::span{buf}.subspan(2)) {
    buf[1] += b;
  }
  Write(fd, buf, 11);
  WaitACK(fd);
}
uint32_t CRC32(int fd, uint32_t from, uint32_t size) {
  uint8_t buf[]{15,
                0,
                0x27,
                uint8_t(from >> 24),
                uint8_t(from >> 16),
                uint8_t(from >> 8),
                uint8_t(from >> 0),
                uint8_t(size >> 24),
                uint8_t(size >> 16),
                uint8_t(size >> 8),
                uint8_t(size >> 0),
                0,
                0,
                0,
                0};
  for (auto b : std::span{buf}.subspan(2)) {
    buf[1] += b;
  }
  Write(fd, buf, sizeof(buf));
  WaitACKOnly(fd);
  Read(fd);
  Read(fd);
  uint32_t crc = Read(fd) << 24;
  crc |= Read(fd) << 16;
  crc |= Read(fd) << 8;
  crc |= Read(fd);
  SendACK(fd);
  if (GetStatus(fd) != 0x40) {
    throw std::runtime_error("cmd failed");
  }
  return crc;
}
void WriteData(int fd, uint32_t address, std::span<const std::byte> data) {
  assert(address % 4 == 0);
  const uint32_t size_padded = (data.size() + 3) & (~3);
  Download(fd, address, size_padded);
  uint32_t pos = 0;
  crypto::CRC32 crc;
  crc.Update(data);
  while (pos != size_padded) {
    const uint8_t now = std::min<uint16_t>(252, size_padded - pos);
    const uint8_t remain = std::min<uint16_t>(now, data.size() - pos);
    uint8_t buf[3]{uint8_t(now + 3), 0x24, 0x24};
    if (remain) {
      for (unsigned i = pos; i < pos + remain; ++i) {
        buf[1] += uint8_t(data[i]);
      }
    }
    if (remain != now) {
      for (int i = 0; i < now - remain; ++i) {
        buf[1] += 0xff;
      }
    }
    Write(fd, buf, 3);
    if (remain) {
      Write(fd, &data[pos], remain);
    }
    if (remain != now) {
      buf[0] = 0xff;
      buf[1] = 0xff;
      buf[2] = 0xff;
      crc.Update(std::as_bytes(std::span{buf}.first(now - remain)));
      Write(fd, buf, now - remain); // write 1 to 3
    }
    WaitACKOnly(fd);
    pos += now;
  }
  const auto expected = crc.Final();
  const auto got = CRC32(fd, address, size_padded);
  if (expected != got) {
    std::print(stderr, "CRC32 expected {:08x} got {:08x}\n", expected, got);
    throw std::runtime_error("invalid crc");
  }
  std::print("CRC32 {:08x} OK\n", got);
}
std::string FindSerialPort() {
  std::string path;
  std::print("Serial devices:\n");
  for (const auto &entry :
       std::filesystem::directory_iterator("/dev/serial/by-id")) {
    if (!entry.path().filename().string().starts_with(
            "usb-BeagleBoard.org_BeagleConnect_"))
      continue;
    std::print(" {}\n", entry.path().filename().string());
    if (!path.empty()) {
      throw std::runtime_error("multiple boards found");
    }
    path = entry.path().string();
  }
  if (path.empty()) {
    throw std::runtime_error("no board found");
  }
  return path;
}
int main(int argc, char **argv) {
  if (argc != 2) {
    std::print(stderr, "Usage: {} <intel hex file>\n", argv[0]);
    return 1;
  }
  auto data = intel_hex::ReadFile(argv[1]);
  constexpr uint32_t FLASH_SIZE = 704 * 1024;
  Bootloader(data, FLASH_SIZE);
  const auto dev = FindSerialPort();
  const auto fd = open(dev.c_str(), O_RDWR | O_NOCTTY | O_SYNC | O_NONBLOCK);
  if (fd < 0)
    throw std::runtime_error("Failed to open serial port");
  if (!isatty(fd))
    throw std::runtime_error("File is not a TTY");
  termios config;
  cfmakeraw(&config);
  config.c_iflag = 0;
  config.c_oflag = 0;
  config.c_lflag = 0;
  config.c_cflag = CREAD | CS8 | PARENB | PARODD;
  if (tcsetattr(fd, TCSAFLUSH, &config) < 0)
    throw std::runtime_error("Failed to config attributes");

  tcsendbreak(fd, 0);
  tcflush(fd, TCIOFLUSH);
  Sync(fd);
  std::print("Bridge OK\n");
  Ping(fd);
  std::print("Chip OK\n");
  GetChipID(fd);
  EraseAllMemory(fd);
  for (const auto &s : data.segments()) {
    WriteData(fd, s.start, s.data);
  }
  std::print("Flash completed\n");
  Reset(fd);
  std::print("Reset...\n");
  return 0;
}
