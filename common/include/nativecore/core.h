#pragma once

#include <cstdint>
#include <memory>
#include <string>
#include <vector>

namespace llvm {
class Module;
class LLVMContext;
} // namespace llvm

namespace nativecore {

struct IRModule;
class AudioManager;
class InputManager;
class VideoManager;
class CameraProvider;

struct SystemInfo {
  std::string name;
  double native_fps = 60.0;
  int screen_width = 256;
  int screen_height = 240;
  int audio_sample_rate = 44100; // 44.1kHz
};

class Core {
public:
  virtual ~Core() = default;

  virtual const SystemInfo &systemInfo() const = 0;

  virtual bool loadROM(const std::vector<uint8_t> &data) = 0;

  virtual IRModule recompile() = 0;

  virtual void emitLLVMIR(llvm::LLVMContext &context, llvm::Module &module) = 0;

  virtual void tick() = 0;

  virtual void reset() = 0;

  virtual uint8_t readMemory(uint16_t address) const = 0;
  virtual void writeMemory(uint16_t address, uint8_t value) = 0;

  virtual const uint32_t *getFramebuffer() const = 0;

  virtual const float *getAudioBuffer(size_t &samples_out) const = 0;
  virtual void clearAudioBuffer() = 0;
  virtual void consumeAudioSamples(size_t samples) = 0;

  virtual void setInputState(int controller, uint8_t buttons) = 0;

  // Optional camera support (default: no camera)
  virtual bool supportsCamera() const { return false; }
  virtual void setCameraProvider(CameraProvider *provider) { (void)provider; }

  virtual bool saveState(std::vector<uint8_t> &out) const {
    (void)out;
    return false;
  }
  virtual bool loadState(const uint8_t *data, size_t size) {
    (void)data;
    (void)size;
    return false;
  }
};

enum class SystemType {
  GameBoy,
  Unknown,
};

SystemType detectSystem(const std::vector<uint8_t> &rom_data);

std::unique_ptr<Core> createCore(SystemType system);
} // namespace nativecore
