#include "gb_recompiler.h"

#include <cstdio>

namespace nativecore {

GBRecompiler::OpcodeInfo GBRecompiler::decodeOpcode(uint8_t opcode,
                                                    uint8_t next_byte) {
  // Returns {mnemonic, length, is_branch, is_unconditional, is_call, is_return,
  // is_indirect}
  switch (opcode) {
  case 0x00:
    return {"NOP", 1, false, false, false, false, false};
  case 0x01:
    return {"LD BC,nn", 3, false, false, false, false, false};
  case 0x02:
    return {"LD (BC),A", 1, false, false, false, false, false};
  case 0x03:
    return {"INC BC", 1, false, false, false, false, false};
  case 0x04:
    return {"INC B", 1, false, false, false, false, false};
  case 0x05:
    return {"DEC B", 1, false, false, false, false, false};
  case 0x06:
    return {"LD B,n", 2, false, false, false, false, false};
  case 0x07:
    return {"RLCA", 1, false, false, false, false, false};
  case 0x08:
    return {"LD (nn),SP", 3, false, false, false, false, false};
  case 0x09:
    return {"ADD HL,BC", 1, false, false, false, false, false};
  case 0x0A:
    return {"LD A,(BC)", 1, false, false, false, false, false};
  case 0x0B:
    return {"DEC BC", 1, false, false, false, false, false};
  case 0x0C:
    return {"INC C", 1, false, false, false, false, false};
  case 0x0D:
    return {"DEC C", 1, false, false, false, false, false};
  case 0x0E:
    return {"LD C,n", 2, false, false, false, false, false};
  case 0x0F:
    return {"RRCA", 1, false, false, false, false, false};

  case 0x10:
    return {"STOP", 2, false, false, false, false, false};
  case 0x11:
    return {"LD DE,nn", 3, false, false, false, false, false};
  case 0x12:
    return {"LD (DE),A", 1, false, false, false, false, false};
  case 0x13:
    return {"INC DE", 1, false, false, false, false, false};
  case 0x14:
    return {"INC D", 1, false, false, false, false, false};
  case 0x15:
    return {"DEC D", 1, false, false, false, false, false};
  case 0x16:
    return {"LD D,n", 2, false, false, false, false, false};
  case 0x17:
    return {"RLA", 1, false, false, false, false, false};
  case 0x18:
    return {"JR n", 2, true, true, false, false, false};
  case 0x19:
    return {"ADD HL,DE", 1, false, false, false, false, false};
  case 0x1A:
    return {"LD A,(DE)", 1, false, false, false, false, false};
  case 0x1B:
    return {"DEC DE", 1, false, false, false, false, false};
  case 0x1C:
    return {"INC E", 1, false, false, false, false, false};
  case 0x1D:
    return {"DEC E", 1, false, false, false, false, false};
  case 0x1E:
    return {"LD E,n", 2, false, false, false, false, false};
  case 0x1F:
    return {"RRA", 1, false, false, false, false, false};

  case 0x20:
    return {"JR NZ,n", 2, true, false, false, false, false};
  case 0x21:
    return {"LD HL,nn", 3, false, false, false, false, false};
  case 0x22:
    return {"LD (HL+),A", 1, false, false, false, false, false};
  case 0x23:
    return {"INC HL", 1, false, false, false, false, false};
  case 0x24:
    return {"INC H", 1, false, false, false, false, false};
  case 0x25:
    return {"DEC H", 1, false, false, false, false, false};
  case 0x26:
    return {"LD H,n", 2, false, false, false, false, false};
  case 0x27:
    return {"DAA", 1, false, false, false, false, false};
  case 0x28:
    return {"JR Z,n", 2, true, false, false, false, false};
  case 0x29:
    return {"ADD HL,HL", 1, false, false, false, false, false};
  case 0x2A:
    return {"LD A,(HL+)", 1, false, false, false, false, false};
  case 0x2B:
    return {"DEC HL", 1, false, false, false, false, false};
  case 0x2C:
    return {"INC L", 1, false, false, false, false, false};
  case 0x2D:
    return {"DEC L", 1, false, false, false, false, false};
  case 0x2E:
    return {"LD L,n", 2, false, false, false, false, false};
  case 0x2F:
    return {"CPL", 1, false, false, false, false, false};

  case 0x30:
    return {"JR NC,n", 2, true, false, false, false, false};
  case 0x31:
    return {"LD SP,nn", 3, false, false, false, false, false};
  case 0x32:
    return {"LD (HL-),A", 1, false, false, false, false, false};
  case 0x33:
    return {"INC SP", 1, false, false, false, false, false};
  case 0x34:
    return {"INC (HL)", 1, false, false, false, false, false};
  case 0x35:
    return {"DEC (HL)", 1, false, false, false, false, false};
  case 0x36:
    return {"LD (HL),n", 2, false, false, false, false, false};
  case 0x37:
    return {"SCF", 1, false, false, false, false, false};
  case 0x38:
    return {"JR C,n", 2, true, false, false, false, false};
  case 0x39:
    return {"ADD HL,SP", 1, false, false, false, false, false};
  case 0x3A:
    return {"LD A,(HL-)", 1, false, false, false, false, false};
  case 0x3B:
    return {"DEC SP", 1, false, false, false, false, false};
  case 0x3C:
    return {"INC A", 1, false, false, false, false, false};
  case 0x3D:
    return {"DEC A", 1, false, false, false, false, false};
  case 0x3E:
    return {"LD A,n", 2, false, false, false, false, false};
  case 0x3F:
    return {"CCF", 1, false, false, false, false, false};

  case 0x76:
    return {"HALT", 1, false, false, false, false, false};

  case 0xC0:
    return {"RET NZ", 1, true, false, false, true, false};
  case 0xC1:
    return {"POP BC", 1, false, false, false, false, false};
  case 0xC2:
    return {"JP NZ,nn", 3, true, false, false, false, false};
  case 0xC3:
    return {"JP nn", 3, true, true, false, false, false};
  case 0xC4:
    return {"CALL NZ,nn", 3, true, false, true, false, false};
  case 0xC5:
    return {"PUSH BC", 1, false, false, false, false, false};
  case 0xC6:
    return {"ADD A,n", 2, false, false, false, false, false};
  case 0xC7:
    return {"RST 00", 1, true, true, true, false, false};
  case 0xC8:
    return {"RET Z", 1, true, false, false, true, false};
  case 0xC9:
    return {"RET", 1, true, true, false, true, false};
  case 0xCA:
    return {"JP Z,nn", 3, true, false, false, false, false};
  case 0xCB:
    return {"CB prefix", 2, false, false, false, false, false};
  case 0xCC:
    return {"CALL Z,nn", 3, true, false, true, false, false};
  case 0xCD:
    return {"CALL nn", 3, true, true, true, false, false};
  case 0xCE:
    return {"ADC A,n", 2, false, false, false, false, false};
  case 0xCF:
    return {"RST 08", 1, true, true, true, false, false};

  case 0xD0:
    return {"RET NC", 1, true, false, false, true, false};
  case 0xD1:
    return {"POP DE", 1, false, false, false, false, false};
  case 0xD2:
    return {"JP NC,nn", 3, true, false, false, false, false};
  case 0xD4:
    return {"CALL NC,nn", 3, true, false, true, false, false};
  case 0xD5:
    return {"PUSH DE", 1, false, false, false, false, false};
  case 0xD6:
    return {"SUB n", 2, false, false, false, false, false};
  case 0xD7:
    return {"RST 10", 1, true, true, true, false, false};
  case 0xD8:
    return {"RET C", 1, true, false, false, true, false};
  case 0xD9:
    return {"RETI", 1, true, true, false, true, false};
  case 0xDA:
    return {"JP C,nn", 3, true, false, false, false, false};
  case 0xDC:
    return {"CALL C,nn", 3, true, false, true, false, false};
  case 0xDE:
    return {"SBC A,n", 2, false, false, false, false, false};
  case 0xDF:
    return {"RST 18", 1, true, true, true, false, false};

  case 0xE0:
    return {"LD (FF00+n),A", 2, false, false, false, false, false};
  case 0xE1:
    return {"POP HL", 1, false, false, false, false, false};
  case 0xE2:
    return {"LD (FF00+C),A", 1, false, false, false, false, false};
  case 0xE5:
    return {"PUSH HL", 1, false, false, false, false, false};
  case 0xE6:
    return {"AND n", 2, false, false, false, false, false};
  case 0xE7:
    return {"RST 20", 1, true, true, true, false, false};
  case 0xE8:
    return {"ADD SP,n", 2, false, false, false, false, false};
  case 0xE9:
    return {"JP HL", 1, true, true, false, false, true};
  case 0xEA:
    return {"LD (nn),A", 3, false, false, false, false, false};
  case 0xEE:
    return {"XOR n", 2, false, false, false, false, false};
  case 0xEF:
    return {"RST 28", 1, true, true, true, false, false};

  case 0xF0:
    return {"LD A,(FF00+n)", 2, false, false, false, false, false};
  case 0xF1:
    return {"POP AF", 1, false, false, false, false, false};
  case 0xF2:
    return {"LD A,(FF00+C)", 1, false, false, false, false, false};
  case 0xF3:
    return {"DI", 1, false, false, false, false, false};
  case 0xF5:
    return {"PUSH AF", 1, false, false, false, false, false};
  case 0xF6:
    return {"OR n", 2, false, false, false, false, false};
  case 0xF7:
    return {"RST 30", 1, true, true, true, false, false};
  case 0xF8:
    return {"LD HL,SP+n", 2, false, false, false, false, false};
  case 0xF9:
    return {"LD SP,HL", 1, false, false, false, false, false};
  case 0xFA:
    return {"LD A,(nn)", 3, false, false, false, false, false};
  case 0xFB:
    return {"EI", 1, false, false, false, false, false};
  case 0xFE:
    return {"CP n", 2, false, false, false, false, false};
  case 0xFF:
    return {"RST 38", 1, true, true, true, false, false};

  default: {
    // 0x40-0x75, 0x77-0x7F: LD r,r
    if (opcode >= 0x40 && opcode <= 0x7F) {
      return {"LD r,r", 1, false, false, false, false, false};
    }
    // 0x80-0xBF: ALU operations
    if (opcode >= 0x80 && opcode <= 0xBF) {
      static const char *ALU_NAMES[] = {"ADD A,r", "ADC A,r", "SUB r",
                                        "SBC A,r", "AND r",   "XOR r",
                                        "OR r",    "CP r"};
      return {
          ALU_NAMES[(opcode >> 3) & 7], 1, false, false, false, false, false};
    }
    return {"???", 1, false, false, false, false, false};
  }
  }
}

GBRecompiler::OpcodeInfo GBRecompiler::decodeCBOpcode(uint8_t opcode) {
  static const char *CB_NAMES[] = {"RLC", "RRC", "RL",   "RR",
                                   "SLA", "SRA", "SWAP", "SRL"};
  uint8_t op = opcode >> 3;
  if (op < 8) {
    return {CB_NAMES[op], 2, false, false, false, false, false};
  }
  if (op < 16) {
    return {"BIT", 2, false, false, false, false, false};
  }
  if (op < 24) {
    return {"RES", 2, false, false, false, false, false};
  }
  return {"SET", 2, false, false, false, false, false};
}

DisasmInstruction GBRecompiler::disassembleOne(const uint8_t *data,
                                               uint16_t pc) {
  DisasmInstruction insn;
  insn.pc = pc;
  insn.opcode = data[0];

  if (data[0] == 0xCB) {
    auto info = decodeCBOpcode(data[1]);
    insn.length = 2;
    insn.mnemonic = info.mnemonic;
    insn.operand_lo = data[1];
    return insn;
  }

  uint8_t next = (insn.opcode == 0xCB || data[0] >= 0xFE) ? 0 : data[1];
  auto info = decodeOpcode(data[0], next);
  insn.length = info.length;
  insn.mnemonic = info.mnemonic;
  insn.is_branch = info.is_branch;
  insn.is_unconditional_jump = info.is_unconditional;
  insn.is_call = info.is_call;
  insn.is_return = info.is_return;
  insn.is_indirect = info.is_indirect;

  if (info.length >= 2)
    insn.operand_lo = data[1];
  if (info.length >= 3)
    insn.operand_hi = data[2];

  // Compute branch target
  if (info.is_branch && !info.is_return && !info.is_indirect) {
    if (info.length == 2) {
      // Relative jump
      insn.branch_target = pc + 2 + static_cast<int8_t>(data[1]);
    } else if (info.length == 3) {
      insn.branch_target = static_cast<uint16_t>(data[1]) |
                           (static_cast<uint16_t>(data[2]) << 8);
    } else if (info.length == 1 && data[0] >= 0xC7) {
      // RST
      insn.branch_target = data[0] & 0x38;
    }
  }

  return insn;
}

void GBRecompiler::lowerBlockToIR(IRBlock &block,
                                  const std::vector<DisasmInstruction> &insns) {
  // translate disassembled instructions to IR operations
  for (auto &insn : insns) {
    IROp op;
    op.opcode = IROpcodeType::Nop;
    op.guest_pc = insn.pc;
    block.ops.push_back(op);
  }
}

void GBRecompiler::emitBlockLLVMIR(
    const IRBlock &block, llvm::LLVMContext &ctx, llvm::Module &mod,
    llvm::Function *func, llvm::BasicBlock *bb,
    const std::unordered_map<uint16_t, llvm::BasicBlock *> &pc_to_bb) {
  // emit LLVM IR from IR block
}

} // namespace nativecore
