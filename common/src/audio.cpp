#include "nativecore/audio.h"

#include <algorithm>
#include <cstring>

namespace nativecore {

AudioManager::AudioManager() = default;
AudioManager::~AudioManager() { shutdown(); }

bool AudioManager::init(int sampleRate) {
  sample_rate_ = sampleRate;

  SDL_AudioSpec spec;
  spec.freq = sampleRate;
  spec.format = SDL_AUDIO_F32;
  spec.channels = 2;

  stream_ = SDL_OpenAudioDeviceStream(SDL_AUDIO_DEVICE_DEFAULT_PLAYBACK, &spec,
                                      nullptr, nullptr);
  if (!stream_)
    return false;

  device_id_ = SDL_GetAudioStreamDevice(stream_);
  SDL_ResumeAudioDevice(device_id_);

  return true;
}

void AudioManager::shutdown() {
  if (stream_) {
    SDL_DestroyAudioStream(stream_);
    stream_ = nullptr;
    device_id_ = 0;
  }
}

size_t AudioManager::getQueuedSampleCount() const {
  if (!stream_)
    return 0;
  const int bytes = SDL_GetAudioStreamQueued(stream_);
  if (bytes <= 0)
    return 0;
  return static_cast<size_t>(bytes) / sizeof(float);
}

void AudioManager::pushSamples(const float *samples, size_t count) {
  if (!stream_ || count == 0)
    return;

  std::lock_guard<std::mutex> lock(mutex_);
  std::vector<float> mixed(count);

  for (size_t i = 0; i < count; i++) {
    mixed[i] = samples[i] * master_volume_;
  }
  SDL_PutAudioStreamData(stream_, mixed.data(),
                         static_cast<int>(count * sizeof(float)));
}

void AudioManager::setMasterVolume(float volume) {
  std::lock_guard<std::mutex> lock(mutex_);
  master_volume_ = std::clamp(volume, 0.0f, 1.0f);
}

void AudioManager::setChannelMuted(int channel, bool muted) {
  if (channel < 0 || channel >= num_channels_)
    return;
  std::lock_guard<std::mutex> lock(mutex_);
  channels_[channel].muted = muted;
}

bool AudioManager::isChannelMuted(int channel) const {
  if (channel < 0 || channel >= num_channels_)
    return false;
  std::lock_guard<std::mutex> lock(mutex_);
  return channels_[channel].muted;
}

void AudioManager::setChannelVolume(int channel, float volume) {
  if (channel < 0 || channel >= num_channels_)
    return;
  std::lock_guard<std::mutex> lock(mutex_);
  channels_[channel].volume = std::clamp(volume, 0.0f, 1.0f);
}

float AudioManager::channelVolume(int channel) const {
  if (channel < 0 || channel >= num_channels_)
    return 1.0f;
  std::lock_guard<std::mutex> lock(mutex_);
  return channels_[channel].volume;
}

void AudioManager::registerChannel(int index, const std::string &name) {
  if (index < 0 || index >= maxChannels)
    return;
  std::lock_guard<std::mutex> lock(mutex_);
  channels_[index].name = name;
  if (index >= num_channels_)
    num_channels_ = index + 1;
}
} // namespace nativecore
