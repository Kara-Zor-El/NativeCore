#pragma once

#include "nativecore/core.h"

#include <chrono>
#include <string>
#include <string_view>
#include <vector>

namespace nativecore {

struct SaveStateEntry {
  std::string id; // folder name / unique id
  std::string display_name;
  std::string created_at;   // ISO 8601
  std::string preview_path; // path to preview.png
  std::chrono::system_clock::time_point created_at_tp;
};

class SaveStateManager {
public:
  SaveStateManager();
  explicit SaveStateManager(const std::string &data_directory);

  void setDataDirectory(const std::string &path) { data_directory_ = path; }
  const std::string &dataDirectory() const { return data_directory_; }

  void setGame(const std::string &system_name, const std::string &rom_path);

  std::vector<SaveStateEntry> listSaves() const;

  /**
   * Save state: write state.sav, preview.png, meta.yaml.
   * If display_name is empty, generates "Save at <time>"
   * Enforces max_save_states by deleting oldest
   */
  std::string save(Core *core, const uint32_t *framebuffer, int width,
                   int height, std::string display_name = "",
                   int max_save_states = 0);

  /**
   * Save to a fixed slot id (e.g. "quick"). Overwrites if present.
   */
  std::string saveToSlot(Core *core, const uint32_t *framebuffer, int width,
                         int height, const std::string &slot_id,
                         const std::string &display_name = "");

  bool load(Core *core, const std::string &save_id);

  void deleteSave(const std::string &save_id);
  bool renameSave(const std::string &save_id,
                  const std::string &new_display_name);

  static constexpr std::string_view QUICK_SAVE_ID = "quick";

private:
  std::string data_directory_;
  std::string system_name_;
  std::string game_id_;

  std::string gameSaveStateDirectory() const;
  std::string makeGameId(const std::string &rom_path) const;
  std::string nextSlotDirectory() const;
  bool writeMeta(const std::string &slot_dir, const std::string &display_name,
                 const std::string &created_at_iso);
  bool writePreview(const std::string &path, const uint32_t *framebuffer,
                    int width, int height);
};

} // namespace nativecore
