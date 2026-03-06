#pragma once

#include <SDL3/SDL.h>
#include <SDL3/SDL_gamepad.h>
#include <cstdint>
#include <functional>
#include <string>
#include <vector>

namespace nativecore {

constexpr int kMaxSupportedPlayers = 8;

struct InputBinding {
  std::string action_name;
  int controller_index;
  SDL_Scancode key = SDL_SCANCODE_UNKNOWN;
  SDL_GamepadButton pad_button = SDL_GAMEPAD_BUTTON_INVALID;
  int pad_axis = -1;
  float axis_threshold = 0.5f;
};

struct InputProfile {
  std::string name;
  int max_controllers = 1;
  std::vector<InputBinding> bindings;
};

class InputManager {
public:
  InputManager();
  ~InputManager();

  void init();
  void shutdown();

  void pollEvents(std::vector<SDL_Event> &events_out);
  void setProfile(const InputProfile &profile);
  const InputProfile &currentProfile() const { return profile_; }

  uint32_t controllerState(int player_index) const;

  bool isActionPressed(const std::string &action_name,
                       int player_index = 0) const;

  int maxControllers() const { return profile_.max_controllers; }

  int connectedControllerCount() const {
    return static_cast<int>(gamepads_.size());
  }

  void startRebind(const std::string &action_name);
  bool isRebinding() const { return rebinding_; }
  const std::string &rebindAction() const { return rebind_action_; }
  void cancelRebind();

  void processEvent(const SDL_Event &event);

  using RebindCallback =
      std::function<void(const std::string &, const InputBinding &)>;
  void setRebindCallback(RebindCallback cb) {
    rebind_callback_ = std::move(cb);
  }

  SDL_Gamepad *gamepad(int index) const {
    if (index < 0 || index >= static_cast<int>(gamepads_.size()))
      return nullptr;
    return gamepads_[index];
  }

  static InputProfile defaultGameBoyProfile();

private:
  InputProfile profile_;
  std::vector<SDL_Gamepad *> gamepads_;
  std::vector<SDL_JoystickID> gamepad_instance_ids_;

  uint32_t button_state_[kMaxSupportedPlayers] = {};

  bool rebinding_ = false;
  std::string rebind_action_;
  RebindCallback rebind_callback_;

  void updateButtonState(const SDL_Event &event);
  void handleRebindEvent(const SDL_Event &event);

  int findBindingIndex(const std::string &action_name) const;
  std::vector<int> findBindingIndices(const std::string &action_name) const;
  int instanceIdToPlayerIndex(SDL_JoystickID instance_id) const;
};

} // namespace nativecore
