#pragma once

#include <string>
#include <yaml-cpp/yaml.h>

namespace nativecore {

class ConfigManager {
public:
  ConfigManager();

  bool load(const std::string &path);
  bool save(const std::string &path) const;
  bool save() const;

  void setDefaults();

  float masterVolume() const;
  void setMasterVolume(float vol);

  bool channelMuted(int channel) const;
  void setChannelMuted(int channel, bool muted);

  YAML::Node inputBindings() const;
  void setInputBindings(const YAML::Node &bindings);

  int windowScale() const;
  void setWindowScale(int scale);

  std::string scaleMode() const;
  void setScaleMode(const std::string &mode);

  bool fullscreen() const;
  void setFullscreen(bool fullscreen);

  double fpsLimit() const;
  void setFpsLimit(double limit);

  bool uncappedFps() const;
  void setUncappedFps(bool uncapped);

  const YAML::Node &raw() const { return config_; }

private:
  YAML::Node config_;
  std::string file_path_;

  void autoSave();
};
} // namespace nativecore
