/*
 * SPDX-FileCopyrightText: 2026 Marc G.
 * SPDX-License-Identifier: GPL-3.0-or-later
 */

#pragma once

#include <QDialog>
#include <QString>
#include <QStringList>

class QLabel;
class QPlainTextEdit;
class QPushButton;
class QProcess;
class QWidget;

namespace KDEKeyCast {

class PermissionsDialog : public QDialog
{
public:
    explicit PermissionsDialog(QWidget *parent = nullptr);

private:
    void refreshStatus();
    void configurePermissions();
    void removePermissions();
    void startPermissionProcess(
        const QString &scriptName,
        const QStringList &arguments
    );

    QLabel *m_statusLabel = nullptr;
    QPlainTextEdit *m_output = nullptr;
    QPushButton *m_configureButton = nullptr;
    QPushButton *m_removeButton = nullptr;
    QProcess *m_process = nullptr;
};

} // namespace KDEKeyCast
