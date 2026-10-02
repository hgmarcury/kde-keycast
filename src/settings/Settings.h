/*
 * SPDX-FileCopyrightText: 2026 Marc G.
 * SPDX-License-Identifier: GPL-3.0-or-later
 */


#pragma once

#include <QString>
#include <QtGlobal>

namespace KDEKeyCast {

class Settings
{
public:
    Settings();

    bool compactMode() const;
    void setCompactMode(bool value);

    bool alwaysOnTop() const;
    void setAlwaysOnTop(bool value);

    qint64 displayDurationMs() const;
    void setDisplayDurationMs(qint64 value);

    bool lightMode() const;
    void setLightMode(bool value);

    bool hideKeys() const;
    void setHideKeys(bool value);

    bool onlyShortcuts() const;
    void setOnlyShortcuts(bool value);

    bool kwinIntegrationPromptDisabled() const;
    void setKwinIntegrationPromptDisabled(bool value);

private:
    static constexpr qint64 MinDisplayDurationMs = 500;
    static constexpr qint64 MaxDisplayDurationMs = 5000;

    QString organizationName_ =
        QStringLiteral("KDE");

    QString applicationName_ =
        QStringLiteral("KDEKeyCast");
};

} // namespace KDEKeyCast
