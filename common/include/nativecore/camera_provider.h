#pragma once

#include <SDL3/SDL.h>

namespace nativecore {

class CameraProvider {
public:
  CameraProvider() = default;
  virtual ~CameraProvider();

  virtual bool open();
  virtual void close();
  virtual bool isOpen() const;

  /**
   * Capture a grayscale camera frame.
   *
   * @param out      Destination buffer (width * height bytes, 0=black,
   * 255=white)
   * @param width    Desired output width
   * @param height   Desired output height
   * @return true if a frame was captured, false if unavailable
   * NOTE: This should probably be turned into color and then have the requester
   * modify the frame.
   */
  virtual bool captureFrame(uint8_t *out, int width, int height);

private:
  SDL_Camera *camera_ = nullptr;
  int src_width_ = 0;
  int src_height_ = 0;
};

} // namespace nativecore
