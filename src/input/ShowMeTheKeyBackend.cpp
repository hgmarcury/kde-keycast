/*
 * SPDX-FileCopyrightText: 2026 Marc G.
 * SPDX-License-Identifier: GPL-3.0-or-later
 */


#include "ShowMeTheKeyBackend.h"

#include <QFileInfo>
#include <QDir>
#include <QCoreApplication>
#include <QDebug>
#include <QTimer>
#include <QObject>

namespace KDEKeyCast {

    ShowMeTheKeyBackend::ShowMeTheKeyBackend()
        : QObject(nullptr)
    {
    }

ShowMeTheKeyBackend::~ShowMeTheKeyBackend()
{
    stop();
}

bool ShowMeTheKeyBackend::start(EventHandler handler)
{
    if (process_.state() != QProcess::NotRunning)
        return true;

    inputReader_.setEventHandler(
        std::move(handler)
    );

    QString executable;

    const QString projectBackend =
        QDir(QCoreApplication::applicationDirPath())
            .absoluteFilePath(
                QStringLiteral(
                    "../vendor/showmethekey-cli/showmethekey-cli"
                )
            );

    const QString libexecDir =
        QStringLiteral(KDE_KEYCAST_LIBEXECDIR);

    const QString installedBackend =
        QFileInfo(libexecDir).isAbsolute()
            ? QDir(libexecDir).absoluteFilePath(
                  QStringLiteral("kde-keycast/showmethekey-cli")
              )
            : QDir(QCoreApplication::applicationDirPath())
                  .absoluteFilePath(
                      QStringLiteral("../%1/kde-keycast/showmethekey-cli")
                          .arg(libexecDir)
                  );

    if (QFileInfo::exists(projectBackend) &&
        QFileInfo(projectBackend).isExecutable()) {

        executable = projectBackend;

        qDebug()
            << "KDE KeyCast: usando backend de desarrollo:"
            << executable;

    } else if (QFileInfo::exists(installedBackend) &&
               QFileInfo(installedBackend).isExecutable()) {

        executable = installedBackend;

        qDebug()
            << "KDE KeyCast: usando backend incluido:"
            << executable;

    } else {

        qCritical()
            << "KDE KeyCast: no se encontró el backend incluido."
            << "\nDesarrollo:"
            << projectBackend
            << "\nInstalación:"
            << installedBackend;

        return false;
    }
    process_.setProgram(executable);

    connect(
        &process_,
        &QProcess::readyReadStandardOutput,
        this,
        &ShowMeTheKeyBackend::readInput,
        Qt::UniqueConnection
    );

    connect(
        &process_,
        qOverload<int, QProcess::ExitStatus>(
            &QProcess::finished
        ),
        this,
        [this](int, QProcess::ExitStatus) {

            QTimer::singleShot(
                1000,
                this,
                [this] {

                    if (process_.state() ==
                        QProcess::NotRunning) {

                        process_.start();
                    }
                }
            );
        }
    );

    process_.start();

    if (!process_.waitForStarted(1500)) {

        qWarning(
            "KDE KeyCast: no se pudo iniciar "
            "showmethekey-cli: %s",
            qPrintable(process_.errorString())
        );

        return false;
    }

    return true;
}

void ShowMeTheKeyBackend::stop()
{
    if (process_.state() ==
        QProcess::NotRunning) {

        return;
    }

    process_.terminate();

    if (!process_.waitForFinished(1000)) {

        process_.kill();
        process_.waitForFinished(1000);
    }
}

bool ShowMeTheKeyBackend::isRunning() const
{
    return process_.state() !=
           QProcess::NotRunning;
}

void ShowMeTheKeyBackend::readInput()
{
    while (process_.canReadLine()) {
        const QByteArray line =
            process_.readLine();

        inputReader_.processLine(line);
    }
}

} // namespace KDEKeyCast
