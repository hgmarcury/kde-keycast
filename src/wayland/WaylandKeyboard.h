#pragma once

#include <functional>

#include <wayland-client.h>

class WaylandKeyboard
{
public:
    using KeymapCallback =
        std::function<void(uint32_t format, int fd, uint32_t size)>;

    using ModifiersCallback =
        std::function<void(uint32_t modsDepressed,
                           uint32_t modsLatched,
                           uint32_t modsLocked,
                           uint32_t group)>;

    static bool isXkbV1Keymap(uint32_t format);

    WaylandKeyboard();
    ~WaylandKeyboard();

    WaylandKeyboard(const WaylandKeyboard &) = delete;
    WaylandKeyboard &operator=(const WaylandKeyboard &) = delete;

    bool start(wl_seat *seat);
    void stop();

    wl_keyboard *handle() const;

    void setKeymapCallback(KeymapCallback callback);
    void setModifiersCallback(ModifiersCallback callback);

private:
    static void keyboardKeymap(
        void *data,
        wl_keyboard *keyboard,
        uint32_t format,
        int fd,
        uint32_t size
    );

    static void keyboardEnter(
        void *data,
        wl_keyboard *keyboard,
        uint32_t serial,
        wl_surface *surface,
        wl_array *keys
    );

    static void keyboardLeave(
        void *data,
        wl_keyboard *keyboard,
        uint32_t serial,
        wl_surface *surface
    );

    static void keyboardKey(
        void *data,
        wl_keyboard *keyboard,
        uint32_t serial,
        uint32_t time,
        uint32_t key,
        uint32_t state
    );

    static void keyboardModifiers(
        void *data,
        wl_keyboard *keyboard,
        uint32_t serial,
        uint32_t modsDepressed,
        uint32_t modsLatched,
        uint32_t modsLocked,
        uint32_t group
    );

    static void keyboardRepeatInfo(
        void *data,
        wl_keyboard *keyboard,
        int32_t rate,
        int32_t delay
    );

    static const wl_keyboard_listener listener;

    wl_keyboard *keyboard = nullptr;

    KeymapCallback keymapCallback;
    ModifiersCallback modifiersCallback;
};
