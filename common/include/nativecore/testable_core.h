#pragma once

#include <cstdint>
#include <string>

namespace nativecore {

// Exposes a debug output channel and register inspection.
class TestableCore {
public:
  virtual ~TestableCore() = default;

  // Text output the running ROM writes to a debug channel (e.g. serial port).
  virtual const std::string &debugOutput() const = 0;

  // Read a CPU register by ID.
  virtual uint8_t peekRegister(int id) const = 0;
};

} // namespace nativecore
