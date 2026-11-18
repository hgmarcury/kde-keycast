/*
 * SPDX-FileCopyrightText: 2026 Marc G.
 * SPDX-License-Identifier: GPL-3.0-or-later
 */

#include "AboutDialog.h"

#include <QCoreApplication>
#include <QDialog>
#include <QLabel>
#include <QPushButton>
#include <QVBoxLayout>

AboutDialog::AboutDialog(QWidget *parent) : QDialog(parent) {
  setWindowTitle(
      QCoreApplication::translate("AboutDialog", "About KDE KeyCast"));

  setModal(true);
  setFixedWidth(380);

  auto *layout = new QVBoxLayout(this);

  layout->setContentsMargins(24, 22, 24, 20);

  layout->setSpacing(10);

  auto *title = new QLabel(QStringLiteral("KDE KeyCast"), this);

  title->setAlignment(Qt::AlignCenter);

  title->setStyleSheet(QStringLiteral("font-size: 18px;"
                                      "font-weight: 600;"));

  layout->addWidget(title);

  auto *description =
      new QLabel(QCoreApplication::translate("AboutDialog",
                                             "Keyboard and mouse visualizer "
                                             "for KDE Plasma on Wayland."),
                 this);

  description->setAlignment(Qt::AlignCenter);

  description->setWordWrap(true);

  layout->addWidget(description);

  layout->addSpacing(6);

  auto *details =
      new QLabel(QCoreApplication::translate(
                     "AboutDialog", "Built with Qt 6 and libxkbcommon."),
                 this);

  details->setAlignment(Qt::AlignCenter);

  details->setStyleSheet(QStringLiteral("color: palette(mid);"));

  layout->addWidget(details);

  layout->addSpacing(8);

  auto *closeButton = new QPushButton(
      QCoreApplication::translate("AboutDialog", "Close"), this);

  closeButton->setDefault(true);

  connect(closeButton, &QPushButton::clicked, this, &QDialog::accept);

  layout->addWidget(closeButton, 0, Qt::AlignRight);
}
