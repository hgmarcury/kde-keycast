#include "StatusToast.h"

#include <QLabel>
#include <QTimer>
#include <QWidget>

namespace KDEKeyCast {

namespace {

QLabel *createToast(
    QWidget *parent,
    const QString &text
)
{
    auto *toast = new QLabel(
        text,
        parent
    );

    toast->setWindowFlags(
        Qt::Tool |
        Qt::FramelessWindowHint |
        Qt::WindowStaysOnTopHint
    );

    toast->setAttribute(
        Qt::WA_DeleteOnClose
    );

    toast->setAlignment(
        Qt::AlignCenter
    );

    toast->setMargin(12);

    toast->adjustSize();

    const QPoint position =
        parent->rect().center() -
        QPoint(
            toast->width() / 2,
            toast->height() / 2
        );

    toast->move(position);

    return toast;
}

void showToast(
    QWidget *parent,
    const QString &text
)
{
    QLabel *toast =
        createToast(parent, text);

    toast->show();

    QTimer::singleShot(
        2500,
        toast,
        &QWidget::close
    );
}

} // namespace

void StatusToast::showInstalling(QWidget *parent)
{
    showToast(
        parent,
        QStringLiteral(
            "Installing KWin integration…"
        )
    );
}

void StatusToast::showEnabling(QWidget *parent)
{
    showToast(
        parent,
        QStringLiteral(
            "Enabling KWin integration…"
        )
    );
}

void StatusToast::showUninstalling(QWidget *parent)
{
    showToast(
        parent,
        QStringLiteral(
            "Uninstalling KWin integration…"
        )
    );
}

void StatusToast::showInstalled(QWidget *parent)
{
    showToast(
        parent,
        QStringLiteral(
            "✓ KWin integration installed"
        )
    );
}

void StatusToast::showEnabled(QWidget *parent)
{
    showToast(
        parent,
        QStringLiteral(
            "✓ KWin integration enabled"
        )
    );
}

void StatusToast::showUninstalled(QWidget *parent)
{
    showToast(
        parent,
        QStringLiteral(
            "✓ KWin integration uninstalled"
        )
    );
}

void StatusToast::showFailed(
    QWidget *parent,
    const QString &error
)
{
    showToast(
        parent,
        QStringLiteral(
            "✕ KWin integration failed\n%1"
        ).arg(error)
    );
}

} // namespace KDEKeyCast
