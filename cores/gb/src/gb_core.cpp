#include "gb_core.h"
#include "nativecore/ir.h"

#include <algorithm>
#include <cstring>

namespace nativecore {

GBCore::GBCore() {
  sys_info_.name = "Game Boy";
  sys_info_.native_fps = 4194304.0 / 70224.0; // ~59.7275 Hz
  sys_info_.screen_width = PPU::SCREEN_W;
  sys_info_.screen_height = PPU::SCREEN_H;
  sys_info_.audio_sample_rate = APU::SAMPLE_RATE;

  cpu_.connectBus(this);
  ppu_.connectBus(this);
}

std::unique_ptr<Core> GBCore::createGameBoyCore() {
  return std::make_unique<GBCore>();
}

namespace GameBoy {
std::unique_ptr<Core> createGameBoyCore() { return std::make_unique<GBCore>(); }
} // namespace GameBoy

const SystemInfo &GBCore::systemInfo() const { return sys_info_; }

bool GBCore::supportsCamera() const {
  return cart_.loaded() && cart_.header().has_camera;
}

void GBCore::setCameraProvider(CameraProvider *provider) {
  cart_.setCameraProvider(provider);
}

bool GBCore::loadROM(const std::vector<uint8_t> &data) {
  if (!cart_.load(data))
    return false;
  reset();
  return true;
}

void GBCore::reset() {
  cpu_.reset();
  ppu_.reset();
  apu_.reset();
  cart_.reset();
  wram_.fill(0);
  hram_.fill(0);
  ie_ = 0;
  if_ = 0xE1;
  div_counter_ = 0xABCC;
  tima_ = 0;
  tma_ = 0;
  tac_ = 0;
  tima_overflow_ = false;
  tima_overflow_cycles_ = 0;
  prev_timer_bit_ = false;
  joypad_select_ = 0x30;
  button_state_ = 0xFF;
  sb_ = 0;
  sc_ = 0;
  serial_timer_ = 0;
  serial_bits_ = 0;
  serial_output_.clear();
  dma_active_ = false;
  dma_source_ = 0;
  dma_offset_ = 0;
  dma_delay_ = 0;
  frame_cycles_ = 0;
}

IRModule GBCore::recompile() {
  IRModule mod;
  mod.name = "gb_rom";
  return mod;
}

void GBCore::emitLLVMIR(llvm::LLVMContext &context, llvm::Module &module) {
  // Stub: full LLVM IR emission for ahead-of-time compilation
}

void GBCore::tick() {
  frame_cycles_ = 0;
  while (frame_cycles_ < CYCLES_PER_FRAME) {
    cpu_.step();
  }
  ppu_.clearFrameReady();
}

void GBCore::tickMCycle() {
  frame_cycles_ += 4;
  ppu_.tick(4);
  apu_.tick(4);
  tickTimer(4);
  tickDMA();
  tickSerial(4);
  cart_.tickCamera(4);
}

void GBCore::requestInterrupt(uint8_t flag) { if_ |= flag; }

// Memory Bus

uint8_t GBCore::busRead(uint16_t addr) const {
  if (addr < 0x8000) {
    return cart_.read(addr);
  }
  if (addr < 0xA000) {
    return ppu_.readVRAM(addr);
  }
  if (addr < 0xC000) {
    return cart_.read(addr);
  }
  if (addr < 0xE000) {
    return wram_[addr & 0x1FFF];
  }
  if (addr < 0xFE00) {
    // Echo RAM
    return wram_[addr & 0x1FFF];
  }
  if (addr < 0xFEA0) {
    if (dma_active_)
      return 0xFF;
    return ppu_.readOAM(addr - 0xFE00);
  }
  if (addr < 0xFF00) {
    return 0xFF; // unusable region
  }
  if (addr < 0xFF80) {
    return readIO(addr);
  }
  if (addr < 0xFFFF) {
    return hram_[addr - 0xFF80];
  }
  return ie_; // 0xFFFF
}

void GBCore::busWrite(uint16_t addr, uint8_t val) {
  if (addr < 0x8000) {
    cart_.write(addr, val);
    return;
  }
  if (addr < 0xA000) {
    ppu_.writeVRAM(addr, val);
    return;
  }
  if (addr < 0xC000) {
    cart_.write(addr, val);
    return;
  }
  if (addr < 0xE000) {
    wram_[addr & 0x1FFF] = val;
    return;
  }
  if (addr < 0xFE00) {
    wram_[addr & 0x1FFF] = val;
    return;
  }
  if (addr < 0xFEA0) {
    if (dma_active_)
      return;
    ppu_.writeOAM(addr - 0xFE00, val);
    return;
  }
  if (addr < 0xFF00) {
    return; // unusable region
  }
  if (addr < 0xFF80) {
    writeIO(addr, val);
    return;
  }
  if (addr < 0xFFFF) {
    hram_[addr - 0xFF80] = val;
    return;
  }
  ie_ = val; // 0xFFFF
}

uint8_t GBCore::readMemory(uint16_t address) const { return busRead(address); }

void GBCore::writeMemory(uint16_t address, uint8_t value) {
  busWrite(address, value);
}

const uint32_t *GBCore::getFramebuffer() const { return ppu_.framebuffer(); }

const float *GBCore::getAudioBuffer(size_t &samples_out) const {
  return apu_.getBuffer(samples_out);
}

void GBCore::clearAudioBuffer() { apu_.clearBuffer(); }

void GBCore::consumeAudioSamples(size_t samples) {
  apu_.consumeSamples(samples);
}

void GBCore::setInputState(int controller, uint8_t buttons) {
  if (controller != 0)
    return;
  uint8_t old_state = button_state_;
  button_state_ = ~buttons;
  // Detect high-to-low transition for joypad interrupt
  if ((old_state & ~button_state_) & 0xFF) {
    requestInterrupt(INT_JOYPAD);
  }
}

// I/O Registers

uint8_t GBCore::readIO(uint16_t addr) const {
  switch (addr) {
  case 0xFF00: { // Joypad
    uint8_t result = joypad_select_ | 0xC0;
    if (!(joypad_select_ & 0x10)) {
      // Direction keys: right, left, up, down (bits 0-3)
      result |= (button_state_ >> 4) & 0x0F;
    }
    if (!(joypad_select_ & 0x20)) {
      // Button keys: A, B, select, start (bits 0-3)
      result |= button_state_ & 0x0F;
    }
    if ((joypad_select_ & 0x30) == 0x30) {
      result |= 0x0F;
    }
    return result;
  }
  case 0xFF01:
    return sb_;
  case 0xFF02:
    return sc_ | 0x7E;
  case 0xFF04:
    return (div_counter_ >> 8) & 0xFF; // DIV
  case 0xFF05:
    return tima_;
  case 0xFF06:
    return tma_;
  case 0xFF07:
    return tac_ | 0xF8;
  case 0xFF0F:
    return if_ | 0xE0;
  case 0xFF46:
    return 0xFF; // DMA (write-only, reading returns last bus value)

  default:
    if (addr >= 0xFF10 && addr <= 0xFF3F) {
      return apu_.readRegister(addr);
    }
    if (addr >= 0xFF40 && addr <= 0xFF4B) {
      return ppu_.readRegister(addr);
    }
    return 0xFF;
  }
}

void GBCore::writeIO(uint16_t addr, uint8_t val) {
  switch (addr) {
  case 0xFF00: // Joypad
    joypad_select_ = val & 0x30;
    break;
  case 0xFF01: // SB
    sb_ = val;
    break;
  case 0xFF02: // SC
    sc_ = val;
    if (val & 0x80) {
      serial_timer_ = 0;
      serial_bits_ = 0;
    }
    break;
  case 0xFF04: { // DIV - writing resets the divider
    bool old_bit = timerBitSelected();
    div_counter_ = 0;
    bool new_bit = timerBitSelected();
    // Falling edge detection for timer
    if (old_bit && !new_bit && (tac_ & 0x04)) {
      tima_++;
      if (tima_ == 0) {
        tima_overflow_ = true;
        tima_overflow_cycles_ = 0;
      }
    }
    break;
  }
  case 0xFF05: // TIMA
    if (!tima_overflow_) {
      tima_ = val;
    }
    if (tima_overflow_ && tima_overflow_cycles_ < 4) {
      // Writing to TIMA during the delay period cancels the overflow
      tima_ = val;
      tima_overflow_ = false;
    }
    break;
  case 0xFF06: // TMA
    tma_ = val;
    if (tima_overflow_ && tima_overflow_cycles_ >= 4) {
      // If TIMA was just reloaded from TMA, update it
      tima_ = val;
    }
    break;
  case 0xFF07: { // TAC
    bool old_bit = timerBitSelected();
    tac_ = val;
    bool new_bit = timerBitSelected();
    // Falling edge when changing TAC
    if (old_bit && !new_bit) {
      tima_++;
      if (tima_ == 0) {
        tima_overflow_ = true;
        tima_overflow_cycles_ = 0;
      }
    }
    break;
  }
  case 0xFF0F: // IF
    if_ = val | 0xE0;
    break;
  case 0xFF46: { // OAM DMA
    dma_source_ = static_cast<uint16_t>(val) << 8;
    dma_active_ = true;
    dma_offset_ = 0;
    dma_delay_ = 2;
    break;
  }
  default:
    if (addr >= 0xFF10 && addr <= 0xFF3F) {
      apu_.writeRegister(addr, val);
    } else if (addr >= 0xFF40 && addr <= 0xFF4B) {
      ppu_.writeRegister(addr, val);
    }
    break;
  }
}

// Timer

bool GBCore::timerBitSelected() const {
  static const int BIT_POSITIONS[] = {9, 3, 5, 7};
  int bit = BIT_POSITIONS[tac_ & 0x03];
  bool counter_bit = (div_counter_ >> bit) & 1;
  bool enabled = (tac_ & 0x04) != 0;
  return counter_bit && enabled;
}

void GBCore::tickTimer(int tcycles) {
  for (int i = 0; i < tcycles; i++) {
    bool old_bit = timerBitSelected();
    div_counter_++;
    bool new_bit = timerBitSelected();

    // Falling edge detection
    if (old_bit && !new_bit) {
      tima_++;
      if (tima_ == 0) {
        tima_overflow_ = true;
        tima_overflow_cycles_ = 0;
      }
    }

    if (tima_overflow_) {
      tima_overflow_cycles_++;
      if (tima_overflow_cycles_ == 4) {
        tima_ = tma_;
        requestInterrupt(INT_TIMER);
        tima_overflow_ = false;
      }
    }
  }
}

// OAM DMA

void GBCore::tickDMA() {
  if (!dma_active_)
    return;

  if (dma_delay_ > 0) {
    dma_delay_--;
    return;
  }

  uint8_t byte = busRead(dma_source_ + dma_offset_);
  ppu_.oamDMAWrite(dma_offset_, byte);
  dma_offset_++;

  if (dma_offset_ >= 160) {
    dma_active_ = false;
  }
}

// Serial

void GBCore::tickSerial(int tcycles) {
  if (!(sc_ & 0x80))
    return;
  if (!(sc_ & 0x01))
    return; // Only internal clock

  serial_timer_ += tcycles;
  // Internal clock: 8192 Hz = 512 T-cycles per bit
  while (serial_timer_ >= 512) {
    serial_timer_ -= 512;
    serial_bits_++;
    if (serial_bits_ >= 8) {
      // Transfer complete - capture the byte for test ROMs
      serial_output_ += static_cast<char>(sb_);
      sb_ = 0xFF; // No connected device
      sc_ &= ~0x80;
      requestInterrupt(INT_SERIAL);
      serial_bits_ = 0;
    }
  }
}

bool GBCore::saveState(std::vector<uint8_t> &out) const {
  out.push_back(GB_SAVE_STATE_VERSION);
  cpu_.saveState(out);
  ppu_.saveState(out);
  apu_.saveState(out);
  cart_.saveState(out);
  for (uint8_t x : wram_) {
    out.push_back(x);
  }
  for (uint8_t x : hram_) {
    out.push_back(x);
  }
  out.push_back(ie_);
  out.push_back(if_);
  out.push_back(static_cast<uint8_t>(div_counter_ >> 8));
  out.push_back(static_cast<uint8_t>(div_counter_));
  out.push_back(tima_);
  out.push_back(tma_);
  out.push_back(tac_);
  out.push_back(tima_overflow_ ? 1 : 0);
  out.push_back(static_cast<uint8_t>(tima_overflow_cycles_));
  out.push_back(prev_timer_bit_ ? 1 : 0);
  out.push_back(joypad_select_);
  out.push_back(button_state_);
  out.push_back(sb_);
  out.push_back(sc_);
  out.push_back(static_cast<uint8_t>(serial_timer_ >> 24));
  out.push_back(static_cast<uint8_t>(serial_timer_ >> 16));
  out.push_back(static_cast<uint8_t>(serial_timer_ >> 8));
  out.push_back(static_cast<uint8_t>(serial_timer_));
  out.push_back(static_cast<uint8_t>(serial_bits_));
  out.push_back(
      static_cast<uint8_t>(std::min(serial_output_.size(), size_t(0xFF))));
  for (size_t i = 0; i < std::min(serial_output_.size(), size_t(0xFF)); i++)
    out.push_back(static_cast<uint8_t>(serial_output_[i]));
  out.push_back(dma_active_ ? 1 : 0);
  out.push_back(static_cast<uint8_t>(dma_source_ >> 8));
  out.push_back(static_cast<uint8_t>(dma_source_));
  out.push_back(dma_offset_);
  out.push_back(static_cast<uint8_t>(dma_delay_));
  out.push_back(static_cast<uint8_t>(frame_cycles_ >> 24));
  out.push_back(static_cast<uint8_t>(frame_cycles_ >> 16));
  out.push_back(static_cast<uint8_t>(frame_cycles_ >> 8));
  out.push_back(static_cast<uint8_t>(frame_cycles_));
  return true;
}

namespace {
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
} // namespace

bool GBCore::loadState(const uint8_t *data, size_t size) {
  const uint8_t *p = data;
  const uint8_t *end = data + size;
  if (p == end)
    return false;
  uint8_t ver = *p++;
  if (ver != GB_SAVE_STATE_VERSION)
    return false;
  if (!cpu_.loadState(p, end) || !ppu_.loadState(p, end) ||
      !apu_.loadState(p, end) || !cart_.loadState(p, end))
    return false;
  if (p + wram_.size() + hram_.size() > end)
    return false;
  std::memcpy(wram_.data(), p, wram_.size());
  p += wram_.size();
  std::memcpy(hram_.data(), p, hram_.size());
  p += hram_.size();
  if (!readU8(p, end, ie_) || !readU8(p, end, if_))
    return false;
  uint8_t hi, lo;
  if (!readU8(p, end, hi) || !readU8(p, end, lo))
    return false;
  div_counter_ = (static_cast<uint16_t>(hi) << 8) | lo;
  if (!readU8(p, end, tima_) || !readU8(p, end, tma_) || !readU8(p, end, tac_))
    return false;
  uint8_t b;
  if (!readU8(p, end, b))
    return false;
  tima_overflow_ = (b != 0);
  if (!readU8(p, end, b))
    return false;
  tima_overflow_cycles_ = b;
  if (!readU8(p, end, b))
    return false;
  prev_timer_bit_ = (b != 0);
  if (!readU8(p, end, joypad_select_) || !readU8(p, end, button_state_) ||
      !readU8(p, end, sb_) || !readU8(p, end, sc_))
    return false;
  uint32_t u32;
  if (!readU32(p, end, u32))
    return false;
  serial_timer_ = static_cast<int>(u32);
  if (!readU8(p, end, b))
    return false;
  serial_bits_ = b;
  if (!readU8(p, end, b))
    return false;
  size_t ser_len = b;
  if (p + ser_len > end)
    return false;
  serial_output_.assign(reinterpret_cast<const char *>(p),
                        reinterpret_cast<const char *>(p) + ser_len);
  p += ser_len;
  if (!readU8(p, end, b))
    return false;
  dma_active_ = (b != 0);
  if (!readU8(p, end, hi) || !readU8(p, end, lo))
    return false;
  dma_source_ = (static_cast<uint16_t>(hi) << 8) | lo;
  if (!readU8(p, end, dma_offset_) || !readU8(p, end, b))
    return false;
  dma_delay_ = b;
  if (!readU32(p, end, u32))
    return false;
  frame_cycles_ = static_cast<int>(u32);
  return true;
}

} // namespace nativecore
