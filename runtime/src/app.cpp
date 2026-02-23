#include "runtime/app.h"
#include "runtime/platform.h"

#include <SDL3/SDL.h>
#include <SDL3/SDL_filesystem.h>

#include <cstdlib>
#include <fstream>
#include <string>

namespace nativecore::runtime {

App::App() = default;
App::~App() { shutdown(); }

bool App::init(std::unique_ptr<Core> core, const std::string &config_path) {
  core_ = std::move(core);
  if (!core_)
    return false;

  if (!SDL_Init(SDL_INIT_VIDEO | SDL_INIT_AUDIO | SDL_INIT_GAMEPAD))
    return false;

  const auto &sys = core_->systemInfo();

  // Load or create config file
  config_path_ = config_path;
  if (config_path_.empty()) {
    auto dir = getConfigDirectory(sys.name);
    config_path_ = dir + "config.yaml";
  }
  config_.load(config_path_);

  // Initialize video
  std::string title = "recomp - " + sys.name;
  if (!video_.init(title, sys.screen_width, sys.screen_height,
                   config_.windowScale())) {
    return false;
  }
  video_.setFullscreen(config_.fullscreen());
  auto mode_str = config_.scaleMode();
  video_.setScaleMode(mode_str == "bilinear" ? ScaleMode::Bilinear
                                             : ScaleMode::Nearest);

  // Initialize audio
  if (!audio_.init(sys.audio_sample_rate))
    return false;
  audio_.setMasterVolume(config_.masterVolume());

  // Set up for Game Boy
  if (sys.name == "Game Boy") {
    audio_.registerChannel(0, "Square 1");
    audio_.registerChannel(1, "Square 2");
    audio_.registerChannel(2, "Wave");
    audio_.registerChannel(3, "Noise");
  }

  // Initialize input
  if (sys.name == "Game Boy") {
    input_.setProfile(InputManager::defaultGameBoyProfile());
  }

  // Initialize frame limiter
  limiter_.setTargetFPS(config_.fpsLimit() > 0 ? config_.fpsLimit()
                                               : sys.native_fps);
  limiter_.setUncapped(config_.uncappedFps());

  // Initialize overlay
  overlay_.init(video_);

  running_ = true;
  return true;
}

void App::run() {
  while (running_) {
    limiter_.beginFrame();
    handleEvents();
    if (!overlay_.isOpen()) {
      update();
    }
    render();
    limiter_.endFrame();
  }
}

void App::shutdown() {
  overlay_.shutdown();
  audio_.shutdown();
  input_.shutdown();
  video_.shutdown();
  SDL_Quit();
  core_.reset();
}

void App::handleEvents() {
  std::vector<SDL_Event> events;
  input_.pollEvents(events);

  for (auto &event : events) {
    if (event.type == SDL_EVENT_QUIT) {
      running_ = false;
      return;
    }
    if (event.type == SDL_EVENT_KEY_DOWN ||
        event.key.scancode == SDL_SCANCODE_F1 && !event.key.repeat) {
      overlay_.toggle();
    }

    if (event.type == SDL_EVENT_KEY_DOWN ||
        event.key.scancode == SDL_SCANCODE_F11 && !event.key.repeat) {
      video_.toggleFullscreen();
    }

    overlay_.processEvent(event);
  }

  if (core_) {
    core_->setInputState(0, input_.controllerState(0));
    core_->setInputState(1, input_.controllerState(1));
  }
}

void App::update() {
  if (core_) {
    core_->tick();
  }
}

void App::render() {
  if (core_) {
    auto *framebuffer = core_->getFramebuffer();
    if (framebuffer) {
      video_.uploadFramebuffer(framebuffer, core_->systemInfo().screen_width,
                               core_->systemInfo().screen_height);
    }

    // Push audio in small chunks
    const size_t samples_per_frame = static_cast<size_t>(
        core_->systemInfo().audio_sample_rate / core_->systemInfo().native_fps +
        0.5);
    const size_t max_queue_samples = samples_per_frame * 6; // ~100ms @ 60fps
    size_t queued = audio_.getQueuedSampleCount();
    const size_t chunk_size = 512u;

    while (true) {
      size_t count = 0;
      const float *samples = core_->getAudioBuffer(count);
      if (!samples || count == 0)
        break;

      if (queued >= max_queue_samples) {
        // Drop excess samples
        core_->consumeAudioSamples(count);
        break;
      }

      size_t space = max_queue_samples - queued;
      size_t chunk = count < chunk_size ? count : chunk_size;
      if (chunk > space)
        chunk = space;
      if (chunk == 0)
        break;

      audio_.pushSamples(samples, chunk);
      core_->consumeAudioSamples(chunk);
      queued += chunk;
    }
  }

  if (overlay_.isOpen()) {
    overlay_.render(audio_, input_, video_, limiter_, config_, core_.get());
  } else {
    video_.present();
  }
}
} // namespace nativecore::runtime