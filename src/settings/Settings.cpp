/*
 * SPDX-FileCopyrightText: 2026 Marc G.
 * SPDX-License-Identifier: GPL-3.0-or-later
 */


#include "Settings.h"

#include <QSettings>

namespace KDEKeyCast {

namespace {

QSettings createSettings()
{
    return QSettings(
        QStringLiteral("KDE"),
        QStringLiteral("KDEKeyCast")
    );
}

} // namespace

Settings::Settings() = default;

bool Settings::compactMode() const
{
    const QSettings settings =
        createSettings();

    return settings.value(
        QStringLiteral("compactMode"),
        false
    ).toBool();
}

void Settings::setCompactMode(bool value)
{
    QSettings settings =
        createSettings();

    settings.setValue(
        QStringLiteral("compactMode"),
        value
    );
}

bool Settings::alwaysOnTop() const
{
    const QSettings settings =
        createSettings();

    return settings.value(
        QStringLiteral("alwaysOnTop"),
        true
    ).toBool();
}

void Settings::setAlwaysOnTop(bool value)
{
    QSettings settings =
        createSettings();

    settings.setValue(
        QStringLiteral("alwaysOnTop"),
        value
    );
}

qint64 Settings::displayDurationMs() const
{
    const QSettings settings =
        createSettings();

    const qint64 value =
        settings.value(
            QStringLiteral("displayDurationMs"),
            1800
        ).toLongLong();

    return qBound(
        MinDisplayDurationMs,
        value,
        MaxDisplayDurationMs
    );
}

void Settings::setDisplayDurationMs(qint64 value)
{
    QSettings settings =
        createSettings();

    settings.setValue(
        QStringLiteral("displayDurationMs"),
        qBound(
            MinDisplayDurationMs,
            value,
            MaxDisplayDurationMs
        )
    );
}

bool Settings::lightMode() const
{
    const QSettings settings =
        createSettings();

    return settings.value(
        QStringLiteral("lightMode"),
        false
    ).toBool();
}

void Settings::setLightMode(bool value)
{
    QSettings settings =
        createSettings();

    settings.setValue(
        QStringLiteral("lightMode"),
        value
    );
}

bool Settings::hideKeys() const
{
    const QSettings settings =
        createSettings();

    return settings.value(
        QStringLiteral("hideKeys"),
        false
    ).toBool();
}

void Settings::setHideKeys(bool value)
{
    QSettings settings =
        createSettings();

    settings.setValue(
        QStringLiteral("hideKeys"),
        value
    );
}

bool Settings::onlyShortcuts() const
{
    const QSettings settings =
        createSettings();

    return settings.value(
        QStringLiteral("onlyShortcuts"),
        false
    ).toBool();
}

void Settings::setOnlyShortcuts(bool value)
{
    QSettings settings =
        createSettings();

    settings.setValue(
        QStringLiteral("onlyShortcuts"),
        value
    );
}

bool Settings::kwinIntegrationPromptDisabled() const
{
    const QSettings settings =
        createSettings();

    return settings.value(
        QStringLiteral("kwinIntegrationPromptDisabled"),
        false
    ).toBool();
}

void Settings::setKwinIntegrationPromptDisabled(bool value)
{
    QSettings settings =
        createSettings();

    settings.setValue(
        QStringLiteral("kwinIntegrationPromptDisabled"),
        value
    );
}
} // namespace KDEKeyCast
