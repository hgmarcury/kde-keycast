/*
 * SPDX-FileCopyrightText: 2026 Marc G.
 * SPDX-License-Identifier: GPL-3.0-or-later
 */

#include "PermissionsDialog.h"

#include <QCoreApplication>
#include <QDir>
#include <QFileInfo>
#include <QGroupBox>
#include <QHBoxLayout>
#include <QLabel>
#include <QMessageBox>
#include <QPlainTextEdit>
#include <QProcess>
#include <QPushButton>
#include <QStandardPaths>
#include <QVBoxLayout>

#include <grp.h>
#include <pwd.h>
#include <unistd.h>

namespace KDEKeyCast {

namespace {

constexpr auto GroupName = "kde-keycast";
constexpr auto StatusPath = "/var/lib/kde-keycast/permissions-configured";

QString currentUserName()
{
    const passwd *user = getpwuid(getuid());

    if (user && user->pw_name) {
        return QString::fromLocal8Bit(user->pw_name);
    }

    return {};
}

} // namespace

PermissionsDialog::PermissionsDialog(QWidget *parent)
    : QDialog(parent)
{
    setWindowTitle(QCoreApplication::translate("PermissionsDialog", "Input permissions — KDE KeyCast"));
    setModal(true);
    resize(520, 400);

    auto *layout = new QVBoxLayout(this);

    auto *description = new QLabel(
        QCoreApplication::translate(
            "PermissionsDialog",
            "KDE KeyCast needs access to keyboard and mouse events "
            "to display input.\n\n"
            "You can configure permissions through Polkit to allow "
            "the bundled backend to run without repeatedly asking "
            "for your password. This grants access to input events; "
            "only configure it if you trust the application."
        ),
        this
    );
    description->setWordWrap(true);
    layout->addWidget(description);

    auto *statusGroup = new QGroupBox(
        QCoreApplication::translate("PermissionsDialog", "Permission status"),
        this
    );
    auto *statusLayout = new QVBoxLayout(statusGroup);

    m_statusLabel = new QLabel(statusGroup);
    m_statusLabel->setWordWrap(true);
    statusLayout->addWidget(m_statusLabel);

    layout->addWidget(statusGroup);

    m_output = new QPlainTextEdit(this);
    m_output->setReadOnly(true);
    m_output->setPlaceholderText(
        QCoreApplication::translate("PermissionsDialog", "Operation results will appear here.")
    );
    m_output->setMaximumBlockCount(500);
    layout->addWidget(m_output, 1);

    auto *buttonsLayout = new QHBoxLayout();

    m_configureButton = new QPushButton(
        QCoreApplication::translate("PermissionsDialog", "Configure permissions…"),
        this
    );
    m_removeButton = new QPushButton(
        QCoreApplication::translate("PermissionsDialog", "Remove permissions…"),
        this
    );
    auto *closeButton = new QPushButton(
        QCoreApplication::translate("PermissionsDialog", "Close"),
        this
    );

    buttonsLayout->addWidget(m_configureButton);
    buttonsLayout->addWidget(m_removeButton);
    buttonsLayout->addStretch();
    buttonsLayout->addWidget(closeButton);

    layout->addLayout(buttonsLayout);

    connect(
        m_configureButton,
        &QPushButton::clicked,
        this,
        [this]() { configurePermissions(); }
    );

    connect(
        m_removeButton,
        &QPushButton::clicked,
        this,
        [this]() { removePermissions(); }
    );

    connect(
        closeButton,
        &QPushButton::clicked,
        this,
        &QDialog::accept
    );

    refreshStatus();
}

void PermissionsDialog::refreshStatus()
{
    const bool configured = QFileInfo::exists(
        QString::fromLatin1(StatusPath)
    );
    const bool groupExists = getgrnam(GroupName) != nullptr;

    bool userInGroup = false;

    if (groupExists) {
        const group *groupInfo = getgrnam(GroupName);

        if (groupInfo) {
            const gid_t groupId = groupInfo->gr_gid;
            userInGroup = (getgid() == groupId);

            const int count = getgroups(0, nullptr);

            if (!userInGroup && count > 0) {
                auto *groups = new gid_t[static_cast<size_t>(count)];

                if (getgroups(count, groups) >= 0) {
                    for (int i = 0; i < count; ++i) {
                        if (groups[i] == groupId) {
                            userInGroup = true;
                            break;
                        }
                    }
                }

                delete[] groups;
            }
        }
    }

    if (configured && groupExists && userInGroup) {
        m_statusLabel->setText(
            QCoreApplication::translate(
                "PermissionsDialog",
                "Permission configuration is registered and your session "
                "belongs to the kde-keycast group."
            )
        );
    } else if (!configured && !groupExists) {
        m_statusLabel->setText(
            QCoreApplication::translate("PermissionsDialog", "No permission configuration was detected.")
        );
    } else {
        m_statusLabel->setText(
            QCoreApplication::translate(
                "PermissionsDialog",
                "The configuration appears to be incomplete or cannot be "
                "fully verified. You can run the configuration again "
                "to check it."
            )
        );
    }

    m_configureButton->setEnabled(m_process == nullptr);
    m_removeButton->setEnabled(m_process == nullptr);
}

void PermissionsDialog::configurePermissions()
{
    const QString userName = currentUserName();

    if (userName.isEmpty()) {
        QMessageBox::critical(
            this,
            QStringLiteral("KDE KeyCast"),
            QCoreApplication::translate("PermissionsDialog", "Could not determine the current user.")
        );
        return;
    }

    const auto answer = QMessageBox::question(
        this,
        QCoreApplication::translate("PermissionsDialog", "Configure permissions"),
        QCoreApplication::translate(
            "PermissionsDialog",
            "Polkit will be configured and the current user will be added "
            "to the kde-keycast group to allow access to the keyboard and "
            "mouse backend without repeatedly asking for the password.\n\n"
            "You may need to log out and back in for the new group "
            "to take effect.\n\n"
            "Do you want to continue?"
        ),
        QMessageBox::Yes | QMessageBox::No,
        QMessageBox::No
    );

    if (answer != QMessageBox::Yes) {
        return;
    }

    startPermissionProcess(
        QStringLiteral("configure-kde-keycast-permissions"),
        {QStringLiteral("--apply"), userName}
    );
}

void PermissionsDialog::removePermissions()
{
    const auto answer = QMessageBox::question(
        this,
        QCoreApplication::translate("PermissionsDialog", "Remove permissions"),
        QCoreApplication::translate(
            "PermissionsDialog",
            "The Polkit rule and permissions registered by KDE KeyCast "
            "will be removed.\n\n"
            "You may need to log out and back in for the group changes "
            "to take effect in your session.\n\n"
            "Do you want to continue?"
        ),
        QMessageBox::Yes | QMessageBox::No,
        QMessageBox::No
    );

    if (answer != QMessageBox::Yes) {
        return;
    }

    startPermissionProcess(
        QStringLiteral("remove-kde-keycast-permissions"),
        {QStringLiteral("--apply")}
    );
}

void PermissionsDialog::startPermissionProcess(
    const QString &scriptName,
    const QStringList &arguments
)
{
    const QString scriptPath = qEnvironmentVariableIsSet("APPIMAGE")
        ? QDir(QStringLiteral("/usr/lib/kde-keycast-appimage"))
              .filePath(scriptName)
        : QDir(QCoreApplication::applicationDirPath())
              .filePath(scriptName);

    if (!QFileInfo(scriptPath).isExecutable()) {
        QMessageBox::critical(
            this,
            QStringLiteral("KDE KeyCast"),
            QCoreApplication::translate(
                "PermissionsDialog",
                "Executable script not found:\n%1"
            ).arg(scriptPath)
        );
        return;
    }

    const QString pkexecPath =
        QStandardPaths::findExecutable(QStringLiteral("pkexec"));

    if (pkexecPath.isEmpty()) {
        QMessageBox::critical(
            this,
            QStringLiteral("KDE KeyCast"),
            QCoreApplication::translate(
                "PermissionsDialog",
                "pkexec was not found. Check that Polkit is installed."
            )
        );
        return;
    }

    m_output->clear();
    m_output->appendPlainText(
        QCoreApplication::translate("PermissionsDialog", "Running: %1").arg(scriptName)
    );

    m_process = new QProcess(this);

    connect(
        m_process,
        &QProcess::readyReadStandardOutput,
        this,
        [this]() {
            m_output->appendPlainText(
                QString::fromLocal8Bit(m_process->readAllStandardOutput())
            );
        }
    );

    connect(
        m_process,
        &QProcess::readyReadStandardError,
        this,
        [this]() {
            m_output->appendPlainText(
                QString::fromLocal8Bit(m_process->readAllStandardError())
            );
        }
    );

    connect(
        m_process,
        &QProcess::errorOccurred,
        this,
        [this](QProcess::ProcessError error) {
            if (error == QProcess::FailedToStart) {
                m_output->appendPlainText(
                    QCoreApplication::translate("PermissionsDialog", "Could not start pkexec: %1")
                        .arg(m_process->errorString())
                );
                m_process->deleteLater();
                m_process = nullptr;
                refreshStatus();
            }
        }
    );

    connect(
        m_process,
        qOverload<int, QProcess::ExitStatus>(&QProcess::finished),
        this,
        [this](int exitCode, QProcess::ExitStatus exitStatus) {
            m_output->appendPlainText(
                exitStatus == QProcess::NormalExit && exitCode == 0
                    ? QCoreApplication::translate("PermissionsDialog", "\nOperation completed.")
                    : QCoreApplication::translate(
                          "PermissionsDialog",
                          "\nThe operation did not complete successfully "
                          "(exit code %1)."
                      ).arg(exitCode)
            );

            m_process->deleteLater();
            m_process = nullptr;
            refreshStatus();
        }
    );

    m_configureButton->setEnabled(false);
    m_removeButton->setEnabled(false);

    m_process->start(pkexecPath, QStringList{scriptPath} + arguments);
}

} // namespace KDEKeyCast
