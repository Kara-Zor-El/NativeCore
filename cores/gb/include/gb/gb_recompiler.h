#pragma once

#include "nativecore/recompiler.h"

namespace nativecore {

class GBRecompiler : public Recompiler {
public:
  DisasmInstruction disassembleOne(const uint8_t *data, uint16_t pc) override;

protected:
  void lowerBlockToIR(IRBlock &block,
                      const std::vector<DisasmInstruction> &insns) override;

  void emitBlockLLVMIR(const IRBlock &block, llvm::LLVMContext &ctx,
                       llvm::Module &mod, llvm::Function *func,
                       llvm::BasicBlock *bb,
                       const std::unordered_map<uint16_t, llvm::BasicBlock *>
                           &pc_to_bb) override;

private:
  struct OpcodeInfo {
    const char *mnemonic;
    uint8_t length;
    bool is_branch;
    bool is_unconditional;
    bool is_call;
    bool is_return;
    bool is_indirect;
  };

  static OpcodeInfo decodeOpcode(uint8_t opcode, uint8_t next_byte);
  static OpcodeInfo decodeCBOpcode(uint8_t opcode);
};

} // namespace nativecore
