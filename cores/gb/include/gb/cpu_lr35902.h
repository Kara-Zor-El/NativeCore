#pragma once

#include <cstdint>
#include <vector>

namespace nativecore {

class GBCore;

class CPU {
public:
  static constexpr uint8_t FLAG_Z = 0x80;
  static constexpr uint8_t FLAG_N = 0x40;
  static constexpr uint8_t FLAG_H = 0x20;
  static constexpr uint8_t FLAG_C = 0x10;

  void connectBus(GBCore *bus) { bus_ = bus; }
  void reset();
  void step();

  uint16_t pc() const { return pc_; }
  uint16_t sp() const { return sp_; }
  uint8_t a() const { return a_; }
  uint8_t f() const { return f_; }
  uint8_t b() const { return b_; }
  uint8_t c() const { return c_; }
  uint8_t d() const { return d_; }
  uint8_t e() const { return e_; }
  uint8_t h() const { return h_; }
  uint8_t l() const { return l_; }

  bool halted() const { return halted_; }
  bool ime() const { return ime_; }
  bool stopped() const { return stopped_; }

  void saveState(std::vector<uint8_t> &out) const;
  bool loadState(const uint8_t *&data, const uint8_t *end);

private:
  GBCore *bus_ = nullptr;

  uint8_t a_ = 0, f_ = 0;
  uint8_t b_ = 0, c_ = 0;
  uint8_t d_ = 0, e_ = 0;
  uint8_t h_ = 0, l_ = 0;
  uint16_t sp_ = 0;
  uint16_t pc_ = 0;

  bool ime_ = false;
  bool ime_scheduled_ = false;
  bool halted_ = false;
  bool halt_bug_ = false;
  bool stopped_ = false;

  uint16_t af() const { return (static_cast<uint16_t>(a_) << 8) | f_; }
  uint16_t bc() const { return (static_cast<uint16_t>(b_) << 8) | c_; }
  uint16_t de() const { return (static_cast<uint16_t>(d_) << 8) | e_; }
  uint16_t hl() const { return (static_cast<uint16_t>(h_) << 8) | l_; }

  void setAF(uint16_t v) {
    a_ = v >> 8;
    f_ = v & 0xF0;
  }
  void setBC(uint16_t v) {
    b_ = v >> 8;
    c_ = v & 0xFF;
  }
  void setDE(uint16_t v) {
    d_ = v >> 8;
    e_ = v & 0xFF;
  }
  void setHL(uint16_t v) {
    h_ = v >> 8;
    l_ = v & 0xFF;
  }

  void setFlag(uint8_t flag, bool val) {
    if (val)
      f_ |= flag;
    else
      f_ &= ~flag;
  }
  bool getFlag(uint8_t flag) const { return (f_ & flag) != 0; }

  uint8_t read(uint16_t addr);
  void write(uint16_t addr, uint8_t val);
  void tick();

  uint8_t fetch();
  uint16_t fetch16();

  uint8_t getReg8(uint8_t idx);
  void setReg8(uint8_t idx, uint8_t val);
  uint16_t getReg16(uint8_t idx);
  void setReg16(uint8_t idx, uint16_t val);
  uint16_t getReg16AF(uint8_t idx);
  void setReg16AF(uint8_t idx, uint16_t val);

  void aluAdd(uint8_t val, bool carry);
  void aluSub(uint8_t val, bool carry);
  void aluAnd(uint8_t val);
  void aluXor(uint8_t val);
  void aluOr(uint8_t val);
  void aluCp(uint8_t val);
  void aluInc(uint8_t &reg);
  void aluDec(uint8_t &reg);
  void addHL(uint16_t val);

  void push16(uint16_t val);
  uint16_t pop16();

  void executeBase(uint8_t opcode);
  void executeCB();
  bool handleInterrupts();

  bool checkCondition(uint8_t cc);
};

} // namespace nativecore
