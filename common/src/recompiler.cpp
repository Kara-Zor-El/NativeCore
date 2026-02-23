#include "nativecore/recompiler.h"

#include <llvm/CodeGen/MachineModuleInfo.h>
#include <llvm/IR/BasicBlock.h>
#include <llvm/IR/DerivedTypes.h>
#include <llvm/IR/Function.h>
#include <llvm/IR/IRBuilder.h>
#include <llvm/IR/LLVMContext.h>
#include <llvm/IR/LegacyPassManager.h>
#include <llvm/IR/Module.h>
#include <llvm/IR/Type.h>
#include <llvm/IR/Verifier.h>
#include <llvm/MC/TargetRegistry.h>
#include <llvm/Passes/PassBuilder.h>
#include <llvm/Support/FileSystem.h>
#include <llvm/Support/TargetSelect.h>
#include <llvm/Support/raw_ostream.h>
#include <llvm/Target/TargetMachine.h>
#include <llvm/Target/TargetOptions.h>
#include <llvm/TargetParser/Host.h>
#include <llvm/TargetParser/Triple.h>

#include <algorithm>
#include <queue>

namespace nativecore {

std::vector<DisasmInstruction> Recompiler::disassembleRange(const uint8_t *data,
                                                            size_t size,
                                                            uint16_t base_pc) {
  std::vector<DisasmInstruction> result;
  uint16_t pc = base_pc;
  while (pc < base_pc + size) {
    size_t offset = pc - base_pc;
    if (offset >= size)
      break;
    auto instruction = disassembleOne(data + offset, pc);
    pc += instruction.length;
    result.push_back(std::move(instruction));
  }
  return result;
}

uint32_t Recompiler::getOrCreateBlock(CFG &cfg, uint16_t pc) {
  auto it = cfg.pc_to_block.find(pc);
  if (it != cfg.pc_to_block.end()) {
    return it->second;
  }
  uint32_t block_id = cfg.blocks.size();
  IRBlock block;
  block.id = block_id;
  block.guest_start_pc = pc;
  block.guest_end_pc = pc;
  cfg.blocks.push_back(block);
  cfg.pc_to_block[pc] = block_id;
  return block_id;
}

void Recompiler::buildCFGRecursive(CFG &cfg, const uint8_t *data, size_t size,
                                   uint16_t base_pc, uint16_t current_pc,
                                   std::set<uint16_t> &visited) {
  if (visited.count(current_pc))
    return;

  size_t offset = current_pc - base_pc;
  if (offset >= size) {
    cfg.unresolved_targets.insert(current_pc);
    return;
  }

  visited.insert(current_pc);
  uint32_t block_id = getOrCreateBlock(cfg, current_pc);

  uint16_t pc = current_pc;
  while (true) {
    offset = pc - base_pc;
    if (offset >= size)
      break;

    auto instruction = disassembleOne(data + offset, pc);
    cfg.blocks[block_id].guest_end_pc = pc + instruction.length;

    if (instruction.is_return)
      break;

    if (instruction.is_unconditional_jump) {
      if (instruction.is_indirect) {
        cfg.blocks[block_id].is_resolved = false;
        cfg.unresolved_targets.insert(pc);
      } else {
        uint32_t target_id = getOrCreateBlock(cfg, instruction.branch_target);
        cfg.blocks[block_id].successors.push_back(target_id);
        cfg.edges.push_back({block_id, target_id, false});
        buildCFGRecursive(cfg, data, size, base_pc, instruction.branch_target,
                          visited);
      }
      break;
    }

    if (instruction.is_branch) {
      uint16_t fallthrough_pc = pc + instruction.length;
      uint32_t target_id = getOrCreateBlock(cfg, instruction.branch_target);
      uint32_t fallthrough_id = getOrCreateBlock(cfg, fallthrough_pc);

      cfg.blocks[block_id].successors.push_back(target_id);
      cfg.blocks[block_id].successors.push_back(fallthrough_id);
      cfg.blocks[target_id].predecessors.push_back(block_id);
      cfg.blocks[fallthrough_id].predecessors.push_back(block_id);
      cfg.edges.push_back({block_id, target_id, false});
      cfg.edges.push_back({block_id, fallthrough_id, true});

      buildCFGRecursive(cfg, data, size, base_pc, instruction.branch_target,
                        visited);
      buildCFGRecursive(cfg, data, size, base_pc, fallthrough_pc, visited);
      break;
    }

    if (instruction.is_call && !instruction.is_indirect) {
      uint32_t target_id = getOrCreateBlock(cfg, instruction.branch_target);
      cfg.edges.push_back({block_id, target_id, false});
      buildCFGRecursive(cfg, data, size, base_pc, instruction.branch_target,
                        visited);
      break;
    }

    pc += instruction.length;

    if (cfg.pc_to_block.count(pc) && cfg.pc_to_block[pc] != block_id) {
      uint32_t next_id = cfg.pc_to_block[pc];
      cfg.blocks[block_id].successors.push_back(next_id);
      cfg.blocks[next_id].predecessors.push_back(block_id);
      cfg.edges.push_back({block_id, next_id, false});
      break;
    }
  }
}

CFG Recompiler::buildCFG(const uint8_t *data, size_t size, uint16_t base_pc,
                         const std::vector<uint16_t> &entry_points) {
  CFG cfg;
  std::set<uint16_t> visited;

  for (auto entry_point : entry_points) {
    cfg.entry_points.insert(entry_point);
    uint32_t block_id = getOrCreateBlock(cfg, entry_point);
    cfg.blocks[block_id].is_entry = true;
    buildCFGRecursive(cfg, data, size, base_pc, entry_point, visited);
  }
  return cfg;
}

IRModule Recompiler::lowerToIR(const CFG &cfg, const uint8_t *data, size_t size,
                               uint16_t base_pc) {
  IRModule mod;
  mod.name = "recompiled";

  for (auto entry_point : cfg.entry_points) {
    IRFunction function;
    function.name = "sub_" + std::to_string(entry_point);
    function.entry_pc = entry_point;

    std::queue<uint32_t> work_queue;
    std::set<uint32_t> visited;

    auto entry_point_it = cfg.pc_to_block.find(entry_point);
    if (entry_point_it == cfg.pc_to_block.end())
      continue;
    work_queue.push(entry_point_it->second);

    while (!work_queue.empty()) {
      uint32_t block = work_queue.front();
      work_queue.pop();
      if (visited.count(block))
        continue;
      visited.insert(block);

      IRBlock ir_block = cfg.blocks[block];

      uint16_t start = ir_block.guest_start_pc;
      uint16_t end = ir_block.guest_end_pc;
      size_t offset = start - base_pc;
      size_t block_size = end - start;
      if (offset + block_size <= size) {
        auto instructions = disassembleRange(data + offset, block_size, start);
        lowerBlockToIR(ir_block, instructions);
      }

      function.blocks.push_back(ir_block);
      mod.pc_to_block[start] = ir_block.id;

      for (auto successor : ir_block.successors) {
        work_queue.push(successor);
      }
    }

    mod.pc_to_function[entry_point] = mod.functions.size();
    mod.functions.push_back(std::move(function));
  }

  return mod;
}

void Recompiler::emitLLVMIR(const IRModule &mod, llvm::LLVMContext &context,
                            llvm::Module &llvm_mod) {
  auto *i8_ty = llvm::Type::getInt8Ty(context);
  auto *i16_ty = llvm::Type::getInt16Ty(context);
  auto *i32_ty = llvm::Type::getInt32Ty(context);
  auto *void_ty = llvm::Type::getVoidTy(context);

  // External memory access functions
  auto *mem_read_ty = llvm::FunctionType::get(i8_ty, {i16_ty}, false);
  auto mem_read_fn =
      llvm_mod.getOrInsertFunction("nativecore_mem_read8", mem_read_ty);

  auto *mem_write_ty = llvm::FunctionType::get(void_ty, {i16_ty, i8_ty}, false);
  auto mem_write_fn =
      llvm_mod.getOrInsertFunction("nativecore_mem_write8", mem_write_ty);

  auto *hw_tick_ty = llvm::FunctionType::get(void_ty, {}, false);
  auto hw_tick_fn =
      llvm_mod.getOrInsertFunction("nativecore_hw_tick", hw_tick_ty);

  (void)mem_read_fn;
  (void)mem_write_fn;
  (void)hw_tick_fn;

  for (const auto &function : mod.functions) {
    auto *func_ty = llvm::FunctionType::get(void_ty, {}, false);
    auto *llvm_func = llvm::Function::Create(
        func_ty, llvm::Function::ExternalLinkage, function.name, &llvm_mod);

    std::unordered_map<uint16_t, llvm::BasicBlock *> pc_to_basic_block;
    for (const auto &block : function.blocks) {
      auto *basic_block = llvm::BasicBlock::Create(
          context, "block_" + std::to_string(block.guest_start_pc), llvm_func);
      pc_to_basic_block[block.guest_start_pc] = basic_block;
    }

    for (const auto &block : function.blocks) {
      auto it = pc_to_basic_block.find(block.guest_start_pc);
      if (it != pc_to_basic_block.end()) {
        emitBlockLLVMIR(block, context, llvm_mod, llvm_func, it->second,
                        pc_to_basic_block);
      }
    }
  }
}

bool Recompiler::compileToObject(llvm::Module &mod,
                                 const std::string &output_path,
                                 const std::string &target_triple) {
  llvm::InitializeAllTargetInfos();
  llvm::InitializeAllTargets();
  llvm::InitializeAllTargetMCs();
  llvm::InitializeAllAsmParsers();
  llvm::InitializeAllAsmPrinters();

  llvm::Triple triple(target_triple.empty()
                          ? llvm::sys::getDefaultTargetTriple()
                          : target_triple);
  mod.setTargetTriple(triple);

  std::string error;
  auto target = llvm::TargetRegistry::lookupTarget(triple.str(), error);
  if (!target)
    return false;

  auto cpu = llvm::sys::getHostCPUName();
  auto features = "";
  llvm::TargetOptions options;

  auto tm = std::unique_ptr<llvm::TargetMachine>(target->createTargetMachine(
      triple, cpu, features, options, llvm::Reloc::PIC_));
  if (!tm)
    return false;

  mod.setDataLayout(tm->createDataLayout());

  std::error_code error_code;
  llvm::raw_fd_ostream dest(output_path, error_code, llvm::sys::fs::OF_None);
  if (error_code)
    return false;

  // Run optimization passes
  llvm::LoopAnalysisManager lam;
  llvm::FunctionAnalysisManager fam;
  llvm::CGSCCAnalysisManager cgam;
  llvm::ModuleAnalysisManager mam;
  llvm::PassBuilder pb(tm.get());

  pb.registerModuleAnalyses(mam);
  pb.registerCGSCCAnalyses(cgam);
  pb.registerFunctionAnalyses(fam);
  pb.registerLoopAnalyses(lam);
  pb.crossRegisterProxies(lam, fam, cgam, mam);

  llvm::ModulePassManager mpm =
      pb.buildPerModuleDefaultPipeline(llvm::OptimizationLevel::O2);
  mpm.run(mod, mam);

  llvm::legacy::PassManager codegen_pm;
  if (tm->addPassesToEmitFile(codegen_pm, dest, nullptr,
                              llvm::CodeGenFileType::ObjectFile)) {
    return false;
  }
  codegen_pm.run(mod);
  dest.flush();
  return true;
}

} // namespace nativecore