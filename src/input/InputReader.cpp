#include "InputReader.h"

#include <QJsonDocument>
#include <QJsonObject>
#include <QJsonParseError>

namespace KDEKeyCast {

void InputReader::setEventHandler(
    EventHandler handler)
{
    handler_ = std::move(handler);
}

void InputReader::emitEvent(
    const InputEvent &event)
{
    if (handler_)
        handler_(event);
}

void InputReader::processLine(
    const QByteArray &line)
{
    const QByteArray trimmed =
        line.trimmed();

    if (trimmed.isEmpty())
        return;

    QJsonParseError error;

    const QJsonDocument doc =
        QJsonDocument::fromJson(
            trimmed,
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

        if (axisName.isEmpty() ||
            qFuzzyIsNull(axisValue)) {

            return;
        }

        InputEvent event;

        event.device =
            InputDevice::Mouse;

        event.action =
            InputAction::Wheel;

        event.mouseAxis =
            axisName == QStringLiteral("VERTICAL")
                ? MouseAxis::Vertical
                : axisName == QStringLiteral("HORIZONTAL")
                    ? MouseAxis::Horizontal
                    : MouseAxis::None;

        event.wheelDelta =
            axisValue;

        event.keyName =
            axisName;

        emitEvent(event);

        return;
    }

    /*
     * Los eventos de teclado y botones necesitan
     * key_name y key_code.
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

        emitEvent(event);

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

    InputEvent event;

    event.device =
        InputDevice::Keyboard;

    event.linuxCode =
        linuxKeyCode;

    event.keyName =
        keyName;

    if (state ==
        QStringLiteral("PRESSED")) {

        event.action =
            InputAction::Pressed;

    } else if (state ==
               QStringLiteral("RELEASED")) {

        event.action =
            InputAction::Released;

    } else {

        return;
    }

    emitEvent(event);
}

} // namespace KDEKeyCast
