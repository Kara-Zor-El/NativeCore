#include "nativecore/input.h"

#include <algorithm>

namespace nativecore {

InputManager::InputManager() = default;
InputManager::~InputManager() { shutdown(); }

void InputManager::init() {
  int count = 0;
  SDL_JoystickID *ids = SDL_GetGamepads(&count);
  if (ids && count > 0) {
    for (int i = 0; i < count; i++) {
      SDL_Gamepad *gp = SDL_OpenGamepad(ids[i]);
      if (gp) {
        gamepads_.push_back(gp);
        gamepad_instance_ids_.push_back(ids[i]);
      }
    }
    SDL_free(ids);
  }
}

void InputManager::shutdown() {
  for (SDL_Gamepad *gp : gamepads_) {
    SDL_CloseGamepad(gp);
  }
  gamepads_.clear();
  gamepad_instance_ids_.clear();
}

void InputManager::pollEvents(std::vector<SDL_Event> &events_out) {
  SDL_Event event;
  while (SDL_PollEvent(&event)) {
    events_out.push_back(event);
  }
}

void InputManager::setProfile(const InputProfile &profile) {
  profile_ = profile;
  if (profile_.max_controllers < 1)
    profile_.max_controllers = 1;
  if (profile_.max_controllers > kMaxSupportedPlayers)
    profile_.max_controllers = kMaxSupportedPlayers;
}

uint32_t InputManager::controllerState(int player_index) const {
  if (player_index < 0 || player_index >= profile_.max_controllers ||
      player_index >= kMaxSupportedPlayers)
    return 0;
  return button_state_[player_index];
}

bool InputManager::isActionPressed(const std::string &action_name,
                                   int player_index) const {
  if (player_index < 0 || player_index >= profile_.max_controllers ||
      player_index >= kMaxSupportedPlayers)
    return false;
  const std::vector<int> indices = findBindingIndices(action_name);
  const uint32_t state = button_state_[player_index];
  for (int i : indices) {
    if (profile_.bindings[static_cast<size_t>(i)].controller_index ==
            player_index &&
        (state & (1u << i)) != 0)
      return true;
  }
  return false;
}

void InputManager::startRebind(const std::string &action_name) {
  rebinding_ = true;
  rebind_action_ = action_name;
}

void InputManager::cancelRebind() {
  rebinding_ = false;
  rebind_action_.clear();
}

void InputManager::processEvent(const SDL_Event &event) {
  if (rebinding_) {
    handleRebindEvent(event);
    return;
  }

  updateButtonState(event);

  if (event.type == SDL_EVENT_GAMEPAD_ADDED) {
    const SDL_JoystickID id = event.gdevice.which;
    SDL_Gamepad *gp = SDL_OpenGamepad(id);
    if (gp) {
      gamepads_.push_back(gp);
      gamepad_instance_ids_.push_back(id);
    }
  } else if (event.type == SDL_EVENT_GAMEPAD_REMOVED) {
    const SDL_JoystickID id = event.gdevice.which;
    auto it = std::find(gamepad_instance_ids_.begin(),
                        gamepad_instance_ids_.end(), id);
    if (it != gamepad_instance_ids_.end()) {
      size_t idx = static_cast<size_t>(it - gamepad_instance_ids_.begin());
      SDL_CloseGamepad(gamepads_[idx]);
      gamepads_.erase(gamepads_.begin() + idx);
      gamepad_instance_ids_.erase(it);
    }
  }
}

void InputManager::updateButtonState(const SDL_Event &event) {
  if (event.type == SDL_EVENT_KEY_DOWN || event.type == SDL_EVENT_KEY_UP) {
    bool pressed = (event.type == SDL_EVENT_KEY_DOWN);
    for (int i = 0; i < static_cast<int>(profile_.bindings.size()); i++) {
      const auto &binding = profile_.bindings[i];
      if (binding.controller_index >= profile_.max_controllers)
        continue;
      if (binding.key == event.key.scancode) {
        if (pressed) {
          button_state_[binding.controller_index] |= (1u << i);
        } else {
          button_state_[binding.controller_index] &= ~(1u << i);
        }
      }
    }
  }

  if (event.type == SDL_EVENT_GAMEPAD_BUTTON_DOWN ||
      event.type == SDL_EVENT_GAMEPAD_BUTTON_UP) {
    const SDL_JoystickID which = event.gbutton.which;
    const int player_index = instanceIdToPlayerIndex(which);
    if (player_index < 0 || player_index >= profile_.max_controllers)
      return;
    bool pressed = (event.type == SDL_EVENT_GAMEPAD_BUTTON_DOWN);
    const Uint8 button = event.gbutton.button;
    for (size_t i = 0; i < profile_.bindings.size(); i++) {
      const auto &binding = profile_.bindings[i];
      if (binding.controller_index != player_index)
        continue;
      if (binding.pad_button == static_cast<SDL_GamepadButton>(button)) {
        if (pressed) {
          button_state_[player_index] |= (1u << i);
        } else {
          button_state_[player_index] &= ~(1u << i);
        }
      }
    }
  }
}

void InputManager::handleRebindEvent(const SDL_Event &event) {
  int index = findBindingIndex(rebind_action_);

  if (index < 0) {
    cancelRebind();
    return;
  }

  if (event.type == SDL_EVENT_KEY_DOWN) {
    if (event.key.scancode == SDL_SCANCODE_ESCAPE) {
      cancelRebind();
      return;
    }

    profile_.bindings[index].key = event.key.scancode;
    InputBinding bound = profile_.bindings[index];
    if (rebind_callback_) {
      rebind_callback_(rebind_action_, bound);
    }
    rebinding_ = false;
    rebind_action_.clear();
  } else if (event.type == SDL_EVENT_GAMEPAD_BUTTON_DOWN) {
    profile_.bindings[index].pad_button =
        static_cast<SDL_GamepadButton>(event.gbutton.button);
    InputBinding bound = profile_.bindings[index];
    rebinding_ = false;
    if (rebind_callback_)
      rebind_callback_(rebind_action_, bound);
    rebind_action_.clear();
  }
}

int InputManager::instanceIdToPlayerIndex(SDL_JoystickID instance_id) const {
  for (size_t i = 0; i < gamepad_instance_ids_.size(); i++) {
    if (gamepad_instance_ids_[i] == instance_id)
      return static_cast<int>(i);
  }
  return -1;
}

int InputManager::findBindingIndex(const std::string &action_name) const {
  for (size_t i = 0; i < profile_.bindings.size(); i++) {
    if (profile_.bindings[i].action_name == action_name)
      return static_cast<int>(i);
  }
  return -1;
}

std::vector<int>
InputManager::findBindingIndices(const std::string &action_name) const {
  std::vector<int> indices;
  for (size_t i = 0; i < profile_.bindings.size(); i++) {
    if (profile_.bindings[i].action_name == action_name)
      indices.push_back(static_cast<int>(i));
  }
  return indices;
}

InputProfile InputManager::defaultGameBoyProfile() {
  InputProfile profile;
  profile.name = "Game Boy Default";
  profile.max_controllers = 1;
  const int p = 0;
  profile.bindings = {
      {"A", p, SDL_SCANCODE_Z, SDL_GAMEPAD_BUTTON_EAST},
      {"B", p, SDL_SCANCODE_X, SDL_GAMEPAD_BUTTON_SOUTH},
      {"Select", p, SDL_SCANCODE_RETURN, SDL_GAMEPAD_BUTTON_WEST},
      {"Start", p, SDL_SCANCODE_SPACE, SDL_GAMEPAD_BUTTON_START},
      {"Right", p, SDL_SCANCODE_RIGHT, SDL_GAMEPAD_BUTTON_DPAD_RIGHT},
      {"Left", p, SDL_SCANCODE_LEFT, SDL_GAMEPAD_BUTTON_DPAD_LEFT},
      {"Up", p, SDL_SCANCODE_UP, SDL_GAMEPAD_BUTTON_DPAD_UP},
      {"Down", p, SDL_SCANCODE_DOWN, SDL_GAMEPAD_BUTTON_DPAD_DOWN},
  };
  return profile;
}
} // namespace nativecore