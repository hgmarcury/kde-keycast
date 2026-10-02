/*
 * SPDX-FileCopyrightText: 2026 Marc G.
 * SPDX-License-Identifier: GPL-3.0-or-later
 */


#pragma once

#include "InputBackend.h"
#include "InputReader.h"

#include <QProcess>

namespace KDEKeyCast {

class ShowMeTheKeyBackend final
    : public QObject
    , public InputBackend
{
    Q_OBJECT

public:
    explicit ShowMeTheKeyBackend();

    ~ShowMeTheKeyBackend() override;

    bool start(EventHandler handler) override;
    void stop() override;

    bool isRunning() const;

private:
    void readInput();

    QProcess process_;
    InputReader inputReader_;
};

} // namespace KDEKeyCast
