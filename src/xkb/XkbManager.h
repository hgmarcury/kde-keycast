/*
 * SPDX-FileCopyrightText: 2026 Marc G.
 * SPDX-License-Identifier: GPL-3.0-or-later
 */

#pragma once

#include <QString>
#include <QStringList>

#include <xkbcommon/xkbcommon.h>
#include <xkbcommon/xkbcommon-compose.h>

enum class ComposeResult
{
    Ignored,
    Composing,
    Composed,
    Cancelled
};

class XkbManager
{
public:
    XkbManager();
    ~XkbManager();

    XkbManager(const XkbManager &) = delete;
    XkbManager &operator=(const XkbManager &) = delete;

    static xkb_keycode_t keycodeFromLinuxCode(int linuxCode);

    bool loadKeymap(const char *keymapText);
    bool isReady() const;
    bool ledActive(const char *name) const;

    void updateKey(int linuxCode, bool pressed);

    void updateModifiers(
        xkb_mod_mask_t modsDepressed,
        xkb_mod_mask_t modsLatched,
        xkb_mod_mask_t modsLocked,
        xkb_layout_index_t group
    );

    QString keysymToText(xkb_keysym_t sym) const;

    QString displayName(
        int linuxCode,
        const QString &fallbackName
    ) const;

    QStringList secondaryLegends(
        int linuxCode,
        const QString &primary,
        bool compactMode
    ) const;

    xkb_keysym_t keySym(int linuxCode) const;

    void resetComposeState();
    void resetComposeSequence();

    ComposeResult feedComposeKey(xkb_keysym_t keysym);

private:
    xkb_keymap *xkbKeymap = nullptr;
    xkb_state *xkbState = nullptr;

    xkb_compose_table *xkbComposeTable = nullptr;
    xkb_compose_state *xkbComposeState = nullptr;

    QStringList composeSequence;
};
