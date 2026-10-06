/*
 * SPDX-FileCopyrightText: 2026 Marc G.
 * SPDX-License-Identifier: GPL-3.0-or-later
 */

import QtQuick
import QtCore
import org.kde.kwin

Item {
    id: root

    property var targetWindow: null
    property var keepAboveWindow: null

    /*
     * ---------------------------------------------------------------
     * Posición persistente
     * ---------------------------------------------------------------
     */

    Settings {
        id: settings

        property bool hasPosition: false
        property int windowX: 0
        property int windowY: 0
    }

    function isKeyCastWindow(client) {
        return client &&
               client.captionNormal === "KDE KeyCast" &&
               !client.popupWindow &&
               !client.popupMenu;
    }

    function restorePosition(client) {
        if (!isKeyCastWindow(client))
            return;

        if (client.deleted)
            return;

        if (!settings.hasPosition)
            return;

        console.log(
            "KDE KeyCast: restaurando posición",
            settings.windowX,
            settings.windowY
        );

        client.frameGeometry =
            Qt.rect(
                settings.windowX,
                settings.windowY,
                client.width,
                client.height
            );
    }

    function restorePositionWithRetry(client) {
        if (!isKeyCastWindow(client))
            return;

        targetWindow = client;

        restorePosition(client);

        restoreTimer.restart();
    }

    function savePosition(client) {
        if (!isKeyCastWindow(client))
            return;

        if (client.deleted)
            return;

        settings.windowX =
            Math.round(client.x);

        settings.windowY =
            Math.round(client.y);

        settings.hasPosition = true;

        console.log(
            "KDE KeyCast: guardando posición",
            settings.windowX,
            settings.windowY
        );
    }

    /*
     * Algunas ventanas todavía no aceptan la posición
     * inmediatamente después de aparecer.
     *
     * Seguimos el mismo patrón de RememberWindowPositions:
     * primer intento inmediato + segundo intento.
     */

    Timer {
        id: restoreTimer

        interval: 1000
        repeat: false

        onTriggered: {
            if (!targetWindow)
                return;

            if (targetWindow.deleted) {
                targetWindow = null;
                return;
            }

            restorePosition(
                targetWindow
            );
        }
    }

    /*
     * ---------------------------------------------------------------
     * Always on Top <-> C++
     * ---------------------------------------------------------------
     */

    DBusCall {
        id: getAlwaysOnTop

        service: "org.kde.KDEKeyCast"
        path: "/KDEKeyCast"
        dbusInterface: "org.kde.KDEKeyCast"
        method: "isAlwaysOnTop"

        onFinished: function(returnValue) {
            if (!keepAboveWindow)
                return;

            if (!isKeyCastWindow(keepAboveWindow))
                return;

            if (returnValue.length === 0)
                return;

            keepAboveWindow.keepAbove =
                Boolean(returnValue[0]);
        }
    }

    DBusCall {
        id: setAlwaysOnTop

        service: "org.kde.KDEKeyCast"
        path: "/KDEKeyCast"
        dbusInterface: "org.kde.KDEKeyCast"
        method: "setKeepAboveState"

        onFailed: {
            console.log(
                "KDE KeyCast: error sincronizando Always on Top"
            );
        }
    }

    function applyAlwaysOnTopState(client) {
        if (!isKeyCastWindow(client))
            return;

        keepAboveWindow =
            client;

        getAlwaysOnTop.call();
    }

    function syncAlwaysOnTopState(client) {
        if (!isKeyCastWindow(client))
            return;

        setAlwaysOnTop.arguments = [
            Boolean(client.keepAbove)
        ];

        setAlwaysOnTop.call();
    }

    /*
     * ---------------------------------------------------------------
     * Ventana KDE KeyCast
     * ---------------------------------------------------------------
     */

    function connectWindow(client) {
        if (!isKeyCastWindow(client))
            return;

        if (client.kdeKeyCastConnected)
            return;

        client.kdeKeyCastConnected =
            true;

        targetWindow =
            client;

        /*
         * Guardar al cerrar.
         *
         * Esto sigue el modelo de RememberWindowPositions.
         */
        client.closed.connect(
            function() {
                savePosition(client);

                if (targetWindow === client)
                    targetWindow = null;

                if (keepAboveWindow === client)
                    keepAboveWindow = null;
            }
        );

        /*
         * Guardar también al terminar un movimiento manual.
         *
         * Así la posición queda persistida incluso antes
         * de cerrar la aplicación.
         */
        client.interactiveMoveResizeFinished.connect(
            function() {
                savePosition(client);
            }
        );

        /*
         * Mantener sincronizado Always on Top con C++.
         */
        client.keepAboveChanged.connect(
            function() {
                syncAlwaysOnTopState(
                    client
                );
            }
        );

        applyAlwaysOnTopState(
            client
        );

        restorePositionWithRetry(
            client
        );
    }

    function findKeyCastWindow() {
        const windows =
            Workspace.stackingOrder;

        for (let i = 0;
             i < windows.length;
             ++i) {

            if (isKeyCastWindow(windows[i]))
                return windows[i];
        }

        return null;
    }

    /*
     * ---------------------------------------------------------------
     * Workspace
     * ---------------------------------------------------------------
     */

    Connections {
        target: Workspace

        function onWindowAdded(client) {
            if (!isKeyCastWindow(client))
                return;

            connectWindow(client);
        }
    }

    Connections {
        target: Workspace

        function onWindowRemoved(client) {
            if (client === targetWindow)
                targetWindow = null;

            if (client === keepAboveWindow)
                keepAboveWindow = null;
        }
    }

    /*
     * ---------------------------------------------------------------
     * Always on Top
     * ---------------------------------------------------------------
     *
     * ShortcutHandler es la API QML nativa de KWin para
     * registrar shortcuts globales.
     */

    function toggleAlwaysOnTop() {
        const client =
            findKeyCastWindow();

        if (!client)
            return;

        client.keepAbove =
            !client.keepAbove;
    }

    ShortcutHandler {
        name: "kde-keycast-toggle-keep-above"
        text: "KDE KeyCast: Toggle Always on Top"
        sequence: "Ctrl+Alt+Shift+T"

        onActivated: {
            root.toggleAlwaysOnTop();
        }
    }

    /*
     * ---------------------------------------------------------------
     * Inicialización
     * ---------------------------------------------------------------
     */

    Component.onCompleted: {
        const client =
            findKeyCastWindow();

        if (client)
            connectWindow(client);
    }
}
