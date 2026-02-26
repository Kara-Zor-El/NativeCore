#pragma once

#include "nativecore/audio.h"
#include "nativecore/config.h"
#include "nativecore/frame_limiter.h"
#include "nativecore/input.h"
#include "nativecore/video.h"

#include <SDL3/SDL.h>
#include <string>

namespace nativecore {
class Core;

class SettingsOverlay {
public:
  SettingsOverlay();
  ~SettingsOverlay();

  bool init(VideoManager &video);
  void shutdown();

  void toggle();
  bool isOpen() const { return open_; }

  void processEvent(const SDL_Event &event);

  void render(AudioManager &audio, InputManager &input, VideoManager &video,
              FrameLimiter &limiter, ConfigManager &config,
              Core *core = nullptr);

private:
  bool open_ = false;
  bool initialized_ = false;
  float current_scale_ = 1.0f;
  std::string core_name_;

  int active_tab_ = 0;

  void applyThemeForCore(const std::string &core_name);
  void renderAudioPanel(AudioManager &audio, ConfigManager &config);
  void renderInputPanel(InputManager &input, ConfigManager &config);
  void renderVideoPanel(VideoManager &video, ConfigManager &config);
  void renderPerformancePanel(FrameLimiter &limiter, ConfigManager &config,
                              VideoManager &video);
};

} // namespace nativecore
