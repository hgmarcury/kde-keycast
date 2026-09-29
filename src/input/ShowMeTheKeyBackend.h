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
        explicit ShowMeTheKeyBackend();

        ~ShowMeTheKeyBackend() override;

        bool start(EventHandler handler) override;
        void stop() override;

        bool isRunning() const;

    private:
        void readInput();
        void handleLine(const QByteArray &line);

        QProcess process_;
        EventHandler handler_;
    };

} // namespace KDEKeyCast
