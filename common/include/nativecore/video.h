
#pragma once

#include <SDL3/SDL.h>
#include <cstdint>
#include <string>

namespace nativecore {

enum class ScaleMode {
  Nearest,
  Bilinear,
};

class VideoManager {
public:
  VideoManager();
  ~VideoManager();

  bool init(const std::string &title, int game_width, int game_height,
            int scale = 3);
  void shutdown();

  void uploadFramebuffer(const uint32_t *pixels, int width, int height);
  void present();

  /** Records drawing the game to an already-acquired swapchain texture into \a
   * cmd. Used when the overlay will draw on top in the same frame; does not
   * acquire or submit. */
  void recordDrawToSwapchain(SDL_GPUCommandBuffer *cmd,
                             SDL_GPUTexture *swapchain_tex,
                             uint32_t swapchain_w, uint32_t swapchain_h);

  void setFullscreen(bool fs);
  bool isFullscreen() const { return fullscreen_; }
  void toggleFullscreen() { setFullscreen(!fullscreen_); }

  void setScale(int scale);
  int scale() const { return scale_; }

  void setScaleMode(ScaleMode mode);
  ScaleMode scaleMode() const { return scale_mode_; }

  void setMaintainAspectRatio(bool maintain) { maintain_aspect_ratio_ = maintain; }
  bool maintainAspectRatio() const { return maintain_aspect_ratio_; }

  SDL_Window *window() const { return window_; }
  SDL_GPUDevice *gpuDevice() const { return gpu_device_; }

  int gameWidth() const { return game_width_; }
  int gameHeight() const { return game_height_; }

private:
  SDL_Window *window_ = nullptr;
  SDL_GPUDevice *gpu_device_ = nullptr;
  SDL_GPUTexture *game_texture_ = nullptr;
  SDL_GPUTransferBuffer *transfer_buffer_ = nullptr;

  int game_width_ = 0;
  int game_height_ = 0;
  int scale_ = 3;
  bool fullscreen_ = false;
  bool maintain_aspect_ratio_ = false;
  ScaleMode scale_mode_ = ScaleMode::Nearest;

  void recreateTexture();
};

} // namespace nativecore
