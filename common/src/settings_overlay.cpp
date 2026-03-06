#include "nativecore/settings_overlay.h"
#include "nativecore/core.h"
#include "nativecore/save_state.h"
#include "nativecore/themes.h"

#include <imgui.h>
#include <imgui_impl_sdl3.h>
#include <imgui_impl_sdlgpu3.h>

#include <controllerimage.h>

static const unsigned char controllerImageData[] = {
#embed "controllerimage-standard.bin"
};
static constexpr std::size_t controllerImageSize = sizeof(controllerImageData);

#include <SDL3/SDL.h>
#include <SDL3/SDL_surface.h>
#include <algorithm>
#include <cstdio>
#include <cstring>
#include <iostream>
#include <string>
#include <unordered_set>

namespace nativecore {

static SDL_GPUTexture *CreateGPUTextureFromSurface(SDL_GPUDevice *device,
                                                   SDL_Surface *surface) {
  if (!device || !surface)
    return nullptr;

  const int w = surface->w;
  const int h = surface->h;
  if (w <= 0 || h <= 0) {
    return nullptr;
  }

  SDL_GPUTextureCreateInfo tex_info = {};
  tex_info.type = SDL_GPU_TEXTURETYPE_2D;
  tex_info.format = SDL_GPU_TEXTUREFORMAT_R8G8B8A8_UNORM;
  tex_info.width = static_cast<uint32_t>(w);
  tex_info.height = static_cast<uint32_t>(h);
  tex_info.layer_count_or_depth = 1;
  tex_info.num_levels = 1;
  tex_info.usage = SDL_GPU_TEXTUREUSAGE_SAMPLER;

  SDL_GPUTexture *texture = SDL_CreateGPUTexture(device, &tex_info);
  if (!texture) {
    return nullptr;
  }

  SDL_GPUTransferBufferCreateInfo tb_info = {};
  tb_info.usage = SDL_GPU_TRANSFERBUFFERUSAGE_UPLOAD;
  tb_info.size = static_cast<uint32_t>(w * h * 4);
  SDL_GPUTransferBuffer *transfer =
      SDL_CreateGPUTransferBuffer(device, &tb_info);
  if (!transfer) {
    SDL_ReleaseGPUTexture(device, texture);
    return nullptr;
  }

  void *mapped = SDL_MapGPUTransferBuffer(device, transfer, false);
  if (!mapped) {
    SDL_ReleaseGPUTransferBuffer(device, transfer);
    SDL_ReleaseGPUTexture(device, texture);
    return nullptr;
  }

  auto *dst = static_cast<uint8_t *>(mapped);

  SDL_Surface *converted = SDL_ConvertSurface(surface, SDL_PIXELFORMAT_RGBA32);
  if (!converted) {
    SDL_UnmapGPUTransferBuffer(device, transfer);
    SDL_ReleaseGPUTransferBuffer(device, transfer);
    SDL_ReleaseGPUTexture(device, texture);
    return nullptr;
  }

  auto *src = static_cast<uint8_t *>(converted->pixels);
  const int row_bytes = w * 4;

  for (int y = 0; y < h; ++y) {
    std::memcpy(dst + y * row_bytes, src + y * converted->pitch, row_bytes);
  }

  SDL_DestroySurface(converted);
  SDL_UnmapGPUTransferBuffer(device, transfer);

  SDL_GPUCommandBuffer *cmd = SDL_AcquireGPUCommandBuffer(device);
  if (!cmd) {
    SDL_ReleaseGPUTransferBuffer(device, transfer);
    SDL_ReleaseGPUTexture(device, texture);
    return nullptr;
  }

  SDL_GPUCopyPass *copy_pass = SDL_BeginGPUCopyPass(cmd);

  SDL_GPUTextureTransferInfo src_info = {};
  src_info.transfer_buffer = transfer;
  src_info.offset = 0;

  SDL_GPUTextureRegion dst_region = {};
  dst_region.texture = texture;
  dst_region.w = static_cast<uint32_t>(w);
  dst_region.h = static_cast<uint32_t>(h);
  dst_region.d = 1;

  SDL_UploadToGPUTexture(copy_pass, &src_info, &dst_region, false);
  SDL_EndGPUCopyPass(copy_pass);
  SDL_SubmitGPUCommandBuffer(cmd);

  SDL_ReleaseGPUTransferBuffer(device, transfer);

  return texture;
}

static SDL_GPUTexture *CreateTextureFromPNG(SDL_GPUDevice *device,
                                            const char *path, int &out_w,
                                            int &out_h) {
  out_w = 0;
  out_h = 0;
  if (!device || !path)
    return nullptr;

  SDL_Surface *surface = SDL_LoadPNG(path);
  if (!surface)
    return nullptr;

  SDL_GPUTexture *texture = CreateGPUTextureFromSurface(device, surface);
  if (texture) {
    out_w = surface->w;
    out_h = surface->h;
  }

  SDL_DestroySurface(surface);
  return texture;
}

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

