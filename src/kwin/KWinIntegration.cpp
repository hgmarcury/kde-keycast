/*
 * SPDX-FileCopyrightText: 2026 Marc G.
 * SPDX-License-Identifier: GPL-3.0-or-later
 */

#include "KWinIntegration.h"

#include <QProcess>
#include <QStandardPaths>
#include <QCoreApplication>
#include <QDir>
#include <QFileInfo>

namespace {

    QString findKWinInstaller()
    {
        const QString applicationDir =
            QCoreApplication::applicationDirPath();

        const QString installedInstaller =
            QDir(applicationDir)
                .absoluteFilePath(
                    QStringLiteral("install-kwin-kde-keycast")
                );

        if (QFileInfo(installedInstaller).isExecutable())
            return installedInstaller;

        const QString developmentInstaller =
            QDir(applicationDir)
                .absoluteFilePath(
                    QStringLiteral("../packaging/install-kwin-kde-keycast.sh")
                );

        if (QFileInfo(developmentInstaller).isExecutable())
            return developmentInstaller;

        return {};
    }

    QString findKWinUninstaller()
    {
        const QString applicationDir =
            QCoreApplication::applicationDirPath();

        const QString installedUninstaller =
            QDir(applicationDir)
                .absoluteFilePath(
                    QStringLiteral("uninstall-kwin-kde-keycast")
                );

        if (QFileInfo(installedUninstaller).isExecutable())
            return installedUninstaller;

        const QString developmentUninstaller =
            QDir(applicationDir)
                .absoluteFilePath(
                    QStringLiteral("../packaging/uninstall-kwin-kde-keycast.sh")
                );

        if (QFileInfo(developmentUninstaller).isExecutable())
            return developmentUninstaller;

        return {};
    }

QString findQdbus()
{
    const QString qdbus6 =
        QStandardPaths::findExecutable(
            QStringLiteral("qdbus6")
        );

    if (!qdbus6.isEmpty())
        return qdbus6;

    return QStandardPaths::findExecutable(
        QStringLiteral("qdbus-qt6")
    );
}

} // namespace

namespace KDEKeyCast {

    bool KWinIntegration::isInstalled()
    {
        const QString dataLocation =
            QStandardPaths::writableLocation(
                QStandardPaths::GenericDataLocation);

        if (dataLocation.isEmpty())
            return false;

        const QString packagePath =
            QDir(dataLocation)
                .absoluteFilePath(
                    QStringLiteral("kwin/scripts/kde-keycast"));

        return QFileInfo(packagePath).isDir() &&
               QFileInfo(
                   QDir(packagePath)
                       .absoluteFilePath(QStringLiteral("metadata.json")))
                   .isFile();
    }

bool KWinIntegration::isEnabled()
{
    const QString kwriteConfig =
        QStandardPaths::findExecutable(
            QStringLiteral("kreadconfig6")
        );

    if (kwriteConfig.isEmpty())
        return false;

    QProcess process;

    process.start(
        kwriteConfig,
        {
            QStringLiteral("--file"),
            QStringLiteral("kwinrc"),
            QStringLiteral("--group"),
            QStringLiteral("Plugins"),
            QStringLiteral("--key"),
            QStringLiteral("kde-keycastEnabled")
        }
    );

    if (!process.waitForFinished(3000))
        return false;

    if (process.exitStatus() != QProcess::NormalExit ||
        process.exitCode() != 0)
        return false;

    const QString value =
        QString::fromLocal8Bit(process.readAllStandardOutput()).trimmed();

    return value.compare(
               QStringLiteral("true"),
               Qt::CaseInsensitive
           ) == 0;
}

bool KWinIntegration::install(QString *errorMessage)
{
    const QString installer = findKWinInstaller();

    if (installer.isEmpty())
        return false;

    QProcess process;

    process.start(installer);
    if (!process.waitForFinished(30000)) {
        if (errorMessage) {
            *errorMessage =
                QStringLiteral(
                    "La instalación ha tardado demasiado."
                );
        }

        return false;
    }

    if (process.exitStatus() != QProcess::NormalExit) {
        if (errorMessage) {
            *errorMessage =
                QStringLiteral(
                    "El instalador terminó de forma inesperada."
                );
        }

        return false;
    }

    if (process.exitCode() != 0) {
        if (errorMessage) {
            *errorMessage =
                QString::fromLocal8Bit(
                    process.readAllStandardError()
                ).trimmed();
        }

        return false;
    }

    return true;
}

bool KWinIntegration::enable()
{
    const QString kwriteConfig =
        QStandardPaths::findExecutable(
            QStringLiteral("kwriteconfig6")
        );

    const QString qdbus = findQdbus();

    if (kwriteConfig.isEmpty() ||
        qdbus.isEmpty()) {

        return false;
    }

    QProcess process;

    process.start(
        kwriteConfig,
        {
            QStringLiteral("--file"),
            QStringLiteral("kwinrc"),
            QStringLiteral("--group"),
            QStringLiteral("Plugins"),
            QStringLiteral("--key"),
            QStringLiteral("kde-keycastEnabled"),
            QStringLiteral("--type"),
            QStringLiteral("bool"),
            QStringLiteral("true"),
            QStringLiteral("--notify")
        }
    );

    if (!process.waitForFinished(3000))
        return false;

    if (process.exitStatus() != QProcess::NormalExit ||
        process.exitCode() != 0)
        return false;

    process.start(
        qdbus,
        {
            QStringLiteral("org.kde.KWin"),
            QStringLiteral("/KWin"),
            QStringLiteral("org.kde.KWin.reconfigure")
        }
    );

    if (!process.waitForFinished(3000))
        return false;

    return process.exitStatus() == QProcess::NormalExit &&
           process.exitCode() == 0;
}

bool KWinIntegration::disable()
{
    const QString kwriteConfig =
        QStandardPaths::findExecutable(
            QStringLiteral("kwriteconfig6")
        );

    const QString qdbus = findQdbus();

    if (kwriteConfig.isEmpty() ||
        qdbus.isEmpty()) {

        return false;
    }

    QProcess process;

    process.start(
        kwriteConfig,
        {
            QStringLiteral("--file"),
            QStringLiteral("kwinrc"),
            QStringLiteral("--group"),
            QStringLiteral("Plugins"),
            QStringLiteral("--key"),
            QStringLiteral("kde-keycastEnabled"),
            QStringLiteral("--type"),
            QStringLiteral("bool"),
            QStringLiteral("false"),
            QStringLiteral("--notify")
        }
    );

    if (!process.waitForFinished(3000))
        return false;

    if (process.exitStatus() != QProcess::NormalExit ||
        process.exitCode() != 0)
        return false;

    process.start(
        qdbus,
        {
            QStringLiteral("org.kde.KWin"),
            QStringLiteral("/KWin"),
            QStringLiteral("org.kde.KWin.reconfigure")
        }
    );

    if (!process.waitForFinished(3000))
        return false;

    return process.exitStatus() == QProcess::NormalExit &&
           process.exitCode() == 0;
}

bool KWinIntegration::uninstall()
{
    const QString uninstaller = findKWinUninstaller();

    if (uninstaller.isEmpty())
        return false;
    QProcess process;

    process.start(uninstaller);

    if (!process.waitForFinished(30000))
        return false;

    return process.exitStatus() == QProcess::NormalExit &&
           process.exitCode() == 0;
}

} // namespace KDEKeyCast
