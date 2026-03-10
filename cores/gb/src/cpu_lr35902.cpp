#include "cpu_lr35902.h"
#include "gb_core.h"

namespace nativecore {

void CPU::reset() {
  // DMG-CPU-06 post-boot register values
  a_ = 0x01;
  f_ = 0xB0;
  b_ = 0x00;
  c_ = 0x13;
  d_ = 0x00;
  e_ = 0xD8;
  h_ = 0x01;
  l_ = 0x4D;
  sp_ = 0xFFFE;
  pc_ = 0x0100;
  ime_ = false;
  ime_scheduled_ = false;
  halted_ = false;
  halt_bug_ = false;
  stopped_ = false;
}

uint8_t CPU::read(uint16_t addr) { return bus_->busRead(addr); }

void CPU::write(uint16_t addr, uint8_t val) { bus_->busWrite(addr, val); }

void CPU::tick() { bus_->tickMCycle(); }

// Trigger OAM IDU corruption if addr is in the OAM range ($FE00-$FEFF).
static void checkOAMBug(GBCore *bus, uint16_t addr, GBCore::OAMBugType type) {
  if (addr >= 0xFE00 && addr <= 0xFEFF) {
    bus->triggerOAMBug(type);
  }
}

uint8_t CPU::fetch() {
  uint8_t val = read(pc_);
  tick();
  if (halt_bug_) {
    halt_bug_ = false;
  } else {
    pc_++;
  }
  return val;
}

uint16_t CPU::fetch16() {
  uint8_t lo = fetch();
  uint8_t hi = fetch();
  return (static_cast<uint16_t>(hi) << 8) | lo;
}

uint8_t CPU::getReg8(uint8_t idx) {
  switch (idx & 7) {
  case 0:
    return b_;
  case 1:
    return c_;
  case 2:
    return d_;
  case 3:
    return e_;
  case 4:
    return h_;
  case 5:
    return l_;
  case 6: {
    uint8_t v = read(hl());
    tick();
    return v;
  }
  case 7:
    return a_;
  }
  return 0;
}

void CPU::setReg8(uint8_t idx, uint8_t val) {
  switch (idx & 7) {
  case 0:
    b_ = val;
    break;
  case 1:
    c_ = val;
    break;
  case 2:
    d_ = val;
    break;
  case 3:
    e_ = val;
    break;
  case 4:
    h_ = val;
    break;
  case 5:
    l_ = val;
    break;
  case 6:
    write(hl(), val);
    tick();
    break;
  case 7:
    a_ = val;
    break;
  }
}

uint16_t CPU::getReg16(uint8_t idx) {
  switch (idx & 3) {
  case 0:
    return bc();
  case 1:
    return de();
  case 2:
    return hl();
  case 3:
    return sp_;
  }
  return 0;
}

void CPU::setReg16(uint8_t idx, uint16_t val) {
  switch (idx & 3) {
  case 0:
    setBC(val);
    break;
  case 1:
    setDE(val);
    break;
  case 2:
    setHL(val);
    break;
  case 3:
    sp_ = val;
    break;
  }
}

uint16_t CPU::getReg16AF(uint8_t idx) {
  switch (idx & 3) {
  case 0:
    return bc();
  case 1:
    return de();
  case 2:
    return hl();
  case 3:
    return af();
  }
  return 0;
}

void CPU::setReg16AF(uint8_t idx, uint16_t val) {
  switch (idx & 3) {
  case 0:
    setBC(val);
    break;
  case 1:
    setDE(val);
    break;
  case 2:
    setHL(val);
    break;
  case 3:
    setAF(val);
    break;
  }
}

void CPU::aluAdd(uint8_t val, bool carry) {
  uint8_t c = carry && getFlag(FLAG_C) ? 1 : 0;
  uint16_t result = a_ + val + c;
  uint8_t half = (a_ & 0x0F) + (val & 0x0F) + c;

  f_ = 0;
  setFlag(FLAG_Z, (result & 0xFF) == 0);
  setFlag(FLAG_H, half > 0x0F);
  setFlag(FLAG_C, result > 0xFF);
  a_ = result & 0xFF;
}

void CPU::aluSub(uint8_t val, bool carry) {
  uint8_t c = carry && getFlag(FLAG_C) ? 1 : 0;
  int result = a_ - val - c;
  int half = (a_ & 0x0F) - (val & 0x0F) - c;

  f_ = FLAG_N;
  setFlag(FLAG_Z, (result & 0xFF) == 0);
  setFlag(FLAG_H, half < 0);
  setFlag(FLAG_C, result < 0);
  a_ = result & 0xFF;
}

void CPU::aluAnd(uint8_t val) {
  a_ &= val;
  f_ = FLAG_H;
  setFlag(FLAG_Z, a_ == 0);
}

void CPU::aluXor(uint8_t val) {
  a_ ^= val;
  f_ = 0;
  setFlag(FLAG_Z, a_ == 0);
}

void CPU::aluOr(uint8_t val) {
  a_ |= val;
  f_ = 0;
  setFlag(FLAG_Z, a_ == 0);
}

void CPU::aluCp(uint8_t val) {
  int result = a_ - val;
  int half = (a_ & 0x0F) - (val & 0x0F);

  f_ = FLAG_N;
  setFlag(FLAG_Z, (result & 0xFF) == 0);
  setFlag(FLAG_H, half < 0);
  setFlag(FLAG_C, result < 0);
}

void CPU::aluInc(uint8_t &reg) {
  uint8_t result = reg + 1;
  setFlag(FLAG_Z, result == 0);
  setFlag(FLAG_N, false);
  setFlag(FLAG_H, (reg & 0x0F) == 0x0F);
  reg = result;
}

void CPU::aluDec(uint8_t &reg) {
  uint8_t result = reg - 1;
  setFlag(FLAG_Z, result == 0);
  setFlag(FLAG_N, true);
  setFlag(FLAG_H, (reg & 0x0F) == 0x00);
  reg = result;
}

void CPU::addHL(uint16_t val) {
  uint32_t result = hl() + val;
  setFlag(FLAG_N, false);
  setFlag(FLAG_H, ((hl() & 0x0FFF) + (val & 0x0FFF)) > 0x0FFF);
  setFlag(FLAG_C, result > 0xFFFF);
  setHL(result & 0xFFFF);
  tick(); // internal cycle
}

void CPU::push16(uint16_t val) {
  tick(); // internal cycle
  // First dec SP: if SP was in OAM range, triggers write corruption
  checkOAMBug(bus_, sp_, GBCore::OAMBugType::Write);
  sp_--;
  write(sp_, (val >> 8) & 0xFF);
  tick();
  // Second dec SP: if new SP+1 still in OAM range
  checkOAMBug(bus_, sp_, GBCore::OAMBugType::Write);
  sp_--;
  write(sp_, val & 0xFF);
  tick();
}

uint16_t CPU::pop16() {
  // First inc of SP (after read) is the glitched one
  uint8_t lo = read(sp_);
  tick();
  checkOAMBug(bus_, sp_, GBCore::OAMBugType::Read); // first SP++ IDU
  sp_++;
  uint8_t hi = read(sp_);
  tick();
  sp_++; // second SP++ does NOT trigger bug
  return (static_cast<uint16_t>(hi) << 8) | lo;
}

bool CPU::checkCondition(uint8_t cc) {
  switch (cc & 3) {
  case 0:
    return !getFlag(FLAG_Z); // NZ
  case 1:
    return getFlag(FLAG_Z); // Z
  case 2:
    return !getFlag(FLAG_C); // NC
  case 3:
    return getFlag(FLAG_C); // C
  }
  return false;
}

bool CPU::handleInterrupts() {
  uint8_t ie = bus_->interruptEnable();
  uint8_t if_reg = bus_->interruptFlags();
  uint8_t pending = ie & if_reg & 0x1F;

  if (pending != 0) {
    halted_ = false;
  }

  if (!ime_ || pending == 0) {
    return false;
  }

  ime_ = false;

  // Two wait cycles
  tick();
  tick();

  // Push PC high byte first
  sp_--;
  write(sp_, (pc_ >> 8) & 0xFF);
  tick();

  // Re-sample IE/IF after high byte push. If the high byte was written to
  // $FFFF (IE register), the pending set may have changed. This is the only
  // point where cancellation can occur — the low byte push is too late.
  ie = bus_->interruptEnable();
  if_reg = bus_->interruptFlags();
  pending = ie & if_reg & 0x1F;

  // Push PC low byte
  sp_--;
  write(sp_, pc_ & 0xFF);
  tick();

  if (pending == 0) {
    pc_ = 0x0000; // Cancelled: no interrupts pending after high-byte push
  } else {
    for (int i = 0; i < 5; i++) {
      if (pending & (1 << i)) {
        bus_->setInterruptFlags(if_reg & ~(1 << i));
        pc_ = 0x0040 + (i * 8);
        break;
      }
    }
  }

  tick(); // fetch at new PC takes a cycle
  return true;
}

void CPU::step() {
  if (handleInterrupts()) {
    return;
  }

  if (halted_) {
    tick(); // consume one M-cycle while halted
    return;
  }

  if (stopped_) {
    tick();
    return;
  }

  bool was_ime_scheduled = ime_scheduled_;
  uint8_t opcode = fetch();
  executeBase(opcode);

  // Only enable IME if it was still scheduled after the instruction ran.
  // DI executed after EI clears ime_scheduled_, so we must re-check here.
  if (was_ime_scheduled && ime_scheduled_) {
    ime_ = true;
    ime_scheduled_ = false;
  }
}

void CPU::executeBase(uint8_t opcode) {
  switch (opcode) {
  // 0x00-0x0F
  case 0x00: // NOP
    break;
  case 0x01: { // LD BC,nn
    uint16_t v = fetch16();
    setBC(v);
    break;
  }
  case 0x02: // LD (BC),A
    write(bc(), a_);
    tick();
    break;
  case 0x03: // INC BC
    checkOAMBug(bus_, bc(), GBCore::OAMBugType::Write);
    setBC(bc() + 1);
    tick();
    break;
  case 0x04: // INC B
    aluInc(b_);
    break;
  case 0x05: // DEC B
    aluDec(b_);
    break;
  case 0x06: // LD B,n
    b_ = fetch();
    break;
  case 0x07: { // RLCA
    uint8_t bit7 = (a_ >> 7) & 1;
    a_ = (a_ << 1) | bit7;
    f_ = 0;
    setFlag(FLAG_C, bit7);
    break;
  }
  case 0x08: { // LD (nn),SP
    uint16_t addr = fetch16();
    write(addr, sp_ & 0xFF);
    tick();
    write(addr + 1, (sp_ >> 8) & 0xFF);
    tick();
    break;
  }
  case 0x09: // ADD HL,BC
    addHL(bc());
    break;
  case 0x0A: // LD A,(BC)
    a_ = read(bc());
    tick();
    break;
  case 0x0B: // DEC BC
    checkOAMBug(bus_, bc(), GBCore::OAMBugType::Write);
    setBC(bc() - 1);
    tick();
    break;
  case 0x0C: // INC C
    aluInc(c_);
    break;
  case 0x0D: // DEC C
    aluDec(c_);
    break;
  case 0x0E: // LD C,n
    c_ = fetch();
    break;
  case 0x0F: { // RRCA
    uint8_t bit0 = a_ & 1;
    a_ = (a_ >> 1) | (bit0 << 7);
    f_ = 0;
    setFlag(FLAG_C, bit0);
    break;
  }

  // 0x10-0x1F
  case 0x10: // STOP
    fetch(); // consume the next byte
    stopped_ = true;
    break;
  case 0x11: { // LD DE,nn
    uint16_t v = fetch16();
    setDE(v);
    break;
  }
  case 0x12: // LD (DE),A
    write(de(), a_);
    tick();
    break;
  case 0x13: // INC DE
    checkOAMBug(bus_, de(), GBCore::OAMBugType::Write);
    setDE(de() + 1);
    tick();
    break;
  case 0x14: // INC D
    aluInc(d_);
    break;
  case 0x15: // DEC D
    aluDec(d_);
    break;
  case 0x16: // LD D,n
    d_ = fetch();
    break;
  case 0x17: { // RLA
    uint8_t bit7 = (a_ >> 7) & 1;
    a_ = (a_ << 1) | (getFlag(FLAG_C) ? 1 : 0);
    f_ = 0;
    setFlag(FLAG_C, bit7);
    break;
  }
  case 0x18: { // JR n
    int8_t offset = static_cast<int8_t>(fetch());
    pc_ += offset;
    tick();
    break;
  }
  case 0x19: // ADD HL,DE
    addHL(de());
    break;
  case 0x1A: // LD A,(DE)
    a_ = read(de());
    tick();
    break;
  case 0x1B: // DEC DE
    checkOAMBug(bus_, de(), GBCore::OAMBugType::Write);
    setDE(de() - 1);
    tick();
    break;
  case 0x1C: // INC E
    aluInc(e_);
    break;
  case 0x1D: // DEC E
    aluDec(e_);
    break;
  case 0x1E: // LD E,n
    e_ = fetch();
    break;
  case 0x1F: { // RRA
    uint8_t bit0 = a_ & 1;
    a_ = (a_ >> 1) | (getFlag(FLAG_C) ? 0x80 : 0);
    f_ = 0;
    setFlag(FLAG_C, bit0);
    break;
  }

  // 0x20-0x2F
  case 0x20: { // JR NZ,n
    int8_t offset = static_cast<int8_t>(fetch());
    if (!getFlag(FLAG_Z)) {
      pc_ += offset;
      tick();
    }
    break;
  }
  case 0x21: { // LD HL,nn
    uint16_t v = fetch16();
    setHL(v);
    break;
  }
  case 0x22: // LD (HL+),A
    checkOAMBug(bus_, hl(), GBCore::OAMBugType::ReadWrite);
    write(hl(), a_);
    tick();
    setHL(hl() + 1);
    break;
  case 0x23: // INC HL
    checkOAMBug(bus_, hl(), GBCore::OAMBugType::Write);
    setHL(hl() + 1);
    tick();
    break;
  case 0x24: // INC H
    aluInc(h_);
    break;
  case 0x25: // DEC H
    aluDec(h_);
    break;
  case 0x26: // LD H,n
    h_ = fetch();
    break;
  case 0x27: { // DAA
    uint8_t correction = 0;
    bool set_c = false;
    if (getFlag(FLAG_H) || (!getFlag(FLAG_N) && (a_ & 0x0F) > 9)) {
      correction |= 0x06;
    }
    if (getFlag(FLAG_C) || (!getFlag(FLAG_N) && a_ > 0x99)) {
      correction |= 0x60;
      set_c = true;
    }
    if (getFlag(FLAG_N)) {
      a_ -= correction;
    } else {
      a_ += correction;
    }
    setFlag(FLAG_Z, a_ == 0);
    setFlag(FLAG_H, false);
    setFlag(FLAG_C, set_c);
    break;
  }
  case 0x28: { // JR Z,n
    int8_t offset = static_cast<int8_t>(fetch());
    if (getFlag(FLAG_Z)) {
      pc_ += offset;
      tick();
    }
    break;
  }
  case 0x29: // ADD HL,HL
    addHL(hl());
    break;
  case 0x2A: // LD A,(HL+)
    checkOAMBug(bus_, hl(), GBCore::OAMBugType::ReadWrite);
    a_ = read(hl());
    tick();
    setHL(hl() + 1);
    break;
  case 0x2B: // DEC HL
    checkOAMBug(bus_, hl(), GBCore::OAMBugType::Write);
    setHL(hl() - 1);
    tick();
    break;
  case 0x2C: // INC L
    aluInc(l_);
    break;
  case 0x2D: // DEC L
    aluDec(l_);
    break;
  case 0x2E: // LD L,n
    l_ = fetch();
    break;
  case 0x2F: // CPL
    a_ = ~a_;
    setFlag(FLAG_N, true);
    setFlag(FLAG_H, true);
    break;

  // 0x30-0x3F
  case 0x30: { // JR NC,n
    int8_t offset = static_cast<int8_t>(fetch());
    if (!getFlag(FLAG_C)) {
      pc_ += offset;
      tick();
    }
    break;
  }
  case 0x31: // LD SP,nn
    sp_ = fetch16();
    break;
  case 0x32: // LD (HL-),A
    checkOAMBug(bus_, hl(), GBCore::OAMBugType::ReadWrite);
    write(hl(), a_);
    tick();
    setHL(hl() - 1);
    break;
  case 0x33: // INC SP
    checkOAMBug(bus_, sp_, GBCore::OAMBugType::Write);
    sp_++;
    tick();
    break;
  case 0x34: { // INC (HL)
    uint8_t val = read(hl());
    tick();
    aluInc(val);
    write(hl(), val);
    tick();
    break;
  }
  case 0x35: { // DEC (HL)
    uint8_t val = read(hl());
    tick();
    aluDec(val);
    write(hl(), val);
    tick();
    break;
  }
  case 0x36: { // LD (HL),n
    uint8_t val = fetch();
    write(hl(), val);
    tick();
    break;
  }
  case 0x37: // SCF
    setFlag(FLAG_N, false);
    setFlag(FLAG_H, false);
    setFlag(FLAG_C, true);
    break;
  case 0x38: { // JR C,n
    int8_t offset = static_cast<int8_t>(fetch());
    if (getFlag(FLAG_C)) {
      pc_ += offset;
      tick();
    }
    break;
  }
  case 0x39: // ADD HL,SP
    addHL(sp_);
    break;
  case 0x3A: // LD A,(HL-)
    checkOAMBug(bus_, hl(), GBCore::OAMBugType::ReadWrite);
    a_ = read(hl());
    tick();
    setHL(hl() - 1);
    break;
  case 0x3B: // DEC SP
    checkOAMBug(bus_, sp_, GBCore::OAMBugType::Write);
    sp_--;
    tick();
    break;
  case 0x3C: // INC A
    aluInc(a_);
    break;
  case 0x3D: // DEC A
    aluDec(a_);
    break;
  case 0x3E: // LD A,n
    a_ = fetch();
    break;
  case 0x3F: // CCF
    setFlag(FLAG_N, false);
    setFlag(FLAG_H, false);
    setFlag(FLAG_C, !getFlag(FLAG_C));
    break;

  // 0x40-0x7F: LD r,r
  case 0x76: // HALT
    halted_ = true;
    if (!ime_) {
      uint8_t ie = bus_->interruptEnable();
      uint8_t if_reg = bus_->interruptFlags();
      if ((ie & if_reg & 0x1F) != 0) {
        halted_ = false;
        halt_bug_ = true;
      }
    }
    break;

  // LD B,r
  case 0x40:
    break; // LD B,B (NOP)
  case 0x41:
    b_ = c_;
    break;
  case 0x42:
    b_ = d_;
    break;
  case 0x43:
    b_ = e_;
    break;
  case 0x44:
    b_ = h_;
    break;
  case 0x45:
    b_ = l_;
    break;
  case 0x46:
    b_ = read(hl());
    tick();
    break;
  case 0x47:
    b_ = a_;
    break;

  // LD C,r
  case 0x48:
    c_ = b_;
    break;
  case 0x49:
    break; // LD C,C (NOP)
  case 0x4A:
    c_ = d_;
    break;
  case 0x4B:
    c_ = e_;
    break;
  case 0x4C:
    c_ = h_;
    break;
  case 0x4D:
    c_ = l_;
    break;
  case 0x4E:
    c_ = read(hl());
    tick();
    break;
  case 0x4F:
    c_ = a_;
    break;

  // LD D,r
  case 0x50:
    d_ = b_;
    break;
  case 0x51:
    d_ = c_;
    break;
  case 0x52:
    break; // LD D,D (NOP)
  case 0x53:
    d_ = e_;
    break;
  case 0x54:
    d_ = h_;
    break;
  case 0x55:
    d_ = l_;
    break;
  case 0x56:
    d_ = read(hl());
    tick();
    break;
  case 0x57:
    d_ = a_;
    break;

  // LD E,r
  case 0x58:
    e_ = b_;
    break;
  case 0x59:
    e_ = c_;
    break;
  case 0x5A:
    e_ = d_;
    break;
  case 0x5B:
    break; // LD E,E (NOP)
  case 0x5C:
    e_ = h_;
    break;
  case 0x5D:
    e_ = l_;
    break;
  case 0x5E:
    e_ = read(hl());
    tick();
    break;
  case 0x5F:
    e_ = a_;
    break;

  // LD H,r
  case 0x60:
    h_ = b_;
    break;
  case 0x61:
    h_ = c_;
    break;
  case 0x62:
    h_ = d_;
    break;
  case 0x63:
    h_ = e_;
    break;
  case 0x64:
    break; // LD H,H (NOP)
  case 0x65:
    h_ = l_;
    break;
  case 0x66:
    h_ = read(hl());
    tick();
    break;
  case 0x67:
    h_ = a_;
    break;

  // LD L,r
  case 0x68:
    l_ = b_;
    break;
  case 0x69:
    l_ = c_;
    break;
  case 0x6A:
    l_ = d_;
    break;
  case 0x6B:
    l_ = e_;
    break;
  case 0x6C:
    l_ = h_;
    break;
  case 0x6D:
    break; // LD L,L (NOP)
  case 0x6E:
    l_ = read(hl());
    tick();
    break;
  case 0x6F:
    l_ = a_;
    break;

  // LD (HL),r
  case 0x70:
    write(hl(), b_);
    tick();
    break;
  case 0x71:
    write(hl(), c_);
    tick();
    break;
  case 0x72:
    write(hl(), d_);
    tick();
    break;
  case 0x73:
    write(hl(), e_);
    tick();
    break;
  case 0x74:
    write(hl(), h_);
    tick();
    break;
  case 0x75:
    write(hl(), l_);
    tick();
    break;
  // 0x76 is HALT (handled above)
  case 0x77:
    write(hl(), a_);
    tick();
    break;

  // LD A,r
  case 0x78:
    a_ = b_;
    break;
  case 0x79:
    a_ = c_;
    break;
  case 0x7A:
    a_ = d_;
    break;
  case 0x7B:
    a_ = e_;
    break;
  case 0x7C:
    a_ = h_;
    break;
  case 0x7D:
    a_ = l_;
    break;
  case 0x7E:
    a_ = read(hl());
    tick();
    break;
  case 0x7F:
    break; // LD A,A (NOP)

  // 0x80-0xBF (ALU operations)
  // ADD A,r
  case 0x80:
    aluAdd(b_, false);
    break;
  case 0x81:
    aluAdd(c_, false);
    break;
  case 0x82:
    aluAdd(d_, false);
    break;
  case 0x83:
    aluAdd(e_, false);
    break;
  case 0x84:
    aluAdd(h_, false);
    break;
  case 0x85:
    aluAdd(l_, false);
    break;
  case 0x86:
    aluAdd(getReg8(6), false);
    break;
  case 0x87:
    aluAdd(a_, false);
    break;

  // ADC A,r
  case 0x88:
    aluAdd(b_, true);
    break;
  case 0x89:
    aluAdd(c_, true);
    break;
  case 0x8A:
    aluAdd(d_, true);
    break;
  case 0x8B:
    aluAdd(e_, true);
    break;
  case 0x8C:
    aluAdd(h_, true);
    break;
  case 0x8D:
    aluAdd(l_, true);
    break;
  case 0x8E:
    aluAdd(getReg8(6), true);
    break;
  case 0x8F:
    aluAdd(a_, true);
    break;

  // SUB r
  case 0x90:
    aluSub(b_, false);
    break;
  case 0x91:
    aluSub(c_, false);
    break;
  case 0x92:
    aluSub(d_, false);
    break;
  case 0x93:
    aluSub(e_, false);
    break;
  case 0x94:
    aluSub(h_, false);
    break;
  case 0x95:
    aluSub(l_, false);
    break;
  case 0x96:
    aluSub(getReg8(6), false);
    break;
  case 0x97:
    aluSub(a_, false);
    break;

  // SBC A,r
  case 0x98:
    aluSub(b_, true);
    break;
  case 0x99:
    aluSub(c_, true);
    break;
  case 0x9A:
    aluSub(d_, true);
    break;
  case 0x9B:
    aluSub(e_, true);
    break;
  case 0x9C:
    aluSub(h_, true);
    break;
  case 0x9D:
    aluSub(l_, true);
    break;
  case 0x9E:
    aluSub(getReg8(6), true);
    break;
  case 0x9F:
    aluSub(a_, true);
    break;

  // AND r
  case 0xA0:
    aluAnd(b_);
    break;
  case 0xA1:
    aluAnd(c_);
    break;
  case 0xA2:
    aluAnd(d_);
    break;
  case 0xA3:
    aluAnd(e_);
    break;
  case 0xA4:
    aluAnd(h_);
    break;
  case 0xA5:
    aluAnd(l_);
    break;
  case 0xA6:
    aluAnd(getReg8(6));
    break;
  case 0xA7:
    aluAnd(a_);
    break;

  // XOR r
  case 0xA8:
    aluXor(b_);
    break;
  case 0xA9:
    aluXor(c_);
    break;
  case 0xAA:
    aluXor(d_);
    break;
  case 0xAB:
    aluXor(e_);
    break;
  case 0xAC:
    aluXor(h_);
    break;
  case 0xAD:
    aluXor(l_);
    break;
  case 0xAE:
    aluXor(getReg8(6));
    break;
  case 0xAF:
    aluXor(a_);
    break;

  // OR r
  case 0xB0:
    aluOr(b_);
    break;
  case 0xB1:
    aluOr(c_);
    break;
  case 0xB2:
    aluOr(d_);
    break;
  case 0xB3:
    aluOr(e_);
    break;
  case 0xB4:
    aluOr(h_);
    break;
  case 0xB5:
    aluOr(l_);
    break;
  case 0xB6:
    aluOr(getReg8(6));
    break;
  case 0xB7:
    aluOr(a_);
    break;

  // CP r
  case 0xB8:
    aluCp(b_);
    break;
  case 0xB9:
    aluCp(c_);
    break;
  case 0xBA:
    aluCp(d_);
    break;
  case 0xBB:
    aluCp(e_);
    break;
  case 0xBC:
    aluCp(h_);
    break;
  case 0xBD:
    aluCp(l_);
    break;
  case 0xBE:
    aluCp(getReg8(6));
    break;
  case 0xBF:
    aluCp(a_);
    break;

  // 0xC0-0xFF (Control flow, stack, misc)
  case 0xC0: // RET NZ
    tick();
    if (!getFlag(FLAG_Z)) {
      pc_ = pop16();
      tick();
    }
    break;
  case 0xC1: // POP BC
    setBC(pop16());
    break;
  case 0xC2: { // JP NZ,nn
    uint16_t addr = fetch16();
    if (!getFlag(FLAG_Z)) {
      pc_ = addr;
      tick();
    }
    break;
  }
  case 0xC3: { // JP nn
    uint16_t addr = fetch16();
    pc_ = addr;
    tick();
    break;
  }
  case 0xC4: { // CALL NZ,nn
    uint16_t addr = fetch16();
    if (!getFlag(FLAG_Z)) {
      push16(pc_);
      pc_ = addr;
    }
    break;
  }
  case 0xC5: // PUSH BC
    push16(bc());
    break;
  case 0xC6: // ADD A,n
    aluAdd(fetch(), false);
    break;
  case 0xC7: // RST 00
    push16(pc_);
    pc_ = 0x0000;
    break;
  case 0xC8: // RET Z
    tick();
    if (getFlag(FLAG_Z)) {
      pc_ = pop16();
      tick();
    }
    break;
  case 0xC9: // RET
    pc_ = pop16();
    tick();
    break;
  case 0xCA: { // JP Z,nn
    uint16_t addr = fetch16();
    if (getFlag(FLAG_Z)) {
      pc_ = addr;
      tick();
    }
    break;
  }
  case 0xCB: // CB prefix
    executeCB();
    break;
  case 0xCC: { // CALL Z,nn
    uint16_t addr = fetch16();
    if (getFlag(FLAG_Z)) {
      push16(pc_);
      pc_ = addr;
    }
    break;
  }
  case 0xCD: { // CALL nn
    uint16_t addr = fetch16();
    push16(pc_);
    pc_ = addr;
    break;
  }
  case 0xCE: // ADC A,n
    aluAdd(fetch(), true);
    break;
  case 0xCF: // RST 08
    push16(pc_);
    pc_ = 0x0008;
    break;

  case 0xD0: // RET NC
    tick();
    if (!getFlag(FLAG_C)) {
      pc_ = pop16();
      tick();
    }
    break;
  case 0xD1: // POP DE
    setDE(pop16());
    break;
  case 0xD2: { // JP NC,nn
    uint16_t addr = fetch16();
    if (!getFlag(FLAG_C)) {
      pc_ = addr;
      tick();
    }
    break;
  }
  // 0xD3 unused
  case 0xD4: { // CALL NC,nn
    uint16_t addr = fetch16();
    if (!getFlag(FLAG_C)) {
      push16(pc_);
      pc_ = addr;
    }
    break;
  }
  case 0xD5: // PUSH DE
    push16(de());
    break;
  case 0xD6: // SUB n
    aluSub(fetch(), false);
    break;
  case 0xD7: // RST 10
    push16(pc_);
    pc_ = 0x0010;
    break;
  case 0xD8: // RET C
    tick();
    if (getFlag(FLAG_C)) {
      pc_ = pop16();
      tick();
    }
    break;
  case 0xD9: // RETI
    pc_ = pop16();
    tick();
    ime_ = true;
    break;
  case 0xDA: { // JP C,nn
    uint16_t addr = fetch16();
    if (getFlag(FLAG_C)) {
      pc_ = addr;
      tick();
    }
    break;
  }
  // 0xDB unused
  case 0xDC: { // CALL C,nn
    uint16_t addr = fetch16();
    if (getFlag(FLAG_C)) {
      push16(pc_);
      pc_ = addr;
    }
    break;
  }
  // 0xDD unused
  case 0xDE: // SBC A,n
    aluSub(fetch(), true);
    break;
  case 0xDF: // RST 18
    push16(pc_);
    pc_ = 0x0018;
    break;

  case 0xE0: { // LD (FF00+n),A
    uint8_t offset = fetch();
    write(0xFF00 + offset, a_);
    tick();
    break;
  }
  case 0xE1: // POP HL
    setHL(pop16());
    break;
  case 0xE2: // LD (FF00+C),A
    write(0xFF00 + c_, a_);
    tick();
    break;
  // 0xE3, 0xE4 unused
  case 0xE5: // PUSH HL
    push16(hl());
    break;
  case 0xE6: // AND n
    aluAnd(fetch());
    break;
  case 0xE7: // RST 20
    push16(pc_);
    pc_ = 0x0020;
    break;
  case 0xE8: { // ADD SP,n
    int8_t offset = static_cast<int8_t>(fetch());
    uint32_t result = sp_ + offset;
    f_ = 0;
    setFlag(FLAG_H, ((sp_ & 0x0F) + (offset & 0x0F)) > 0x0F);
    setFlag(FLAG_C, ((sp_ & 0xFF) + (offset & 0xFF)) > 0xFF);
    sp_ = result & 0xFFFF;
    tick();
    tick();
    break;
  }
  case 0xE9: // JP HL
    pc_ = hl();
    break;
  case 0xEA: { // LD (nn),A
    uint16_t addr = fetch16();
    write(addr, a_);
    tick();
    break;
  }
  // 0xEB, 0xEC, 0xED unused
  case 0xEE: // XOR n
    aluXor(fetch());
    break;
  case 0xEF: // RST 28
    push16(pc_);
    pc_ = 0x0028;
    break;

  case 0xF0: { // LD A,(FF00+n)
    uint8_t offset = fetch();
    a_ = read(0xFF00 + offset);
    tick();
    break;
  }
  case 0xF1: // POP AF
    setAF(pop16());
    break;
  case 0xF2: // LD A,(FF00+C)
    a_ = read(0xFF00 + c_);
    tick();
    break;
  case 0xF3: // DI
    ime_ = false;
    ime_scheduled_ = false;
    break;
  // 0xF4 unused
  case 0xF5: // PUSH AF
    push16(af());
    break;
  case 0xF6: // OR n
    aluOr(fetch());
    break;
  case 0xF7: // RST 30
    push16(pc_);
    pc_ = 0x0030;
    break;
  case 0xF8: { // LD HL,SP+n
    int8_t offset = static_cast<int8_t>(fetch());
    uint32_t result = sp_ + offset;
    f_ = 0;
    setFlag(FLAG_H, ((sp_ & 0x0F) + (offset & 0x0F)) > 0x0F);
    setFlag(FLAG_C, ((sp_ & 0xFF) + (offset & 0xFF)) > 0xFF);
    setHL(result & 0xFFFF);
    tick();
    break;
  }
  case 0xF9: // LD SP,HL
    sp_ = hl();
    tick();
    break;
  case 0xFA: { // LD A,(nn)
    uint16_t addr = fetch16();
    a_ = read(addr);
    tick();
    break;
  }
  case 0xFB: // EI
    ime_scheduled_ = true;
    break;
  // 0xFC, 0xFD unused
  case 0xFE: // CP n
    aluCp(fetch());
    break;
  case 0xFF: // RST 38
    push16(pc_);
    pc_ = 0x0038;
    break;

  default:
    // Undefined opcodes act as NOPs
    break;
  }
}

void CPU::executeCB() {
  uint8_t opcode = fetch();
  uint8_t reg_idx = opcode & 0x07;
  uint8_t operation = opcode >> 3;
  bool is_hl = (reg_idx == 6);

  uint8_t val;
  if (is_hl) {
    val = read(hl());
    tick();
  } else {
    val = getReg8(reg_idx);
  }

  uint8_t result = val;

  switch (operation) {
  case 0: { // RLC
    uint8_t bit7 = (val >> 7) & 1;
    result = (val << 1) | bit7;
    f_ = 0;
    setFlag(FLAG_Z, result == 0);
    setFlag(FLAG_C, bit7);
    break;
  }
  case 1: { // RRC
    uint8_t bit0 = val & 1;
    result = (val >> 1) | (bit0 << 7);
    f_ = 0;
    setFlag(FLAG_Z, result == 0);
    setFlag(FLAG_C, bit0);
    break;
  }
  case 2: { // RL
    uint8_t bit7 = (val >> 7) & 1;
    result = (val << 1) | (getFlag(FLAG_C) ? 1 : 0);
    f_ = 0;
    setFlag(FLAG_Z, result == 0);
    setFlag(FLAG_C, bit7);
    break;
  }
  case 3: { // RR
    uint8_t bit0 = val & 1;
    result = (val >> 1) | (getFlag(FLAG_C) ? 0x80 : 0);
    f_ = 0;
    setFlag(FLAG_Z, result == 0);
    setFlag(FLAG_C, bit0);
    break;
  }
  case 4: { // SLA
    uint8_t bit7 = (val >> 7) & 1;
    result = val << 1;
    f_ = 0;
    setFlag(FLAG_Z, result == 0);
    setFlag(FLAG_C, bit7);
    break;
  }
  case 5: { // SRA
    uint8_t bit0 = val & 1;
    result = (val >> 1) | (val & 0x80);
    f_ = 0;
    setFlag(FLAG_Z, result == 0);
    setFlag(FLAG_C, bit0);
    break;
  }
  case 6: { // SWAP
    result = ((val & 0x0F) << 4) | ((val >> 4) & 0x0F);
    f_ = 0;
    setFlag(FLAG_Z, result == 0);
    break;
  }
  case 7: { // SRL
    uint8_t bit0 = val & 1;
    result = val >> 1;
    f_ = 0;
    setFlag(FLAG_Z, result == 0);
    setFlag(FLAG_C, bit0);
    break;
  }
  default: {
    uint8_t bit = operation & 7;
    if (operation >= 8 && operation < 16) {
      // BIT
      f_ = (f_ & FLAG_C);
      setFlag(FLAG_Z, (val & (1 << bit)) == 0);
      setFlag(FLAG_H, true);
      return; // BIT doesn't write back
    } else if (operation >= 16 && operation < 24) {
      // RES
      result = val & ~(1 << bit);
    } else {
      // SET
      result = val | (1 << bit);
    }
    break;
  }
  }

  if (is_hl) {
    write(hl(), result);
    tick();
  } else {
    setReg8(reg_idx, result);
  }
}

static void writeU16(std::vector<uint8_t> &out, uint16_t v) {
  out.push_back(static_cast<uint8_t>(v));
  out.push_back(static_cast<uint8_t>(v >> 8));
}
static bool readU16(const uint8_t *&p, const uint8_t *end, uint16_t &v) {
  if (p + 2 > end)
    return false;
  v = static_cast<uint16_t>(p[0]) | (static_cast<uint16_t>(p[1]) << 8);
  p += 2;
  return true;
}
static bool readU8(const uint8_t *&p, const uint8_t *end, uint8_t &v) {
  if (p + 1 > end)
    return false;
  v = p[0];
  p += 1;
  return true;
}
static bool readBool(const uint8_t *&p, const uint8_t *end, bool &v) {
  uint8_t b;
  if (!readU8(p, end, b))
    return false;
  v = (b != 0);
  return true;
}

void CPU::saveState(std::vector<uint8_t> &out) const {
  out.push_back(a_);
  out.push_back(f_);
  out.push_back(b_);
  out.push_back(c_);
  out.push_back(d_);
  out.push_back(e_);
  out.push_back(h_);
  out.push_back(l_);
  writeU16(out, sp_);
  writeU16(out, pc_);
  out.push_back(ime_ ? 1 : 0);
  out.push_back(ime_scheduled_ ? 1 : 0);
  out.push_back(halted_ ? 1 : 0);
  out.push_back(halt_bug_ ? 1 : 0);
  out.push_back(stopped_ ? 1 : 0);
}

bool CPU::loadState(const uint8_t *&data, const uint8_t *end) {
  if (!readU8(data, end, a_) || !readU8(data, end, f_) ||
      !readU8(data, end, b_) || !readU8(data, end, c_) ||
      !readU8(data, end, d_) || !readU8(data, end, e_) ||
      !readU8(data, end, h_) || !readU8(data, end, l_))
    return false;
  if (!readU16(data, end, sp_) || !readU16(data, end, pc_))
    return false;
  if (!readBool(data, end, ime_) || !readBool(data, end, ime_scheduled_) ||
      !readBool(data, end, halted_) || !readBool(data, end, halt_bug_) ||
      !readBool(data, end, stopped_))
    return false;
  return true;
}

} // namespace nativecore
