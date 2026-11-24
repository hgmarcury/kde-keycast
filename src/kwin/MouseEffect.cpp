/*
 * SPDX-FileCopyrightText: 2026 Marc G.
 * SPDX-License-Identifier: GPL-3.0-or-later
 */

#include "MouseEffect.h"

#include <QProcess>
#include <QStandardPaths>

namespace {

const QString effectId = QStringLiteral("kde-keycast-cursor");

QString findQdbus() {
  const QString qdbus6 =
      QStandardPaths::findExecutable(QStringLiteral("qdbus6"));

  if (!qdbus6.isEmpty())
    return qdbus6;

  return QStandardPaths::findExecutable(QStringLiteral("qdbus-qt6"));
}

bool callEffect(const QString &method) {
  const QString qdbus = findQdbus();

  if (qdbus.isEmpty())
    return false;

  QProcess process;

  process.start(qdbus,
                {QStringLiteral("org.kde.KWin"), QStringLiteral("/Effects"),
                 QStringLiteral("org.kde.kwin.Effects.") + method, effectId});

  if (!process.waitForFinished(3000))
    return false;

  return process.exitStatus() == QProcess::NormalExit &&
         process.exitCode() == 0;
}

bool queryEffect(const QString &method) {
  const QString qdbus = findQdbus();

  if (qdbus.isEmpty())
    return false;

  QProcess process;

  process.start(qdbus,
                {QStringLiteral("org.kde.KWin"), QStringLiteral("/Effects"),
                 QStringLiteral("org.kde.kwin.Effects.") + method, effectId});

  if (!process.waitForFinished(3000) ||
      process.exitStatus() != QProcess::NormalExit || process.exitCode() != 0) {
    return false;
  }

  return QString::fromLocal8Bit(process.readAllStandardOutput())
             .trimmed()
             .compare(QStringLiteral("true"), Qt::CaseInsensitive) == 0;
}

} // namespace

namespace KDEKeyCast {

bool MouseEffect::isSupported() {
  return queryEffect(QStringLiteral("isEffectSupported"));
}

bool MouseEffect::isEnabled() {
  return queryEffect(QStringLiteral("isEffectLoaded"));
}

bool MouseEffect::enable() {
  if (isEnabled())
    return true;

  return callEffect(QStringLiteral("loadEffect"));
}

bool MouseEffect::disable() {
  if (!isEnabled())
    return true;

  return callEffect(QStringLiteral("unloadEffect"));
}

bool MouseEffect::setDarkMode(bool dark) {
  const QString kwriteConfig =
      QStandardPaths::findExecutable(QStringLiteral("kwriteconfig6"));

  const QString qdbus = findQdbus();

  if (kwriteConfig.isEmpty() || qdbus.isEmpty())
    return false;

  QProcess process;

  process.start(kwriteConfig,
                {QStringLiteral("--file"), QStringLiteral("kwinrc"),
                 QStringLiteral("--group"),
                 QStringLiteral("Effect-kde-keycast-cursor"),
                 QStringLiteral("--key"), QStringLiteral("DarkMode"),
                 QStringLiteral("--type"), QStringLiteral("bool"),
                 dark ? QStringLiteral("true") : QStringLiteral("false"),
                 QStringLiteral("--notify")});

  if (!process.waitForFinished(3000) ||
      process.exitStatus() != QProcess::NormalExit || process.exitCode() != 0) {
    return false;
  }

  process.start(qdbus,
                {QStringLiteral("org.kde.KWin"), QStringLiteral("/Effects"),
                 QStringLiteral("org.kde.kwin.Effects.reconfigureEffect"),
                 effectId});

  if (!process.waitForFinished(3000))
    return false;

  return process.exitStatus() == QProcess::NormalExit &&
         process.exitCode() == 0;
}

} // namespace KDEKeyCast
