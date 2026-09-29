#pragma once

#include <QStringList>
#include <QtGlobal>

struct VisualEntry
{
    QStringList keys;
    QStringList keySecondaryLegends;

    qint64 created = 0;
    qint64 expires = 0;

    bool combination = false;
    int repeatCount = 1;

    // Código Linux de la tecla física cuando la entrada
    // representa una única tecla. Las combinaciones utilizan -1.
    int linuxKeyCode = -1;

    bool composePending = false;
    bool keyboardEntry = false;
    bool maskKeyboard = false;
};
