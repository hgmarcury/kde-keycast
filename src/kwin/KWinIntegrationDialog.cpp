#include "KWinIntegrationDialog.h"

#include <QMessageBox>

namespace KDEKeyCast {

bool KWinIntegrationDialog::askInstall()
{
    QMessageBox messageBox;

    messageBox.setWindowTitle(
        QStringLiteral("KDE KeyCast")
    );

    messageBox.setText(
        QStringLiteral("¿Quieres instalar la integración con KWin?")
    );

    messageBox.setInformativeText(
        QStringLiteral(
            "La integración permite a KDE KeyCast gestionar "
            "automáticamente la posición y el estado de la ventana "
            "en KDE Plasma."
        )
    );

    messageBox.setStandardButtons(
        QMessageBox::Yes |
        QMessageBox::No
    );

    messageBox.setDefaultButton(
        QMessageBox::Yes
    );

    return messageBox.exec() == QMessageBox::Yes;
}

} // namespace KDEKeyCast
