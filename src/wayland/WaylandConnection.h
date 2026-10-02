/*
 * SPDX-FileCopyrightText: 2026 Marc G.
 * SPDX-License-Identifier: GPL-3.0-or-later
 */

#pragma once

#include <wayland-client.h>

class WaylandConnection
{
public:
    WaylandConnection();
    ~WaylandConnection();

    WaylandConnection(const WaylandConnection &) = delete;
    WaylandConnection &operator=(const WaylandConnection &) = delete;

    bool connect();

    void disconnect();

    wl_display *display() const;
    wl_registry *registry() const;
    wl_seat *seat() const;

    int socketFd() const;

    bool roundtrip();

    bool dispatchPending();

    bool flush();

private:
    static void registryGlobal(
        void *data,
        wl_registry *registry,
        uint32_t name,
        const char *interface,
        uint32_t version
    );

    static void registryGlobalRemove(
        void *data,
        wl_registry *registry,
        uint32_t name
    );

    static const wl_registry_listener registryListener;

    wl_display *display_ = nullptr;
    wl_registry *registry_ = nullptr;
    wl_seat *seat_ = nullptr;
};
