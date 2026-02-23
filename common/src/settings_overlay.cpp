#include "nativecore/settings_overlay.h"
#include "nativecore/core.h"

#include <imgui.h>
#include <imgui_impl_sdl3.h>
#include <imgui_impl_sdlgpu3.h>

#include <algorithm>
#include <cstdio>

namespace nativecore {

SettingsOverlay::SettingsOverlay() = default;
SettingsOverlay::~SettingsOverlay() { shutdown(); }

bool SettingsOverlay::init(VideoManager &video) {
  IMGUI_CHECKVERSION();
  ImGui::CreateContext();

  auto &io = ImGui::GetIO();
  io.ConfigFlags |= ImGuiConfigFlags_NavEnableKeyboard;
  io.ConfigFlags |= ImGuiConfigFlags_NavEnableGamepad;

  ImGui::StyleColorsDark();

  ImGui_ImplSDL3_InitForOther(video.window());
  ImGui_ImplSDLGPU3_InitInfo init_info = {};
  init_info.ColorTargetFormat =
      SDL_GetGPUSwapchainTextureFormat(video.gpuDevice(), video.window());
  ImGui_ImplSDLGPU3_Init(&init_info);

  initialized_ = true;
  return true;
}

void SettingsOverlay::shutdown() {
  if (initialized_) {
    ImGui_ImplSDLGPU3_Shutdown();
    ImGui_ImplSDL3_Shutdown();
    ImGui::DestroyContext();
    initialized_ = false;
  }
}

void SettingsOverlay::toggle() { open_ = !open_; }

void SettingsOverlay::processEvent(const SDL_Event &event) {
  if (initialized_ && open_) {
    ImGui_ImplSDL3_ProcessEvent(&event);
  }
}

void SettingsOverlay::render(AudioManager &audio, InputManager &input,
                             VideoManager &video, FrameLimiter &limiter,
                             ConfigManager &config, Core *core) {
  if (!initialized_ || !open_)
    return;

  ImGui_ImplSDLGPU3_NewFrame();
  ImGui_ImplSDL3_NewFrame();
  ImGui::NewFrame();

  ImGui::SetNextWindowPos(ImVec2(20, 20), ImGuiCond_FirstUseEver);
  ImGui::SetNextWindowSize(ImVec2(420, 500), ImGuiCond_FirstUseEver);

  if (ImGui::Begin("Settings", &open_, ImGuiWindowFlags_NoCollapse)) {
    if (ImGui::BeginTabBar("SettingsTabs", ImGuiTabBarFlags_None)) {
      if (ImGui::BeginTabItem("Audio")) {
        renderAudioPanel(audio, config);
        ImGui::EndTabItem();
      }
      if (ImGui::BeginTabItem("Input")) {
        renderInputPanel(input, config);
        ImGui::EndTabItem();
      }
      if (ImGui::BeginTabItem("Video")) {
        renderVideoPanel(video, config);
        ImGui::EndTabItem();
      }
      if (ImGui::BeginTabItem("Performance")) {
        renderPerformancePanel(limiter, config);
        ImGui::EndTabItem();
      }
      ImGui::EndTabBar();
    }
  }

  ImGui::End();

  ImGui::Render();

  auto *cmd = SDL_AcquireGPUCommandBuffer(video.gpuDevice());
  if (!cmd)
    return;
  SDL_GPUTexture *swapchain_tex = nullptr;
  uint32_t swapchain_width, swapchain_height;
  if (SDL_AcquireGPUSwapchainTexture(cmd, video.window(), &swapchain_tex,
                                     &swapchain_width, &swapchain_height) &&
      swapchain_tex) {
    // Draw game to swapchain first so the overlay composites on top of the
    // current frame.
    video.recordDrawToSwapchain(cmd, swapchain_tex, swapchain_width,
                                swapchain_height);

    // Required: upload vertex/index buffers before the render pass (SDL GPU
    // backend).
    ImGui_ImplSDLGPU3_PrepareDrawData(ImGui::GetDrawData(), cmd);

    SDL_GPUColorTargetInfo color_info = {};
    color_info.texture = swapchain_tex;
    color_info.load_op = SDL_GPU_LOADOP_LOAD;
    color_info.store_op = SDL_GPU_STOREOP_STORE;

    auto *render_pass = SDL_BeginGPURenderPass(cmd, &color_info, 1, nullptr);
    ImGui_ImplSDLGPU3_RenderDrawData(ImGui::GetDrawData(), cmd, render_pass);
    SDL_EndGPURenderPass(render_pass);
  }
  SDL_SubmitGPUCommandBuffer(cmd);
}

void SettingsOverlay::renderAudioPanel(AudioManager &audio,
                                       ConfigManager &config) {
  float vol = audio.masterVolume();
  if (ImGui::SliderFloat("Master Volume", &vol, 0.0f, 1.0f)) {
    audio.setMasterVolume(vol);
    config.setMasterVolume(vol);
  }

  ImGui::Separator();
  ImGui::Text("Channels");

  for (int i = 0; i < audio.channelCount(); i++) {
    auto &ch = audio.channelState(i);
    ImGui::PushID(i);

    bool muted = ch.muted;
    if (ImGui::Checkbox(("Mute##ch" + std::to_string(i)).c_str(), &muted)) {
      audio.setChannelMuted(i, muted);
      config.setChannelMuted(i, muted);
    }

    ImGui::SameLine();
    float ch_vol = ch.volume;
    if (ImGui::SliderFloat(ch.name.c_str(), &ch_vol, 0.0f, 1.0f)) {
      audio.setChannelVolume(i, ch_vol);
    }

    ImGui::PopID();
  }
}

void SettingsOverlay::renderInputPanel(InputManager &input,
                                       ConfigManager &config) {
  // TODO: Implement input panel
}

void SettingsOverlay::renderVideoPanel(VideoManager &video,
                                       ConfigManager &config) {
  int scale = video.scale();
  if (ImGui::SliderInt("Window Scale", &scale, 1, 8)) {
    video.setScale(scale);
    config.setWindowScale(scale);
  }

  bool fullscreen = video.isFullscreen();
  if (ImGui::Checkbox("Fullscreen", &fullscreen)) {
    video.setFullscreen(fullscreen);
    config.setFullscreen(fullscreen);
  }

  const char *modes[] = {"Nearest", "Bilinear"};
  int current = static_cast<int>(video.scaleMode());
  if (ImGui::Combo("Scale Mode", &current, modes, 2)) {
    video.setScaleMode(static_cast<ScaleMode>(current));
    config.setScaleMode(current == 0 ? "nearest" : "bilinear");
  }
}

void SettingsOverlay::renderPerformancePanel(FrameLimiter &limiter,
                                             ConfigManager &config) {
  bool uncapped = limiter.isUncapped();
  if (ImGui::Checkbox("Uncapped FPS", &uncapped)) {
    limiter.setUncapped(uncapped);
    config.setUncappedFps(uncapped);
  }

  if (!uncapped) {
    float fps = static_cast<float>(limiter.targetFPS());
    if (ImGui::SliderFloat("FPS Limit", &fps, 15.0f, 240.0f, "%.0f")) {
      limiter.setTargetFPS(fps);
      config.setFpsLimit(fps);
    }
  }

  ImGui::Separator();
  ImGui::Text("Current FPS: %.1f", limiter.currentFPS());
  ImGui::Text("Frame Time: %.2f ms", limiter.frameTimeMS());

  // Frame time history graph
  static float history[120] = {};
  static int history_idx = 0;
  history[history_idx] = static_cast<float>(limiter.frameTimeMS());
  history_idx = (history_idx + 1) % 120;
  ImGui::PlotLines("Frame Time (ms)", history, 120, history_idx, nullptr, 0.0f,
                   33.3f, ImVec2(0, 60));
}

} // namespace nativecore