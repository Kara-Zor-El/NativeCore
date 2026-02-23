#pragma once

#include <chrono>

namespace nativecore {

class FrameLimiter {
public:
  void setTargetFPS(double fps);
  double targetFPS() const { return target_fps_; }

  void setUncapped(bool uncapped);
  bool isUncapped() const { return uncapped_; }

  void beginFrame();
  void endFrame();

  double currentFPS() const { return current_fps_; }
  double frameTimeMS() const { return frame_time_ms_; }

private:
  using Clock = std::chrono::high_resolution_clock;
  using TimePoint = Clock::time_point;
  using Duration = std::chrono::duration<double>;

  double target_fps_ = 60.0;
  bool uncapped_ = false;

  TimePoint frame_start_;
  TimePoint last_fps_update_;
  int frame_count_ = 0;
  double current_fps_ = 0.0;
  double frame_time_ms_ = 0.0;

  Duration targetFrameTime() const { return Duration(1.0 / target_fps_); }
};
} // namespace nativecore
