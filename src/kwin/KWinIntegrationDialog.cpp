/*
 * SPDX-FileCopyrightText: 2026 Marc G.
 * SPDX-License-Identifier: GPL-3.0-or-later
 */

#include "KWinIntegrationDialog.h"

#include <QMessageBox>
#include <QCheckBox>
#include <QCoreApplication>

namespace KDEKeyCast {

KWinIntegrationDialog::Result KWinIntegrationDialog::askInstall()
{
    QMessageBox messageBox;

    messageBox.setWindowTitle(
        QStringLiteral("KDE KeyCast")
    );

    messageBox.setText(
        QCoreApplication::translate(
            "KWinIntegrationDialog",
            "Do you want to install the KWin integration?"
        )
    );

    messageBox.setInformativeText(
        QCoreApplication::translate(
            "KWinIntegrationDialog",
            "The integration allows KDE KeyCast to manage its window "
            "automatically in KDE Plasma.\n\n"
            "It can keep the window always on top and remember "
            "its position when KDE KeyCast is launched again."
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
            QCoreApplication::translate(
                "KWinIntegrationDialog",
                "Do not show again"
            ),
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
            QCoreApplication::translate(
                "KWinIntegrationDialog",
                "Do you want to uninstall the KWin integration?"
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
        QCoreApplication::translate(
            "KWinIntegrationDialog",
            "Could not modify the KWin integration."
        )
    );
}

} // namespace KDEKeyCast
