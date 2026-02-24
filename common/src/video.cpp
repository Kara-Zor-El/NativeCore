#include "nativecore/video.h"

#include <cstring>

namespace nativecore {

VideoManager::VideoManager() = default;
VideoManager::~VideoManager() { shutdown(); }

bool VideoManager::init(const std::string &title, int game_width,
                        int game_height, int scale) {
  game_width_ = game_width;
  game_height_ = game_height;
  scale_ = scale;

  window_ = SDL_CreateWindow(title.c_str(), game_width_ * scale_,
                             game_height_ * scale_, SDL_WINDOW_RESIZABLE);
  if (!window_) {
    const char *err = SDL_GetError();
    SDL_SetError("SDL_CreateWindow: %s", (err && *err) ? err : "unknown");
    return false;
  }

  gpu_device_ = SDL_CreateGPUDevice(SDL_GPU_SHADERFORMAT_SPIRV |
                                        SDL_GPU_SHADERFORMAT_MSL |
                                        SDL_GPU_SHADERFORMAT_DXIL,
                                    true, nullptr);

  if (!gpu_device_) {
    const char *err = SDL_GetError();
    SDL_DestroyWindow(window_);
    window_ = nullptr;
    SDL_SetError("SDL_CreateGPUDevice: %s", (err && *err) ? err : "unknown");
    return false;
  }

  if (!SDL_ClaimWindowForGPUDevice(gpu_device_, window_)) {
    const char *err = SDL_GetError();
    SDL_DestroyGPUDevice(gpu_device_);
    SDL_DestroyWindow(window_);
    gpu_device_ = nullptr;
    window_ = nullptr;
    SDL_SetError("SDL_ClaimWindowForGPUDevice: %s",
                 (err && *err) ? err : "unknown");
    return false;
  }

  recreateTexture();
  if (!game_texture_ || !transfer_buffer_) {
    const char *err = SDL_GetError();
    shutdown();
    SDL_SetError("Failed to create game texture: %s",
                 (err && *err) ? err : "unknown");
    return false;
  }
  return true;
}

void VideoManager::shutdown() {
  if (gpu_device_) {
    if (transfer_buffer_) {
      SDL_ReleaseGPUTransferBuffer(gpu_device_, transfer_buffer_);
      transfer_buffer_ = nullptr;
    }
    if (game_texture_) {
      SDL_ReleaseGPUTexture(gpu_device_, game_texture_);
      game_texture_ = nullptr;
    }
    SDL_ReleaseWindowFromGPUDevice(gpu_device_, window_);
    SDL_DestroyGPUDevice(gpu_device_);
    gpu_device_ = nullptr;
  }
  if (window_) {
    SDL_DestroyWindow(window_);
    window_ = nullptr;
  }
}

void VideoManager::recreateTexture() {
  if (game_texture_) {
    SDL_ReleaseGPUTexture(gpu_device_, game_texture_);
  }
  if (transfer_buffer_) {
    SDL_ReleaseGPUTransferBuffer(gpu_device_, transfer_buffer_);
  }

  SDL_GPUTextureCreateInfo tex_info = {};
  tex_info.type = SDL_GPU_TEXTURETYPE_2D;
  // Framebuffer is 0xAARRGGBB
  // in little-endian memory that is B,G,R,A = BGRA
  tex_info.format = SDL_GPU_TEXTUREFORMAT_B8G8R8A8_UNORM;
  tex_info.width = static_cast<uint32_t>(game_width_);
  tex_info.height = static_cast<uint32_t>(game_height_);
  tex_info.layer_count_or_depth = 1;
  tex_info.num_levels = 1;
  tex_info.usage =
      SDL_GPU_TEXTUREUSAGE_SAMPLER | SDL_GPU_TEXTUREUSAGE_COLOR_TARGET;
  game_texture_ = SDL_CreateGPUTexture(gpu_device_, &tex_info);

  SDL_GPUTransferBufferCreateInfo tb_info = {};
  tb_info.usage = SDL_GPU_TRANSFERBUFFERUSAGE_UPLOAD;
  tb_info.size = static_cast<uint32_t>(game_width_ * game_height_ * 4);
  transfer_buffer_ = SDL_CreateGPUTransferBuffer(gpu_device_, &tb_info);
}

void VideoManager::uploadFramebuffer(const uint32_t *pixels, int width,
                                     int height) {
  if (!gpu_device_ || !transfer_buffer_ || !game_texture_)
    return;
  if (width != game_width_ || height != game_height_)
    return;

  void *mapped = SDL_MapGPUTransferBuffer(gpu_device_, transfer_buffer_, false);
  if (!mapped)
    return;
  std::memcpy(mapped, pixels, width * height * 4);
  SDL_UnmapGPUTransferBuffer(gpu_device_, transfer_buffer_);

  auto *cmd = SDL_AcquireGPUCommandBuffer(gpu_device_);
  if (!cmd)
    return;

  auto *copy_pass = SDL_BeginGPUCopyPass(cmd);

  SDL_GPUTextureTransferInfo src = {};
  src.transfer_buffer = transfer_buffer_;
  src.offset = 0;

  SDL_GPUTextureRegion dst = {};
  dst.texture = game_texture_;
  dst.w = static_cast<uint32_t>(width);
  dst.h = static_cast<uint32_t>(height);
  dst.d = 1;

  SDL_UploadToGPUTexture(copy_pass, &src, &dst, false);
  SDL_EndGPUCopyPass(copy_pass);
  SDL_SubmitGPUCommandBuffer(cmd);
}

void VideoManager::recordDrawToSwapchain(SDL_GPUCommandBuffer *cmd,
                                         SDL_GPUTexture *swapchain_tex,
                                         uint32_t swapchain_w,
                                         uint32_t swapchain_h) {
  if (!cmd || !swapchain_tex)
    return;
  if (!game_texture_)
    return;

  uint32_t dest_x = 0;
  uint32_t dest_y = 0;
  uint32_t dest_w = swapchain_w;
  uint32_t dest_h = swapchain_h;

  if (maintain_aspect_ratio_ && swapchain_w > 0 && swapchain_h > 0 &&
      game_width_ > 0 && game_height_ > 0) {
    // Scale to fit while preserving aspect ratio
    const double scale_w = static_cast<double>(swapchain_w) / game_width_;
    const double scale_h = static_cast<double>(swapchain_h) / game_height_;
    const double scale = (scale_w < scale_h) ? scale_w : scale_h;
    dest_w = static_cast<uint32_t>(game_width_ * scale + 0.5);
    dest_h = static_cast<uint32_t>(game_height_ * scale + 0.5);
    if (dest_w > swapchain_w)
      dest_w = swapchain_w;
    if (dest_h > swapchain_h)
      dest_h = swapchain_h;
    dest_x = (swapchain_w - dest_w) / 2;
    dest_y = (swapchain_h - dest_h) / 2;

    // Clear entire swapchain to black first
    SDL_GPUColorTargetInfo color_info = {};
    color_info.texture = swapchain_tex;
    color_info.load_op = SDL_GPU_LOADOP_CLEAR;
    color_info.store_op = SDL_GPU_STOREOP_STORE;
    color_info.clear_color = {0.0f, 0.0f, 0.0f, 1.0f};
    auto *pass = SDL_BeginGPURenderPass(cmd, &color_info, 1, nullptr);
    SDL_EndGPURenderPass(pass);
  }

  SDL_GPUBlitInfo blit = {};
  blit.source.texture = game_texture_;
  blit.source.w = static_cast<uint32_t>(game_width_);
  blit.source.h = static_cast<uint32_t>(game_height_);
  blit.destination.texture = swapchain_tex;
  blit.destination.x = dest_x;
  blit.destination.y = dest_y;
  blit.destination.w = dest_w;
  blit.destination.h = dest_h;
  blit.filter = (scale_mode_ == ScaleMode::Nearest) ? SDL_GPU_FILTER_NEAREST
                                                    : SDL_GPU_FILTER_LINEAR;
  blit.load_op = maintain_aspect_ratio_ ? SDL_GPU_LOADOP_LOAD
                                         : SDL_GPU_LOADOP_CLEAR;
  blit.clear_color = {0.0f, 0.0f, 0.0f, 1.0f};

  SDL_BlitGPUTexture(cmd, &blit);
}

void VideoManager::present() {
  if (!gpu_device_ || !window_)
    return;

  auto *cmd = SDL_AcquireGPUCommandBuffer(gpu_device_);
  if (!cmd)
    return;

  SDL_GPUTexture *swapchain_tex = nullptr;
  uint32_t sw, sh;
  if (!SDL_AcquireGPUSwapchainTexture(cmd, window_, &swapchain_tex, &sw, &sh)) {
    SDL_SubmitGPUCommandBuffer(cmd);
    return;
  }
  if (!swapchain_tex) {
    SDL_SubmitGPUCommandBuffer(cmd);
    return;
  }

  recordDrawToSwapchain(cmd, swapchain_tex, sw, sh);
  SDL_SubmitGPUCommandBuffer(cmd);
}

void VideoManager::setFullscreen(bool fs) {
  fullscreen_ = fs;
  SDL_SetWindowFullscreen(window_, fs);
}

void VideoManager::setScale(int scale) {
  scale_ = scale;
  if (!fullscreen_ && window_) {
    SDL_SetWindowSize(window_, game_width_ * scale, game_height_ * scale);
  }
}

void VideoManager::setScaleMode(ScaleMode mode) { scale_mode_ = mode; }

} // namespace nativecore