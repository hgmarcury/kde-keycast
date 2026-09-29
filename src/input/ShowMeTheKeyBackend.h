#pragma once

#include "InputBackend.h"

#include <QProcess>
#include <QString>

namespace KDEKeyCast {


    class ShowMeTheKeyBackend final
        : public QObject
        , public InputBackend
    {
        Q_OBJECT

    public:
        explicit ShowMeTheKeyBackend(
            QString executable =
                QStringLiteral("showmethekey-cli")
        );

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
