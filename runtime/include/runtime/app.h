#pragma once

#include "nativecore/audio.h"
#include "nativecore/config.h"
#include "nativecore/core.h"
#include "nativecore/frame_limiter.h"
#include "nativecore/input.h"
#include "nativecore/settings_overlay.h"
#include "nativecore/video.h"

#include <memory>
#include <string>

namespace nativecore::runtime {

class App {
public:
  App();
  ~App();

  bool init(std::unique_ptr<Core> core, const std::string &config_path = "");
  void run();
  void shutdown();

private:
  std::unique_ptr<Core> core_;

  AudioManager audio_;
  InputManager input_;
  VideoManager video_;
  FrameLimiter limiter_;
  ConfigManager config_;
  SettingsOverlay overlay_;

  bool running_ = false;
  std::string config_path_;

  void applyConfig();
  void handleEvents();
  void update();
  void render();
};

} // namespace nativecore::runtime