#pragma once

#include <array>
#include <cstdint>
#include <vector>

namespace nativecore {

struct SquareChannel {
  bool enabled = false;
  bool dac_enabled = false;
  uint8_t duty = 0;
  uint8_t duty_pos = 0;
  uint8_t length_counter = 0;
  bool length_enabled = false;
  uint8_t volume = 0;
  uint8_t volume_init = 0;
  bool envelope_add = false;
  uint8_t envelope_period = 0;
  uint8_t envelope_timer = 0;
  uint16_t frequency = 0;
  int timer = 0;
  bool trigger = false;

  // Sweep (channel 1 only)
  bool has_sweep = false;
  uint8_t sweep_period = 0;
  uint8_t sweep_shift = 0;
  bool sweep_negate = false;
  uint8_t sweep_timer = 0;
  bool sweep_enabled = false;
  uint16_t sweep_shadow = 0;
  bool sweep_negate_used = false;

  void reset();
  void triggerChannel();
  void tickTimer();
  void tickLength();
  void tickEnvelope();
  void tickSweep();
  uint16_t sweepCalc();
  int8_t output() const;
};

struct WaveChannel {
  bool enabled = false;
  bool dac_enabled = false;
  uint16_t length_counter = 0;
  bool length_enabled = false;
  uint8_t volume_code = 0;
  uint16_t frequency = 0;
  int timer = 0;
  uint8_t position = 0;
  uint8_t sample_buffer = 0;

  std::array<uint8_t, 16> wave_ram{};

  void reset();
  void triggerChannel();
  void tickTimer();
  void tickLength();
  int8_t output() const;
};

struct NoiseChannel {
  bool enabled = false;
  bool dac_enabled = false;
  uint8_t length_counter = 0;
  bool length_enabled = false;
  uint8_t volume = 0;
  uint8_t volume_init = 0;
  bool envelope_add = false;
  uint8_t envelope_period = 0;
  uint8_t envelope_timer = 0;
  uint8_t clock_shift = 0;
  bool width_mode = false;
  uint8_t divisor_code = 0;
  int timer = 0;
  uint16_t lfsr = 0x7FFF; // 16-bit linear feedback shift register

  void reset();
  void triggerChannel();
  void tickTimer();
  void tickLength();
  void tickEnvelope();
  int8_t output() const;
};

class APU {
public:
  static constexpr int SAMPLE_RATE = 44100; // 44.1 kHz
  static constexpr int CPU_CLOCK =
      4194304; // period of the cpu clock (4.194304 MHz)

  void reset();
  void tick(int tcycles);

  uint8_t readRegister(uint16_t addr) const;
  void writeRegister(uint16_t addr, uint8_t val);

  const float *getBuffer(size_t &count) const;
  void consumeSamples(size_t count);
  void clearBuffer();

private:
  bool power_ = false;
  uint8_t frame_seq_step_ = 0;
  int frame_seq_timer_ = 0;

  SquareChannel ch1_;
  SquareChannel ch2_;
  WaveChannel ch3_;
  NoiseChannel ch4_;

  uint8_t nr50_ = 0;
  uint8_t nr51_ = 0;

  int sample_timer_ = 0;
  std::vector<float> buffer_;

  // APU's frame sequencer ticks every 8192 CPU cycles (512 Hz at 4.194304 MHz)
  static constexpr int FRAME_SEQ_PERIOD = 8192;

  void tickFrameSequencer();
  void generateSample();
};

} // namespace nativecore
