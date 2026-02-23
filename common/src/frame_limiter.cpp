#include "nativecore/frame_limiter.h"

#include <thread>

namespace nativecore {

void FrameLimiter::setTargetFPS(double fps) {
  if (fps > 0.0)
    target_fps_ = fps;
}

void FrameLimiter::setUncapped(bool uncapped) { uncapped_ = uncapped; }

void FrameLimiter::beginFrame() { frame_start_ = Clock::now(); }

void FrameLimiter::endFrame() {
  auto now = Clock::now();
  Duration elapsed = now - frame_start_;
  frame_time_ms_ = elapsed.count() * 1000.0;

  if (!uncapped_) {
    Duration target = targetFrameTime();
    Duration remaining = target - elapsed;

    // Sleep for most of the remaining time, then spin-wait for precision
    if (remaining.count() > 0.002) {
      auto sleep_time = remaining - Duration(0.001);
      std::this_thread::sleep_for(
          std::chrono::duration_cast<std::chrono::microseconds>(sleep_time));
    }

    while (Clock::now() - frame_start_ < target) {
      // Spin
    }
  }

  // Update FPS counter every 500ms
  frame_count_++;
  auto fps_elapsed = Duration(Clock::now() - last_fps_update_);
  if (fps_elapsed.count() >= 0.5) {
    current_fps_ = frame_count_ / fps_elapsed.count();
    frame_count_ = 0;
    last_fps_update_ = Clock::now();
  }
}

} // namespace nativecore