
#include "ShortcutHelpDialog.h"

#include "../core/Shortcuts.h"

#include <QDialog>
#include <QHBoxLayout>
#include <QLabel>
#include <QPushButton>
#include <QVBoxLayout>

ShortcutHelpDialog::ShortcutHelpDialog(
    QWidget *parent
)
    : QDialog(parent)
{
    setWindowTitle(
        QStringLiteral("Keyboard shortcuts")
    );

    setModal(true);
    setFixedWidth(360);

    auto *layout =
        new QVBoxLayout(this);

    layout->setContentsMargins(
        20,
        18,
        20,
        18
    );

    layout->setSpacing(8);

    auto addShortcut =
        [this, layout](
            const QString &shortcut,
            const QString &description)
    {
        auto *row =
            new QHBoxLayout();

        row->setSpacing(16);

        auto *keyLabel =
            new QLabel(
                shortcut,
                this
            );

        auto *descriptionLabel =
            new QLabel(
                description,
                this
            );

        keyLabel->setMinimumWidth(110);

        keyLabel->setAlignment(
            Qt::AlignLeft |
            Qt::AlignVCenter
        );

        keyLabel->setStyleSheet(
            QStringLiteral(
                "font-weight: 600;"
            )
        );

        descriptionLabel->setAlignment(
            Qt::AlignLeft |
            Qt::AlignVCenter
        );

        row->addWidget(keyLabel);
        row->addWidget(
            descriptionLabel,
            1
        );

        layout->addLayout(row);
    };

    addShortcut(
        QString::fromLatin1(
            KeyCastShortcuts::CompactMode
        ),
        QStringLiteral("Compact mode")
    );

    addShortcut(
        QString::fromLatin1(
            KeyCastShortcuts::AlwaysOnTop
        ),
        QStringLiteral("Always on top")
    );

    addShortcut(
        QString::fromLatin1(
            KeyCastShortcuts::HideKeys
        ),
        QStringLiteral("Hide keys")
    );

    addShortcut(
        QString::fromLatin1(
            KeyCastShortcuts::ClickThrough
        ),
        QStringLiteral("Click-through")
    );

    addShortcut(
        QString::fromLatin1(
            KeyCastShortcuts::PauseInput
        ),
        QStringLiteral("Pause input")
    );

    layout->addSpacing(8);

    auto *closeButton =
        new QPushButton(
            QStringLiteral("Close"),
            this
        );

    closeButton->setDefault(true);

    connect(
        closeButton,
        &QPushButton::clicked,
        this,
        &QDialog::accept
    );

    layout->addWidget(
        closeButton,
        0,
        Qt::AlignRight
    );
}
