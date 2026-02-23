#pragma once

#include <mutex>
#include <string>
#include <vector>

#include <SDL3/SDL.h>

namespace nativecore {
struct AudioChannelState {
  std::string name;
  bool muted = false;
  float volume = 1.0f;
};

class AudioManager {
public:
  static constexpr int maxChannels = 32;
  static constexpr int defaultSampleRate = 44100; // 44.1kHz

  AudioManager();
  ~AudioManager();

  bool init(int sampleRate = defaultSampleRate);
  void shutdown();

  void pushSamples(const float *samples, size_t count);

  size_t getQueuedSampleCount() const;

  void setMasterVolume(float volume);
  float masterVolume() const { return master_volume_; }

  void setChannelMuted(int channel, bool muted);
  bool isChannelMuted(int channel) const;

  void setChannelVolume(int channel, float volume);
  float channelVolume(int channel) const;

  void registerChannel(int index, const std::string &name);
  int channelCount() const { return num_channels_; }
  const AudioChannelState &channelState(int index) const {
    return channels_[index];
  }

private:
  SDL_AudioStream *stream_ = nullptr;
  SDL_AudioDeviceID device_id_ = 0;

  float master_volume_ = 1.0f;
  int sample_rate_ = defaultSampleRate;
  int num_channels_ = 0;
  AudioChannelState channels_[maxChannels];

  mutable std::mutex mutex_;
};
} // namespace nativecore
