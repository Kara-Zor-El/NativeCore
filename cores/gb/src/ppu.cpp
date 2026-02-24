#include "ppu.h"
#include "gb_core.h"

#include <algorithm>
#include <cstring>

namespace nativecore {

void PPU::reset() {
  lcdc_ = 0x91;
  stat_ = 0;
  scy_ = 0;
  scx_ = 0;
  ly_ = 0;
  lyc_ = 0;
  bgp_ = 0xFC;
  obp0_ = 0xFF;
  obp1_ = 0xFF;
  wy_ = 0;
  wx_ = 0;
  mode_ = Mode::OAMSearch;
  mode_clock_ = 0;
  window_line_ = 0;
  window_was_active_ = false;
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

uint8_t PPU::readOAM(uint16_t addr) const {
  if ((mode_ == Mode::OAMSearch || mode_ == Mode::PixelTransfer) &&
      (lcdc_ & 0x80))
    return 0xFF;
  return oam_[addr & 0xFF];
}

void PPU::writeOAM(uint16_t addr, uint8_t val) {
  if ((mode_ == Mode::OAMSearch || mode_ == Mode::PixelTransfer) &&
      (lcdc_ & 0x80))
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
      ly_ = 0;
      mode_ = Mode::HBlank;
      mode_clock_ = 0;
      stat_ &= ~0x03;
    }
    if (!was_on && (val & 0x80)) {
      mode_ = Mode::OAMSearch;
      mode_clock_ = 0;
      window_line_ = 0;
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
  if ((stat_ & 0x20) && mode_ == Mode::OAMSearch)
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
  case Mode::OAMSearch:
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
    int hblank_dur = CYCLES_PER_LINE - 80 - getMode3Duration();
    if (mode_clock_ >= hblank_dur) {
      mode_clock_ -= hblank_dur;
      ly_++;

      if (ly_ == SCREEN_H) {
        mode_ = Mode::VBlank;
        frame_ready_ = true;
        bus_->requestInterrupt(GBCore::INT_VBLANK);
      } else {
        mode_ = Mode::OAMSearch;
      }

      // LYC comparison
      bool match = (ly_ == lyc_);
      if (match)
        stat_ |= 0x04;
      else
        stat_ &= ~0x04;
      checkSTATInterrupt();
    }
    break;
  }

  case Mode::VBlank:
    if (mode_clock_ >= CYCLES_PER_LINE) {
      mode_clock_ -= CYCLES_PER_LINE;
      ly_++;

      if (ly_ >= SCANLINES) {
        ly_ = 0;
        mode_ = Mode::OAMSearch;
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

} // namespace nativecore
