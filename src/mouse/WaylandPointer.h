#pragma once

#include "../input/InputEvent.h"

#include <QDebug>

#include <wayland-client.h>

#include <functional>
#include <cstdint>

namespace KDEKeyCast {

class WaylandPointer
{
public:
    using EventHandler =
        std::function<void(const InputEvent &)>;

    WaylandPointer() = default;

    WaylandPointer(const WaylandPointer &) = delete;
    WaylandPointer &operator=(const WaylandPointer &) = delete;

    ~WaylandPointer()
    {
        stop();
    }

    bool start(
        wl_seat *seat,
        EventHandler handler)
    {
        stop();

        if (!seat || !handler)
            return false;

        handler_ = std::move(handler);

        pointer_ = wl_seat_get_pointer(seat);

        if (!pointer_) {
            handler_ = {};
            return false;
        }

        wl_pointer_add_listener(
            pointer_,
            &pointerListener_,
            this);

        return true;
    }

    void stop()
    {
        if (pointer_) {
            wl_pointer_destroy(pointer_);
            pointer_ = nullptr;
        }

        handler_ = {};
    }

    bool isActive() const noexcept
    {
        return pointer_ != nullptr;
    }

private:
    static void enter(
        void *,
        wl_pointer *,
        uint32_t,
        wl_surface *,
        wl_fixed_t,
        wl_fixed_t)
    {
    }

    static void leave(
        void *,
        wl_pointer *,
        uint32_t,
        wl_surface *)
    {
    }

    static void motion(
        void *,
        wl_pointer *,
        uint32_t,
        wl_fixed_t,
        wl_fixed_t)
    {
    }

    static void button(
        void *data,
        wl_pointer *,
        uint32_t serial,
        uint32_t time,
        uint32_t button,
        uint32_t state)
    {
        auto *self =
            static_cast<WaylandPointer *>(data);

        if (!self || !self->handler_)
            return;

        InputEvent event;

        event.device = InputDevice::Mouse;
        event.action =
            state == WL_POINTER_BUTTON_STATE_PRESSED
                ? InputAction::Pressed
                : InputAction::Released;

        event.linuxCode =
            static_cast<int>(button);

        event.keyName =
            MouseInputName(button);

        event.timestamp = time;

        self->handler_(event);

        Q_UNUSED(serial);
    }

    static void axis(
        void *data,
        wl_pointer *,
        uint32_t time,
        uint32_t axis,
        wl_fixed_t value)
    {
        auto *self =
            static_cast<WaylandPointer *>(data);

        if (!self || !self->handler_)
            return;

        const double delta =
            wl_fixed_to_double(value);

        if (delta == 0.0)
            return;

        InputEvent event;

        event.device = InputDevice::Mouse;
        event.action = InputAction::Wheel;

        event.mouseAxis =
            axis == WL_POINTER_AXIS_HORIZONTAL_SCROLL
                ? MouseAxis::Horizontal
                : MouseAxis::Vertical;

        event.wheelDelta = delta;
        event.timestamp = time;

        event.text =
            axis == WL_POINTER_AXIS_HORIZONTAL_SCROLL
                ? (delta < 0.0
                    ? QStringLiteral("Wheel ←")
                    : QStringLiteral("Wheel →"))
                : (delta < 0.0
                    ? QStringLiteral("Wheel ↓")
                    : QStringLiteral("Wheel ↑"));

        self->handler_(event);
    }

    static void frame(
        void *,
        wl_pointer *)
    {
    }

    static void axisSource(
        void *,
        wl_pointer *,
        uint32_t)
    {
    }

    static void axisStop(
        void *,
        wl_pointer *,
        uint32_t,
        uint32_t)
    {
    }

    static void axisDiscrete(
        void *,
        wl_pointer *,
        uint32_t,
        int32_t)
    {
    }

    static QString MouseInputName(
        uint32_t button)
    {
        switch (button) {
        case 0x110:
            return QStringLiteral("BTN_LEFT");

        case 0x111:
            return QStringLiteral("BTN_RIGHT");

        case 0x112:
            return QStringLiteral("BTN_MIDDLE");

        case 0x113:
            return QStringLiteral("BTN_SIDE");

        case 0x114:
            return QStringLiteral("BTN_EXTRA");

        default:
            return QStringLiteral("BTN_%1")
                .arg(button);
        }
    }

    wl_pointer *pointer_ = nullptr;

    EventHandler handler_;

    static inline const wl_pointer_listener
        pointerListener_{
            &enter,
            &leave,
            &motion,
            &button,
            &axis,
            &frame,
            &axisSource,
            &axisStop,
            &axisDiscrete
        };
};

} // namespace KDEKeyCast
