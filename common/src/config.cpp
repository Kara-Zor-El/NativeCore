#include "nativecore/config.h"

#include <yaml-cpp/yaml.h>

#include <fstream>

namespace nativecore {

namespace {

template <typename T>
T getValue(const YAML::Node &node, const T &defaultValue) {
  if (!node.IsDefined() || node.IsNull())
    return defaultValue;
  return node.as<T>(defaultValue);
}

} // namespace

ConfigManager::ConfigManager() { setDefaults(); }

bool ConfigManager::load(const std::string &path) {
  file_path_ = path;
  std::ifstream file(path);
  if (!file.is_open())
    return false;
  try {
    config_ = YAML::Load(file);
    if (!config_.IsMap()) {
      setDefaults();
      return false;
    }
    return true;
  } catch (...) {
    setDefaults();
    return false;
  }
}

bool ConfigManager::save(const std::string &path) const {
  try {
    std::ofstream file(path);
    if (!file.is_open())
      return false;
    YAML::Emitter out;
    out << config_;
    file << out.c_str();
    return true;
  } catch (...) {
    return false;
  }
}

bool ConfigManager::save() const {
  if (file_path_.empty())
    return false;
  return save(file_path_);
}

void ConfigManager::setDefaults() {
  config_ = YAML::Node(YAML::NodeType::Map);
  config_["audio"]["master_volume"] = 1.0;
  config_["audio"]["muted_channels"] = YAML::Node(YAML::NodeType::Sequence);
  config_["video"]["scale"] = 3;
  config_["video"]["fullscreen"] = false;
  config_["video"]["scale_mode"] = "nearest";
  config_["video"]["maintain_aspect_ratio"] = false;
  config_["input"]["bindings"] = YAML::Node(YAML::NodeType::Map);
  config_["performance"]["fps_limit"] = 60.0;
  config_["performance"]["uncapped"] = false;
  config_["gb"]["color_palette"] = 0;
}

void ConfigManager::autoSave() {
  if (!file_path_.empty()) {
    save(file_path_);
  }
}

float ConfigManager::masterVolume() const {
  return getValue(config_["audio"]["master_volume"], 1.0f);
}

void ConfigManager::setMasterVolume(float vol) {
  config_["audio"]["master_volume"] = vol;
  autoSave();
}

bool ConfigManager::channelMuted(int channel) const {
  const YAML::Node &arr = config_["audio"]["muted_channels"];
  if (!arr.IsDefined() || !arr.IsSequence())
    return false;
  for (size_t i = 0; i < arr.size(); i++) {
    if (getValue(arr[i], -1) == channel)
      return true;
  }
  return false;
}

void ConfigManager::setChannelMuted(int channel, bool muted) {
  YAML::Node arr = config_["audio"]["muted_channels"];
  if (!arr.IsSequence())
    arr = YAML::Node(YAML::NodeType::Sequence);
  bool found = false;
  for (size_t i = 0; i < arr.size(); i++) {
    if (getValue(arr[i], -1) == channel) {
      found = true;
      if (!muted) {
        arr.remove(i);
        config_["audio"]["muted_channels"] = arr;
      }
      break;
    }
  }
  if (muted && !found) {
    arr.push_back(channel);
    config_["audio"]["muted_channels"] = arr;
  }
  autoSave();
}

YAML::Node ConfigManager::inputBindings() const {
  YAML::Node bindings = config_["input"]["bindings"];
  if (bindings.IsDefined() && bindings.IsMap())
    return bindings;
  return YAML::Node(YAML::NodeType::Map);
}

void ConfigManager::setInputBindings(const YAML::Node &bindings) {
  config_["input"]["bindings"] = bindings;
  autoSave();
}

int ConfigManager::windowScale() const {
  return getValue(config_["video"]["scale"], 3);
}

void ConfigManager::setWindowScale(int scale) {
  config_["video"]["scale"] = scale;
  autoSave();
}

bool ConfigManager::fullscreen() const {
  return getValue(config_["video"]["fullscreen"], false);
}

void ConfigManager::setFullscreen(bool fs) {
  config_["video"]["fullscreen"] = fs;
  autoSave();
}

std::string ConfigManager::scaleMode() const {
  return getValue(config_["video"]["scale_mode"], std::string("nearest"));
}

void ConfigManager::setScaleMode(const std::string &mode) {
  config_["video"]["scale_mode"] = mode;
  autoSave();
}

bool ConfigManager::maintainAspectRatio() const {
  return getValue(config_["video"]["maintain_aspect_ratio"], false);
}

void ConfigManager::setMaintainAspectRatio(bool maintain) {
  config_["video"]["maintain_aspect_ratio"] = maintain;
  autoSave();
}

double ConfigManager::fpsLimit() const {
  return getValue(config_["performance"]["fps_limit"], 60.0);
}

void ConfigManager::setFpsLimit(double fps) {
  config_["performance"]["fps_limit"] = fps;
  autoSave();
}

bool ConfigManager::uncappedFps() const {
  return getValue(config_["performance"]["uncapped"], false);
}

void ConfigManager::setUncappedFps(bool uncapped) {
  config_["performance"]["uncapped"] = uncapped;
  autoSave();
}

} // namespace nativecore
