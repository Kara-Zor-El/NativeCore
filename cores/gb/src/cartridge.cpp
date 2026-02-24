/** NOTE: this only impliments cartridges of standard sizes (32KB * 2^n)
 * and ram with sizes of 0, 2KB, 8KB, 32KB, 128KB, 64KB plus MBC2's 512 bytes
 * The following are not supported:
 * - MMM01 (0x0B-0x0D)
 * - 0x08, 0x09
 * - MBC6 (0x20)
 * - MBC7 (0x22) (contains accelerometer + rumble)
 * - Pocket Camera (0xFC/0x1F)
 * - Bandai TAMA5 (0xFD)
 * - Hudson HuC-3 (0xFE)
 * - Hudson HuC-1 (0xFF)
 * Partially supported cartridges:
 * - MBC3 - no real time clock support yet, no proper latching
 * - MBC5 - no rumble support yet
 * - No battery save/load (whilst is tracked, no saving or loading from disk)
 */
#include "cartridge.h"

#include <algorithm>
#include <cstring>

namespace nativecore {

bool Cartridge::load(const std::vector<uint8_t> &data) {
  if (data.size() < 0x150)
    return false;

  rom_ = data;
  parseHeader();

  // Pad ROM to power-of-2 size
  size_t target = 32768; // 32KB
  while (target < rom_.size())
    target *= 2;
  rom_.resize(target, 0xFF);

  ram_.assign(header_.ram_size, 0);

  reset();
  return true;
}

void Cartridge::parseHeader() {
  // Title (0x0134-0x0143)
  header_.title.clear();
  for (int i = 0x0134; i <= 0x0143; i++) {
    char c = static_cast<char>(rom_[i]);
    if (c == 0)
      break;
    header_.title += c;
  }

  // Cartridge type (0x0147)
  uint8_t type = rom_[0x0147];
  header_.has_battery = false;
  header_.has_timer = false;
  switch (type) {
  case 0x00:
    header_.mbc_type = MBCType::None;
    break;
  case 0x01:
    header_.mbc_type = MBCType::MBC1;
    break;
  case 0x02:
    header_.mbc_type = MBCType::MBC1;
    break;
  case 0x03:
    header_.mbc_type = MBCType::MBC1;
    header_.has_battery = true;
    break;
  case 0x05:
    header_.mbc_type = MBCType::MBC2;
    break;
  case 0x06:
    header_.mbc_type = MBCType::MBC2;
    header_.has_battery = true;
    break;
  case 0x0F:
    header_.mbc_type = MBCType::MBC3;
    header_.has_battery = true;
    header_.has_timer = true;
    break;
  case 0x10:
    header_.mbc_type = MBCType::MBC3;
    header_.has_battery = true;
    header_.has_timer = true;
    break;
  case 0x11:
    header_.mbc_type = MBCType::MBC3;
    break;
  case 0x12:
    header_.mbc_type = MBCType::MBC3;
    break;
  case 0x13:
    header_.mbc_type = MBCType::MBC3;
    header_.has_battery = true;
    break;
  case 0x19:
    header_.mbc_type = MBCType::MBC5;
    break;
  case 0x1A:
    header_.mbc_type = MBCType::MBC5;
    break;
  case 0x1B:
    header_.mbc_type = MBCType::MBC5;
    header_.has_battery = true;
    break;
  case 0x1C:
    header_.mbc_type = MBCType::MBC5;
    break;
  case 0x1D:
    header_.mbc_type = MBCType::MBC5;
    break;
  case 0x1E:
    header_.mbc_type = MBCType::MBC5;
    header_.has_battery = true;
    break;
  default:
    header_.mbc_type = MBCType::None;
    break;
  }

  // ROM size (0x0148)
  uint8_t rom_code = rom_[0x0148];
  header_.rom_size = 32768 << rom_code;

  // RAM size (0x0149)
  switch (rom_[0x0149]) {
  case 0x00:
    header_.ram_size = 0;
    break;
  case 0x01:
    header_.ram_size = 2048; // 2KB
    break;
  case 0x02:
    header_.ram_size = 8192; // 8KB
    break;
  case 0x03:
    header_.ram_size = 32768; // 32KB
    break;
  case 0x04:
    header_.ram_size = 131072; // 128KB
    break;
  case 0x05:
    header_.ram_size = 65536; // 64KB
    break;
  default:
    header_.ram_size = 0;
    break;
  }

  if (header_.mbc_type == MBCType::MBC2) {
    header_.ram_size = 512; // 512 bytes
  }

  // Header checksum (0x014D)
  header_.checksum = rom_[0x014D];
}

void Cartridge::reset() {
  rom_bank_ = 1;
  ram_bank_ = 0;
  ram_enabled_ = false;
  mbc1_bank_lo_ = 1;
  mbc1_bank_hi_ = 0;
  mbc1_mode_ = false;
  rtc_register_ = 0;
  rtc_latched_ = false;
  rtc_latch_data_ = 0xFF;
  rtc_s_ = 0;
  rtc_m_ = 0;
  rtc_h_ = 0;
  rtc_dl_ = 0;
  rtc_dh_ = 0;
  mbc5_rom_bank_ = 1;
}

uint8_t Cartridge::read(uint16_t addr) const {
  switch (header_.mbc_type) {
  case MBCType::None:
    return readNone(addr);
  case MBCType::MBC1:
    return readMBC1(addr);
  case MBCType::MBC2:
    return readMBC2(addr);
  case MBCType::MBC3:
    return readMBC3(addr);
  case MBCType::MBC5:
    return readMBC5(addr);
  }
  return 0xFF;
}

void Cartridge::write(uint16_t addr, uint8_t val) {
  switch (header_.mbc_type) {
  case MBCType::None:
    writeNone(addr, val);
    break;
  case MBCType::MBC1:
    writeMBC1(addr, val);
    break;
  case MBCType::MBC2:
    writeMBC2(addr, val);
    break;
  case MBCType::MBC3:
    writeMBC3(addr, val);
    break;
  case MBCType::MBC5:
    writeMBC5(addr, val);
    break;
  }
}

// No MBC

uint8_t Cartridge::readNone(uint16_t addr) const {
  if (addr < 0x8000) {
    if (addr < rom_.size())
      return rom_[addr];
    return 0xFF;
  }
  if (addr >= 0xA000 && addr < 0xC000) {
    uint16_t offset = addr - 0xA000;
    if (offset < ram_.size())
      return ram_[offset];
  }
  return 0xFF;
}

void Cartridge::writeNone(uint16_t addr, uint8_t val) {
  if (addr >= 0xA000 && addr < 0xC000) {
    uint16_t offset = addr - 0xA000;
    if (offset < ram_.size())
      ram_[offset] = val;
  }
}

// MBC1

uint8_t Cartridge::readMBC1(uint16_t addr) const {
  if (addr < 0x4000) {
    uint32_t bank = mbc1_mode_ ? (mbc1_bank_hi_ << 5) : 0;
    uint32_t offset = (bank * 0x4000) + addr;
    return rom_[offset % rom_.size()];
  }
  if (addr < 0x8000) {
    uint32_t bank = (mbc1_bank_hi_ << 5) | mbc1_bank_lo_;
    uint32_t offset = (bank * 0x4000) + (addr - 0x4000);
    return rom_[offset % rom_.size()];
  }
  if (addr >= 0xA000 && addr < 0xC000) {
    if (!ram_enabled_ || ram_.empty())
      return 0xFF;
    uint32_t bank = mbc1_mode_ ? mbc1_bank_hi_ : 0;
    uint32_t offset = (bank * 0x2000) + (addr - 0xA000);
    return ram_[offset % ram_.size()];
  }
  return 0xFF;
}

void Cartridge::writeMBC1(uint16_t addr, uint8_t val) {
  if (addr < 0x2000) {
    ram_enabled_ = (val & 0x0F) == 0x0A;
  } else if (addr < 0x4000) {
    mbc1_bank_lo_ = val & 0x1F;
    if (mbc1_bank_lo_ == 0)
      mbc1_bank_lo_ = 1;
  } else if (addr < 0x6000) {
    mbc1_bank_hi_ = val & 0x03;
  } else if (addr < 0x8000) {
    mbc1_mode_ = val & 0x01;
  } else if (addr >= 0xA000 && addr < 0xC000) {
    if (!ram_enabled_ || ram_.empty())
      return;
    uint32_t bank = mbc1_mode_ ? mbc1_bank_hi_ : 0;
    uint32_t offset = (bank * 0x2000) + (addr - 0xA000);
    ram_[offset % ram_.size()] = val;
  }
}

// MBC2

uint8_t Cartridge::readMBC2(uint16_t addr) const {
  if (addr < 0x4000) {
    return rom_[addr];
  }
  if (addr < 0x8000) {
    uint32_t bank = rom_bank_ ? rom_bank_ : 1;
    uint32_t offset = (bank * 0x4000) + (addr - 0x4000);
    return rom_[offset % rom_.size()];
  }
  if (addr >= 0xA000 && addr < 0xA200) {
    if (!ram_enabled_)
      return 0xFF;
    return ram_[(addr - 0xA000) % ram_.size()] | 0xF0;
  }
  return 0xFF;
}

void Cartridge::writeMBC2(uint16_t addr, uint8_t val) {
  if (addr < 0x4000) {
    if (addr & 0x0100) {
      rom_bank_ = val & 0x0F;
      if (rom_bank_ == 0)
        rom_bank_ = 1;
    } else {
      ram_enabled_ = (val & 0x0F) == 0x0A;
    }
  } else if (addr >= 0xA000 && addr < 0xA200) {
    if (!ram_enabled_)
      return;
    ram_[(addr - 0xA000) % ram_.size()] = val & 0x0F;
  }
}

// MBC3

uint8_t Cartridge::readMBC3(uint16_t addr) const {
  if (addr < 0x4000) {
    return rom_[addr];
  }
  if (addr < 0x8000) {
    uint32_t bank = rom_bank_ ? rom_bank_ : 1;
    uint32_t offset = (bank * 0x4000) + (addr - 0x4000);
    return rom_[offset % rom_.size()];
  }
  if (addr >= 0xA000 && addr < 0xC000) {
    if (!ram_enabled_)
      return 0xFF;
    if (ram_bank_ <= 3) {
      if (ram_.empty())
        return 0xFF;
      uint32_t offset = (ram_bank_ * 0x2000) + (addr - 0xA000);
      return ram_[offset % ram_.size()];
    }
    // RTC registers
    switch (ram_bank_) {
    case 0x08:
      return rtc_s_;
    case 0x09:
      return rtc_m_;
    case 0x0A:
      return rtc_h_;
    case 0x0B:
      return rtc_dl_ & 0xFF;
    case 0x0C:
      return rtc_dh_;
    }
  }
  return 0xFF;
}

void Cartridge::writeMBC3(uint16_t addr, uint8_t val) {
  if (addr < 0x2000) {
    ram_enabled_ = (val & 0x0F) == 0x0A;
  } else if (addr < 0x4000) {
    rom_bank_ = val & 0x7F;
    if (rom_bank_ == 0)
      rom_bank_ = 1;
  } else if (addr < 0x6000) {
    ram_bank_ = val;
  } else if (addr < 0x8000) {
    // Latch clock
    if (rtc_latch_data_ == 0x00 && val == 0x01) {
      rtc_latched_ = !rtc_latched_;
    }
    rtc_latch_data_ = val;
  } else if (addr >= 0xA000 && addr < 0xC000) {
    if (!ram_enabled_)
      return;
    if (ram_bank_ <= 3) {
      if (ram_.empty())
        return;
      uint32_t offset = (ram_bank_ * 0x2000) + (addr - 0xA000);
      ram_[offset % ram_.size()] = val;
    }
    switch (ram_bank_) {
    case 0x08:
      rtc_s_ = val & 0x3F;
      break;
    case 0x09:
      rtc_m_ = val & 0x3F;
      break;
    case 0x0A:
      rtc_h_ = val & 0x1F;
      break;
    case 0x0B:
      rtc_dl_ = val;
      break;
    case 0x0C:
      rtc_dh_ = val & 0xC1;
      break;
    }
  }
}

// MBC5

uint8_t Cartridge::readMBC5(uint16_t addr) const {
  if (addr < 0x4000) {
    return rom_[addr];
  }
  if (addr < 0x8000) {
    uint32_t offset = (mbc5_rom_bank_ * 0x4000) + (addr - 0x4000);
    return rom_[offset % rom_.size()];
  }
  if (addr >= 0xA000 && addr < 0xC000) {
    if (!ram_enabled_ || ram_.empty())
      return 0xFF;
    uint32_t offset = (ram_bank_ * 0x2000) + (addr - 0xA000);
    return ram_[offset % ram_.size()];
  }
  return 0xFF;
}

void Cartridge::writeMBC5(uint16_t addr, uint8_t val) {
  if (addr < 0x2000) {
    ram_enabled_ = (val & 0x0F) == 0x0A;
  } else if (addr < 0x3000) {
    mbc5_rom_bank_ = (mbc5_rom_bank_ & 0x100) | val;
  } else if (addr < 0x4000) {
    mbc5_rom_bank_ = (mbc5_rom_bank_ & 0xFF) | ((val & 0x01) << 8);
  } else if (addr < 0x6000) {
    ram_bank_ = val & 0x0F;
  } else if (addr >= 0xA000 && addr < 0xC000) {
    if (!ram_enabled_ || ram_.empty())
      return;
    uint32_t offset = (ram_bank_ * 0x2000) + (addr - 0xA000);
    ram_[offset % ram_.size()] = val;
  }
}

} // namespace nativecore
