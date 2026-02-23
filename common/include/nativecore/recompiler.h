#pragma once
#include <cstdint>
#include <set>
#include <unordered_map>
#include <vector>

#include "nativecore/ir.h"

namespace llvm {
class BasicBlock;
class LLVMContext;
class Module;
class Function;
} // namespace llvm

namespace nativecore {

struct DisasmInstruction {
  uint16_t pc = 0;
  uint8_t opcode = 0;
  uint8_t operand_lo = 0;
  uint8_t operand_hi = 0;
  uint8_t length = 1;
  std::string mnemonic;
  bool is_branch = false;
  bool is_unconditional_jump = false;
  bool is_call = false;
  bool is_return = false;
  bool is_indirect = false;
  uint16_t branch_target = 0;
};

struct CFGEdge {
  uint32_t from_block;
  uint32_t to_block;
  bool is_fallthrough = false;
};

// Control Flow Graph
struct CFG {
  std::vector<IRBlock> blocks;
  std::vector<CFGEdge> edges;
  std::unordered_map<uint16_t, uint32_t> pc_to_block;

  std::set<uint16_t> entry_points;
  std::set<uint16_t> unresolved_targets;
};

class Recompiler {
public:
  virtual ~Recompiler() = default;

  virtual DisasmInstruction disassembleOne(const uint8_t *data,
                                           uint16_t pc) = 0;

  std::vector<DisasmInstruction>
  disassembleRange(const uint8_t *data, size_t size, uint16_t base_pc);

  CFG buildCFG(const uint8_t *data, size_t size, uint16_t base_pc,
               const std::vector<uint16_t> &entry_points);

  IRModule lowerToIR(const CFG &cfg, const uint8_t *data, size_t size,
                     uint16_t base_pc);

  void emitLLVMIR(const IRModule &mod, llvm::LLVMContext &ctx,
                  llvm::Module &llvm_mod);

  bool compileToObject(llvm::Module &mod, const std::string &output_path,
                       const std::string &target_triple = "");

protected:
  virtual void lowerBlockToIR(IRBlock &block,
                              const std::vector<DisasmInstruction> &insns) = 0;

  virtual void emitBlockLLVMIR(
      const IRBlock &block, llvm::LLVMContext &ctx, llvm::Module &mod,
      llvm::Function *func, llvm::BasicBlock *bb,
      const std::unordered_map<uint16_t, llvm::BasicBlock *> &pc_to_bb) = 0;

private:
  void buildCFGRecursive(CFG &cfg, const uint8_t *data, size_t size,
                         uint16_t base_pc, uint16_t current_pc,
                         std::set<uint16_t> &visited);

  uint32_t getOrCreateBlock(CFG &cfg, uint16_t pc);
};

} // namespace nativecore
