/*
 * SPDX-FileCopyrightText: 2026 Marc G.
 * SPDX-License-Identifier: GPL-3.0-or-later
 */

#pragma once

namespace KDEKeyCast {

class MouseEffect {
public:
  static bool isSupported();
  static bool isEnabled();

  static bool enable();
  static bool disable();

  static bool setDarkMode(bool dark);
};

} // namespace KDEKeyCast
