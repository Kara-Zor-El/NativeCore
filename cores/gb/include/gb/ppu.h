#pragma once

#include <array>
#include <cstdint>
#include <vector>

namespace nativecore {

class GBCore;

class PPU {
public:
  static constexpr int SCREEN_W = 160;
  static constexpr int SCREEN_H = 144;
  static constexpr int SCANLINES = 154;
  static constexpr int CYCLES_PER_LINE = 456;

  enum class Mode : uint8_t {
    HBlank = 0,
    VBlank = 1,
    OAMScan = 2,
    PixelTransfer = 3,
  };

  void connectBus(GBCore *bus) { bus_ = bus; }
  void reset();
  void tick(int tcycles);

  const uint32_t *framebuffer() const { return framebuffer_.data(); }
  bool frameReady() const { return frame_ready_; }
  void clearFrameReady() { frame_ready_ = false; }

  uint8_t readVRAM(uint16_t addr) const;
  void writeVRAM(uint16_t addr, uint8_t val);
  uint8_t readOAM(uint16_t addr) const;
  void writeOAM(uint16_t addr, uint8_t val);

  uint8_t readRegister(uint16_t addr) const;
  void writeRegister(uint16_t addr, uint8_t val);

  uint8_t lcdc() const { return lcdc_; }
  uint8_t ly() const { return ly_; }
  Mode mode() const { return mode_; }
  bool oamLocked() const;

  void oamDMAWrite(uint8_t index, uint8_t val);

  // OAM corruption bug (DMG only): triggered when a 16-bit IDU operation
  // occurs on an address in $FE00-$FEFF during mode 2 (OAM scan).
  // row is the OAM row currently being scanned by the PPU (0–19).
  void oamBugCorruptWrite(int row) const;
  void oamBugCorruptRead(int row) const;
  void oamBugCorruptReadWrite(int row) const;

  int oamRow() const {
    if (mode_ != Mode::OAMScan)
      return -1;
    return mode_clock_ / 4;
  }

  // Direct access for DMA (bypasses mode restrictions)
  uint8_t readVRAMDirect(uint16_t addr) const { return vram_[addr & 0x1FFF]; }
  uint8_t readOAMDirect(uint16_t addr) const { return oam_[addr & 0xFF]; }

  void saveState(std::vector<uint8_t> &out) const;
  bool loadState(const uint8_t *&data, const uint8_t *end);

private:
  GBCore *bus_ = nullptr;

  uint8_t lcdc_ = 0x91;
  uint8_t stat_ = 0;
  uint8_t scy_ = 0, scx_ = 0;
  uint8_t ly_ = 0, lyc_ = 0;
  uint8_t bgp_ = 0xFC;
  uint8_t obp0_ = 0xFF, obp1_ = 0xFF;
  uint8_t wy_ = 0, wx_ = 0;

  Mode mode_ = Mode::OAMScan;
  int mode_clock_ = 0;
  int window_line_ = 0;
  bool window_was_active_ = false;
  bool lcd_on_delay_ = false; // first scanline after LCD ON has special timing

  bool frame_ready_ = false;
  bool stat_line_ = false;

  std::array<uint8_t, 8192> vram_{};
  mutable std::array<uint8_t, 160> oam_{};
  std::array<uint32_t, SCREEN_W * SCREEN_H> framebuffer_{};

  // TODO: offer other color palettes via a settings hook (and allow custom
  // palettes)
  static constexpr uint32_t PALETTE[4] = {
      0xFFE0F8D0, // lightest (white-green)
      0xFF88C070, // light
      0xFF346856, // dark
      0xFF081820, // darkest
  };

  void checkSTATInterrupt();
  void renderScanline();
  void renderBackground(std::array<uint8_t, SCREEN_W> &line_colors);
  void renderWindow(std::array<uint8_t, SCREEN_W> &line_colors);
  void renderSprites(const std::array<uint8_t, SCREEN_W> &bg_colors);

  uint32_t applyPalette(uint8_t palette, uint8_t color_id) const;
  int getMode3Duration() const;

  struct SpriteEntry {
    uint8_t y, x, tile, flags;
  };
};

} // namespace nativecore
