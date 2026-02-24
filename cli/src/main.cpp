#include "gb/gb_core.h"
#include "nativecore/core.h"
#include "nativecore/recompiler.h"
#include "runtime/app.h"

#include <llvm/IR/LLVMContext.h>
#include <llvm/IR/LegacyPassManager.h>
#include <llvm/IR/Module.h>
#include <llvm/MC/TargetRegistry.h>
#include <llvm/Support/FileSystem.h>
#include <llvm/Support/TargetSelect.h>
#include <llvm/Support/raw_ostream.h>
#include <llvm/Target/TargetMachine.h>
#include <llvm/Target/TargetOptions.h>
#include <llvm/TargetParser/Host.h>
#include <llvm/TargetParser/Triple.h>

#include <cstring>
#include <filesystem>
#include <fstream>
#include <iomanip>
#include <iostream>
#include <optional>
#include <string>
#include <vector>

namespace fs = std::filesystem;

static std::vector<uint8_t> readFile(const std::string &path) {
  std::ifstream file(path, std::ios::binary);
  if (!file.is_open())
    return {};
  return {std::istreambuf_iterator<char>(file),
          std::istreambuf_iterator<char>()};
}

static void printUsage(const char *program) {
  std::cerr << "Usage: " << program << " <rom_file> [options]\n"
            << "\nOptions:\n"
            << "  -o <dir>        Output directory for the recompiled app\n"
            << "  --run           Run the ROM directly (interpreter mode)\n"
            << "  --emit-ir       Emit LLVM IR to stdout\n"
            << "  -h, --help      Show this help message and exit\n";
}

int main(int argc, char *argv[]) {
  if (argc < 2) {
    printUsage(argv[0]);
    return 1;
  }

  std::string rom_path;
  std::string output_dir;
  bool run_mode = false;
  bool emit_ir = false;

  for (int i = 1; i < argc; i++) {
    if (std::strcmp(argv[i], "-o") == 0 && i + 1 < argc) {
      output_dir = argv[++i];
    } else if (std::strcmp(argv[i], "--run") == 0) {
      run_mode = true;
    } else if (std::strcmp(argv[i], "--emit-ir") == 0) {
      emit_ir = true;
    } else if (std::strcmp(argv[i], "-h") == 0 ||
               std::strcmp(argv[i], "--help") == 0) {
      printUsage(argv[0]);
      return 0;
    } else {
      rom_path = argv[i];
    }
  }

  if (rom_path.empty()) {
    std::cerr << "Error: no ROM path specified\n";
    return 1;
  }

  auto rom_data = readFile(rom_path);
  if (rom_data.empty()) {
    std::cerr << "Error: failed to read ROM: " << rom_path << "\n";
    return 1;
  }

  auto system = nativecore::detectSystem(rom_data);
  if (system == nativecore::SystemType::Unknown) {
    std::cerr << "Error: unrecognized/unsupported ROM format\n";
    return 1;
  }

  const char *system_name = nullptr;
  switch (system) {
  case nativecore::SystemType::GameBoy:
    system_name = "Game Boy";
    break;
  default:
    system_name = "Unknown";
    break;
  }
  std::cout << "Detected system: " << system_name << "\n";

  auto core = nativecore::createCore(system);
  if (!core->loadROM(rom_data)) {
    std::cerr << "Error: failed to load ROM\n";
    return 1;
  }

  if (run_mode) {
    nativecore::runtime::App app;
    auto error = app.init(std::move(core));
    if (!error.empty()) {
      std::cerr << "Error: " << error << "\n";
      return 1;
    }
    app.run();
    return 0;
  }

  if (emit_ir) {
    llvm::InitializeAllTargetInfos();
    llvm::InitializeAllTargets();
    llvm::InitializeAllTargetMCs();
    llvm::InitializeAllAsmParsers();
    llvm::InitializeAllAsmPrinters();

    llvm::LLVMContext context;
    auto llvm_mod = std::make_unique<llvm::Module>("recompiled", context);
    core->emitLLVMIR(context, *llvm_mod);
    llvm_mod->print(llvm::outs(), nullptr);
    return 0;
  }

  // By default recompile to native object
  if (output_dir.empty()) {
    output_dir = fs::path(rom_path).stem().string();
  }

  std::cout << "Recompiling...\n";

  llvm::InitializeAllTargetInfos();
  llvm::InitializeAllTargets();
  llvm::InitializeAllTargetMCs();
  llvm::InitializeAllAsmParsers();
  llvm::InitializeAllAsmPrinters();

  llvm::LLVMContext context;
  auto llvm_mod = std::make_unique<llvm::Module>("recompiled", context);
  core->emitLLVMIR(context, *llvm_mod);

  fs::create_directories(output_dir);
  std::string object_path = output_dir + "/game.o";

  // Use the base recompiler to compile to object
  auto ir_mod = core->recompile();

  auto *gameboy_core = dynamic_cast<nativecore::GBCore *>(core.get());
  (void)gameboy_core;

  // Compile LLVM module to object file
  auto mod_ptr = llvm_mod.get();
  llvm::Triple triple(llvm::sys::getDefaultTargetTriple());
  mod_ptr->setTargetTriple(triple);

  std::string error;
  auto *target = llvm::TargetRegistry::lookupTarget(triple.str(), error);
  if (!target) {
    std::cerr << "Error: " << error << "\n";
    return 1;
  }

  auto *tm =
      target->createTargetMachine(triple, "generic", "", llvm::TargetOptions{},
                                  std::optional(llvm::Reloc::PIC_));
  mod_ptr->setDataLayout(tm->createDataLayout());

  std::error_code error_code;
  llvm::raw_fd_ostream dest(object_path, error_code, llvm::sys::fs::OF_None);
  if (error_code) {
    std::cerr << "Error: " << error_code.message() << "\n";
    return 1;
  }

  llvm::legacy::PassManager codegen_pm;
  if (tm->addPassesToEmitFile(codegen_pm, dest, nullptr,
                              llvm::CodeGenFileType::ObjectFile)) {
    std::cerr << "Error: failed to add passes to emit object file\n";
    return 1;
  }
  codegen_pm.run(*mod_ptr);
  dest.flush();

  std::cout << "Object file written to: " << object_path << "\n";

  std::string game_title = fs::path(rom_path).stem().string();

  // TODO: Generate output application / project
  return 0;
}
