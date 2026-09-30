#pragma once

namespace KDEKeyCast {

class KWinIntegration
{
public:
    static bool isInstalled();
    static bool isEnabled();

    static bool install();
    static bool uninstall();
};

} // namespace KDEKeyCast
