/*
 * SPDX-FileCopyrightText: 2026 Marc G.
 * SPDX-License-Identifier: GPL-3.0-or-later
 */

#pragma once

class QWidget;

namespace KDEKeyCast {

    class KWinIntegrationDialog
    {
    public:
        struct Result
        {
            bool install = false;
            bool dontShowAgain = false;
        };

        static Result askInstall();
        static bool confirmUninstall(QWidget *parent);
        static void showError();
    };

} // namespace KDEKeyCast
