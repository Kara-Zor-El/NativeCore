#pragma once

#include <cstdint>
#include <memory>
#include <string>
#include <vector>

namespace nativecore {

enum class MBCType {
  None,
  MBC1,
  MBC2,
  MBC3,
  MBC5,
};

struct CartridgeHeader {
  std::string title;
  MBCType mbc_type = MBCType::None;
  uint32_t rom_size = 0;
  uint32_t ram_size = 0;
  bool has_battery = false;
  bool has_timer = false;
  uint8_t checksum = 0;
};

class Cartridge {
public:
  bool load(const std::vector<uint8_t> &data);
  void reset();

  uint8_t read(uint16_t addr) const;
  void write(uint16_t addr, uint8_t val);

  const CartridgeHeader &header() const { return header_; }
  bool loaded() const { return !rom_.empty(); }

  const std::vector<uint8_t> &ram() const { return ram_; }
  void setRAM(const std::vector<uint8_t> &data) { ram_ = data; }

private:
  CartridgeHeader header_;
  std::vector<uint8_t> rom_;
  std::vector<uint8_t> ram_;

  uint8_t rom_bank_ = 1;
  uint8_t ram_bank_ = 0;
  bool ram_enabled_ = false;

  // MBC1
  uint8_t mbc1_bank_lo_ = 1;
  uint8_t mbc1_bank_hi_ = 0;
  bool mbc1_mode_ = false;

  // MBC3
  uint8_t rtc_register_ = 0;
  bool rtc_latched_ = false;
  uint8_t rtc_latch_data_ = 0xFF;
  uint8_t rtc_s_ = 0, rtc_m_ = 0, rtc_h_ = 0;
  uint16_t rtc_dl_ = 0;
  uint8_t rtc_dh_ = 0;

  // MBC5
  uint16_t mbc5_rom_bank_ = 1;

  void parseHeader();

  uint8_t readNone(uint16_t addr) const;
  void writeNone(uint16_t addr, uint8_t val);

  uint8_t readMBC1(uint16_t addr) const;
  void writeMBC1(uint16_t addr, uint8_t val);

  uint8_t readMBC2(uint16_t addr) const;
  void writeMBC2(uint16_t addr, uint8_t val);

  uint8_t readMBC3(uint16_t addr) const;
  void writeMBC3(uint16_t addr, uint8_t val);

  uint8_t readMBC5(uint16_t addr) const;
  void writeMBC5(uint16_t addr, uint8_t val);
};

} // namespace nativecore
