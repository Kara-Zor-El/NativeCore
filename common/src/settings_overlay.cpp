#include "nativecore/settings_overlay.h"
#include "nativecore/core.h"
#include "nativecore/themes.h"

#include <imgui.h>
#include <imgui_impl_sdl3.h>
#include <imgui_impl_sdlgpu3.h>

#include <SDL3/SDL.h>
#include <algorithm>
#include <cstdio>
#include <string>

namespace nativecore {

SettingsOverlay::SettingsOverlay() = default;
SettingsOverlay::~SettingsOverlay() { shutdown(); }

bool SettingsOverlay::init(VideoManager &video) {
  IMGUI_CHECKVERSION();
  ImGui::CreateContext();

  auto &io = ImGui::GetIO();
  io.ConfigFlags |= ImGuiConfigFlags_NavEnableKeyboard;
  io.ConfigFlags |= ImGuiConfigFlags_NavEnableGamepad;

  applyThemeForCore(std::string("Default"));

  ImGui_ImplSDL3_InitForOther(video.window());
  ImGui_ImplSDLGPU3_InitInfo init_info = {};
  init_info.Device = video.gpuDevice();
  init_info.ColorTargetFormat =
      SDL_GetGPUSwapchainTextureFormat(video.gpuDevice(), video.window());
  init_info.MSAASamples = SDL_GPU_SAMPLECOUNT_1;
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

void SettingsOverlay::applyThemeForCore(const std::string &core_name) {
  ImGui::GetStyle() = ImGuiStyle();
  if (core_name == "Game Boy") {
    themes::apply_game_boy();
  } else {
    themes::apply_default();
  }
}

void SettingsOverlay::render(AudioManager &audio, InputManager &input,
                             VideoManager &video, FrameLimiter &limiter,
                             ConfigManager &config, Core *core) {
  if (!initialized_ || !open_)
    return;

  const float raw_scale = static_cast<float>(video.scale());
  float effective_scale = 1.0f;
  if (raw_scale <= 1.0f) {
    effective_scale = 0.65f;
  } else if (raw_scale <= 2.0f) {
    effective_scale = 0.8f;
  } else if (raw_scale <= 3.0f) {
    effective_scale = 1.25f;
  } else {
    effective_scale = 1.5f;
  }

  const std::string core_name = core ? core->systemInfo().name : "Default";
  if (effective_scale != current_scale_ || core_name != core_name_) {
    ImGui::GetStyle() = ImGuiStyle();
    applyThemeForCore(core_name);
    ImGui::GetStyle().ScaleAllSizes(effective_scale);
    ImGui::GetIO().FontGlobalScale = effective_scale;
    current_scale_ = effective_scale;
    core_name_ = core_name;
  }

  ImGui_ImplSDLGPU3_NewFrame();
  ImGui_ImplSDL3_NewFrame();

  int pixel_w = 0, pixel_h = 0;
  SDL_GetWindowSizeInPixels(video.window(), &pixel_w, &pixel_h);
  if (pixel_w > 0 && pixel_h > 0) {
    auto &io = ImGui::GetIO();
    io.DisplaySize =
        ImVec2(static_cast<float>(pixel_w), static_cast<float>(pixel_h));
    io.DisplayFramebufferScale = ImVec2(1.0f, 1.0f);
  }

  ImGui::NewFrame();

  ImGui::SetNextWindowPos(ImVec2(0, 0), ImGuiCond_Always);
  ImGui::SetNextWindowSize(ImGui::GetIO().DisplaySize, ImGuiCond_Always);

  auto flags = ImGuiWindowFlags_NoCollapse | ImGuiWindowFlags_NoResize |
               ImGuiWindowFlags_NoMove | ImGuiWindowFlags_NoTitleBar;

  if (ImGui::Begin("Settings", &open_, flags)) {
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
        renderPerformancePanel(limiter, config, video);
        ImGui::EndTabItem();
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

static const char *keyName(SDL_Scancode scancode) {
  if (scancode == SDL_SCANCODE_UNKNOWN)
    return "-";
  const char *name = SDL_GetScancodeName(scancode);
  return name && name[0] ? name : "-";
}

static const char *gamepadButtonName(SDL_GamepadButton button) {
  if (button == SDL_GAMEPAD_BUTTON_INVALID)
    return "-";
  const char *name = SDL_GetGamepadStringForButton(button);
  return name && name[0] ? name : "-";
}

void SettingsOverlay::renderInputPanel(InputManager &input,
                                       ConfigManager &config) {
  const InputProfile &profile = input.currentProfile();
  if (profile.bindings.empty()) {
    ImGui::Text("No bindings for this profile.");
    return;
  }

  if (input.isRebinding()) {
    ImGui::TextColored(ImVec4(1.0f, 0.8f, 0.2f, 1.0f),
                       "Press a key or gamepad button... (Esc to cancel)");
    ImGui::Separator();
  }

  ImGui::Text("Player 0");
  ImGui::Separator();

  if (ImGui::BeginTable("InputBindings", 4,
                        ImGuiTableFlags_Borders | ImGuiTableFlags_RowBg)) {
    ImGui::TableSetupColumn("Action", ImGuiTableColumnFlags_WidthStretch);
    ImGui::TableSetupColumn("Key", ImGuiTableColumnFlags_WidthFixed,
                            80.0f * current_scale_);
    ImGui::TableSetupColumn("Gamepad", ImGuiTableColumnFlags_WidthFixed,
                            80.0f * current_scale_);
    ImGui::TableSetupColumn("", ImGuiTableColumnFlags_WidthFixed,
                            70.0f * current_scale_);
    ImGui::TableHeadersRow();

    for (size_t i = 0; i < profile.bindings.size(); i++) {
      const InputBinding &b = profile.bindings[i];
      if (b.controller_index != 0)
        continue;

      ImGui::TableNextRow();
      ImGui::PushID(static_cast<int>(i));

      ImGui::TableNextColumn();
      ImGui::Text("%s", b.action_name.c_str());

      ImGui::TableNextColumn();
      ImGui::Text("%s", keyName(b.key));

      ImGui::TableNextColumn();
      ImGui::Text("%s", gamepadButtonName(b.pad_button));

      ImGui::TableNextColumn();
      bool is_rebinding_this =
          input.isRebinding() && input.rebindAction() == b.action_name;
      if (is_rebinding_this)
        ImGui::BeginDisabled();
      if (ImGui::Button("Rebind")) {
        input.startRebind(b.action_name);
      }
      if (is_rebinding_this)
        ImGui::EndDisabled();

      ImGui::PopID();
    }
    ImGui::EndTable();
  }
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

  bool maintain_aspect = video.maintainAspectRatio();
  if (ImGui::Checkbox("Maintain aspect ratio", &maintain_aspect)) {
    video.setMaintainAspectRatio(maintain_aspect);
    config.setMaintainAspectRatio(maintain_aspect);
  }
  if (ImGui::IsItemHovered(ImGuiHoveredFlags_AllowWhenDisabled)) {
    ImGui::SetTooltip("Keep original aspect ratio when scaling");
  }
}

void SettingsOverlay::renderPerformancePanel(FrameLimiter &limiter,
                                             ConfigManager &config,
                                             VideoManager &video) {
  bool vsync = config.vsync();
  if (ImGui::Checkbox("VSync", &vsync)) {
    config.setVsync(vsync);
    video.setVsync(vsync);
  }
  if (ImGui::IsItemHovered(ImGuiHoveredFlags_AllowWhenDisabled)) {
    ImGui::SetTooltip("Sync to display refresh. Disable to allow FPS above "
                      "display rate (e.g. >120 on 120Hz), may cause tearing.");
  }

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
  const float graph_height = std::max(60.0f, 80.0f * current_scale_);
  ImGui::Text("Frame Time: ");
  ImGui::PushID("frame_time_graph");
  ImGui::PlotLines("", history, 120, history_idx, nullptr, 0.0f, 33.3f,
                   ImVec2(-1.0f, graph_height));
  ImGui::PopID();
}

} // namespace nativecore
