#include "nativecore/save_state.h"

#include <SDL3/SDL_surface.h>

#include <yaml-cpp/yaml.h>

#include <algorithm>
#include <chrono>
#include <cstdint>
#include <cstring>
#include <ctime>
#include <filesystem>
#include <fstream>
#include <iomanip>
#include <sstream>

namespace nativecore {

namespace fs = std::filesystem;

SaveStateManager::SaveStateManager() = default;

SaveStateManager::SaveStateManager(const std::string &data_directory)
    : data_directory_(data_directory) {}

std::string SaveStateManager::makeGameId(const std::string &rom_path) const {
  fs::path p(rom_path);
  std::string abs;
  try {
    abs = fs::absolute(p).string();
  } catch (...) {
    abs = rom_path;
  }
  // Simple stable hash (djb2)
  // See: https://theartincode.stanis.me/008-djb2/ for more information.
  uint32_t h = 5381;
  for (unsigned char c : abs)
    h = ((h << 5) + h) + c;
  std::ostringstream os;
  os << std::hex << h;
  return os.str();
}

void SaveStateManager::setGame(const std::string &system_name,
                               const std::string &rom_path) {
  system_name_ = system_name;
  game_id_ = makeGameId(rom_path);
}

std::string SaveStateManager::gameSaveStateDirectory() const {
  if (data_directory_.empty() || system_name_.empty() || game_id_.empty())
    return "";
  fs::path p(data_directory_);
  p /= "save_states";
  p /= system_name_;
  p /= game_id_;
  return p.string();
}

std::string SaveStateManager::nextSlotDirectory() const {
  std::string base = gameSaveStateDirectory();
  if (base.empty())
    return "";
  fs::create_directories(base);
  auto now = std::chrono::system_clock::now();
  auto us = std::chrono::duration_cast<std::chrono::microseconds>(
                now.time_since_epoch())
                .count();
  std::ostringstream os;
  os << "slot_" << us;
  fs::path slot(base);
  slot /= os.str();
  return slot.string();
}

bool SaveStateManager::writeMeta(const std::string &slot_dir,
                                 const std::string &display_name,
                                 const std::string &created_at_iso) {
  fs::path meta(slot_dir);
  meta /= "meta.yaml";
  try {
    YAML::Emitter out;
    out << YAML::BeginMap;
    out << YAML::Key << "display_name" << YAML::Value << display_name;
    out << YAML::Key << "created_at" << YAML::Value << created_at_iso;
    out << YAML::EndMap;
    std::ofstream f(meta.string());
    if (!f)
      return false;
    f << out.c_str();
    return true;
  } catch (...) {
    return false;
  }
}

bool SaveStateManager::writePreview(const std::string &path,
                                    const uint32_t *framebuffer, int width,
                                    int height) {
  if (!framebuffer || width <= 0 || height <= 0)
    return false;
  const int pitch = width * 4;
  SDL_Surface *surface = SDL_CreateSurfaceFrom(
      width, height, SDL_PIXELFORMAT_ARGB8888,
      const_cast<void *>(static_cast<const void *>(framebuffer)), pitch);
  if (!surface)
    return false;
  const bool ok = SDL_SavePNG(surface, path.c_str());
  SDL_DestroySurface(surface);
  return ok;
}

std::vector<SaveStateEntry> SaveStateManager::listSaves() const {
  std::vector<SaveStateEntry> entries;
  std::string base = gameSaveStateDirectory();
  if (base.empty() || !fs::exists(base))
    return entries;

  for (const auto &entry : fs::directory_iterator(base)) {
    if (!entry.is_directory())
      continue;
    std::string id = entry.path().filename().string();
    fs::path meta_path = entry.path() / "meta.yaml";
    fs::path preview_path = entry.path() / "preview.png";
    if (!fs::exists(meta_path))
      continue;
    try {
      YAML::Node node = YAML::LoadFile(meta_path.string());
      SaveStateEntry e;
      e.id = id;
      e.display_name = node["display_name"].as<std::string>("");
      e.created_at = node["created_at"].as<std::string>("");
      e.preview_path = fs::exists(preview_path) ? preview_path.string() : "";
      // Parse ISO time for sorting
      std::tm tm = {};
      std::istringstream is(e.created_at);
      is >> std::get_time(&tm, "%Y-%m-%dT%H:%M:%S");
      if (!is.fail())
        e.created_at_tp =
            std::chrono::system_clock::from_time_t(std::mktime(&tm));
      else
        e.created_at_tp = std::chrono::system_clock::time_point{};
      entries.push_back(e);
    } catch (...) {
      continue;
    }
  }
  std::sort(entries.begin(), entries.end(),
            [](const SaveStateEntry &a, const SaveStateEntry &b) {
              return a.created_at_tp > b.created_at_tp;
            });
  return entries;
}

std::string SaveStateManager::save(Core *core, const uint32_t *framebuffer,
                                   int width, int height,
                                   std::string display_name,
                                   int max_save_states) {
  if (!core || gameSaveStateDirectory().empty())
    return "";
  std::vector<uint8_t> state;
  if (!core->saveState(state))
    return "";

  std::string slot_dir = nextSlotDirectory();
  if (slot_dir.empty())
    return "";

  fs::create_directories(slot_dir);

  auto now = std::chrono::system_clock::now();
  std::time_t t = std::chrono::system_clock::to_time_t(now);
  std::tm *tm = std::localtime(&t);
  std::ostringstream iso;
  iso << std::put_time(tm, "%Y-%m-%dT%H:%M:%S");
  std::string created_at_iso = iso.str();

  if (display_name.empty())
    display_name = "Save at " + created_at_iso;

  fs::path state_path(slot_dir);
  state_path /= "state.sav";
  {
    std::ofstream f(state_path.string(), std::ios::binary);
    if (!f ||
        !f.write(reinterpret_cast<const char *>(state.data()), state.size()))
      return "";
  }

  fs::path preview_path(slot_dir);
  preview_path /= "preview.png";
  if (framebuffer && width > 0 && height > 0)
    writePreview(preview_path.string(), framebuffer, width, height);

  if (!writeMeta(slot_dir, display_name, created_at_iso))
    return "";

  std::string save_id = fs::path(slot_dir).filename().string();

  if (max_save_states > 0) {
    std::vector<SaveStateEntry> all = listSaves();
    while (static_cast<int>(all.size()) > max_save_states) {
      deleteSave(all.back().id);
      all = listSaves();
    }
  }

  return save_id;
}

std::string SaveStateManager::saveToSlot(Core *core,
                                         const uint32_t *framebuffer, int width,
                                         int height, const std::string &slot_id,
                                         const std::string &display_name) {
  if (!core || gameSaveStateDirectory().empty() || slot_id.empty())
    return "";
  std::vector<uint8_t> state;
  if (!core->saveState(state))
    return "";

  fs::path slot_dir(gameSaveStateDirectory());
  slot_dir /= slot_id;
  fs::create_directories(slot_dir);
  std::string slot_str = slot_dir.string();

  auto now = std::chrono::system_clock::now();
  std::time_t t = std::chrono::system_clock::to_time_t(now);
  std::tm *tm = std::localtime(&t);
  std::ostringstream iso;
  iso << std::put_time(tm, "%Y-%m-%dT%H:%M:%S");
  std::string created_at_iso = iso.str();
  std::string name = display_name.empty() ? "Quick save" : display_name;

  fs::path state_path(slot_dir);
  state_path /= "state.sav";
  {
    std::ofstream f(state_path.string(), std::ios::binary);
    if (!f ||
        !f.write(reinterpret_cast<const char *>(state.data()), state.size()))
      return "";
  }
  fs::path preview_path(slot_dir);
  preview_path /= "preview.png";
  if (framebuffer && width > 0 && height > 0)
    writePreview(preview_path.string(), framebuffer, width, height);
  if (!writeMeta(slot_str, name, created_at_iso))
    return "";
  return slot_id;
}

bool SaveStateManager::load(Core *core, const std::string &save_id) {
  if (!core || save_id.empty())
    return false;
  std::string base = gameSaveStateDirectory();
  if (base.empty())
    return false;
  fs::path slot(base);
  slot /= save_id;
  fs::path state_file = slot / "state.sav";
  if (!fs::exists(state_file))
    return false;
  std::ifstream f(state_file.string(), std::ios::binary);
  if (!f)
    return false;
  std::vector<uint8_t> data((std::istreambuf_iterator<char>(f)),
                            std::istreambuf_iterator<char>());
  f.close();
  if (data.empty())
    return false;
  return core->loadState(data.data(), data.size());
}

void SaveStateManager::deleteSave(const std::string &save_id) {
  std::string base = gameSaveStateDirectory();
  if (base.empty() || save_id.empty())
    return;
  fs::path slot(base);
  slot /= save_id;
  try {
    fs::remove_all(slot);
  } catch (...) {
  }
}

bool SaveStateManager::renameSave(const std::string &save_id,
                                  const std::string &new_display_name) {
  std::string base = gameSaveStateDirectory();
  if (base.empty() || save_id.empty())
    return false;
  fs::path meta(base);
  meta /= save_id;
  meta /= "meta.yaml";
  if (!fs::exists(meta))
    return false;
  try {
    YAML::Node node = YAML::LoadFile(meta.string());
    node["display_name"] = new_display_name;
    std::ofstream f(meta.string());
    if (!f)
      return false;
    YAML::Emitter out;
    out << node;
    f << out.c_str();
    return true;
  } catch (...) {
    return false;
  }
}

} // namespace nativecore
