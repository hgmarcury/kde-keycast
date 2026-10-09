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

    const bool isDevelopmentBuild =
        QFileInfo(applicationDir).fileName()
        == QStringLiteral("build");

    QString backend;

    if (isDevelopmentBuild
        && QFileInfo(developmentBackend).isExecutable()) {
        backend = developmentBackend;
    } else if (QFileInfo(installedBackend).isExecutable()) {
        backend = installedBackend;
    } else {
        const QString error =
            QStringLiteral("No se encontró el backend incluido.");
        qCritical() << "KDE KeyCast:" << error;
        emit backendError(error);
        return false;
    }

    const QString pkexec =
        QStandardPaths::findExecutable(
            QStringLiteral("pkexec")
        );

    if (pkexec.isEmpty()) {
        const QString error =
            QStringLiteral(
                "No se encontró pkexec. Comprueba que Polkit "
                "esté instalado y disponible en el sistema."
            );
        qWarning() << "KDE KeyCast:" << error;
        emit backendError(error);
        return false;
    }

    process_.setProgram(pkexec);
    process_.setArguments({backend});
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
        },
        Qt::UniqueConnection
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
