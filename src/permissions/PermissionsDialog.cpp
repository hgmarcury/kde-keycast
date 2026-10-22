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
    setWindowTitle(QStringLiteral("Permisos de entrada — KDE KeyCast"));
    setModal(true);
    resize(520, 400);

    auto *layout = new QVBoxLayout(this);

    auto *description = new QLabel(
        QStringLiteral(
            "KDE KeyCast necesita acceder a eventos del teclado y del ratón "
            "para mostrar las pulsaciones.\n\n"
            "Puedes configurar permisos mediante Polkit para permitir que "
            "el backend incluido se ejecute sin solicitar la contraseña "
            "repetidamente. Esto concede acceso a eventos de entrada; "
            "configúralo solo si confías en la aplicación."
        ),
        this
    );
    description->setWordWrap(true);
    layout->addWidget(description);

    auto *statusGroup = new QGroupBox(
        QStringLiteral("Estado de los permisos"),
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
        QStringLiteral("Aquí aparecerán los resultados de las operaciones.")
    );
    m_output->setMaximumBlockCount(500);
    layout->addWidget(m_output, 1);

    auto *buttonsLayout = new QHBoxLayout();

    m_configureButton = new QPushButton(
        QStringLiteral("Configurar permisos…"),
        this
    );
    m_removeButton = new QPushButton(
        QStringLiteral("Retirar permisos…"),
        this
    );
    auto *closeButton = new QPushButton(
        QStringLiteral("Cerrar"),
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
            QStringLiteral(
                "La configuración de permisos está registrada y tu sesión "
                "pertenece al grupo kde-keycast."
            )
        );
    } else if (!configured && !groupExists) {
        m_statusLabel->setText(
            QStringLiteral("No se detecta una configuración de permisos.")
        );
    } else {
        m_statusLabel->setText(
            QStringLiteral(
                "La configuración parece incompleta o no puede verificarse "
                "por completo. Puedes volver a ejecutar la configuración "
                "para comprobarla."
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
            QStringLiteral("No se ha podido determinar el usuario actual.")
        );
        return;
    }

    const auto answer = QMessageBox::question(
        this,
        QStringLiteral("Configurar permisos"),
        QStringLiteral(
            "Se configurará Polkit y se añadirá el usuario actual al grupo "
            "kde-keycast para permitir el acceso al backend de teclado y "
            "ratón sin solicitar la contraseña repetidamente.\n\n"
            "Es posible que tengas que cerrar la sesión y volver a entrar "
            "para que el nuevo grupo se aplique.\n\n"
            "¿Quieres continuar?"
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
        QStringLiteral("Retirar permisos"),
        QStringLiteral(
            "Se intentará retirar la regla de Polkit y los permisos "
            "registrados por KDE KeyCast.\n\n"
            "Puede que tengas que cerrar la sesión y volver a entrar para "
            "que los cambios de grupo se reflejen en tu sesión.\n\n"
            "¿Quieres continuar?"
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
            QStringLiteral(
                "No se encuentra el script ejecutable:\n%1"
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
            QStringLiteral(
                "No se encuentra pkexec. Comprueba que Polkit esté instalado."
            )
        );
        return;
    }

    m_output->clear();
    m_output->appendPlainText(
        QStringLiteral("Ejecutando: %1").arg(scriptName)
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
                    QStringLiteral("No se pudo iniciar pkexec: %1")
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
                    ? QStringLiteral("\nOperación completada.")
                    : QStringLiteral(
                          "\nLa operación no terminó correctamente "
                          "(código %1)."
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
