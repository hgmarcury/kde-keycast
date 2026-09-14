#pragma once

#include <QString>

#include <cstdint>

namespace KDEKeyCast {

enum class InputDevice {
    Keyboard,
    Mouse
};

enum class InputAction {
    Pressed,
    Released,
    Wheel
};

enum class MouseAxis {
    None,
    Vertical,
    Horizontal
};

struct InputEvent {
    InputDevice device = InputDevice::Keyboard;
    InputAction action = InputAction::Pressed;

    // Linux/XKB information when available.
    int linuxCode = -1;
    QString keyName;
    QString text;

    // Mouse information.
    MouseAxis mouseAxis = MouseAxis::None;
    double wheelDelta = 0.0;

    // Wayland timestamp when available.
    std::uint32_t timestamp = 0;

    bool isKeyboard() const noexcept
    {
        return device == InputDevice::Keyboard;
    }

    bool isMouse() const noexcept
    {
        return device == InputDevice::Mouse;
    }

    bool isWheel() const noexcept
    {
        return device == InputDevice::Mouse &&
               action == InputAction::Wheel;
    }
};

} // namespace KDEKeyCast
