
#pragma once

#include <QPoint>
#include <functional>

class QWidget;

class QMenu;

namespace KDEKeyCast {

struct ContextMenuState
{
    bool compactMode = false;
    bool lightMode = false;
    bool hideKeys = false;
    bool onlyShortcuts = false;
    bool pauseInput = false;
    bool clickThrough = false;
    bool alwaysOnTop = true;
    bool kwinIntegrationInstalled = false;
    bool kwinIntegrationEnabled = false;
};

class ContextMenu
{
public:
    struct Actions
    {
        std::function<void()> toggleCompactMode;
        std::function<void()> toggleLightMode;
        std::function<void()> toggleHideKeys;
        std::function<void()> toggleOnlyShortcuts;
        std::function<void()> togglePauseInput;
        std::function<void()> toggleClickThrough;
        std::function<void()> toggleAlwaysOnTop;

        std::function<void()> manageKWinIntegration;

        std::function<void()> showShortcutHelp;
        std::function<void()> showAbout;
        std::function<void()> quit;

        std::function<void(QMenu &)> addDisplayDuration;

    };

    static void exec(
        QWidget *parent,
        const QPoint &globalPos,
        const ContextMenuState &state,
        const Actions &actions
    );
};

} // namespace KDEKeyCast
