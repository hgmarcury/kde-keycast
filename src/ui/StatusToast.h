/*
 * SPDX-FileCopyrightText: 2026 Marc G.
 * SPDX-License-Identifier: GPL-3.0-or-later
 */

#pragma once

#include <QString>

class QWidget;

namespace KDEKeyCast {

class StatusToast
{
public:
    static void showInstalling(QWidget *parent);
    static void showEnabling(QWidget *parent);
    static void showDisabling(QWidget *parent);
    static void showUninstalling(QWidget *parent);

    static void showInstalled(QWidget *parent);
    static void showEnabled(QWidget *parent);
    static void showDisabled(QWidget *parent);
    static void showUninstalled(QWidget *parent);

    static void showFailed(QWidget *parent, const QString &error);
    static void showInputError(
        QWidget *parent,
        const QString &error
    );
};

} // namespace KDEKeyCast
