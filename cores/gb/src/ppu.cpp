#include "ppu.h"
#include "gb_core.h"

#include <algorithm>
#include <cstring>
#include <vector>

namespace nativecore {

namespace {
void writeU32(std::vector<uint8_t> &out, uint32_t v) {
  out.push_back(static_cast<uint8_t>(v));
  out.push_back(static_cast<uint8_t>(v >> 8));
  out.push_back(static_cast<uint8_t>(v >> 16));
  out.push_back(static_cast<uint8_t>(v >> 24));
}
bool readU8(const uint8_t *&p, const uint8_t *end, uint8_t &v) {
  if (p + 1 > end)
    return false;
  v = *p++;
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

void PPU::reset() {
  lcdc_ = 0x91;
  // Post-boot: LYC flag (bit 2) set because LY=0 == LYC=0 at boot.
  // Mode bits are 0 (will be reflected via mode_ field).
  stat_ = 0x04; // LYC coincidence flag set (LY==LYC==0 at boot)
  scy_ = 0;
  scx_ = 0;
  ly_ = 0;
  lyc_ = 0;
  bgp_ = 0xFC;
  obp0_ = 0xFF;
  obp1_ = 0xFF;
  wy_ = 0;
  wx_ = 0;
  // At PC=$0100 on real DMG hardware the PPU is already partway through a
  // frame. The PPU clock is 34 T-cycles ahead of the current OAM-Search
  // line start. This places the boot_hwio register reads at the correct
  // PPU state: STAT shows HBlank on line 9, and LY=10 on its read.
  mode_ = Mode::OAMScan;
  mode_clock_ = 34;
  window_line_ = 0;
  window_was_active_ = false;
  lcd_on_delay_ = false;
  frame_ready_ = false;
  stat_line_ = false;
  vram_.fill(0);
  oam_.fill(0);
  framebuffer_.fill(PALETTE[0]);
}

uint8_t PPU::readVRAM(uint16_t addr) const {
  if (mode_ == Mode::PixelTransfer && (lcdc_ & 0x80))
    return 0xFF;
  return vram_[addr & 0x1FFF];
}

void PPU::writeVRAM(uint16_t addr, uint8_t val) {
  if (mode_ == Mode::PixelTransfer && (lcdc_ & 0x80))
    return;
  vram_[addr & 0x1FFF] = val;
}

// OAM is locked during OAMSearch and PixelTransfer, except for the last
// 4 T-cycles of OAMSearch (mode_clock_ >= 76): on real DMG hardware the OAM
// becomes accessible one M-cycle before PixelTransfer begins.
bool PPU::oamLocked() const {
  if (!(lcdc_ & 0x80))
    return false;
  if (mode_ == Mode::PixelTransfer)
    return true;
  if (mode_ == Mode::OAMScan && mode_clock_ < 76)
    return true;
  return false;
}

uint8_t PPU::readOAM(uint16_t addr) const {
  if (oamLocked())
    return 0xFF;
  return oam_[addr & 0xFF];
}

void PPU::writeOAM(uint16_t addr, uint8_t val) {
  if (oamLocked())
    return;
  oam_[addr & 0xFF] = val;
}

void PPU::oamDMAWrite(uint8_t index, uint8_t val) { oam_[index] = val; }

uint8_t PPU::readRegister(uint16_t addr) const {
  switch (addr) {
  case 0xFF40:
    return lcdc_;
  case 0xFF41:
    return stat_ | 0x80 | static_cast<uint8_t>(mode_);
  case 0xFF42:
    return scy_;
  case 0xFF43:
    return scx_;
  case 0xFF44:
    return ly_;
  case 0xFF45:
    return lyc_;
  case 0xFF47:
    return bgp_;
  case 0xFF48:
    return obp0_;
  case 0xFF49:
    return obp1_;
  case 0xFF4A:
    return wy_;
  case 0xFF4B:
    return wx_;
  default:
    return 0xFF;
  }
}

void PPU::writeRegister(uint16_t addr, uint8_t val) {
  switch (addr) {
  case 0xFF40: {
    bool was_on = lcdc_ & 0x80;
    lcdc_ = val;
    if (was_on && !(val & 0x80)) {
      // LCD turned off: reset to line 0, HBlank mode
      // Mode bits (0-1) are cleared; LYC coincidence bit (2) is RETAINED.
      // stat_line_ is also retained so that re-enabling PPU can detect edge
      // correctly.
      ly_ = 0;
      mode_ = Mode::HBlank;
      mode_clock_ = 0;
      stat_ &= ~0x03; // clear mode bits (0-1) only
    }
    if (!was_on && (val & 0x80)) {
      // LCD turned on: starts in mode 0 (HBlank) for the first few cycles
      // before beginning OAM search. The first line has special timing.
      mode_ = Mode::HBlank;
      mode_clock_ = 0;
      window_line_ = 0;
      lcd_on_delay_ = true; // first scanline has unusual timing
      // Update LYC coincidence bit and potentially fire STAT interrupt
      bool match = (ly_ == lyc_);
      if (match)
        stat_ |= 0x04;
      else
        stat_ &= ~0x04;
      checkSTATInterrupt();
    }
    break;
  }
  case 0xFF41:
    stat_ = (val & 0x78);
    checkSTATInterrupt();
    break;
  case 0xFF42:
    scy_ = val;
    break;
  case 0xFF43:
    scx_ = val;
    break;
  case 0xFF44:
    break; // LY is read-only
  case 0xFF45:
    lyc_ = val;
    if (lcdc_ & 0x80) {
      bool match = (ly_ == lyc_);
      if (match)
        stat_ |= 0x04;
      else
        stat_ &= ~0x04;
      checkSTATInterrupt();
    }
    break;
  case 0xFF47:
    bgp_ = val;
    break;
  case 0xFF48:
    obp0_ = val;
    break;
  case 0xFF49:
    obp1_ = val;
    break;
  case 0xFF4A:
    wy_ = val;
    break;
  case 0xFF4B:
    wx_ = val;
    break;
  }
}

void PPU::checkSTATInterrupt() {
  if (!bus_)
    return;

  bool line = false;
  if ((stat_ & 0x40) && (stat_ & 0x04))
    line = true; // LYC coincidence
  if ((stat_ & 0x20) && mode_ == Mode::OAMScan)
    line = true;
  if ((stat_ & 0x10) && mode_ == Mode::VBlank)
    line = true;
  if ((stat_ & 0x08) && mode_ == Mode::HBlank)
    line = true;

  if (line && !stat_line_) {
    bus_->requestInterrupt(GBCore::INT_STAT);
  }
  stat_line_ = line;
}

int PPU::getMode3Duration() const {
  int duration = 172;
  duration += scx_ & 7;

  // Count sprites on this scanline
  if (lcdc_ & 0x02) {
    int sprite_h = (lcdc_ & 0x04) ? 16 : 8;
    int count = 0;
    for (int i = 0; i < 40 && count < 10; i++) {
      int sy = oam_[i * 4] - 16;
      if (ly_ >= sy && ly_ < sy + sprite_h) {
        count++;
        duration += 6;
      }
    }
  }

  if (duration > 289)
    duration = 289;
  return duration;
}

void PPU::tick(int tcycles) {
  if (!(lcdc_ & 0x80))
    return;

  mode_clock_ += tcycles;

  switch (mode_) {
  case Mode::OAMScan:
    if (mode_clock_ >= 80) {
      mode_clock_ -= 80;
      mode_ = Mode::PixelTransfer;
    }
    break;

  case Mode::PixelTransfer: {
    int mode3_dur = getMode3Duration();
    if (mode_clock_ >= mode3_dur) {
      mode_clock_ -= mode3_dur;
      mode_ = Mode::HBlank;

      renderScanline();
      checkSTATInterrupt();
    }
    break;
  }

  case Mode::HBlank: {
    if (lcd_on_delay_) {
      // First scanline after LCD ON: mode 0 for 80 T (OAM search is skipped),
      // then directly into pixel transfer. The 80 T window matches hardware:
      // lcdon_write_timing shows OAM is accessible for nops 0-17 (write at
      // mode_clock_ = 8+4*17=76 < 80) and blocked at nop 18 (76+4=80).
      if (mode_clock_ >= 80) {
        mode_clock_ -= 80;
        mode_ = Mode::PixelTransfer;
        lcd_on_delay_ = false;
      }
      break;
    }
    int hblank_dur = CYCLES_PER_LINE - 80 - getMode3Duration();
    if (mode_clock_ >= hblank_dur) {
      mode_clock_ -= hblank_dur;
      ly_++;

      if (ly_ == SCREEN_H) {
        // At the start of VBlank (line 144), mode 2 (OAM) interrupt fires
        // simultaneously with VBlank, before mode transitions to VBlank.
        mode_ = Mode::OAMScan;
        bool match = (ly_ == lyc_);
        if (match)
          stat_ |= 0x04;
        else
          stat_ &= ~0x04;
        checkSTATInterrupt(); // fires mode 2 interrupt if enabled

        mode_ = Mode::VBlank;
        frame_ready_ = true;
        bus_->requestInterrupt(GBCore::INT_VBLANK);

        // Now check VBlank STAT interrupt with mode = VBlank
        // (but don't re-update LYC since we just did it above)
        checkSTATInterrupt();
      } else {
        mode_ = Mode::OAMScan;
        // LYC comparison
        bool match = (ly_ == lyc_);
        if (match)
          stat_ |= 0x04;
        else
          stat_ &= ~0x04;
        checkSTATInterrupt();
      }
    }
    break;
  }

  case Mode::VBlank:
    if (mode_clock_ >= CYCLES_PER_LINE) {
      mode_clock_ -= CYCLES_PER_LINE;
      ly_++;

      if (ly_ >= SCANLINES) {
        ly_ = 0;
        mode_ = Mode::OAMScan;
        window_line_ = 0;
        window_was_active_ = false;

        bool match = (ly_ == lyc_);
        if (match)
          stat_ |= 0x04;
        else
          stat_ &= ~0x04;
        checkSTATInterrupt();
      } else {
        bool match = (ly_ == lyc_);
        if (match)
          stat_ |= 0x04;
        else
          stat_ &= ~0x04;
        checkSTATInterrupt();
      }
    }
    break;
  }
}

uint32_t PPU::applyPalette(uint8_t palette, uint8_t color_id) const {
  uint8_t shade = (palette >> (color_id * 2)) & 0x03;
  return PALETTE[shade];
}

void PPU::renderScanline() {
  if (ly_ >= SCREEN_H)
    return;

  std::array<uint8_t, SCREEN_W> bg_colors{};
  bg_colors.fill(0);

  if (lcdc_ & 0x01) {
    renderBackground(bg_colors);
  } else {
    for (int x = 0; x < SCREEN_W; x++) {
      framebuffer_[ly_ * SCREEN_W + x] = PALETTE[0];
    }
  }

  if ((lcdc_ & 0x21) == 0x21) {
    renderWindow(bg_colors);
  }

  if (lcdc_ & 0x02) {
    renderSprites(bg_colors);
  }
}

void PPU::renderBackground(std::array<uint8_t, SCREEN_W> &line_colors) {
  uint16_t tile_map = (lcdc_ & 0x08) ? 0x1C00 : 0x1800;
  bool signed_addr = !(lcdc_ & 0x10);
  uint16_t tile_data = signed_addr ? 0x0800 : 0x0000;

  uint8_t y = ly_ + scy_;
  uint8_t tile_row = y / 8;
  uint8_t pixel_row = y % 8;

  for (int x = 0; x < SCREEN_W; x++) {
    uint8_t px = x + scx_;
    uint8_t tile_col = px / 8;
    uint8_t pixel_col = px % 8;

    uint16_t map_addr = tile_map + tile_row * 32 + tile_col;
    uint8_t tile_id = vram_[map_addr];

    uint16_t data_addr;
    if (signed_addr) {
      data_addr = tile_data + (static_cast<int8_t>(tile_id) + 128) * 16;
    } else {
      data_addr = tile_data + tile_id * 16;
    }
    data_addr += pixel_row * 2;

    uint8_t lo = vram_[data_addr];
    uint8_t hi = vram_[data_addr + 1];

    uint8_t bit = 7 - pixel_col;
    uint8_t color_id = ((hi >> bit) & 1) << 1 | ((lo >> bit) & 1);

    line_colors[x] = color_id;
    framebuffer_[ly_ * SCREEN_W + x] = applyPalette(bgp_, color_id);
  }
}

void PPU::renderWindow(std::array<uint8_t, SCREEN_W> &line_colors) {
  if (wy_ > ly_)
    return;
  if (wx_ > 166)
    return;

  int wx_actual = wx_ - 7;
  if (wx_actual >= SCREEN_W)
    return;

  uint16_t tile_map = (lcdc_ & 0x40) ? 0x1C00 : 0x1800;
  bool signed_addr = !(lcdc_ & 0x10);
  uint16_t tile_data = signed_addr ? 0x0800 : 0x0000;

  uint8_t win_y = window_line_;
  uint8_t tile_row = win_y / 8;
  uint8_t pixel_row = win_y % 8;

  bool window_drawn = false;

  for (int x = (wx_actual < 0 ? 0 : wx_actual); x < SCREEN_W; x++) {
    int win_x = x - wx_actual;
    uint8_t tile_col = win_x / 8;
    uint8_t pixel_col = win_x % 8;

    uint16_t map_addr = tile_map + tile_row * 32 + tile_col;
    uint8_t tile_id = vram_[map_addr];

    uint16_t data_addr;
    if (signed_addr) {
      data_addr = tile_data + (static_cast<int8_t>(tile_id) + 128) * 16;
    } else {
      data_addr = tile_data + tile_id * 16;
    }
    data_addr += pixel_row * 2;

    uint8_t lo = vram_[data_addr];
    uint8_t hi = vram_[data_addr + 1];

    uint8_t bit = 7 - pixel_col;
    uint8_t color_id = ((hi >> bit) & 1) << 1 | ((lo >> bit) & 1);

    line_colors[x] = color_id;
    framebuffer_[ly_ * SCREEN_W + x] = applyPalette(bgp_, color_id);
    window_drawn = true;
  }

  if (window_drawn) {
    window_line_++;
    window_was_active_ = true;
  }
}

void PPU::renderSprites(const std::array<uint8_t, SCREEN_W> &bg_colors) {
  int sprite_h = (lcdc_ & 0x04) ? 16 : 8;

  struct OAMEntry {
    uint8_t y, x, tile, flags;
    int index;
  };

  // Collect sprites visible on this scanline (max 10)
  OAMEntry visible[10];
  int count = 0;
  for (int i = 0; i < 40 && count < 10; i++) {
    int sy = oam_[i * 4] - 16;
    if (ly_ >= sy && ly_ < sy + sprite_h) {
      visible[count] = {oam_[i * 4], oam_[i * 4 + 1], oam_[i * 4 + 2],
                        oam_[i * 4 + 3], i};
      count++;
    }
  }

  // DMG priority: lower X first, then lower OAM index
  std::stable_sort(
      visible, visible + count,
      [](const OAMEntry &a, const OAMEntry &b) { return a.x < b.x; });

  // Render back-to-front so higher priority overwrites
  for (int s = count - 1; s >= 0; s--) {
    auto &spr = visible[s];
    int sy = spr.y - 16;
    int sx = spr.x - 8;

    uint8_t tile = spr.tile;
    if (sprite_h == 16)
      tile &= 0xFE;

    int row = ly_ - sy;
    if (spr.flags & 0x40) // Y flip
      row = sprite_h - 1 - row;

    uint16_t addr = tile * 16 + row * 2;
    uint8_t lo = vram_[addr];
    uint8_t hi = vram_[addr + 1];

    for (int px = 0; px < 8; px++) {
      int screen_x = sx + px;
      if (screen_x < 0 || screen_x >= SCREEN_W)
        continue;

      uint8_t bit = (spr.flags & 0x20) ? px : (7 - px); // X flip
      uint8_t color_id = ((hi >> bit) & 1) << 1 | ((lo >> bit) & 1);
      if (color_id == 0)
        continue; // transparent

      // BG priority: if flag set and bg color != 0, sprite hidden
      if ((spr.flags & 0x80) && bg_colors[screen_x] != 0)
        continue;

      uint8_t palette = (spr.flags & 0x10) ? obp1_ : obp0_;
      framebuffer_[ly_ * SCREEN_W + screen_x] = applyPalette(palette, color_id);
    }
  }
}

// OAM is organized as 20 rows of 8 bytes (4 objects × 4 bytes × rows = 160
// bytes). But Pan Docs describes rows as 8-byte (16-bit word × 4) chunks. Row i
// corresponds to oam_[i*8 .. i*8+7]. Objects 0 and 1 (row 0: $FE00-$FE07) are
// not affected by write corruption.

static uint16_t oamWord(const std::array<uint8_t, 160> &oam, int row,
                        int word) {
  int idx = row * 8 + word * 2;
  return static_cast<uint16_t>(oam[idx]) |
         (static_cast<uint16_t>(oam[idx + 1]) << 8);
}

static void setOAMWord(std::array<uint8_t, 160> &oam, int row, int word,
                       uint16_t val) {
  int idx = row * 8 + word * 2;
  oam[idx] = val & 0xFF;
  oam[idx + 1] = (val >> 8) & 0xFF;
}

void PPU::oamBugCorruptWrite(int row) const {
  // Objects 0 and 1 (row 0) are never affected.
  if (row <= 0 || row >= 20)
    return;

  // Write corruption: the last three words are copied from the preceding row;
  // the first word = ((a ^ c) & (b ^ c)) ^ c
  // where a = current row word 0, b = preceding row word 0, c = preceding row
  // word 2
  uint16_t a = oamWord(oam_, row, 0);
  uint16_t b = oamWord(oam_, row - 1, 0);
  uint16_t c = oamWord(oam_, row - 1, 2);

  uint16_t new_w0 = ((a ^ c) & (b ^ c)) ^ c;
  setOAMWord(oam_, row, 0, new_w0);
  setOAMWord(oam_, row, 1, oamWord(oam_, row - 1, 1));
  setOAMWord(oam_, row, 2, oamWord(oam_, row - 1, 2));
  setOAMWord(oam_, row, 3, oamWord(oam_, row - 1, 3));
}

void PPU::oamBugCorruptRead(int row) const {
  if (row <= 0 || row >= 20)
    return;

  // Read corruption: same as write but first word = b | (a & c)
  uint16_t a = oamWord(oam_, row, 0);
  uint16_t b = oamWord(oam_, row - 1, 0);
  uint16_t c = oamWord(oam_, row - 1, 2);

  uint16_t new_w0 = b | (a & c);
  setOAMWord(oam_, row, 0, new_w0);
  setOAMWord(oam_, row, 1, oamWord(oam_, row - 1, 1));
  setOAMWord(oam_, row, 2, oamWord(oam_, row - 1, 2));
  setOAMWord(oam_, row, 3, oamWord(oam_, row - 1, 3));
}

void PPU::oamBugCorruptReadWrite(int row) const {
  // Read-during-increase/decrease: more complex pattern.
  // Not triggered for rows 0-3 or row 19.
  if (row <= 3 || row >= 19)
    return;

  // a = first word two rows before, b = first word in preceding row,
  // c = first word in current row, d = third word in preceding row
  uint16_t a = oamWord(oam_, row - 2, 0);
  uint16_t b = oamWord(oam_, row - 1, 0);
  uint16_t c = oamWord(oam_, row, 0);
  uint16_t d = oamWord(oam_, row - 1, 2);

  uint16_t new_b = (b & (a | c | d)) | (a & c & d);

  // Copy preceding row (after corruption of its first word) to current row and
  // two rows before.
  uint16_t w1_prev = oamWord(oam_, row - 1, 1);
  uint16_t w2_prev = oamWord(oam_, row - 1, 2);
  uint16_t w3_prev = oamWord(oam_, row - 1, 3);

  // Apply to row-1 first word
  setOAMWord(oam_, row - 1, 0, new_b);

  // Copy row-1 to current row
  setOAMWord(oam_, row, 0, new_b);
  setOAMWord(oam_, row, 1, w1_prev);
  setOAMWord(oam_, row, 2, w2_prev);
  setOAMWord(oam_, row, 3, w3_prev);

  // Copy row-1 to row-2
  setOAMWord(oam_, row - 2, 0, new_b);
  setOAMWord(oam_, row - 2, 1, w1_prev);
  setOAMWord(oam_, row - 2, 2, w2_prev);
  setOAMWord(oam_, row - 2, 3, w3_prev);
}

void PPU::saveState(std::vector<uint8_t> &out) const {
  out.push_back(lcdc_);
  out.push_back(stat_);
  out.push_back(scy_);
  out.push_back(scx_);
  out.push_back(ly_);
  out.push_back(lyc_);
  out.push_back(bgp_);
  out.push_back(obp0_);
  out.push_back(obp1_);
  out.push_back(wy_);
  out.push_back(wx_);
  out.push_back(static_cast<uint8_t>(mode_));
  writeU32(out, static_cast<uint32_t>(mode_clock_));
  writeU32(out, static_cast<uint32_t>(window_line_));
  out.push_back(window_was_active_ ? 1 : 0);
  out.push_back(lcd_on_delay_ ? 1 : 0);
  out.push_back(frame_ready_ ? 1 : 0);
  out.push_back(stat_line_ ? 1 : 0);
  for (uint8_t x : vram_)
    out.push_back(x);
  for (uint8_t x : oam_)
    out.push_back(x);
  for (uint32_t x : framebuffer_) {
    out.push_back(static_cast<uint8_t>(x));
    out.push_back(static_cast<uint8_t>(x >> 8));
    out.push_back(static_cast<uint8_t>(x >> 16));
    out.push_back(static_cast<uint8_t>(x >> 24));
  }
}

bool PPU::loadState(const uint8_t *&data, const uint8_t *end) {
  if (!readU8(data, end, lcdc_) || !readU8(data, end, stat_) ||
      !readU8(data, end, scy_) || !readU8(data, end, scx_) ||
      !readU8(data, end, ly_) || !readU8(data, end, lyc_) ||
      !readU8(data, end, bgp_) || !readU8(data, end, obp0_) ||
      !readU8(data, end, obp1_) || !readU8(data, end, wy_) ||
      !readU8(data, end, wx_))
    return false;
  uint8_t m;
  if (!readU8(data, end, m))
    return false;
  mode_ = static_cast<Mode>(m);
  uint32_t u32;
  if (!readU32(data, end, u32))
    return false;
  mode_clock_ = static_cast<int>(u32);
  if (!readU32(data, end, u32))
    return false;
  window_line_ = static_cast<int>(u32);
  if (!readBool(data, end, window_was_active_) ||
      !readBool(data, end, lcd_on_delay_) ||
      !readBool(data, end, frame_ready_) || !readBool(data, end, stat_line_))
    return false;
  if (data + vram_.size() > end)
    return false;
  std::memcpy(vram_.data(), data, vram_.size());
  data += vram_.size();
  if (data + oam_.size() > end)
    return false;
  std::memcpy(oam_.data(), data, oam_.size());
  data += oam_.size();
  if (data + framebuffer_.size() * 4 > end)
    return false;
  for (size_t i = 0; i < framebuffer_.size(); i++) {
    framebuffer_[i] = static_cast<uint32_t>(data[0]) |
                      (static_cast<uint32_t>(data[1]) << 8) |
                      (static_cast<uint32_t>(data[2]) << 16) |
                      (static_cast<uint32_t>(data[3]) << 24);
    data += 4;
  }
  return true;
}

} // namespace nativecore
