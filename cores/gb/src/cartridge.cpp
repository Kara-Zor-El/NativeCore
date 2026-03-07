/** NOTE: this only impliments cartridges of standard sizes (32KB * 2^n)
 * and ram with sizes of 0, 2KB, 8KB, 32KB, 128KB, 64KB plus MBC2's 512 bytes
 * The following are not supported:
 * - MMM01 (0x0B-0x0D)
 * - 0x08, 0x09
 * - MBC6 (0x20)
 * - MBC7 (0x22) (contains accelerometer + rumble)
 * - Bandai TAMA5 (0xFD)
 * - Hudson HuC-3 (0xFE)
 * - Hudson HuC-1 (0xFF)
 * Partially supported cartridges:
 * - MBC3 - no real time clock support yet, no proper latching
 * - MBC5 - no rumble support yet
 * - Pocket Camera (0xFC) - camera via host webcam
 * - No battery save/load (whilst is tracked, no saving or loading from disk)
 */
#include "cartridge.h"
#include "nativecore/camera_provider.h"
#include <cstring>
#include <unordered_map>

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

  // MBC1M detection: 1 MiB multicart ROMs have a Nintendo logo at bank $10.
  // The logo starts at offset 0x0104 within each bank's header region, so
  // for bank $10 that's at ROM offset 0x10 * 0x4000 + 0x0104 = 0x40104.
  mbc1_multicart_ = false;
  if (header_.mbc_type == MBCType::MBC1 && header_.rom_size == 1048576) {
    static const uint8_t nintendo_logo[] = {
        0xCE, 0xED, 0x66, 0x66, 0xCC, 0x0D, 0x00, 0x0B,
        0x03, 0x73, 0x00, 0x83, 0x00, 0x0C, 0x00, 0x0D,
    };
    size_t logo_offset = 0x10 * 0x4000 + 0x0104;
    if (logo_offset + 16 <= rom_.size()) {
      mbc1_multicart_ =
          std::memcmp(rom_.data() + logo_offset, nintendo_logo, 16) == 0;
    }
  }

  reset();

  // std::cout << "rom size: " << header_.rom_size << std::endl;
  // std::cout << "rom type: 0x" << static_cast<int>(rom_[0x0149]) << std::endl;
  // std::cout << "ram size: " << header_.ram_size << std::endl;
  // std::cout << "mbc type: 0x" << static_cast<int>(header_.mbc_type)
  //           << std::endl;
  // std::cout << "checksum: 0x" << static_cast<int>(header_.checksum)
  //           << std::endl;
  // std::cout << "title: " << header_.title << std::endl;
  // std::cout << std::endl;

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
  case 0xFC:
    header_.mbc_type = MBCType::PocketCamera;
    header_.has_battery = true;
    header_.has_camera = true;
    break;
  default:
    header_.mbc_type = MBCType::None;
    break;
  }

  // ROM size (0x0148)
  // this works better than the previous approach as previously it only
  // accounted for officially supported sizes
  const std::unordered_map<uint8_t, uint32_t> rom_sizes = {
      {0x00, 32768},   // 32KiB
      {0x01, 65536},   // 64KiB
      {0x02, 131072},  // 128KiB
      {0x03, 262144},  // 256KiB
      {0x04, 524288},  // 512KiB
      {0x05, 1048576}, // 1MiB
      {0x06, 2097152}, // 2MiB
      {0x07, 4194304}, // 4MiB
      {0x08, 8388608}, // 8MiB
      {0x52, 1153433}, // 1.1MiB
      {0x53, 1258291}, // 1.2MiB
      {0x54, 1572864}  // 1.5MiB
  };
  header_.rom_size = rom_sizes.at(rom_[0x0148]);

  // RAM size (0x0149)
  switch (rom_[0x0149]) {
  case 0x00:
    header_.ram_size = 0; // No RAM
    break;
  case 0x01:
    break; // Unused
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

  if (header_.mbc_type == MBCType::PocketCamera) {
    header_.ram_size = 131072; // Pocket Camera
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
  std::memset(cam_regs_, 0, sizeof(cam_regs_));
  cam_capturing_ = false;
  cam_clocks_left_ = 0;
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
  case MBCType::PocketCamera:
    return readPocketCamera(addr);
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
  case MBCType::PocketCamera:
    writePocketCamera(addr, val);
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
    uint32_t bank = 0;
    if (mbc1_mode_) {
      bank = mbc1_multicart_ ? (mbc1_bank_hi_ << 4) : (mbc1_bank_hi_ << 5);
    }
    uint32_t offset = (bank * 0x4000) + addr;
    return rom_[offset % rom_.size()];
  }
  if (addr < 0x8000) {
    uint32_t bank;
    if (mbc1_multicart_) {
      // MBC1M: upper 2-bit register at bits 4-5, lower 4-bit register (bit 4
      // ignored)
      bank = (mbc1_bank_hi_ << 4) | (mbc1_bank_lo_ & 0x0F);
    } else {
      bank = (mbc1_bank_hi_ << 5) | mbc1_bank_lo_;
    }
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
    if (mbc1_multicart_) {
      // MBC1M: 00->01 translation uses full 5-bit register value
      mbc1_bank_lo_ = val & 0x1F;
      if (mbc1_bank_lo_ == 0)
        mbc1_bank_lo_ = 1;
    } else {
      mbc1_bank_lo_ = val & 0x1F;
      if (mbc1_bank_lo_ == 0)
        mbc1_bank_lo_ = 1;
    }
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
  // MBC2 RAM: A000-A1FF primary, A200-BFFF mirror (only lower 9 bits used)
  if (addr >= 0xA000 && addr < 0xC000) {
    if (!ram_enabled_)
      return 0xFF;
    uint16_t ram_idx = (addr - 0xA000) & 0x1FF;
    return ram_[ram_idx % ram_.size()] | 0xF0;
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
  } else if (addr >= 0xA000 && addr < 0xC000) {
    if (!ram_enabled_)
      return;
    uint16_t ram_idx = (addr - 0xA000) & 0x1FF;
    ram_[ram_idx % ram_.size()] = val & 0x0F;
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

// Pocket Camera (Game Boy Camera)
uint8_t Cartridge::readPocketCamera(uint16_t addr) const {
  if (addr < 0x4000) {
    return rom_[addr];
  }
  if (addr < 0x8000) {
    uint32_t offset = (mbc5_rom_bank_ * 0x4000) + (addr - 0x4000);
    return rom_[offset % rom_.size()];
  }
  if (addr >= 0xA000 && addr < 0xC000) {
    // Bank >= 0x10: camera registers
    if (ram_bank_ >= 0x10) {
      uint16_t reg = (addr - 0xA000) & 0x7F; // registers mirror every 0x80
      if (reg == 0x00) {
        // A000 bit 0: 1 = capture in progress, 0 = finished.
        return cam_capturing_ ? 0x01 : 0x00;
      }
      // All other camera registers are write-only and return 0
      return 0x00;
    }

    // Normal RAM bank, but while a capture is
    // in progress, RAM must read back as 0x00.
    if (ram_.empty())
      return 0xFF;
    if (cam_capturing_)
      return 0x00;

    uint32_t offset = (ram_bank_ * 0x2000) + (addr - 0xA000);
    return ram_[offset % ram_.size()];
  }
  return 0xFF;
}

void Cartridge::writePocketCamera(uint16_t addr, uint8_t val) {
  if (addr < 0x2000) {
    ram_enabled_ = (val & 0x0F) == 0x0A;
  } else if (addr < 0x3000) {
    mbc5_rom_bank_ = (mbc5_rom_bank_ & 0x100) | val;
  } else if (addr < 0x4000) {
    mbc5_rom_bank_ = (mbc5_rom_bank_ & 0xFF) | ((val & 0x01) << 8);
  } else if (addr < 0x6000) {
    ram_bank_ = val & 0x1F; // banks 0x00-0x0F = RAM, 0x10 = camera regs
  } else if (addr >= 0xA000 && addr < 0xC000) {
    // Bank >= 0x10: camera registers
    if (ram_bank_ >= 0x10) {
      uint16_t reg = (addr - 0xA000) & 0x7F;
      if (reg < 0x36)
        cam_regs_[reg] = val;
      // Writing bit 0 to register 0 triggers a capture
      if (reg == 0x00 && (val & 0x01)) {
        performCameraCapture();
      }
      return;
    }
    // Normal RAM bank. Writes require RAM to be enabled and are ignored
    // while a capture is in progress.
    if (!ram_enabled_ || ram_.empty() || cam_capturing_)
      return;
    uint32_t offset = (ram_bank_ * 0x2000) + (addr - 0xA000);
    ram_[offset % ram_.size()] = val;
  }
}

void Cartridge::performCameraCapture() {
  // The Game Boy Camera captures a 128x112 image and stores it as
  // 2bpp tile data starting at RAM bank 0, offset 0x0100.
  // The image uses a 4x4 dithering matrix from camera registers A006-A035.

  static constexpr int CAM_W = 128;
  static constexpr int CAM_H = 112;

  // Get a grayscale frame from the host webcam
  uint8_t grayscale[CAM_W * CAM_H];
  bool have_frame = false;

  if (camera_provider_) {
    have_frame = camera_provider_->captureFrame(grayscale, CAM_W, CAM_H);
  }

  if (!have_frame) {
    // No camera available — fill with mid-gray (will produce a
    // dithered pattern, not just blank).
    std::memset(grayscale, 128, sizeof(grayscale));
  }

  // Build the 3-threshold dithering matrix from registers A006-A035.
  // 48 bytes = 3 thresholds x 16 entries (4x4 matrix in tile order).
  uint8_t threshold[3][16];
  for (int t = 0; t < 3; t++) {
    for (int i = 0; i < 16; i++) {
      threshold[t][i] = cam_regs_[0x06 + t * 16 + i];
    }
  }

  // Convert grayscale image into 2bpp tile data.
  // The image is 128x112 = 16x14 tiles = 224 tiles.
  // Each tile is 8x8 pixels, stored as 16 bytes (2 bytes per line).
  // Output goes to RAM bank 0 at offset 0x0100.

  if (ram_.size() < 0x2000)
    return; // safety check

  size_t ram_offset = 0x0100;

  for (int tile_y = 0; tile_y < 14; tile_y++) {
    for (int tile_x = 0; tile_x < 16; tile_x++) {
      for (int py = 0; py < 8; py++) {
        uint8_t low_byte = 0;
        uint8_t high_byte = 0;

        for (int px = 0; px < 8; px++) {
          int img_x = tile_x * 8 + px;
          int img_y = tile_y * 8 + py;
          uint8_t pixel = grayscale[img_y * CAM_W + img_x];

          // Dithering matrix index (4x4 pattern within pixel coords)
          int dither_idx = (img_y & 3) * 4 + (img_x & 3);

          // Apply 3-threshold dithering to get 2-bit color
          // (0=lightest, 3=darkest)
          uint8_t color = (pixel < threshold[0][dither_idx]) +
                          (pixel < threshold[1][dither_idx]) +
                          (pixel < threshold[2][dither_idx]);

          // Pack into 2bpp tile format
          int bit_pos = 7 - px;
          low_byte |= (color & 0x01) << bit_pos;
          high_byte |= ((color >> 1) & 0x01) << bit_pos;
        }

        if (ram_offset + 1 < ram_.size()) {
          ram_[ram_offset++] = low_byte;
          ram_[ram_offset++] = high_byte;
        }
      }
    }
  }
}

void Cartridge::tickCamera(int tcycles) {
  if (!cam_capturing_)
    return;

  cam_clocks_left_ -= tcycles;
  if (cam_clocks_left_ <= 0) {
    cam_capturing_ = false;
    cam_clocks_left_ = 0;

    performCameraCapture();
  }
}

namespace {
void writeU8(std::vector<uint8_t> &out, uint8_t v) { out.push_back(v); }
void writeU16(std::vector<uint8_t> &out, uint16_t v) {
  out.push_back(static_cast<uint8_t>(v));
  out.push_back(static_cast<uint8_t>(v >> 8));
}
void writeU32(std::vector<uint8_t> &out, uint32_t v) {
  out.push_back(static_cast<uint8_t>(v));
  out.push_back(static_cast<uint8_t>(v >> 8));
  out.push_back(static_cast<uint8_t>(v >> 16));
  out.push_back(static_cast<uint8_t>(v >> 24));
}
void writeBool(std::vector<uint8_t> &out, bool v) { out.push_back(v ? 1 : 0); }
bool readU8(const uint8_t *&p, const uint8_t *end, uint8_t &v) {
  if (p + 1 > end)
    return false;
  v = *p++;
  return true;
}
bool readU16(const uint8_t *&p, const uint8_t *end, uint16_t &v) {
  if (p + 2 > end)
    return false;
  v = static_cast<uint16_t>(p[0]) | (static_cast<uint16_t>(p[1]) << 8);
  p += 2;
  return true;
}
bool readU32(const uint8_t *&p, const uint8_t *end, uint32_t &v) {
  if (p + 4 > end)
    return false;
  v = static_cast<uint32_t>(p[0]) | (static_cast<uint32_t>(p[1]) << 8) |
      (static_cast<uint32_t>(p[2]) << 16) | (static_cast<uint32_t>(p[3]) << 24);
  p += 4;
  return true;
}
bool readBool(const uint8_t *&p, const uint8_t *end, bool &v) {
  uint8_t b;
  if (!readU8(p, end, b))
    return false;
  v = (b != 0);
  return true;
}
} // namespace

void Cartridge::saveState(std::vector<uint8_t> &out) const {
  writeU32(out, static_cast<uint32_t>(ram_.size()));
  for (uint8_t x : ram_)
    out.push_back(x);
  writeU8(out, rom_bank_);
  writeU8(out, ram_bank_);
  writeBool(out, ram_enabled_);
  writeU8(out, mbc1_bank_lo_);
  writeU8(out, mbc1_bank_hi_);
  writeBool(out, mbc1_mode_);
  writeU8(out, rtc_register_);
  writeBool(out, rtc_latched_);
  writeU8(out, rtc_latch_data_);
  writeU8(out, rtc_s_);
  writeU8(out, rtc_m_);
  writeU8(out, rtc_h_);
  writeU16(out, rtc_dl_);
  writeU8(out, rtc_dh_);
  writeU16(out, mbc5_rom_bank_);
  // Pocket Camera state
  for (size_t i = 0; i < 0x36; i++)
    writeU8(out, cam_regs_[i]);
  writeBool(out, cam_capturing_);
  writeU32(out, static_cast<uint32_t>(cam_clocks_left_));
}

bool Cartridge::loadState(const uint8_t *&data, const uint8_t *end) {
  uint32_t ram_size;
  if (!readU32(data, end, ram_size) || data + ram_size > end)
    return false;
  if (ram_size != ram_.size())
    return false; // ROM must be same
  for (uint32_t i = 0; i < ram_size; i++) {
    uint8_t b;
    if (!readU8(data, end, b))
      return false;
    ram_[i] = b;
  }
  if (!readU8(data, end, rom_bank_) || !readU8(data, end, ram_bank_) ||
      !readBool(data, end, ram_enabled_) || !readU8(data, end, mbc1_bank_lo_) ||
      !readU8(data, end, mbc1_bank_hi_) || !readBool(data, end, mbc1_mode_) ||
      !readU8(data, end, rtc_register_) || !readBool(data, end, rtc_latched_) ||
      !readU8(data, end, rtc_latch_data_) || !readU8(data, end, rtc_s_) ||
      !readU8(data, end, rtc_m_) || !readU8(data, end, rtc_h_) ||
      !readU16(data, end, rtc_dl_) || !readU8(data, end, rtc_dh_) ||
      !readU16(data, end, mbc5_rom_bank_))
    return false;
  // Pocket Camera state
  for (size_t i = 0; i < 0x36; i++) {
    if (!readU8(data, end, cam_regs_[i]))
      return false;
  }
  if (!readBool(data, end, cam_capturing_))
    return false;
  uint32_t clocks;
  if (!readU32(data, end, clocks))
    return false;
  cam_clocks_left_ = static_cast<int>(clocks);
  return true;
}

} // namespace nativecore