  gpu_device_ = video.gpuDevice();

  ControllerImage_Init();
  if (!ControllerImage_AddData(controllerImageData, controllerImageSize)) {
    std::cerr << "Failed to load embedded controller image data" << std::endl;
  }

  initialized_ = true;
  return true;
}

void SettingsOverlay::shutdown() {
  if (initialized_) {
    if (gpu_device_) {
      for (auto &entry : preview_textures_) {
        if (entry.second.texture) {
          SDL_ReleaseGPUTexture(gpu_device_, entry.second.texture);
        }
      }
      preview_textures_.clear();
      gpu_device_ = nullptr;
    }

    for (auto *tex : frame_textures_) {
      SDL_ReleaseGPUTexture(gpu_device_, tex);
    }
    frame_textures_.clear();

    ControllerImage_Quit();

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
                             ConfigManager &config, Core *core,
                             SaveStateManager *save_state_mgr) {
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
      }
      if (ImGui::BeginTabItem("Save States")) {
        renderSaveStatesPanel(core, save_state_mgr, config);
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

  for (auto *tex : frame_textures_) {
    SDL_ReleaseGPUTexture(gpu_device_, tex);
  }
  frame_textures_.clear();
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

    auto sdl_gamepad = input.gamepad(profile.bindings[0].controller_index);
    ControllerImage_Device *device =
        ControllerImage_CreateGamepadDevice(sdl_gamepad);
    if (!device) {
      device = ControllerImage_CreateGamepadDeviceByIdString("xbox360");
    }

    for (size_t i = 0; i < profile.bindings.size(); i++) {
      const InputBinding &b = profile.bindings[i];
      SDL_GPUTexture *controller_button_texture = nullptr;

      if (b.controller_index != 0)
        continue;

      if (device) {
        auto *button_surface =
            ControllerImage_CreateSurfaceForButton(device, b.pad_button, 32);
        if (button_surface) {
          controller_button_texture =
              CreateGPUTextureFromSurface(gpu_device_, button_surface);
          if (controller_button_texture) {
            frame_textures_.push_back(controller_button_texture);
          }
          SDL_DestroySurface(button_surface);
        }
      }

      ImGui::TableNextRow();
      ImGui::PushID(static_cast<int>(i));

      ImGui::TableNextColumn();
      ImGui::Text("%s", b.action_name.c_str());

      ImGui::TableNextColumn();
      ImGui::Text("%s", keyName(b.key));

      ImGui::TableNextColumn();
      if (controller_button_texture) {
        ImGui::Image(reinterpret_cast<ImTextureID>(controller_button_texture),
                     ImVec2(32, 32));
      } else {
        ImGui::Text("%s", gamepadButtonName(b.pad_button));
      }

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
    if (device) {
      ControllerImage_DestroyDevice(device);
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

void SettingsOverlay::renderSaveStatesPanel(Core *core,
                                            SaveStateManager *save_state_mgr,
                                            ConfigManager &config) {
  if (!core || !save_state_mgr) {
    ImGui::Text("No game loaded or save states unavailable.");
    return;
  }
  int max_saves = config.maxSaveStates();
  ImGui::PushID("max_save_states");
  ImGui::Text("Max save states per game");
  ImGui::PopID();
  ImGui::PushID("max_save_states_slider");
  if (ImGui::SliderInt("", &max_saves, 1, 50)) {
    config.setMaxSaveStates(max_saves);
  }
  ImGui::PopID();
  // TODO: make this rebindable
  ImGui::Text("Quick: F5 save, F7 load.");
  ImGui::Separator();

  if (ImGui::Button("New save")) {
    const uint32_t *fb = core->getFramebuffer();
    int w = core->systemInfo().screen_width;
    int h = core->systemInfo().screen_height;
    if (fb && w > 0 && h > 0)
      save_state_mgr->save(core, fb, w, h, "", max_saves);
  }
  ImGui::SameLine();
  if (ImGui::Button("Quick load")) {
    save_state_mgr->load(core, SaveStateManager::QUICK_SAVE_ID.data());
  }

  ImGui::Separator();
  ImGui::Text("Saved states:");
  auto saves = save_state_mgr->listSaves();

  if (gpu_device_) {
    std::unordered_set<std::string> live_paths;
    live_paths.reserve(saves.size());
    for (const auto &s : saves) {
      if (!s.preview_path.empty())
        live_paths.insert(s.preview_path);
    }
    for (auto it = preview_textures_.begin(); it != preview_textures_.end();) {
      if (live_paths.find(it->first) == live_paths.end()) {
        if (it->second.texture) {
          SDL_ReleaseGPUTexture(gpu_device_, it->second.texture);
        }
        it = preview_textures_.erase(it);
      } else {
        ++it;
      }
    }
  }

  static std::string renaming_id;
  static char rename_buf[256] = {};
  const float thumb_height = 96.0f * current_scale_;
  const float card_width = 200.0f * current_scale_;
  const float card_height = 180.0f * current_scale_;
  const float card_spacing = 24.0f * current_scale_;

  ImGui::BeginChild("SaveStatesScroll", ImVec2(0, 220.0f * current_scale_),
                    false, ImGuiWindowFlags_HorizontalScrollbar);

  for (const auto &s : saves) {
    ImGui::PushID(s.id.c_str());

    SDL_GPUTexture *texture = nullptr;
    int tex_w = 0;
    int tex_h = 0;
    if (gpu_device_ && !s.preview_path.empty()) {
      auto it = preview_textures_.find(s.preview_path);
      if (it != preview_textures_.end()) {
        texture = it->second.texture;
        tex_w = it->second.width;
        tex_h = it->second.height;
      } else {
        int w = 0, h = 0;
        SDL_GPUTexture *tex =
            CreateTextureFromPNG(gpu_device_, s.preview_path.c_str(), w, h);
        if (tex && w > 0 && h > 0) {
          PreviewTexture pt;
          pt.texture = tex;
          pt.width = w;
          pt.height = h;
          preview_textures_[s.preview_path] = pt;
          texture = tex;
          tex_w = w;
          tex_h = h;
        }
      }
    }

    ImGui::PushStyleColor(ImGuiCol_ChildBg,
                          ImGui::GetStyle().Colors[ImGuiCol_FrameBg]);
    ImGui::BeginChild("SaveStateCard", ImVec2(card_width, card_height), true,
                      ImGuiWindowFlags_NoScrollbar);

    if (texture && tex_w > 0 && tex_h > 0) {
      const float aspect =
          static_cast<float>(tex_w) / static_cast<float>(tex_h);
      const float thumb_width = thumb_height * aspect;
      float avail = ImGui::GetContentRegionAvail().x;
      float pad = (avail - thumb_width) * 0.5f;
      if (pad > 0.0f)
        ImGui::SetCursorPosX(ImGui::GetCursorPosX() + pad);
      const ImVec2 p_min = ImGui::GetCursorScreenPos();
      const ImVec2 p_max(p_min.x + thumb_width, p_min.y + thumb_height);
      const float rounding = 4.0f * current_scale_;
      ImGui::GetWindowDrawList()->AddImageRounded(
          reinterpret_cast<ImTextureID>(texture), p_min, p_max, ImVec2(0, 0),
          ImVec2(1, 1), IM_COL32_WHITE, rounding);
      ImGui::Dummy(ImVec2(thumb_width, thumb_height));
    } else {
      float placeholder_w = thumb_height * 1.2f;
      float avail = ImGui::GetContentRegionAvail().x;
      float pad = (avail - placeholder_w) * 0.5f;
      if (pad > 0.0f)
        ImGui::SetCursorPosX(ImGui::GetCursorPosX() + pad);
      ImGui::Dummy(ImVec2(placeholder_w, thumb_height));
    }

    if (s.id == renaming_id) {
      ImGui::InputText("Name", rename_buf, sizeof(rename_buf));
      if (ImGui::Button("Apply")) {
        save_state_mgr->renameSave(s.id, rename_buf);
        renaming_id.clear();
      }
      ImGui::SameLine();
      if (ImGui::Button("Cancel"))
        renaming_id.clear();
    } else {
      ImGui::TextWrapped("%s", s.display_name.c_str());
      bool display_name_has_date =
          !s.created_at.empty() &&
          s.display_name.find(s.created_at) != std::string::npos;
      if (!display_name_has_date && !s.created_at.empty())
        ImGui::Text("%s", s.created_at.c_str());
      if (ImGui::Button("Load"))
        save_state_mgr->load(core, s.id);
      ImGui::SameLine();
      if (ImGui::Button("Delete"))
        save_state_mgr->deleteSave(s.id);
      ImGui::SameLine();
      if (ImGui::Button("Rename")) {
        renaming_id = s.id;
        std::snprintf(rename_buf, sizeof(rename_buf), "%s",
                      s.display_name.c_str());
      }
    }

    ImGui::EndChild();
    ImGui::PopStyleColor();
    ImGui::SameLine(0.0f, card_spacing);

    ImGui::PopID();
  }

  ImGui::EndChild();
}

} // namespace nativecore
