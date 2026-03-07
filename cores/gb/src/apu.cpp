#include "apu.h"

#include <cstring>
#include <vector>

namespace nativecore {

// Square Channel

static const uint8_t DUTY_TABLE[4][8] = {
    {0, 0, 0, 0, 0, 0, 0, 1}, // 12.5%
    {1, 0, 0, 0, 0, 0, 0, 1}, // 25%
    {1, 0, 0, 0, 0, 1, 1, 1}, // 50%
    {0, 1, 1, 1, 1, 1, 1, 0}, // 75%
};

void SquareChannel::reset() {
  enabled = false;
  dac_enabled = false;
  duty = 0;
  duty_pos = 0;
  length_counter = 0;
  length_enabled = false;
  volume = 0;
  volume_init = 0;
  envelope_add = false;
  envelope_period = 0;
  envelope_timer = 0;
  frequency = 0;
  timer = 0;
  trigger = false;
  sweep_period = 0;
  sweep_shift = 0;
  sweep_negate = false;
  sweep_timer = 0;
  sweep_enabled = false;
  sweep_shadow = 0;
  sweep_negate_used = false;
}

void SquareChannel::triggerChannel() {
  enabled = dac_enabled;
  if (length_counter == 0)
    length_counter = 64;
  timer = (2048 - frequency) * 4;
  volume = volume_init;
  envelope_timer = envelope_period;

  if (has_sweep) {
    sweep_shadow = frequency;
    sweep_timer = sweep_period ? sweep_period : 8;
    sweep_negate_used = false;
    sweep_enabled = (sweep_period != 0 || sweep_shift != 0);
    if (sweep_shift > 0) {
      uint16_t new_freq = sweepCalc();
      if (new_freq > 2047)
        enabled = false;
    }
  }
}

void SquareChannel::tickTimer() {
  timer--;
  if (timer <= 0) {
    timer = (2048 - frequency) * 4;
    duty_pos = (duty_pos + 1) & 7;
  }
}

void SquareChannel::tickLength() {
  if (length_enabled && length_counter > 0) {
    length_counter--;
    if (length_counter == 0)
      enabled = false;
  }
}

void SquareChannel::tickEnvelope() {
  if (envelope_period == 0)
    return;
  envelope_timer--;
  if (envelope_timer <= 0) {
    envelope_timer = envelope_period;
    if (envelope_add && volume < 15)
      volume++;
    else if (!envelope_add && volume > 0)
      volume--;
  }
}

uint16_t SquareChannel::sweepCalc() {
  uint16_t new_freq = sweep_shadow >> sweep_shift;
  if (sweep_negate) {
    new_freq = sweep_shadow - new_freq;
    sweep_negate_used = true;
  } else {
    new_freq = sweep_shadow + new_freq;
  }
  return new_freq;
}

void SquareChannel::tickSweep() {
  if (!has_sweep)
    return;
  sweep_timer--;
  if (sweep_timer <= 0) {
    sweep_timer = sweep_period ? sweep_period : 8;
    if (sweep_enabled && sweep_period > 0) {
      uint16_t new_freq = sweepCalc();
      if (new_freq <= 2047 && sweep_shift > 0) {
        sweep_shadow = new_freq;
        frequency = new_freq;
        // Overflow check
        if (sweepCalc() > 2047)
          enabled = false;
      }
      if (new_freq > 2047)
        enabled = false;
    }
  }
}

int8_t SquareChannel::output() const {
  if (!enabled || !dac_enabled)
    return 0;
  return DUTY_TABLE[duty][duty_pos] ? volume : -volume;
}

// Wave Channel

void WaveChannel::reset() {
  enabled = false;
  dac_enabled = false;
  length_counter = 0;
  length_enabled = false;
  volume_code = 0;
  frequency = 0;
  timer = 0;
  position = 0;
  sample_buffer = 0;
  // starting shouldn't clear the wave ram
}

void WaveChannel::triggerChannel() {
  enabled = dac_enabled;
  if (length_counter == 0)
    length_counter = 256;
  timer = (2048 - frequency) * 2;
  position = 0;
}

void WaveChannel::tickTimer() {
  timer--;
  if (timer <= 0) {
    timer = (2048 - frequency) * 2;
    position = (position + 1) & 31;
    uint8_t byte = wave_ram[position / 2];
    sample_buffer = (position & 1) ? (byte & 0x0F) : (byte >> 4);
  }
}

void WaveChannel::tickLength() {
  if (length_enabled && length_counter > 0) {
    length_counter--;
    if (length_counter == 0)
      enabled = false;
  }
}

int8_t WaveChannel::output() const {
  if (!enabled || !dac_enabled)
    return 0;
  uint8_t shifted;
  switch (volume_code) {
  case 0:
    shifted = 0;
    break;
  case 1:
    shifted = sample_buffer;
    break;
  case 2:
    shifted = sample_buffer >> 1;
    break;
  case 3:
    shifted = sample_buffer >> 2;
    break;
  default:
    shifted = 0;
    break;
  }
  return static_cast<int8_t>(shifted) - 8;
}

// Noise Channel

static const int NOISE_DIVISORS[8] = {8, 16, 32, 48, 64, 80, 96, 112};

void NoiseChannel::reset() {
  enabled = false;
  dac_enabled = false;
  length_counter = 0;
  length_enabled = false;
  volume = 0;
  volume_init = 0;
  envelope_add = false;
  envelope_period = 0;
  envelope_timer = 0;
  clock_shift = 0;
  width_mode = false;
  divisor_code = 0;
  timer = 0;
  lfsr = 0x7FFF;
}

void NoiseChannel::triggerChannel() {
  enabled = dac_enabled;
  if (length_counter == 0)
    length_counter = 64;
  timer = NOISE_DIVISORS[divisor_code] << clock_shift;
  volume = volume_init;
  envelope_timer = envelope_period;
  lfsr = 0x7FFF;
}

void NoiseChannel::tickTimer() {
  timer--;
  if (timer <= 0) {
    timer = NOISE_DIVISORS[divisor_code] << clock_shift;
    uint8_t xor_result = (lfsr & 1) ^ ((lfsr >> 1) & 1);
    lfsr >>= 1;
    lfsr |= xor_result << 14;
    if (width_mode) {
      lfsr &= ~(1 << 6);
      lfsr |= xor_result << 6;
    }
  }
}

void NoiseChannel::tickLength() {
  if (length_enabled && length_counter > 0) {
    length_counter--;
    if (length_counter == 0)
      enabled = false;
  }
}

void NoiseChannel::tickEnvelope() {
  if (envelope_period == 0)
    return;
  envelope_timer--;
  if (envelope_timer <= 0) {
    envelope_timer = envelope_period;
    if (envelope_add && volume < 15)
      volume++;
    else if (!envelope_add && volume > 0)
      volume--;
  }
}

int8_t NoiseChannel::output() const {
  if (!enabled || !dac_enabled)
    return 0;
  return (lfsr & 1) ? -volume : volume;
}

// APU

void APU::reset() {
  power_ = true;
  frame_seq_step_ = 0;
  frame_seq_timer_ = 0;
  sample_timer_ = 0;
  buffer_.clear();

  ch1_.reset();
  ch1_.has_sweep = true;
  // Post-DMG-boot-ROM state: ch1 was triggered for the startup sound
  ch1_.dac_enabled = true;
  ch1_.enabled = true;
  ch1_.duty = 2;         // NR11 = $BF: duty 2 (50%)
  ch1_.volume_init = 15; // NR12 = $F3: volume 15, add=false (down), period 3
  ch1_.envelope_add = false;
  ch1_.envelope_period = 3;
  ch2_.reset();
  ch3_.reset();
  ch4_.reset();

  nr50_ = 0x77;
  nr51_ = 0xF3;
}

void APU::tick(int tcycles) {
  if (!power_) {
    for (int i = 0; i < tcycles; i++) {
      sample_timer_ += SAMPLE_RATE;
      if (sample_timer_ >= CPU_CLOCK) {
        sample_timer_ -= CPU_CLOCK;
        buffer_.push_back(0.0f);
        buffer_.push_back(0.0f);
      }
    }
    return;
  }

  for (int t = 0; t < tcycles; t++) {
    ch1_.tickTimer();
    ch2_.tickTimer();
    ch3_.tickTimer();
    ch4_.tickTimer();

    frame_seq_timer_++;
    if (frame_seq_timer_ >= FRAME_SEQ_PERIOD) {
      frame_seq_timer_ = 0;
      tickFrameSequencer();
    }

    sample_timer_ += SAMPLE_RATE;
    if (sample_timer_ >= CPU_CLOCK) {
      sample_timer_ -= CPU_CLOCK;
      generateSample();
    }
  }
}

void APU::tickFrameSequencer() {
  switch (frame_seq_step_) {
  case 0:
    ch1_.tickLength();
    ch2_.tickLength();
    ch3_.tickLength();
    ch4_.tickLength();
    break;
  case 2:
    ch1_.tickLength();
    ch2_.tickLength();
    ch3_.tickLength();
    ch4_.tickLength();
    ch1_.tickSweep();
    break;
  case 4:
    ch1_.tickLength();
    ch2_.tickLength();
    ch3_.tickLength();
    ch4_.tickLength();
    break;
  case 6:
    ch1_.tickLength();
    ch2_.tickLength();
    ch3_.tickLength();
    ch4_.tickLength();
    ch1_.tickSweep();
    break;
  case 7:
    ch1_.tickEnvelope();
    ch2_.tickEnvelope();
    ch4_.tickEnvelope();
    break;
  }
  frame_seq_step_ = (frame_seq_step_ + 1) & 7;
}

void APU::generateSample() {
  float left = 0.0f, right = 0.0f;

  int8_t ch1_out = ch1_.output();
  int8_t ch2_out = ch2_.output();
  int8_t ch3_out = ch3_.output();
  int8_t ch4_out = ch4_.output();

  if (nr51_ & 0x10)
    left += ch1_out;
  if (nr51_ & 0x20)
    left += ch2_out;
  if (nr51_ & 0x40)
    left += ch3_out;
  if (nr51_ & 0x80)
    left += ch4_out;

  if (nr51_ & 0x01)
    right += ch1_out;
  if (nr51_ & 0x02)
    right += ch2_out;
  if (nr51_ & 0x04)
    right += ch3_out;
  if (nr51_ & 0x08)
    right += ch4_out;

  int left_vol = ((nr50_ >> 4) & 7) + 1;
  int right_vol = (nr50_ & 7) + 1;

  left *= left_vol;
  right *= right_vol;

  // Normalize: max per channel is 15, 4 channels, volume 8 => max 480
  constexpr float SCALE = 1.0f / 480.0f;
  buffer_.push_back(left * SCALE);
  buffer_.push_back(right * SCALE);
}

uint8_t APU::readRegister(uint16_t addr) const {
  // OR masks for read-only bits
  static constexpr uint8_t READ_MASKS[] = {
      0x80, 0x3F, 0x00, 0xFF, 0xBF, // NR10-NR14
      0xFF, 0x3F, 0x00, 0xFF, 0xBF, // NR20-NR24 (NR20 unused)
      0x7F, 0xFF, 0x9F, 0xFF, 0xBF, // NR30-NR34
      0xFF, 0xFF, 0x00, 0x00, 0xBF, // NR40-NR44 (NR40 unused)
      0x00, 0x00, 0x70,             // NR50-NR52
  };

  if (addr >= 0xFF30 && addr <= 0xFF3F) {
    if (ch3_.enabled)
      return ch3_.wave_ram[ch3_.position / 2];
    return ch3_.wave_ram[addr - 0xFF30];
  }

  uint8_t reg_idx = addr - 0xFF10;
  if (reg_idx >= sizeof(READ_MASKS))
    return 0xFF;
  uint8_t mask = READ_MASKS[reg_idx];

  switch (addr) {
  case 0xFF10:
    return 0x80 | (ch1_.sweep_period << 4) | (ch1_.sweep_negate ? 0x08 : 0) |
           ch1_.sweep_shift;
  case 0xFF11:
    return (ch1_.duty << 6) | 0x3F;
  case 0xFF12:
    return (ch1_.volume_init << 4) | (ch1_.envelope_add ? 0x08 : 0) |
           ch1_.envelope_period;
  case 0xFF13:
    return 0xFF;
  case 0xFF14:
    return (ch1_.length_enabled ? 0x40 : 0) | 0xBF;

  case 0xFF16:
    return (ch2_.duty << 6) | 0x3F;
  case 0xFF17:
    return (ch2_.volume_init << 4) | (ch2_.envelope_add ? 0x08 : 0) |
           ch2_.envelope_period;
  case 0xFF18:
    return 0xFF;
  case 0xFF19:
    return (ch2_.length_enabled ? 0x40 : 0) | 0xBF;

  case 0xFF1A:
    return ch3_.dac_enabled ? 0xFF : 0x7F;
  case 0xFF1B:
    return 0xFF;
  case 0xFF1C:
    return (ch3_.volume_code << 5) | 0x9F;
  case 0xFF1D:
    return 0xFF;
  case 0xFF1E:
    return (ch3_.length_enabled ? 0x40 : 0) | 0xBF;

  case 0xFF20:
    return 0xFF;
  case 0xFF21:
    return (ch4_.volume_init << 4) | (ch4_.envelope_add ? 0x08 : 0) |
           ch4_.envelope_period;
  case 0xFF22:
    return (ch4_.clock_shift << 4) | (ch4_.width_mode ? 0x08 : 0) |
           ch4_.divisor_code;
  case 0xFF23:
    return (ch4_.length_enabled ? 0x40 : 0) | 0xBF;

  case 0xFF24:
    return nr50_;
  case 0xFF25:
    return nr51_;
  case 0xFF26: {
    uint8_t val = 0x70;
    if (power_)
      val |= 0x80;
    if (ch1_.enabled)
      val |= 0x01;
    if (ch2_.enabled)
      val |= 0x02;
    if (ch3_.enabled)
      val |= 0x04;
    if (ch4_.enabled)
      val |= 0x08;
    return val;
  }

  default:
    return 0xFF | mask;
  }
}

void APU::writeRegister(uint16_t addr, uint8_t val) {
  if (addr >= 0xFF30 && addr <= 0xFF3F) {
    ch3_.wave_ram[addr - 0xFF30] = val;
    return;
  }

  bool is_power_ctrl = (addr == 0xFF26);
  if (!power_ && !is_power_ctrl) {
    // When powered off, only NR52 and length counters are writable
    if (addr == 0xFF11)
      ch1_.length_counter = 64 - (val & 0x3F);
    else if (addr == 0xFF16)
      ch2_.length_counter = 64 - (val & 0x3F);
    else if (addr == 0xFF1B)
      ch3_.length_counter = 256 - val;
    else if (addr == 0xFF20)
      ch4_.length_counter = 64 - (val & 0x3F);
    return;
  }

  switch (addr) {
  // Channel 1 - Square with sweep
  case 0xFF10:
    ch1_.sweep_period = (val >> 4) & 7;
    ch1_.sweep_negate = val & 0x08;
    ch1_.sweep_shift = val & 0x07;
    if (ch1_.sweep_negate_used && !ch1_.sweep_negate)
      ch1_.enabled = false;
    break;
  case 0xFF11:
    ch1_.duty = (val >> 6) & 3;
    ch1_.length_counter = 64 - (val & 0x3F);
    break;
  case 0xFF12:
    ch1_.volume_init = (val >> 4) & 0x0F;
    ch1_.envelope_add = val & 0x08;
    ch1_.envelope_period = val & 0x07;
    ch1_.dac_enabled = (val & 0xF8) != 0;
    if (!ch1_.dac_enabled)
      ch1_.enabled = false;
    break;
  case 0xFF13:
    ch1_.frequency = (ch1_.frequency & 0x700) | val;
    break;
  case 0xFF14:
    ch1_.frequency = (ch1_.frequency & 0xFF) | ((val & 0x07) << 8);
    ch1_.length_enabled = val & 0x40;
    if (val & 0x80)
      ch1_.triggerChannel();
    break;

  // Channel 2 - Square
  case 0xFF16:
    ch2_.duty = (val >> 6) & 3;
    ch2_.length_counter = 64 - (val & 0x3F);
    break;
  case 0xFF17:
    ch2_.volume_init = (val >> 4) & 0x0F;
    ch2_.envelope_add = val & 0x08;
    ch2_.envelope_period = val & 0x07;
    ch2_.dac_enabled = (val & 0xF8) != 0;
    if (!ch2_.dac_enabled)
      ch2_.enabled = false;
    break;
  case 0xFF18:
    ch2_.frequency = (ch2_.frequency & 0x700) | val;
    break;
  case 0xFF19:
    ch2_.frequency = (ch2_.frequency & 0xFF) | ((val & 0x07) << 8);
    ch2_.length_enabled = val & 0x40;
    if (val & 0x80)
      ch2_.triggerChannel();
    break;

  // Channel 3 - Wave
  case 0xFF1A:
    ch3_.dac_enabled = val & 0x80;
    if (!ch3_.dac_enabled)
      ch3_.enabled = false;
    break;
  case 0xFF1B:
    ch3_.length_counter = 256 - val;
    break;
  case 0xFF1C:
    ch3_.volume_code = (val >> 5) & 3;
    break;
  case 0xFF1D:
    ch3_.frequency = (ch3_.frequency & 0x700) | val;
    break;
  case 0xFF1E:
    ch3_.frequency = (ch3_.frequency & 0xFF) | ((val & 0x07) << 8);
    ch3_.length_enabled = val & 0x40;
    if (val & 0x80)
      ch3_.triggerChannel();
    break;

  // Channel 4 - Noise
  case 0xFF20:
    ch4_.length_counter = 64 - (val & 0x3F);
    break;
  case 0xFF21:
    ch4_.volume_init = (val >> 4) & 0x0F;
    ch4_.envelope_add = val & 0x08;
    ch4_.envelope_period = val & 0x07;
    ch4_.dac_enabled = (val & 0xF8) != 0;
    if (!ch4_.dac_enabled)
      ch4_.enabled = false;
    break;
  case 0xFF22:
    ch4_.clock_shift = (val >> 4) & 0x0F;
    ch4_.width_mode = val & 0x08;
    ch4_.divisor_code = val & 0x07;
    break;
  case 0xFF23:
    ch4_.length_enabled = val & 0x40;
    if (val & 0x80)
      ch4_.triggerChannel();
    break;

  // Master control
  case 0xFF24:
    nr50_ = val;
    break;
  case 0xFF25:
    nr51_ = val;
    break;
  case 0xFF26:
    power_ = val & 0x80;
    if (!power_) {
      ch1_.reset();
      ch1_.has_sweep = true;
      ch2_.reset();
      ch3_.reset();
      ch4_.reset();
      nr50_ = 0;
      nr51_ = 0;
    }
    break;
  }
}

const float *APU::getBuffer(size_t &count) const {
  count = buffer_.size();
  if (count == 0)
    return nullptr;
  return buffer_.data();
}

void APU::consumeSamples(size_t count) {
  if (count >= buffer_.size()) {
    buffer_.clear();
  } else {
    buffer_.erase(buffer_.begin(), buffer_.begin() + count);
  }
}

void APU::clearBuffer() { buffer_.clear(); }

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
void saveSquare(std::vector<uint8_t> &out, const SquareChannel &c) {
  writeBool(out, c.enabled);
  writeBool(out, c.dac_enabled);
  writeU8(out, c.duty);
  writeU8(out, c.duty_pos);
  writeU8(out, c.length_counter);
  writeBool(out, c.length_enabled);
  writeU8(out, c.volume);
  writeU8(out, c.volume_init);
  writeBool(out, c.envelope_add);
  writeU8(out, c.envelope_period);
  writeU8(out, c.envelope_timer);
  writeU16(out, c.frequency);
  writeU32(out, static_cast<uint32_t>(c.timer));
  writeBool(out, c.trigger);
  writeBool(out, c.has_sweep);
  writeU8(out, c.sweep_period);
  writeU8(out, c.sweep_shift);
  writeBool(out, c.sweep_negate);
  writeU8(out, c.sweep_timer);
  writeBool(out, c.sweep_enabled);
  writeU16(out, c.sweep_shadow);
  writeBool(out, c.sweep_negate_used);
}
bool loadSquare(const uint8_t *&p, const uint8_t *end, SquareChannel &c) {
  if (!readBool(p, end, c.enabled) || !readBool(p, end, c.dac_enabled) ||
      !readU8(p, end, c.duty) || !readU8(p, end, c.duty_pos) ||
      !readU8(p, end, c.length_counter) ||
      !readBool(p, end, c.length_enabled) || !readU8(p, end, c.volume) ||
      !readU8(p, end, c.volume_init) || !readBool(p, end, c.envelope_add) ||
      !readU8(p, end, c.envelope_period) || !readU8(p, end, c.envelope_timer) ||
      !readU16(p, end, c.frequency))
    return false;
  uint32_t u;
  if (!readU32(p, end, u))
    return false;
  c.timer = static_cast<int>(u);
  if (!readBool(p, end, c.trigger) || !readBool(p, end, c.has_sweep) ||
      !readU8(p, end, c.sweep_period) || !readU8(p, end, c.sweep_shift) ||
      !readBool(p, end, c.sweep_negate) || !readU8(p, end, c.sweep_timer) ||
      !readBool(p, end, c.sweep_enabled) || !readU16(p, end, c.sweep_shadow) ||
      !readBool(p, end, c.sweep_negate_used))
    return false;
  return true;
}
void saveWave(std::vector<uint8_t> &out, const WaveChannel &c) {
  writeBool(out, c.enabled);
  writeBool(out, c.dac_enabled);
  writeU16(out, c.length_counter);
  writeBool(out, c.length_enabled);
  writeU8(out, c.volume_code);
  writeU16(out, c.frequency);
  writeU32(out, static_cast<uint32_t>(c.timer));
  writeU8(out, c.position);
  writeU8(out, c.sample_buffer);
  for (uint8_t x : c.wave_ram)
    out.push_back(x);
}
bool loadWave(const uint8_t *&p, const uint8_t *end, WaveChannel &c) {
  if (!readBool(p, end, c.enabled) || !readBool(p, end, c.dac_enabled) ||
      !readU16(p, end, c.length_counter) ||
      !readBool(p, end, c.length_enabled) || !readU8(p, end, c.volume_code) ||
      !readU16(p, end, c.frequency))
    return false;
  uint32_t u;
  if (!readU32(p, end, u))
    return false;
  c.timer = static_cast<int>(u);
  if (!readU8(p, end, c.position) || !readU8(p, end, c.sample_buffer))
    return false;
  if (p + c.wave_ram.size() > end)
    return false;
  std::memcpy(c.wave_ram.data(), p, c.wave_ram.size());
  p += c.wave_ram.size();
  return true;
}
void saveNoise(std::vector<uint8_t> &out, const NoiseChannel &c) {
  writeBool(out, c.enabled);
  writeBool(out, c.dac_enabled);
  writeU8(out, c.length_counter);
  writeBool(out, c.length_enabled);
  writeU8(out, c.volume);
  writeU8(out, c.volume_init);
  writeBool(out, c.envelope_add);
  writeU8(out, c.envelope_period);
  writeU8(out, c.envelope_timer);
  writeU8(out, c.clock_shift);
  writeBool(out, c.width_mode);
  writeU8(out, c.divisor_code);
  writeU32(out, static_cast<uint32_t>(c.timer));
  writeU16(out, c.lfsr);
}
bool loadNoise(const uint8_t *&p, const uint8_t *end, NoiseChannel &c) {
  if (!readBool(p, end, c.enabled) || !readBool(p, end, c.dac_enabled) ||
      !readU8(p, end, c.length_counter) ||
      !readBool(p, end, c.length_enabled) || !readU8(p, end, c.volume) ||
      !readU8(p, end, c.volume_init) || !readBool(p, end, c.envelope_add) ||
      !readU8(p, end, c.envelope_period) || !readU8(p, end, c.envelope_timer) ||
      !readU8(p, end, c.clock_shift) || !readBool(p, end, c.width_mode) ||
      !readU8(p, end, c.divisor_code))
    return false;
  uint32_t u;
  if (!readU32(p, end, u))
    return false;
  c.timer = static_cast<int>(u);
  return readU16(p, end, c.lfsr);
}
} // namespace

void APU::saveState(std::vector<uint8_t> &out) const {
  writeBool(out, power_);
  writeU8(out, frame_seq_step_);
  writeU32(out, static_cast<uint32_t>(frame_seq_timer_));
  saveSquare(out, ch1_);
  saveSquare(out, ch2_);
  saveWave(out, ch3_);
  saveNoise(out, ch4_);
  writeU8(out, nr50_);
  writeU8(out, nr51_);
  writeU32(out, static_cast<uint32_t>(sample_timer_));
}

bool APU::loadState(const uint8_t *&data, const uint8_t *end) {
  if (!readBool(data, end, power_) || !readU8(data, end, frame_seq_step_))
    return false;
  uint32_t u;
  if (!readU32(data, end, u))
    return false;
  frame_seq_timer_ = static_cast<int>(u);
  if (!loadSquare(data, end, ch1_) || !loadSquare(data, end, ch2_) ||
      !loadWave(data, end, ch3_) || !loadNoise(data, end, ch4_) ||
      !readU8(data, end, nr50_) || !readU8(data, end, nr51_))
    return false;
  if (!readU32(data, end, u))
    return false;
  sample_timer_ = static_cast<int>(u);
  buffer_.clear();
  return true;
}

} // namespace nativecore
