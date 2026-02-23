#pragma once

#include <cstdint>
#include <string>
#include <unordered_map>
#include <vector>

namespace nativecore {

enum class IROpcodeType {
  // Arithmetic
  Add,
  Sub,
  Mul,
  And,
  Or,
  Xor,
  Not,
  ShiftLeft,
  ShiftRight,
  RotateLeft,
  RotateRight,

  // Comparison
  Compare,
  Test,

  // Data movement
  Load8,
  Load16,
  Store8,
  Store16,
  Move,
  LoadImm,
  ZeroExtend,
  SignExtend,

  // Memory bus
  MemRead8,
  MemRead16,
  MemWrite8,
  MemWrite16,

  // Control flow
  Jump,
  JumpIf,
  Call,
  Return,
  JumpIndirect,

  // Flag manipulation
  SetFlag,
  GetFlag,
  UpdateFlags,

  // Stack
  Push8,
  Push16,
  Pop8,
  Pop16,

  // Special
  Nop,
  Break,
  Halt,
  InterruptCheck,
  SyscallHWTick,
};

enum class IRValueType {
  Void,
  U8,
  U16,
  U32,
  Bool,
};

struct IRValue {
  uint32_t id = 0;
  IRValueType type = IRValueType::Void;
  bool is_const = false;
  uint32_t const_val = 0;

  static IRValue constant(IRValueType type, uint32_t val) {
    return {0, type, true, val};
  }
};

struct IROp {
  IROpcodeType opcode;
  IRValue dest;
  IRValue src1;
  IRValue src2;
  uint32_t immediate = 0;

  uint16_t guest_pc = 0;
};

struct IRBlock {
  uint32_t id = 0;
  uint16_t guest_start_pc = 0;
  uint16_t guest_end_pc = 0;

  std::vector<IROp> ops;

  std::vector<uint32_t> successors;
  std::vector<uint32_t> predecessors;

  bool is_entry = false;
  bool is_resolved = true;
};

struct IRFunction {
  std::string name;
  uint16_t entry_pc = 0;
  std::vector<IRBlock> blocks;
  uint32_t next_value_id = 1;
  uint32_t next_block_id = 0;

  IRValue newValue(IRValueType type) {
    return {next_value_id++, type, false, 0};
  }

  uint32_t newBlockId() { return next_block_id++; }
};

struct IRModule {
  std::string name;
  std::vector<IRFunction> functions;

  std::unordered_map<uint16_t, uint32_t> pc_to_function;
  std::unordered_map<uint16_t, uint32_t> pc_to_block;
};

} // namespace nativecore
