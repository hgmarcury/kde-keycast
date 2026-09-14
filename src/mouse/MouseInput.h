#pragma once

#include "../input/InputEvent.h"

#include <QString>

namespace KDEKeyCast {

class MouseInput
{
public:
    static QString buttonName(int linuxButton)
    {
        switch (linuxButton) {
        case 0x110:
            return QStringLiteral("Mouse Left");

        case 0x111:
            return QStringLiteral("Mouse Right");

        case 0x112:
            return QStringLiteral("Mouse Middle");

        case 0x113:
            return QStringLiteral("Mouse Side");

        case 0x114:
            return QStringLiteral("Mouse Extra");

        default:
            return QStringLiteral("Mouse %1")
                .arg(linuxButton);
        }
    }

    static QString wheelName(
        MouseAxis axis,
        double delta)
    {
        if (axis == MouseAxis::Horizontal) {
            return delta < 0
                ? QStringLiteral("Wheel ←")
                : QStringLiteral("Wheel →");
        }

        return delta < 0
            ? QStringLiteral("Wheel ↓")
            : QStringLiteral("Wheel ↑");
    }
};

} // namespace KDEKeyCast
