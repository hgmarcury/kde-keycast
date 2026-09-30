#pragma once

namespace KDEKeyCast {

    class KWinIntegrationDialog
    {
    public:
        struct Result
        {
            bool install = false;
            bool dontShowAgain = false;
        };

        static Result askInstall();
        static void showError();
    };

} // namespace KDEKeyCast
