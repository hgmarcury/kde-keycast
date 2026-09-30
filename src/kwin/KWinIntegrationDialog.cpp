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
        QStringLiteral(
            "¿Quieres instalar la integración con KWin?"
        )
    );

    messageBox.setInformativeText(
        QStringLiteral(
            "La integración permite que KDE KeyCast gestione "
            "automáticamente la ventana en KDE Plasma.\n\n"
            "Podrá mantener la ventana siempre visible y recordar "
            "su posición al volver a iniciar KDE KeyCast."
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
