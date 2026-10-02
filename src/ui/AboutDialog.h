/*
 * SPDX-FileCopyrightText: 2026 Marc G.
 * SPDX-License-Identifier: GPL-3.0-or-later
 */


#pragma once

#include <QDialog>

class QWidget;

class AboutDialog : public QDialog
{
public:
    explicit AboutDialog(
        QWidget *parent = nullptr
    );
};
