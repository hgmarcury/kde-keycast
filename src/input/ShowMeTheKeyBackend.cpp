/*
 * SPDX-FileCopyrightText: 2026 Marc G.
 * SPDX-License-Identifier: GPL-3.0-or-later
 */

#include "ShowMeTheKeyBackend.h"

#include <QCoreApplication>
#include <QDir>
#include <QFileInfo>
#include <QDebug>
#include <QStandardPaths>

#include <grp.h>
#include <unistd.h>
#include <algorithm>
#include <vector>

namespace KDEKeyCast {

    namespace {

    bool hasInputGroup()
    {
        const int count = getgroups(0, nullptr);
        if (count < 0)
            return false;

        std::vector<gid_t> groups(count);

        const int actualCount = getgroups(
            static_cast<int>(groups.size()),
            groups.data()
        );

        if (actualCount < 0)
            return false;

        groups.resize(actualCount);
        groups.push_back(getegid());

        for (const char *name : {"input"}) {
            const struct group *entry = getgrnam(name);
            if (!entry)
                continue;

            if (std::find(groups.begin(), groups.end(), entry->gr_gid)
                != groups.end()) {
                return true;
            }
        }

        return false;
    }

    } // namespace

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

    stopping_ = false;
    inputReader_.setEventHandler(std::move(handler));

    const QString applicationDir =
        QCoreApplication::applicationDirPath();

    const QString developmentBackend =
        QDir(applicationDir).absoluteFilePath(
            QStringLiteral("showmethekey-cli")
        );

    const QString libexecDir =
        QStringLiteral(KDE_KEYCAST_LIBEXECDIR);

    const QString installedBackend =
        QDir(applicationDir).absoluteFilePath(
            QStringLiteral("../%1/kde-keycast/showmethekey-cli")
                .arg(libexecDir)
        );

    const bool isAppImage =
        qEnvironmentVariableIsSet("APPIMAGE");

    const bool isDevelopmentBuild =
        QFileInfo(applicationDir).fileName()
        == QStringLiteral("build");

    QString backend;

    if (isAppImage) {
        // Backend instalado en una ruta estable para la AppImage.
        backend =
            QStringLiteral(
                "/usr/lib/kde-keycast-appimage/showmethekey-cli"
            );
    } else if (isDevelopmentBuild
               && QFileInfo(developmentBackend).isExecutable()) {
        backend = developmentBackend;
    } else if (QFileInfo(installedBackend).isExecutable()) {
        backend = QFileInfo(installedBackend).canonicalFilePath();
    } else {
        const QString error =
            QStringLiteral("No se encontró el backend incluido.");
        qCritical() << "KDE KeyCast:" << error;
        emit backendError(error);
        return false;
    }

    if (!QFileInfo(backend).isExecutable()) {
        const QString error = isAppImage
            ? QStringLiteral(
                  "No se encontró el backend de KDE KeyCast AppImage. "
                  "Instala o repara la integración de la AppImage."
              )
            : QStringLiteral("No se encontró el backend incluido.");

        qCritical() << "KDE KeyCast:" << error;
        emit backendError(error);
        return false;
    }

    QString program;
    QStringList arguments;

    if (hasInputGroup()) {
        program = backend;
        qInfo() << "KDE KeyCast: ejecutando el backend incluido"
                   " directamente (grupo input).";
    } else {
        program = QStandardPaths::findExecutable(
            QStringLiteral("pkexec")
        );

        if (program.isEmpty()) {
            const QString error =
                QStringLiteral(
                    "No se encontró pkexec. Comprueba que Polkit "
                    "esté instalado y disponible en el sistema."
                );
            qWarning() << "KDE KeyCast:" << error;
            emit backendError(error);
            return false;
        }

        arguments.append(backend);
    }

    process_.setProgram(program);
    process_.setArguments(arguments);
    process_.setProcessChannelMode(QProcess::SeparateChannels);

    connect(
        &process_,
        &QProcess::readyReadStandardOutput,
        this,
        &ShowMeTheKeyBackend::readInput,
        Qt::UniqueConnection
    );

    connect(
        &process_,
        &QProcess::readyReadStandardError,
        this,
        [this]() {
            const QByteArray output =
                process_.readAllStandardError();

            if (!output.isEmpty()) {
                qWarning().noquote()
                    << "KDE KeyCast backend:"
                    << QString::fromLocal8Bit(output).trimmed();
            }
        }
    );

    connect(
        &process_,
        qOverload<int, QProcess::ExitStatus>(
            &QProcess::finished
        ),
        this,
        [this](int exitCode, QProcess::ExitStatus exitStatus) {
            if (stopping_)
                return;

            QString error;

            if (exitStatus == QProcess::CrashExit) {
                error = QStringLiteral(
                    "The input backend terminated unexpectedly."
                );
            } else if (exitCode == 126) {
                error = QStringLiteral(
                    "Polkit authorization was denied or cancelled."
                );
            } else if (exitCode == 127) {
                error = QStringLiteral(
                    "Polkit could not execute the input backend."
                );
            } else {
                error = QStringLiteral(
                    "The input backend terminated (code %1)."
                ).arg(exitCode);
            }

            qWarning() << "KDE KeyCast:" << error;
            emit backendError(error);

            // Do not retry automatically: this could reopen the
            // Polkit authentication dialog repeatedly.
        }
    );

    connect(
        &process_,
        &QProcess::errorOccurred,
        this,
        [this](QProcess::ProcessError error) {
            if (stopping_)
                return;

            if (error == QProcess::FailedToStart) {
                const QString message =
                    QStringLiteral(
                        "No se pudo iniciar el backend mediante Polkit: %1"
                    ).arg(process_.errorString());

                qWarning() << "KDE KeyCast:" << message;
                emit backendError(message);
            }
        }
    );

    process_.start();

    if (!process_.waitForStarted(1500)) {
        const QString error =
            QStringLiteral(
                "No se pudo iniciar pkexec: %1"
            ).arg(process_.errorString());

        qWarning() << "KDE KeyCast:" << error;
        emit backendError(error);
        return false;
    }

    return true;
}

void ShowMeTheKeyBackend::stop()
{
    stopping_ = true;

    if (process_.state() == QProcess::NotRunning)
        return;

    // El backend incluido escucha "stop\n" por stdin.
    process_.write("stop\n");
    process_.waitForBytesWritten(500);

    if (!process_.waitForFinished(1500)) {
        process_.terminate();

        if (!process_.waitForFinished(1000)) {
            process_.kill();
            process_.waitForFinished(1000);
        }
    }
}

bool ShowMeTheKeyBackend::isRunning() const
{
    return process_.state() != QProcess::NotRunning;
}

void ShowMeTheKeyBackend::readInput()
{
    while (process_.canReadLine()) {
        const QByteArray line = process_.readLine();
        inputReader_.processLine(line);
    }
}

} // namespace KDEKeyCast
