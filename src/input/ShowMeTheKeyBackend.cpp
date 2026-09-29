
#include "ShowMeTheKeyBackend.h"

#include <QJsonDocument>
#include <QJsonObject>
#include <QFileInfo>
#include <QDir>
#include <QCoreApplication>
#include <QDebug>
#include <QTimer>
#include <QObject>

namespace KDEKeyCast {

ShowMeTheKeyBackend::ShowMeTheKeyBackend(
    QString executable)
    : QObject(nullptr)
    , executable_(std::move(executable))
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

    handler_ = std::move(handler);

    QString executable;

    const QString projectBackend =
        QDir(QCoreApplication::applicationDirPath())
            .absoluteFilePath(
                QStringLiteral(
                    "../vendor/showmethekey-cli/showmethekey-cli"
                )
            );

    const QString installedBackend =
        QDir(QCoreApplication::applicationDirPath())
            .absoluteFilePath(
                QStringLiteral(
                    "../lib/kde-keycast/showmethekey-cli"
                )
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
        },
        Qt::UniqueConnection
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
            process_.readLine().trimmed();

        if (line.isEmpty())
            continue;

        handleLine(line);
    }
}

void ShowMeTheKeyBackend::handleLine(
    const QByteArray &line)
{
    QJsonParseError error;

    const QJsonDocument doc =
        QJsonDocument::fromJson(
            line,
            &error
        );

    if (error.error !=
            QJsonParseError::NoError ||
        !doc.isObject()) {

        return;
    }

    const QJsonObject obj =
        doc.object();

    const QString eventName =
        obj.value(
            QStringLiteral("event_name")
        ).toString();

    const QString keyName =
        obj.value(
            QStringLiteral("key_name")
        ).toString();

    const int linuxKeyCode =
        obj.value(
            QStringLiteral("key_code")
        ).toInt(-1);

    const QString state =
        obj.value(
            QStringLiteral("state_name")
        ).toString();

    /*
     * -------------------------------------------------------
     * MOUSE WHEEL
     * -------------------------------------------------------
     *
     * POINTER_AXIS no tiene key_name ni key_code.
     */

    if (eventName ==
        QStringLiteral("POINTER_AXIS")) {

        const QString axisName =
            obj.value(
                QStringLiteral("axis_name")
            ).toString();

        const double axisValue =
            obj.value(
                QStringLiteral("axis_value")
            ).toDouble();

        InputEvent event;

        event.device =
            InputDevice::Mouse;

        event.action =
            InputAction::Wheel;

        event.keyName =
            axisName;

        event.text =
            axisName;

        event.wheelDelta =
            axisValue;

        if (axisName.contains(
                QStringLiteral("HORIZONTAL"),
                Qt::CaseInsensitive)) {

            event.mouseAxis =
                MouseAxis::Horizontal;

        } else {

            event.mouseAxis =
                MouseAxis::Vertical;
        }

        if (handler_)
            handler_(event);

        return;
    }

    /*
     * Los eventos que quedan necesitan información
     * de tecla/botón.
     */

    if (keyName.isEmpty())
        return;

    if (linuxKeyCode < 0)
        return;

    /*
     * -------------------------------------------------------
     * MOUSE BUTTON
     * -------------------------------------------------------
     */

    if (eventName ==
        QStringLiteral("POINTER_BUTTON")) {

        if (state !=
            QStringLiteral("PRESSED")) {

            return;
        }

        InputEvent event;

        event.device =
            InputDevice::Mouse;

        event.action =
            InputAction::Pressed;

        event.linuxCode =
            linuxKeyCode;

        event.keyName =
            keyName;

        event.text =
            keyName;

        if (handler_)
            handler_(event);

        return;
    }

    /*
     * -------------------------------------------------------
     * KEYBOARD
     * -------------------------------------------------------
     */

    if (eventName !=
        QStringLiteral("KEYBOARD_KEY")) {

        return;
    }

    InputAction action;

    if (state ==
        QStringLiteral("PRESSED")) {

        action =
            InputAction::Pressed;

    } else if (state ==
               QStringLiteral("RELEASED")) {

        action =
            InputAction::Released;

    } else {

        return;
    }

    InputEvent event;

    event.device =
        InputDevice::Keyboard;

    event.action =
        action;

    event.linuxCode =
        linuxKeyCode;

    event.keyName =
        keyName;

    event.text =
        keyName;

    if (handler_)
        handler_(event);
}

} // namespace KDEKeyCast
