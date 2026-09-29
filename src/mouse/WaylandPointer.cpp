#include "WaylandPointer.h"

#include <algorithm>
#include <cerrno>
#include <cstring>

#include <QDebug>

#include <linux/input-event-codes.h>

WaylandPointer::WaylandPointer() = default;

WaylandPointer::~WaylandPointer()
{
    stop();
}

bool WaylandPointer::start(
    std::function<void(const QString &, bool)> callback)
{
    if (display)
        return true;

    onButton = std::move(callback);

    display = wl_display_connect(nullptr);
    if (!display) {
        qWarning() << "WaylandPointer: no se pudo conectar a Wayland";
        return false;
    }

    registry = wl_display_get_registry(display);
    if (!registry) {
        stop();
        return false;
    }

    static const wl_registry_listener listener = {
        registryGlobal,
        registryGlobalRemove
    };

    wl_registry_add_listener(registry, &listener, this);

    if (wl_display_roundtrip(display) < 0) {
        stop();
        return false;
    }

    if (!seat) {
        qWarning() << "WaylandPointer: no se encontró wl_seat";
        stop();
        return false;
    }

    pointer = wl_seat_get_pointer(seat);
    if (!pointer) {
        qWarning() << "WaylandPointer: no se pudo obtener wl_pointer";
        stop();
        return false;
    }

    static const wl_pointer_listener pointerListener = {
        pointerEnter,
        pointerLeave,
        pointerMotion,
        pointerButton,
        pointerAxis,
        pointerFrame,
        pointerAxisSource,
        pointerAxisStop,
        pointerAxisDiscrete,
        pointerAxisValue120,
        pointerAxisRelativeDirection
    };

    wl_pointer_add_listener(pointer, &pointerListener, this);

    if (wl_display_roundtrip(display) < 0) {
        stop();
        return false;
    }

    fd = wl_display_get_fd(display);
    if (fd < 0) {
        stop();
        return false;
    }

    qDebug() << "WaylandPointer: activo";
    return true;
}

void WaylandPointer::dispatch()
{
    if (!display)
        return;

    if (wl_display_dispatch_pending(display) < 0) {
        stop();
        return;
    }

    if (wl_display_flush(display) < 0) {
        if (errno != EAGAIN)
            stop();
    }
}

void WaylandPointer::stop()
{
    if (pointer) {
        wl_pointer_destroy(pointer);
        pointer = nullptr;
    }

    if (seat) {
        wl_seat_destroy(seat);
        seat = nullptr;
    }

    if (registry) {
        wl_registry_destroy(registry);
        registry = nullptr;
    }

    if (display) {
        wl_display_disconnect(display);
        display = nullptr;
    }

    fd = -1;
}

int WaylandPointer::socketFd() const
{
    return fd;
}

void WaylandPointer::registryGlobal(
    void *data,
    wl_registry *registry,
    uint32_t name,
    const char *interface,
    uint32_t version)
{
    auto *self = static_cast<WaylandPointer *>(data);

    if (std::strcmp(interface, wl_seat_interface.name) == 0) {
        const uint32_t bindVersion = std::min(version, 9u);

        self->seat = static_cast<wl_seat *>(
            wl_registry_bind(
                registry,
                name,
                &wl_seat_interface,
                bindVersion));
    }
}

void WaylandPointer::registryGlobalRemove(
    void *,
    wl_registry *,
    uint32_t)
{
}

void WaylandPointer::pointerEnter(
    void *,
    wl_pointer *,
    uint32_t,
    wl_surface *,
    wl_fixed_t,
    wl_fixed_t)
{
}

void WaylandPointer::pointerLeave(
    void *,
    wl_pointer *,
    uint32_t,
    wl_surface *)
{
}

void WaylandPointer::pointerMotion(
    void *,
    wl_pointer *,
    uint32_t,
    wl_fixed_t,
    wl_fixed_t)
{
}

void WaylandPointer::pointerButton(
    void *data,
    wl_pointer *,
    uint32_t,
    uint32_t,
    uint32_t button,
    uint32_t state)
{
    auto *self = static_cast<WaylandPointer *>(data);

    if (!self->onButton)
        return;

    QString name;

    switch (button) {
    case BTN_LEFT:
        name = QStringLiteral("BTN_LEFT");
        break;

    case BTN_RIGHT:
        name = QStringLiteral("BTN_RIGHT");
        break;

    case BTN_MIDDLE:
        name = QStringLiteral("BTN_MIDDLE");
        break;

    case BTN_SIDE:
        name = QStringLiteral("BTN_SIDE");
        break;

    case BTN_EXTRA:
        name = QStringLiteral("BTN_EXTRA");
        break;

    default:
        name = QStringLiteral("BTN_%1").arg(button);
        break;
    }

    self->onButton(
        name,
        state == WL_POINTER_BUTTON_STATE_PRESSED);
}

void WaylandPointer::pointerAxis(
    void *data,
    wl_pointer *,
    uint32_t,
    uint32_t axis,
    wl_fixed_t value)
{
    auto *self = static_cast<WaylandPointer *>(data);

    if (!self->onButton)
        return;

    /*
     * Wayland no expone la rueda como un "button".
     * Convertimos cada movimiento de rueda en un evento visual.
     *
     * El signo sigue la convención estándar de Wayland:
     * positivo = desplazamiento hacia abajo/derecha según el eje.
     */
    if (axis == WL_POINTER_AXIS_VERTICAL_SCROLL) {
        if (value > 0) {
            self->onButton(
                QStringLiteral("BTN_WHEEL_DOWN"),
                true);
        } else if (value < 0) {
            self->onButton(
                QStringLiteral("BTN_WHEEL_UP"),
                true);
        }
    } else if (axis == WL_POINTER_AXIS_HORIZONTAL_SCROLL) {
        if (value > 0) {
            self->onButton(
                QStringLiteral("BTN_WHEEL_RIGHT"),
                true);
        } else if (value < 0) {
            self->onButton(
                QStringLiteral("BTN_WHEEL_LEFT"),
                true);
        }
    }
}

void WaylandPointer::pointerFrame(
    void *,
    wl_pointer *)
{
}

void WaylandPointer::pointerAxisSource(
    void *,
    wl_pointer *,
    uint32_t)
{
}

void WaylandPointer::pointerAxisStop(
    void *,
    wl_pointer *,
    uint32_t,
    uint32_t)
{
}

void WaylandPointer::pointerAxisDiscrete(
    void *,
    wl_pointer *,
    uint32_t,
    int32_t)
{
}

void WaylandPointer::pointerAxisValue120(
    void *,
    wl_pointer *,
    uint32_t,
    int32_t)
{
}

void WaylandPointer::pointerAxisRelativeDirection(
    void *,
    wl_pointer *,
    uint32_t,
    uint32_t)
{
}
