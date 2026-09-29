#include "WaylandKeyboard.h"

#include <unistd.h>

WaylandKeyboard::WaylandKeyboard() = default;

WaylandKeyboard::~WaylandKeyboard()
{
    stop();
}

bool WaylandKeyboard::start(wl_seat *seat)
{
    if (!seat)
        return false;

    if (keyboard)
        return true;

    keyboard = wl_seat_get_keyboard(seat);

    if (!keyboard)
        return false;

    if (wl_keyboard_add_listener(
            keyboard,
            &listener,
            this) < 0) {

        wl_keyboard_destroy(keyboard);
        keyboard = nullptr;
        return false;
    }

    return true;
}

void WaylandKeyboard::stop()
{
    if (!keyboard)
        return;

    wl_keyboard_destroy(keyboard);
    keyboard = nullptr;
}

wl_keyboard *WaylandKeyboard::handle() const
{
    return keyboard;
}

void WaylandKeyboard::setKeymapCallback(
    KeymapCallback callback)
{
    keymapCallback = std::move(callback);
}

void WaylandKeyboard::setModifiersCallback(
    ModifiersCallback callback)
{
    modifiersCallback = std::move(callback);
}

void WaylandKeyboard::keyboardKeymap(
    void *data,
    wl_keyboard *,
    uint32_t format,
    int fd,
    uint32_t size)
{
    auto *self =
        static_cast<WaylandKeyboard *>(data);

    if (self->keymapCallback) {
        self->keymapCallback(
            format,
            fd,
            size
        );
    } else {
        ::close(fd);
    }
}

void WaylandKeyboard::keyboardEnter(
    void *,
    wl_keyboard *,
    uint32_t,
    wl_surface *,
    wl_array *)
{
}

void WaylandKeyboard::keyboardLeave(
    void *,
    wl_keyboard *,
    uint32_t,
    wl_surface *)
{
}

void WaylandKeyboard::keyboardKey(
    void *,
    wl_keyboard *,
    uint32_t,
    uint32_t,
    uint32_t,
    uint32_t)
{
}

void WaylandKeyboard::keyboardModifiers(
    void *data,
    wl_keyboard *,
    uint32_t,
    uint32_t modsDepressed,
    uint32_t modsLatched,
    uint32_t modsLocked,
    uint32_t group)
{
    auto *self =
        static_cast<WaylandKeyboard *>(data);

    if (self->modifiersCallback) {
        self->modifiersCallback(
            modsDepressed,
            modsLatched,
            modsLocked,
            group
        );
    }
}

void WaylandKeyboard::keyboardRepeatInfo(
    void *,
    wl_keyboard *,
    int32_t,
    int32_t)
{
}

const wl_keyboard_listener
WaylandKeyboard::listener = {
    WaylandKeyboard::keyboardKeymap,
    WaylandKeyboard::keyboardEnter,
    WaylandKeyboard::keyboardLeave,
    WaylandKeyboard::keyboardKey,
    WaylandKeyboard::keyboardModifiers,
    WaylandKeyboard::keyboardRepeatInfo
};
