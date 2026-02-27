#include "nativecore/camera_provider.h"

#include <SDL3/SDL_camera.h>
#include <SDL3/SDL_log.h>
#include <SDL3/SDL_surface.h>

#include <cstring>

namespace nativecore {

CameraProvider::~CameraProvider() { close(); }

bool CameraProvider::open() {
  if (camera_)
    return true;

  // Get list of camera devices
  int count = 0;
  SDL_CameraID *devices = SDL_GetCameras(&count);
  if (!devices || count == 0) {
    SDL_Log("CameraProvider: no camera devices found");
    return false;
  }

  // TODO: Allow camera to be selected instead of just using the first one.
  SDL_CameraID dev_id = devices[0];
  SDL_free(devices);

  SDL_CameraSpec desired_spec{};
  desired_spec.width = 320;
  desired_spec.height = 240;
  desired_spec.framerate_numerator = 15;
  desired_spec.framerate_denominator = 1;
  desired_spec.colorspace = SDL_COLORSPACE_UNKNOWN;
  desired_spec.format = SDL_PIXELFORMAT_UNKNOWN;

  camera_ = SDL_OpenCamera(dev_id, &desired_spec);
  if (!camera_) {
    SDL_Log("CameraProvider: failed to open camera: %s", SDL_GetError());
    return false;
  }

  SDL_Log("CameraProvider: camera opened successfully");
  return true;
}

void CameraProvider::close() {
  if (camera_) {
    SDL_CloseCamera(camera_);
    camera_ = nullptr;
  }
  src_width_ = 0;
  src_height_ = 0;
}

bool CameraProvider::isOpen() const { return camera_ != nullptr; }

bool CameraProvider::captureFrame(uint8_t *out, int width, int height) {
  if (!camera_ || !out)
    return false;

  Uint64 timestamp = 0;
  Uint64 latest_timestamp = 0;
  SDL_Surface *latest_frame = nullptr;
  while (SDL_Surface *frame = SDL_AcquireCameraFrame(camera_, &timestamp)) {
    if (!latest_frame || timestamp >= latest_timestamp) {
      if (latest_frame) {
        SDL_ReleaseCameraFrame(camera_, latest_frame);
      }
      latest_frame = frame;
      latest_timestamp = timestamp;
    } else {
      SDL_ReleaseCameraFrame(camera_, frame);
    }
  }

  if (!latest_frame)
    return false; // no frame available yet

  // Convert the frame to RGBA8888 so we can easily extract luminance
  SDL_Surface *rgba =
      SDL_ConvertSurface(latest_frame, SDL_PIXELFORMAT_RGBA8888);
  SDL_ReleaseCameraFrame(camera_, latest_frame);

  if (!rgba)
    return false;

  int fw = rgba->w;
  int fh = rgba->h;
  const uint8_t *pixels = static_cast<const uint8_t *>(rgba->pixels);
  int pitch = rgba->pitch;

  // Downscale to (width x height) and convert to grayscale.
  for (int y = 0; y < height; y++) {
    int src_y = y * fh / height;
    if (src_y >= fh)
      src_y = fh - 1;
    const uint8_t *row = pixels + src_y * pitch;

    for (int x = 0; x < width; x++) {
      int src_x = x * fw / width;
      if (src_x >= fw)
        src_x = fw - 1;

      const uint8_t *px = row + src_x * 4;
      // RGBA8888: R=px[0], G=px[1], B=px[2]
      // BT.601 luminance
      // R: 0.299 * 2^8 = 76.544 = 77
      // G: 0.587 * 2^8 = 150.272 = 150
      // B: 0.114 * 2^8 = 29.184 = 29
      // NOTE: there is a max error of +/- 1LSB due to rounding of the
      // coefficients.
      uint8_t gray =
          static_cast<uint8_t>((px[0] * 77 + px[1] * 150 + px[2] * 29) >> 8);
      out[y * width + x] = gray;
    }
  }

  SDL_DestroySurface(rgba);
  return true;
}

} // namespace nativecore
