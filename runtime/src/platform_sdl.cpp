#include "runtime/platform.h"

#include <SDL3/SDL.h>
#include <cstdlib>

namespace nativecore::runtime {

PlatformInfo getPlatformInfo() {
  PlatformInfo info;
#if (defined(__aarch64__) || defined(_M_ARM64))
  info.arch = "arm64";
#elif (defined(__x86_64__) || defined(_M_X64))
  info.arch = "x86_64";
#elif (defined(__i386__) || defined(_M_IX86))
  info.arch = "x86";
#else
  info.arch = "unknown";
#endif

#if defined(__APPLE__)
  info.os_name = "macos";
  info.is_macos = true;
#elif (defined(__linux__))
  info.os_name = "linux";
  info.is_linux = true;
#elif (defined(_WIN32))
  info.os_name = "windows";
  info.is_windows = true;
#else
  info.os_name = "unknown";
#endif

  return info;
}

std::string getDataDirectory() {
  char *pref_path = SDL_GetPrefPath("NativeCore", "NativeCore");
  if (!pref_path)
    return ".";
  std::string result(pref_path);
  SDL_free(pref_path);
  return result;
}

std::string getConfigDirectory(const std::string &app_name) {
  char *pref_path = SDL_GetPrefPath("NativeCore", app_name.c_str());
  if (!pref_path)
    return ".";
  std::string result(pref_path);
  SDL_free(pref_path);
  return result;
}

} // namespace nativecore::runtime