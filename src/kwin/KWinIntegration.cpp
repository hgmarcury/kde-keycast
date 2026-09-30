#include "KWinIntegration.h"

#include <QProcess>
#include <QStandardPaths>
#include <QCoreApplication>
#include <QDir>
#include <QFileInfo>

namespace {

QString findInstaller()
{
    const QString applicationDir =
        QCoreApplication::applicationDirPath();

    const QString installedInstaller =
        QDir(applicationDir)
            .absoluteFilePath(
                QStringLiteral("install-kde-keycast")
            );

    if (QFileInfo(installedInstaller).isExecutable())
        return installedInstaller;

    const QString developmentInstaller =
        QDir(applicationDir)
            .absoluteFilePath(
                QStringLiteral("../packaging/install-kde-keycast.sh")
            );

    if (QFileInfo(developmentInstaller).isExecutable())
        return developmentInstaller;

    return {};
}

QString findKWinPackage(const QString &installer)
{
    const QString applicationDir =
        QCoreApplication::applicationDirPath();

    const QString developmentPackage =
        QDir(applicationDir)
            .absoluteFilePath(
                QStringLiteral("../kwin/kde-keycast")
            );

    if (QFileInfo(developmentPackage).isDir() &&
        QFileInfo(installer).fileName() ==
            QStringLiteral("install-kde-keycast.sh")) {

        return developmentPackage;
    }

    const QString installedPackage =
        QDir(applicationDir)
            .absoluteFilePath(
                QStringLiteral("../share/kwin/scripts/kde-keycast")
            );

    if (QFileInfo(installedPackage).isDir())
        return installedPackage;

    return {};
}

} // namespace

namespace KDEKeyCast {

bool KWinIntegration::isInstalled()
{
    const QString kpackageTool =
        QStandardPaths::findExecutable(
            QStringLiteral("kpackagetool6")
        );

    if (kpackageTool.isEmpty())
        return false;

    QProcess process;

    process.start(
        kpackageTool,
        {
            QStringLiteral("--type=KWin/Script"),
            QStringLiteral("--show"),
            QStringLiteral("kde-keycast")
        }
    );

    if (!process.waitForFinished(3000))
        return false;

    return process.exitStatus() == QProcess::NormalExit &&
           process.exitCode() == 0;
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

bool KWinIntegration::install()
{
    const QString installer = findInstaller();

    if (installer.isEmpty())
        return false;

    const QString kwinPackage =
        findKWinPackage(installer);

    QProcess process;

    if (kwinPackage.isEmpty()) {
        process.start(installer);
    } else {
        process.start(
            installer,
            {kwinPackage}
        );
    }

    if (!process.waitForFinished(30000))
        return false;

    return process.exitStatus() == QProcess::NormalExit &&
           process.exitCode() == 0;
}

bool KWinIntegration::enable()
{
    const QString kwriteConfig =
        QStandardPaths::findExecutable(
            QStringLiteral("kwriteconfig6")
        );

    const QString qdbus =
        QStandardPaths::findExecutable(
            QStringLiteral("qdbus6")
        );

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

bool KWinIntegration::uninstall()
{
    const QString installer = findInstaller();

    if (installer.isEmpty())
        return false;

    QProcess process;

    process.start(
        installer,
        {QStringLiteral("--uninstall")}
    );

    if (!process.waitForFinished(30000))
        return false;

    return process.exitStatus() == QProcess::NormalExit &&
           process.exitCode() == 0;
}

} // namespace KDEKeyCast
