#include "runtime/app.h"
#include "runtime/platform.h"

#include <SDL3/SDL.h>
#include <SDL3/SDL_filesystem.h>
#include <imgui.h>

#include <cstdlib>
#include <fstream>
#include <string>

namespace nativecore::runtime {

App::App() = default;
App::~App() { shutdown(); }

std::string App::init(std::unique_ptr<Core> core,
                      const std::string &config_path,
                      const std::string &rom_path) {
  core_ = std::move(core);
  rom_path_ = rom_path;
  if (!core_)
    return std::string("Core is null");

  if (!SDL_Init(SDL_INIT_VIDEO | SDL_INIT_AUDIO | SDL_INIT_GAMEPAD))
    return std::string("Failed to initialize SDL: ") + SDL_GetError();

  const auto &sys = core_->systemInfo();

  // Load or create config file
  config_path_ = config_path;
  if (config_path_.empty()) {
    config_path_ = getDataDirectory() + "config.yaml";
  }
  config_.load(config_path_);

  save_state_mgr_.setDataDirectory(getDataDirectory());
  if (!rom_path_.empty())
    save_state_mgr_.setGame(sys.name, rom_path_);

  // Initialize video
  std::string title = "recomp - " + sys.name;
  if (!video_.init(title, sys.screen_width, sys.screen_height,
                   config_.windowScale())) {
    const char *err = SDL_GetError();
    return std::string("Failed to initialize video: ") +
           (err && *err ? err : "unknown");
  }
  video_.setFullscreen(config_.fullscreen());
  video_.setVsync(config_.vsync());
  auto mode_str = config_.scaleMode();
  video_.setScaleMode(mode_str == "bilinear" ? ScaleMode::Bilinear
                                             : ScaleMode::Nearest);
  video_.setMaintainAspectRatio(config_.maintainAspectRatio());

  // Initialize audio
  if (!audio_.init(sys.audio_sample_rate))
    return std::string("Failed to initialize audio: ") + SDL_GetError();
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
  return std::string();
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

    if (event.type == SDL_EVENT_KEY_DOWN && !event.key.repeat) {
      if (event.key.scancode == SDL_SCANCODE_F1) {
        overlay_.toggle();
        continue;
      }
      if (event.key.scancode == SDL_SCANCODE_F11) {
        video_.toggleFullscreen();
        continue;
      }
    }

    overlay_.processEvent(event);

    const bool want_keyboard =
        overlay_.isOpen() && ImGui::GetIO().WantCaptureKeyboard;

    if (!want_keyboard) {
      input_.processEvent(event);
    }
  }

  if (core_ && !overlay_.isOpen()) {
    bool quick_save = input_.isActionPressed("Quick Save", 0);
    bool quick_load = input_.isActionPressed("Quick Load", 0);
    if (quick_save && !prev_quick_save_)
      quick_save_pending_ = true;
    if (quick_load && !prev_quick_load_)
      save_state_mgr_.load(core_.get(), SaveStateManager::QUICK_SAVE_ID.data());
    prev_quick_save_ = quick_save;
    prev_quick_load_ = quick_load;
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
    const int gw = core_->systemInfo().screen_width;
    const int gh = core_->systemInfo().screen_height;
    if (framebuffer) {
      video_.uploadFramebuffer(framebuffer, gw, gh);
      if (quick_save_pending_) {
        save_state_mgr_.saveToSlot(core_.get(), framebuffer, gw, gh,
                                   SaveStateManager::QUICK_SAVE_ID.data(),
                                   "Quick save");
        quick_save_pending_ = false;
      }
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
    overlay_.render(audio_, input_, video_, limiter_, config_, core_.get(),
                    &save_state_mgr_);
  } else {
    video_.present();
  }
}
} // namespace nativecore::runtime
