#pragma once

#include <QString>
#include <functional>

#include <wayland-client.h>

class WaylandPointer
{
public:
    WaylandPointer();
    ~WaylandPointer();

    bool start(std::function<void(const QString &, bool)> callback);
    void dispatch();
    void stop();

    int socketFd() const;

private:
    static void registryGlobal(
        void *data,
        wl_registry *registry,
        uint32_t name,
        const char *interface,
        uint32_t version);

    static void registryGlobalRemove(
        void *,
        wl_registry *,
        uint32_t);

    static void pointerEnter(
        void *,
        wl_pointer *,
        uint32_t,
        wl_surface *,
        wl_fixed_t,
        wl_fixed_t);

    static void pointerLeave(
        void *,
        wl_pointer *,
        uint32_t,
        wl_surface *);

    static void pointerMotion(
        void *,
        wl_pointer *,
        uint32_t,
        wl_fixed_t,
        wl_fixed_t);

    static void pointerButton(
        void *data,
        wl_pointer *,
        uint32_t,
        uint32_t,
        uint32_t button,
        uint32_t state);

    static void pointerAxis(
        void *data,
        wl_pointer *,
        uint32_t,
        uint32_t axis,
        wl_fixed_t value);

    static void pointerFrame(
        void *,
        wl_pointer *);

    static void pointerAxisSource(
        void *,
        wl_pointer *,
        uint32_t);

    static void pointerAxisStop(
        void *,
        wl_pointer *,
        uint32_t,
        uint32_t);

    static void pointerAxisDiscrete(
        void *,
        wl_pointer *,
        uint32_t,
        int32_t);

    static void pointerAxisValue120(
        void *,
        wl_pointer *,
        uint32_t,
        int32_t);

    static void pointerAxisRelativeDirection(
        void *,
        wl_pointer *,
        uint32_t,
        uint32_t);

    wl_display *display = nullptr;
    wl_registry *registry = nullptr;
    wl_seat *seat = nullptr;
    wl_pointer *pointer = nullptr;

    int fd = -1;

    std::function<void(const QString &, bool)> onButton;
};
