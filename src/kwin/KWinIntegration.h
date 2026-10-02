/*
 * SPDX-FileCopyrightText: 2026 Marc G.
 * SPDX-License-Identifier: GPL-3.0-or-later
 */

#pragma once

#include <QString>

namespace KDEKeyCast {

class KWinIntegration
{
public:
    static bool isInstalled();
    static bool isEnabled();

    static bool install(QString *errorMessage = nullptr);
    static bool uninstall();
    static bool enable();
    static bool disable();
};

} // namespace KDEKeyCast
