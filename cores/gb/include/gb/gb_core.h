#pragma once

#include "apu.h"
#include "cartridge.h"
#include "cpu_lr35902.h"
#include "nativecore/core.h"
#include "ppu.h"

#include <array>
#include <string>
#include <vector>

namespace nativecore {

class GBCore : public Core {
public:
  static constexpr int CPU_CLOCK = 4194304;
  static constexpr int CYCLES_PER_FRAME = 70224;

  static constexpr uint8_t INT_VBLANK = 0x01;
  static constexpr uint8_t INT_STAT = 0x02;
  static constexpr uint8_t INT_TIMER = 0x04;
  static constexpr uint8_t INT_SERIAL = 0x08;
  static constexpr uint8_t INT_JOYPAD = 0x10;

  GBCore();
  ~GBCore() override = default;

  // Core interface
  const SystemInfo &systemInfo() const override;
  bool loadROM(const std::vector<uint8_t> &data) override;
  IRModule recompile() override;
  void emitLLVMIR(llvm::LLVMContext &context, llvm::Module &module) override;
  void tick() override;
  void reset() override;
  uint8_t readMemory(uint16_t address) const override;
  void writeMemory(uint16_t address, uint8_t value) override;
  const uint32_t *getFramebuffer() const override;
  const float *getAudioBuffer(size_t &samples_out) const override;
  void clearAudioBuffer() override;
  void consumeAudioSamples(size_t samples) override;
  void setInputState(int controller, uint8_t buttons) override;

  // Bus interface (called by CPU, PPU, etc.)
  uint8_t busRead(uint16_t addr) const;
  void busWrite(uint16_t addr, uint8_t val);
  void tickMCycle();

  void requestInterrupt(uint8_t flag);
  uint8_t interruptEnable() const { return ie_; }
  uint8_t interruptFlags() const { return if_; }
  void setInterruptFlags(uint8_t val) { if_ = val | 0xE0; }

  // Serial output capture (for test ROMs)
  const std::string &serialOutput() const { return serial_output_; }

  // Direct component access (for tests/debug)
  CPU &cpu() { return cpu_; }
  PPU &ppu() { return ppu_; }
  APU &apu() { return apu_; }
  Cartridge &cartridge() { return cart_; }

  static std::unique_ptr<Core> createGameBoyCore();

private:
  SystemInfo sys_info_;
  CPU cpu_;
  PPU ppu_;
  APU apu_;
  Cartridge cart_;

  std::array<uint8_t, 8192> wram_{};
  std::array<uint8_t, 127> hram_{};

  uint8_t ie_ = 0;
  uint8_t if_ = 0xE1;

  // Timer
  uint16_t div_counter_ = 0;
  uint8_t tima_ = 0;
  uint8_t tma_ = 0;
  uint8_t tac_ = 0;
  bool tima_overflow_ = false;
  int tima_overflow_cycles_ = 0;
  bool prev_timer_bit_ = false;

  // Joypad
  uint8_t joypad_select_ = 0;
  uint8_t button_state_ = 0xFF;

  // Serial
  uint8_t sb_ = 0;
  uint8_t sc_ = 0;
  int serial_timer_ = 0;
  int serial_bits_ = 0;
  std::string serial_output_;

  // OAM DMA
  bool dma_active_ = false;
  uint16_t dma_source_ = 0;
  uint8_t dma_offset_ = 0;
  int dma_delay_ = 0;

  int frame_cycles_ = 0;

  void tickTimer(int tcycles);
  void tickDMA();
  void tickSerial(int tcycles);

  uint8_t readIO(uint16_t addr) const;
  void writeIO(uint16_t addr, uint8_t val);

  bool timerBitSelected() const;
};

namespace GameBoy {
std::unique_ptr<Core> createGameBoyCore();
}

} // namespace nativecore
