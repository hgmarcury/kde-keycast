#pragma once

#include "InputBackend.h"

#include <QProcess>

namespace KDEKeyCast {

class ShowMeTheKeyBackend final : public InputBackend
{
public:
    explicit ShowMeTheKeyBackend(
        QString executable = QStringLiteral("showmethekey-cli"));

    ~ShowMeTheKeyBackend() override;

    bool start(EventHandler handler) override;
    void stop() override;

    bool isRunning() const;

private:
    void readInput();
    void handleLine(const QByteArray &line);

    QString executable_;
    QProcess process_;
    EventHandler handler_;
};

} // namespace KDEKeyCast
