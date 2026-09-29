#include "WaylandConnection.h"

#include <QDebug>

#include <algorithm>
#include <cstring>

WaylandConnection::WaylandConnection() = default;

WaylandConnection::~WaylandConnection()
{
    disconnect();
}

bool WaylandConnection::connect()
{
    if (display_)
        return true;

    display_ = wl_display_connect(nullptr);

    if (!display_) {
        qWarning("KDE KeyCast: no se pudo conectar a Wayland");
        return false;
    }

    registry_ =
        wl_display_get_registry(display_);

    if (!registry_) {
        qWarning("KDE KeyCast: no se pudo obtener wl_registry");
        disconnect();
        return false;
    }

    wl_registry_add_listener(
        registry_,
        &registryListener,
        this
    );

    if (wl_display_roundtrip(display_) < 0) {
        qWarning("KDE KeyCast: fallo en wl_display_roundtrip");
        disconnect();
        return false;
    }

    if (!seat_) {
        qWarning(
            "KDE KeyCast: Wayland no proporcionó ningún wl_seat"
        );
        disconnect();
        return false;
    }

    return true;
}

void WaylandConnection::disconnect()
{
    if (seat_) {
        wl_seat_destroy(seat_);
        seat_ = nullptr;
    }

    if (registry_) {
        wl_registry_destroy(registry_);
        registry_ = nullptr;
    }

    if (display_) {
        wl_display_disconnect(display_);
        display_ = nullptr;
    }
}

wl_display *WaylandConnection::display() const
{
    return display_;
}

wl_registry *WaylandConnection::registry() const
{
    return registry_;
}

wl_seat *WaylandConnection::seat() const
{
    return seat_;
}

int WaylandConnection::socketFd() const
{
    if (!display_)
        return -1;

    return wl_display_get_fd(display_);
}

void WaylandConnection::registryGlobal(
    void *data,
    wl_registry *registry,
    uint32_t name,
    const char *interface,
    uint32_t version
)
{
    auto *self =
        static_cast<WaylandConnection *>(data);

    if (std::strcmp(
            interface,
            wl_seat_interface.name
        ) != 0) {
        return;
    }

    const uint32_t bindVersion =
        std::min<uint32_t>(version, 7);

    self->seat_ =
        static_cast<wl_seat *>(
            wl_registry_bind(
                registry,
                name,
                &wl_seat_interface,
                bindVersion
            )
        );

    qDebug()
        << "KDE KeyCast: wl_seat encontrado";
}

void WaylandConnection::registryGlobalRemove(
    void *,
    wl_registry *,
    uint32_t
)
{
}

const wl_registry_listener
    WaylandConnection::registryListener = {
        WaylandConnection::registryGlobal,
        WaylandConnection::registryGlobalRemove
    };
