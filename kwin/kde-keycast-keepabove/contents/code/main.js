let targetPid = -1;
let targetWindow = null;
let targetWindowConnected = false;
let keepAboveState = null;

let initialSyncActive = false;
let initialDesiredState = null;


const keyCastService =
    "org.kde.KDEKeyCast";

const keyCastPath =
    "/KDEKeyCast";

const keyCastInterface =
    "org.kde.KDEKeyCast";

const keyCastCaption =
    "KDE KeyCast";

function isKeyCastWindow(window)
{
    return window &&
           window.pid === targetPid &&
           window.captionNormal === keyCastCaption &&
           !window.popupWindow &&
           !window.popupMenu;
}


function notifyKeyCast(state)
{
    const enabled = Boolean(state);

    print(
        "KDE KeyCast: enviando estado keepAbove =",
        enabled
    );

    callDBus(
        keyCastService,
        keyCastPath,
        keyCastInterface,
        "setKeepAboveState",
        enabled
    );
}


/*
 * Conecta exactamente un objeto Window de KWin.
 *
 * KWin puede destruir/recrear el objeto Window aunque
 * el proceso siga teniendo el mismo PID.
 */
function connectTargetWindow(window)
{
    if (!isKeyCastWindow(window))
        return;

    if (targetWindow === window &&
        targetWindowConnected) {

        return;
    }


    targetWindow =
        window;

    targetWindowConnected =
        true;


    print(
        "KDE KeyCast: target window connected, pid =",
        window.pid
    );


    /*
     * -----------------------------------------------------------
     * Obtener la configuración persistida.
     * -----------------------------------------------------------
     */
    callDBus(
        keyCastService,
        keyCastPath,
        keyCastInterface,
        "isAlwaysOnTop",
        function(state)
        {
            if (window !== targetWindow)
                return;

            if (!isKeyCastWindow(window))
                return;


            const desiredState =
                Boolean(state);


            print(
                "KDE KeyCast: estado persistido =",
                desiredState,
                "estado actual KWin =",
                Boolean(window.keepAbove)
            );


            /*
             * Aplicamos SIEMPRE la configuración persistida
             * a la nueva ventana.
             */
            window.keepAbove =
                desiredState;


            keepAboveState =
                desiredState;


            /*
             * A partir de AQUÍ, los cambios de KWin sí son
             * cambios reales que deben volver a C++.
             */
            window.keepAboveChanged.connect(
                function()
                {
                    if (window !== targetWindow)
                        return;

                    if (!targetWindowConnected)
                        return;

                    if (!isKeyCastWindow(window))
                        return;


                    keepAboveState =
                        Boolean(window.keepAbove);


                    print(
                        "KDE KeyCast: keepAboveChanged =",
                        keepAboveState
                    );


                    notifyKeyCast(
                        keepAboveState
                    );
                }
            );
        }
    );
}
/*
 * Busca la ventana actual de la instancia.
 */
function findTargetWindow()
{
    if (targetPid <= 0)
        return null;


    const windows =
        workspace.windowList();


    for (const window of windows) {

        if (isKeyCastWindow(window))
            return window;
    }


    return null;
}

/*
 * Toggle solicitado por Qt mediante el shortcut global.
 *
 * KWin realiza el cambio.
 */
function toggleAlwaysOnTop()
{
    const window =
        findTargetWindow();


    if (!window) {

        print(
            "KDE KeyCast: target window not found"
        );

        resolveTargetPid();

        return;
    }


    /*
     * Aseguramos que la ventana encontrada es también
     * nuestra ventana conectada actual.
     */
    if (window !== targetWindow) {

        print(
            "KDE KeyCast: nueva referencia de ventana detectada"
        );

        connectTargetWindow(window);
    }


    if (window !== targetWindow ||
        !targetWindowConnected) {

        print(
            "KDE KeyCast: no se pudo conectar la ventana"
        );

        return;
    }


    const newState =
        !Boolean(window.keepAbove);


    print(
        "KDE KeyCast: keepAbove =",
        newState
    );


    /*
     * Este es el ÚNICO lugar donde cambiamos
     * window.keepAbove directamente.
     */
    window.keepAbove =
        newState;
}


/*
 * Shortcut global.
 */
registerShortcut(
    "kde-keycast-toggle-keep-above",
    "KDE KeyCast: Toggle Always on Top",
    "Ctrl+Shift+T",
    toggleAlwaysOnTop
);


/*
 * Obtiene el PID de la instancia actual de KDE KeyCast.
 */
function resolveTargetPid()
{
    callDBus(
        keyCastService,
        keyCastPath,
        keyCastInterface,
        "processId",
        function(pid)
        {
            const value =
                Number(pid);


            if (!Number.isFinite(value))
                return;


            /*
             * Nueva instancia del proceso.
             */
            if (targetPid !== value) {

                print(
                    "KDE KeyCast: nueva instancia detectada:",
                    value,
                    "(anterior:",
                    targetPid,
                    ")"
                );

                targetPid =
                    value;

                targetWindow =
                    null;

                targetWindowConnected =
                    false;

                keepAboveState =
                    null;
            }


            print(
                "KDE KeyCast: target pid =",
                targetPid
            );


            const window =
                findTargetWindow();


            if (window) {

                connectTargetWindow(
                    window
                );

            } else {

                print(
                    "KDE KeyCast: target window not found"
                );
            }
        }
    );
}


/*
 * ---------------------------------------------------------------
 * Nueva ventana
 * ---------------------------------------------------------------
 */

workspace.windowAdded.connect(
    function(window)
    {
        if (!window)
            return;

        /*
         * Solo nos interesan ventanas principales de KDE KeyCast.
         *
         * No podemos comprobar todavía el PID porque puede ser
         * una instancia nueva del proceso.
         */
        if (window.captionNormal !== keyCastCaption)
            return;

        if (window.popupWindow ||
            window.popupMenu)
            return;


        print(
            "KDE KeyCast: ventana detectada, resolviendo PID..."
        );


        resolveTargetPid();
    }
);


/*
 * ---------------------------------------------------------------
 * Ventana eliminada
 * ---------------------------------------------------------------
 */

workspace.windowRemoved.connect(
    function(window)
    {
        if (window !== targetWindow)
            return;


        print(
            "KDE KeyCast: target window disconnected"
        );


        /*
         * MUY IMPORTANTE:
         *
         * Primero invalidamos la conexión.
         * De este modo cualquier callback pendiente
         * de la ventana antigua queda descartado.
         */
        targetWindowConnected =
            false;


        targetWindow =
            null;

        initialSyncActive =
            false;

        initialDesiredState =
            null;


        /*
         * Conservamos keepAboveState solamente como
         * información del último estado conocido.
         *
         * No lo usamos para forzar el estado de una
         * nueva ventana.
         */
    }
);


/*
 * Inicialización.
 */
resolveTargetPid();
