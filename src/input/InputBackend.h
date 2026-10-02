/*
 * SPDX-FileCopyrightText: 2026 Marc G.
 * SPDX-License-Identifier: GPL-3.0-or-later
 */

#pragma once

#include "InputEvent.h"

#include <functional>

namespace KDEKeyCast {

class InputBackend
{
public:
    using EventHandler = std::function<void(const InputEvent &)>;

    virtual ~InputBackend() = default;

    virtual bool start(EventHandler handler) = 0;
    virtual void stop() = 0;
};

} // namespace KDEKeyCast
