#pragma once

#include <QString>

namespace KDEKeyCast {

class KWinIntegration
{
public:
    static bool isInstalled();
    static bool isEnabled();

    static bool install(QString *errorMessage = nullptr);
    static bool uninstall();
    static bool enable();
    static bool disable();
};

} // namespace KDEKeyCast
