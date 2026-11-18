/*
 * SPDX-FileCopyrightText: 2026 Marc G.
 * SPDX-License-Identifier: GPL-3.0-or-later
 */

#include "ShortcutHelpDialog.h"

#include "../core/Shortcuts.h"

#include <QCoreApplication>
#include <QDialog>
#include <QHBoxLayout>
#include <QLabel>
#include <QPushButton>
#include <QVBoxLayout>

ShortcutHelpDialog::ShortcutHelpDialog(QWidget *parent) : QDialog(parent) {
  setWindowTitle(
      QCoreApplication::translate("ShortcutHelpDialog", "Keyboard shortcuts"));

  setModal(true);
  setFixedWidth(360);

  auto *layout = new QVBoxLayout(this);

  layout->setContentsMargins(20, 18, 20, 18);

  layout->setSpacing(8);

  auto addShortcut = [this, layout](const QString &shortcut,
                                    const QString &description) {
    auto *row = new QHBoxLayout();

    row->setSpacing(16);

    auto *keyLabel = new QLabel(shortcut, this);

    auto *descriptionLabel = new QLabel(description, this);

    keyLabel->setMinimumWidth(110);

    keyLabel->setAlignment(Qt::AlignLeft | Qt::AlignVCenter);

    keyLabel->setStyleSheet(QStringLiteral("font-weight: 600;"));

    descriptionLabel->setAlignment(Qt::AlignLeft | Qt::AlignVCenter);

    row->addWidget(keyLabel);
    row->addWidget(descriptionLabel, 1);

    layout->addLayout(row);
  };

  addShortcut(
      QString::fromLatin1(KeyCastShortcuts::CompactMode),
      QCoreApplication::translate("ShortcutHelpDialog", "Compact mode"));

  addShortcut(
      QString::fromLatin1(KeyCastShortcuts::AlwaysOnTop),
      QCoreApplication::translate("ShortcutHelpDialog", "Always on top"));

  addShortcut(QString::fromLatin1(KeyCastShortcuts::HideKeys),
              QCoreApplication::translate("ShortcutHelpDialog", "Hide keys"));

  addShortcut(
      QString::fromLatin1(KeyCastShortcuts::ClickThrough),
      QCoreApplication::translate("ShortcutHelpDialog", "Click-through"));

  addShortcut(QString::fromLatin1(KeyCastShortcuts::PauseInput),
              QCoreApplication::translate("ShortcutHelpDialog", "Pause input"));

  layout->addSpacing(8);

  auto *closeButton = new QPushButton(
      QCoreApplication::translate("ShortcutHelpDialog", "Close"), this);

  closeButton->setDefault(true);

  connect(closeButton, &QPushButton::clicked, this, &QDialog::accept);

  layout->addWidget(closeButton, 0, Qt::AlignRight);
}
