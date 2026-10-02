/*
 * SPDX-FileCopyrightText: 2026 Marc G.
 * SPDX-License-Identifier: GPL-3.0-or-later
 */

#include "KWinIntegrationDialog.h"

#include <QMessageBox>
#include <QCheckBox>

namespace KDEKeyCast {

KWinIntegrationDialog::Result KWinIntegrationDialog::askInstall()
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

    QCheckBox *dontShowAgain =
        new QCheckBox(
            QStringLiteral("No volver a mostrar"),
            &messageBox
        );

    messageBox.setCheckBox(dontShowAgain);

    const bool install =
        messageBox.exec() == QMessageBox::Yes;

    Result result;

    result.install = install;
    result.dontShowAgain =
        dontShowAgain->isChecked();

    return result;
}

bool KWinIntegrationDialog::confirmUninstall(QWidget *parent)
{
    const QMessageBox::StandardButton result =
        QMessageBox::question(
            parent,
            QStringLiteral("KDE KeyCast"),
            QStringLiteral(
                "¿Quieres desinstalar la integración con KWin?"
            ),
            QMessageBox::Yes |
            QMessageBox::No,
            QMessageBox::No
        );

    return result == QMessageBox::Yes;
}

void KWinIntegrationDialog::showError()
{
    QMessageBox::critical(
        nullptr,
        QStringLiteral("KDE KeyCast"),
        QStringLiteral(
            "No se ha podido modificar la integración con KWin."
        )
    );
}

} // namespace KDEKeyCast
