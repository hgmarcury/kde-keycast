#pragma once

#include "InputEvent.h"

#include <functional>

#include <QByteArray>

namespace KDEKeyCast {

class InputReader
{
public:
    using EventHandler =
        std::function<void(const InputEvent &)>;

    InputReader() = default;

    void setEventHandler(EventHandler handler);

    void processLine(const QByteArray &line);

private:
    EventHandler handler_;

    void emitEvent(const InputEvent &event);
};

} // namespace KDEKeyCast
