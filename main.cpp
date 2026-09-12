#include <QApplication>
#include <QWidget>
#include <QPainter>
#include <QTimer>
#include <QProcess>
#include <QJsonDocument>
#include <QJsonObject>
#include <QDateTime>
#include <QFont>
#include <QFontMetrics>
#include <QColor>
#include <QVector>
#include <QHash>
#include <QSet>
#include <QMouseEvent>
#include <QWindow>
#include <QScreen>
#include <QSocketNotifier>

#include <wayland-client.h>

#include <xkbcommon/xkbcommon.h>

#include <sys/mman.h>
#include <unistd.h>

#include <cerrno>
#include <cstring>

struct VisualEntry {
    QStringList keys;
    qint64 created = 0;
    qint64 expires = 0;
    bool combination = false;
    int repeatCount = 1;
};


class KeyCast : public QWidget
{
public:
    KeyCast()
    {
        setAttribute(Qt::WA_TranslucentBackground);
        setAttribute(Qt::WA_NoSystemBackground);
        setAttribute(Qt::WA_OpaquePaintEvent, false);
        setAttribute(Qt::WA_TransparentForMouseEvents, false);

        setWindowFlags(
            Qt::FramelessWindowHint |
            Qt::Tool |
            Qt::WindowStaysOnTopHint
        );

        setWindowTitle(QStringLiteral("KDE KeyCast"));
        resize(400, 106);
        setFixedSize(400, 106);


        if (auto *screen = QGuiApplication::primaryScreen()) {
            const QRect area = screen->availableGeometry();

            move(
                area.left() + (area.width() - width()) / 2,
                area.top() + area.height() - height() - 80
            );
        }

        setupWaylandKeyboard();

        process.setProgram(
            QStringLiteral("/usr/bin/showmethekey-cli")
        );

        connect(
            &process,
            &QProcess::readyReadStandardOutput,
            this,
            &KeyCast::readInput
        );

        connect(
            &process,
            qOverload<int, QProcess::ExitStatus>(&QProcess::finished),
            this,
            [this](int, QProcess::ExitStatus) {
                QTimer::singleShot(1000, this, [this] {
                    if (process.state() == QProcess::NotRunning)
                        process.start();
                });
            }
        );

        process.start();

        if (!process.waitForStarted(1500)) {
            qWarning(
                "KDE KeyCast: no se pudo iniciar showmethekey-cli: %s",
                qPrintable(process.errorString())
            );
        }

        timer.setInterval(16);

        connect(
            &timer,
            &QTimer::timeout,
            this,
            [this] {

                /*
                 * Detectamos dónde está realmente la ventana.
                 * Esto permite actualizar el lado de expansión
                 * después de que el usuario la haya arrastrado.
                 */
                if (!lastWindowGeometry.isNull() &&
                    geometry() != lastWindowGeometry) {

                    lastWindowGeometry = geometry();

                    const QRect screenArea =
                        QGuiApplication::primaryScreen()
                            ? QGuiApplication::primaryScreen()
                                ->availableGeometry()
                            : QRect();

                    if (!screenArea.isNull()) {

                        const int screenCenter =
                            screenArea.left() +
                            screenArea.width() / 2;

                        const int windowCenter =
                            geometry().left() +
                            geometry().width() / 2;

                        if (windowCenter <
                            screenCenter - screenArea.width() / 20) {

                            expansionSide = -1;

                        } else if (windowCenter >
                                   screenCenter + screenArea.width() / 20) {

                            expansionSide = 1;

                        } else {

                            expansionSide = 0;
                        }
                    }
                }

                trimVisualQueue();
                update();
            }
        );

        timer.start();

        show();
    }

    ~KeyCast()
    {
        if (waylandNotifier)
            delete waylandNotifier;

        if (keyboard)
            wl_keyboard_destroy(keyboard);

        if (seat)
            wl_seat_destroy(seat);

        if (registry)
            wl_registry_destroy(registry);

        if (display)
            wl_display_disconnect(display);

        if (xkbState)
            xkb_state_unref(xkbState);

        if (xkbKeymap)
            xkb_keymap_unref(xkbKeymap);
    }

protected:

    void mousePressEvent(QMouseEvent *event) override
    {
        if (event->button() == Qt::LeftButton) {

            if (windowHandle()) {

                lastWindowGeometry = geometry();

                const QRect screenArea =
                    QGuiApplication::primaryScreen()
                        ? QGuiApplication::primaryScreen()
                            ->availableGeometry()
                        : QRect();

                if (!screenArea.isNull()) {

                    const int screenCenter =
                        screenArea.left() +
                        screenArea.width() / 2;

                    const int windowCenter =
                        geometry().left() +
                        geometry().width() / 2;

                    if (windowCenter < screenCenter) {
                        expansionSide = -1;
                    } else if (windowCenter > screenCenter) {
                        expansionSide = 1;
                    } else {
                        expansionSide = 0;
                    }
                }

                if (windowHandle()->startSystemMove()) {
                    event->accept();
                    return;
                }
            }
        }

        QWidget::mousePressEvent(event);
    }

    void paintEvent(QPaintEvent *) override
{
    QPainter p(this);

    p.setRenderHint(QPainter::Antialiasing);
    p.setRenderHint(QPainter::TextAntialiasing);

    trimVisualQueue();

    if (visualQueue.isEmpty())
        return;

    /*
     * ---------------------------------------------------------------
     * Tipografía
     * ---------------------------------------------------------------
     */

    QFont font =
        QApplication::font();

    font.setPixelSize(18);
    font.setBold(true);

    p.setFont(font);

    QFontMetrics fm(font);

    /*
     * ---------------------------------------------------------------
     * Geometría
     * ---------------------------------------------------------------
     */

    const int spacing = 8;
    const int sideMargin = 12;

    const int boxHeight = 52;
    const int minimumBoxWidth = 
        compactMode ? 42 : 44;

    /*
     * Dejamos unos píxeles para la sombra inferior de las teclas.
     */
    const int top =
        (height() - boxHeight) / 2 - 2;

    /*
     * ---------------------------------------------------------------
     * Construcción de las tarjetas
     * ---------------------------------------------------------------
     *
     * Empezamos por la entrada más reciente y vamos hacia atrás.
     *
     * Si no caben todas, desaparecen las más antiguas.
     *
     * El orden visual continúa siendo:
     *
     *     antigua -> reciente
     *
     * independientemente de dónde esté situada la ventana.
     */

    struct DrawEntry {
        int index;
        int width;
        QString text;
    };

    QVector<DrawEntry> visibleEntries;

    int usedWidth = 0;

    for (int i = visualQueue.size() - 1; i >= 0; --i) {

        const VisualEntry &entry =
            visualQueue[i];

        QStringList visualKeys;

for (const QString &key : entry.keys) {

    QString visualKey = key;

    if (compactMode)
        visualKey =
            compactDisplayName(key);

    visualKeys.append(visualKey);
}

QString text =
    visualKeys.join(
        QStringLiteral(" + ")
    );

if (entry.repeatCount > 1) {
    text +=
        QStringLiteral(" ×%1")
            .arg(entry.repeatCount);
}


        const int textWidth =
            fm.horizontalAdvance(text);

        const int boxWidth =
            qMax(
                minimumBoxWidth,
                textWidth + 24
            );

        const int requiredWidth =
            visibleEntries.isEmpty()
                ? boxWidth
                : boxWidth + spacing;

        if (usedWidth +
                requiredWidth +
                sideMargin * 2 >
            width()) {

            break;
        }

        visibleEntries.prepend(
            DrawEntry{
                i,
                boxWidth,
                text
            }
        );

        usedWidth +=
            requiredWidth;
    }

    if (visibleEntries.isEmpty())
        return;

    /*
     * ---------------------------------------------------------------
     * Centrado
     * ---------------------------------------------------------------
     */

    int x =
        (width() - usedWidth) / 2;

    /*
     * ---------------------------------------------------------------
     * Colores
     * ---------------------------------------------------------------
     */

    const QColor shadowColor(
        0,
        0,
        0,
        150
    );

    const QColor keyTop(
        62,
        64,
        69,
        255
    );

    const QColor keyBottom(
        39,
        41,
        45,
        255
    );

    const QColor keyTopHighlight(
        255,
        255,
        255,
        30
    );

    const QColor keyBorder(
        255,
        255,
        255,
        45
    );

    const QColor keyBottomBorder(
        0,
        0,
        0,
        120
    );

    const QColor textColor(
        250,
        250,
        252,
        255
    );

    const QColor combinationBorder(
        255,
        255,
        255,
        75
    );

    /*
     * ---------------------------------------------------------------
     * Dibujar teclas
     * ---------------------------------------------------------------
     */

    for (const DrawEntry &draw : visibleEntries) {

        const VisualEntry &entry =
            visualQueue[draw.index];

        QRectF r(
            x,
            top,
            draw.width,
            boxHeight
        );

        /*
         * -----------------------------------------------------------
         * Sombra
         * -----------------------------------------------------------
         */

        const QRectF shadowRect =
            r.adjusted(
                0,
                3,
                0,
                4
            );

        p.setPen(Qt::NoPen);
        p.setBrush(shadowColor);

        p.drawRoundedRect(
            shadowRect,
            8,
            8
        );

        /*
         * -----------------------------------------------------------
         * Cuerpo de la tecla
         * -----------------------------------------------------------
         */

        QLinearGradient gradient(
            r.topLeft(),
            r.bottomLeft()
        );

        gradient.setColorAt(
            0.0,
            keyTop
        );

        gradient.setColorAt(
            1.0,
            keyBottom
        );

        p.setPen(Qt::NoPen);
        p.setBrush(gradient);

        p.drawRoundedRect(
            r,
            8,
            8
        );

        /*
         * -----------------------------------------------------------
         * Borde exterior
         * -----------------------------------------------------------
         */

        const QColor borderColor =
            entry.combination
                ? combinationBorder
                : keyBorder;

        p.setPen(
            QPen(
                borderColor,
                1.0
            )
        );

        p.setBrush(Qt::NoBrush);

        p.drawRoundedRect(
            r.adjusted(
                0.5,
                0.5,
                -0.5,
                -0.5
            ),
            8,
            8
        );

        /*
         * -----------------------------------------------------------
         * Brillo superior
         * -----------------------------------------------------------
         *
         * Una pequeña línea clara en la parte superior da sensación
         * de plástico/metal y evita el aspecto de "tarjeta plana".
         */

        QRectF highlightRect =
            r.adjusted(
                7,
                2,
                -7,
                -r.height() + 4
            );

        p.setPen(
            QPen(
                keyTopHighlight,
                1.0
            )
        );

        p.drawLine(
            highlightRect.topLeft(),
            highlightRect.topRight()
        );

        /*
         * -----------------------------------------------------------
         * Texto
         * -----------------------------------------------------------
         */

        p.setPen(textColor);
        p.setBrush(Qt::NoBrush);

        p.drawText(
            r,
            Qt::AlignCenter,
            draw.text
        );

        x +=
            draw.width +
            spacing;
    }
}



private:

    // Dirección de expansión del historial:
    // -1 = hacia la derecha
    //  1 = hacia la izquierda
    //  0 = centrado
    int expansionSide = 0;

    // Última geometría conocida de la ventana.
    QRect lastWindowGeometry;

    // ---------------------------------------------------------------
    // Linux / XKB
    // ---------------------------------------------------------------

    static xkb_keycode_t xkbKeycodeFromLinuxCode(int linuxCode)
    {
        if (linuxCode < 0)
            return XKB_KEYCODE_INVALID;

        /*
         * Wayland y XKB utilizan el mismo código evdev con un offset
         * de 8 para obtener el keycode XKB.
         */
        return static_cast<xkb_keycode_t>(
            linuxCode + 8
        );
    }

    static bool isModifier(const QString &key)
    {
        return
            key == QStringLiteral("KEY_LEFTCTRL") ||
            key == QStringLiteral("KEY_RIGHTCTRL") ||
            key == QStringLiteral("KEY_LEFTALT") ||
            key == QStringLiteral("KEY_RIGHTALT") ||
            key == QStringLiteral("KEY_LEFTSHIFT") ||
            key == QStringLiteral("KEY_RIGHTSHIFT") ||
            key == QStringLiteral("KEY_LEFTMETA") ||
            key == QStringLiteral("KEY_RIGHTMETA");
    }

    static bool isMouseButton(const QString &key)
    {
        return key.startsWith(
            QStringLiteral("BTN_")
        );
    }

    static bool containsControlCharacter(
        const QString &text
    )
    {
        for (const QChar ch : text) {

            const ushort u =
                ch.unicode();

            if (u < 0x20 || u == 0x7f)
                return true;
        }

        return false;
    }

    static QString keysymToText(
    xkb_keysym_t sym
)
{
    if (sym == XKB_KEY_NoSymbol)
        return QString();

    /*
     * ---------------------------------------------------------------
     * Teclas especiales
     * ---------------------------------------------------------------
     *
     * Estas teclas no representan un carácter Unicode que queramos
     * mostrar directamente.
     */
    switch (sym) {

    case XKB_KEY_Escape:
        return QStringLiteral("Esc");

    case XKB_KEY_Return:
        return QStringLiteral("Enter");

    case XKB_KEY_BackSpace:
        return QStringLiteral("Backspace");

    case XKB_KEY_Tab:
        return QStringLiteral("Tab");

    case XKB_KEY_space:
        return QStringLiteral("Space");

    case XKB_KEY_Delete:
        return QStringLiteral("Delete");

    case XKB_KEY_Insert:
        return QStringLiteral("Insert");

    case XKB_KEY_Home:
        return QStringLiteral("Home");

    case XKB_KEY_End:
        return QStringLiteral("End");

    case XKB_KEY_Left:
        return QStringLiteral("←");

    case XKB_KEY_Right:
        return QStringLiteral("→");

    case XKB_KEY_Up:
        return QStringLiteral("↑");

    case XKB_KEY_Down:
        return QStringLiteral("↓");

    case XKB_KEY_Page_Up:
        return QStringLiteral("Page Up");

    case XKB_KEY_Page_Down:
        return QStringLiteral("Page Down");

    case XKB_KEY_Print:
        return QStringLiteral("Print");

    case XKB_KEY_Sys_Req:
        return QStringLiteral("SysRq");

    case XKB_KEY_Pause:
        return QStringLiteral("Pause");

    case XKB_KEY_Scroll_Lock:
        return QStringLiteral("Scroll Lock");

    case XKB_KEY_Num_Lock:
        return QStringLiteral("Num Lock");

    case XKB_KEY_Caps_Lock:
        return QStringLiteral("Caps Lock");

    case XKB_KEY_F1:
        return QStringLiteral("F1");

    case XKB_KEY_F2:
        return QStringLiteral("F2");

    case XKB_KEY_F3:
        return QStringLiteral("F3");

    case XKB_KEY_F4:
        return QStringLiteral("F4");

    case XKB_KEY_F5:
        return QStringLiteral("F5");

    case XKB_KEY_F6:
        return QStringLiteral("F6");

    case XKB_KEY_F7:
        return QStringLiteral("F7");

    case XKB_KEY_F8:
        return QStringLiteral("F8");

    case XKB_KEY_F9:
        return QStringLiteral("F9");

    case XKB_KEY_F10:
        return QStringLiteral("F10");

    case XKB_KEY_F11:
        return QStringLiteral("F11");

    case XKB_KEY_F12:
        return QStringLiteral("F12");

    /*
     * Teclas muertas frecuentes.
     *
     * Las mostramos como símbolo visual en lugar de "dead_acute".
     */
    case XKB_KEY_dead_acute:
        return QStringLiteral("´");

    case XKB_KEY_dead_grave:
        return QStringLiteral("`");

    case XKB_KEY_dead_diaeresis:
        return QStringLiteral("¨");

    case XKB_KEY_dead_circumflex:
        return QStringLiteral("^");

    case XKB_KEY_dead_tilde:
        return QStringLiteral("~");

    default:
        break;
    }

    /*
     * ---------------------------------------------------------------
     * CARACTERES UNICODE
     * ---------------------------------------------------------------
     *
     * Esta es la parte importante.
     *
     * xkb_keysym_to_utf32() convierte directamente el keysym XKB
     * al punto de código Unicode correspondiente.
     *
     * Ejemplos:
     *
     *     XKB_KEY_a       -> U+0061 -> a
     *     XKB_KEY_A       -> U+0041 -> A
     *     XKB_KEY_ntilde  -> U+00F1 -> ñ
     *     XKB_KEY_Ntilde  -> U+00D1 -> Ñ
     *     XKB_KEY_exclam  -> U+0021 -> !
     *     XKB_KEY_question -> U+003F -> ?
     *     XKB_KEY_euro    -> U+20AC -> €
     *
     * De esta forma NO necesitamos mantener una tabla manual de
     * cientos de keysyms.
     */

    const uint32_t unicode =
        xkb_keysym_to_utf32(sym);

    if (unicode != 0 &&
        unicode != 0xFFFD &&
        unicode <= 0x10FFFF) {

        /*
         * No queremos introducir caracteres de control en el overlay.
         */
        if (unicode < 0x20 ||
            (unicode >= 0x7F && unicode <= 0x9F)) {

            return QString();
        }

        /*
         * QString::fromUcs4() permite representar correctamente tanto
         * caracteres BMP como caracteres Unicode suplementarios.
         */
        const char32_t ucs4[] = { static_cast<char32_t>(unicode), U'\0' };

        const QString result =
            QString::fromUcs4(ucs4);


        if (!result.isEmpty())
            return result;
    }

    /*
     * ---------------------------------------------------------------
     * FALLBACK
     * ---------------------------------------------------------------
     *
     * Algunas teclas tienen un keysym pero no un carácter Unicode.
     *
     * En ese caso conservamos el nombre XKB.
     *
     * Esto cubre casos como:
     *
     *     XF86AudioMute
     *     XF86AudioPlay
     *     etc.
     */

    char name[128] {};

    if (xkb_keysym_get_name(
            sym,
            name,
            sizeof(name)) > 0) {

        QString result =
            QString::fromLatin1(name);

        /*
         * Mejoramos algunos nombres de teclas muertas que puedan
         * escapar del switch anterior.
         */
        if (result.startsWith(
                QStringLiteral("dead_"))) {

            const QString dead =
                result.mid(5);

            if (dead == QStringLiteral("acute"))
                return QStringLiteral("´");

            if (dead == QStringLiteral("grave"))
                return QStringLiteral("`");

            if (dead == QStringLiteral("tilde"))
                return QStringLiteral("~");

            if (dead == QStringLiteral("diaeresis"))
                return QStringLiteral("¨");

            if (dead == QStringLiteral("circumflex"))
                return QStringLiteral("^");
        }

        return result;
    }

    return QString();
}


    QString displayName(
        const QString &name
    ) const
    {
        static const QHash<QString, QString> map = {

    {"KEY_LEFTCTRL", "Ctrl"},
    {"KEY_RIGHTCTRL", "Ctrl"},

    {"KEY_LEFTALT", "Alt"},
    {"KEY_RIGHTALT", "AltGr"},

    {"KEY_LEFTSHIFT", "Shift"},
    {"KEY_RIGHTSHIFT", "Shift"},

    {"KEY_LEFTMETA", "⌘"},
    {"KEY_RIGHTMETA", "⌘"},

    /*
     * ---------------------------------------------------------------
     * Teclas especiales compactas
     * ---------------------------------------------------------------
     */

    {"KEY_ESC", "Esc"},
    {"KEY_ENTER", "↵"},
    {"KEY_BACKSPACE", "⌫"},
    {"KEY_TAB", "⇥"},
    {"KEY_SPACE", "␣"},
    {"KEY_CAPSLOCK", "⇪"},

    /*
     * Navegación.
     */

    {"KEY_UP", "↑"},
    {"KEY_DOWN", "↓"},
    {"KEY_LEFT", "←"},
    {"KEY_RIGHT", "→"},

    {"KEY_HOME", "Home"},
    {"KEY_END", "End"},
    {"KEY_PAGEUP", "PgUp"},
    {"KEY_PAGEDOWN", "PgDn"},

    {"KEY_INSERT", "Ins"},
    {"KEY_DELETE", "Del"},

    /*
     * Sistema.
     */

    {"KEY_PRINT", "PrtSc"},
    {"KEY_SYSRQ", "SysRq"},
    {"KEY_PAUSE", "Pause"},
    {"KEY_SCROLLLOCK", "ScrLk"},
    {"KEY_NUMLOCK", "Num"},

    /*
     * Función.
     */

    {"KEY_F1", "F1"},
    {"KEY_F2", "F2"},
    {"KEY_F3", "F3"},
    {"KEY_F4", "F4"},
    {"KEY_F5", "F5"},
    {"KEY_F6", "F6"},
    {"KEY_F7", "F7"},
    {"KEY_F8", "F8"},
    {"KEY_F9", "F9"},
    {"KEY_F10", "F10"},
    {"KEY_F11", "F11"},
    {"KEY_F12", "F12"},

    /*
     * Ratón.
     */

    {"BTN_LEFT", "◀"},
    {"BTN_RIGHT", "▶"},
    {"BTN_MIDDLE", "●"},
    {"BTN_SIDE", "🖱4"},
    {"BTN_EXTRA", "🖱5"}
};


        if (map.contains(name))
            return map.value(name);

        if (name.startsWith(
                QStringLiteral("KEY_"))) {

            const QString key =
                name.mid(4);

            if (key.size() == 1)
                return key.toLower();

            return key;
        }

        return name;
    }
    
    QString compactDisplayName(
    const QString &name
) const
{
    static const QHash<QString, QString> map = {

        {"Ctrl", "⌃"},
        {"Alt", "⌥"},
        {"AltGr", "⎇"},
        {"Shift", "⇧"},
        {"Meta", "⌘"},

        {"Esc", "Esc"},
        {"Enter", "↵"},
        {"Backspace", "⌫"},
        {"Tab", "⇥"},
        {"Space", "␣"},
        {"Caps Lock", "⇪"},

        {"Home", "⇱"},
        {"End", "⇲"},
        {"Page Up", "⇞"},
        {"Page Down", "⇟"},

        {"Insert", "Ins"},
        {"Delete", "⌦"},

        {"Print", "⎙"},
        {"SysRq", "SysRq"},
        {"Pause", "Ⅱ"},
        {"Scroll Lock", "ScrLk"},
        {"Num Lock", "Num"},

        {"Mouse Left", "◀"},
        {"Mouse Right", "▶"},
        {"Mouse Middle", "●"},
        {"Mouse 4", "◀4"},
        {"Mouse 5", "▶5"}
    };

    if (map.contains(name))
        return map.value(name);

    return name;
}


    /*
     * ---------------------------------------------------------------
     * Wayland keyboard
     * ---------------------------------------------------------------
     */

    static void registryGlobal(
        void *data,
        wl_registry *registry,
        uint32_t name,
        const char *interface,
        uint32_t version
    )
    {
        KeyCast *self =
            static_cast<KeyCast *>(data);

        if (std::strcmp(
                interface,
                wl_seat_interface.name) == 0) {

            const uint32_t bindVersion =
                qMin<uint32_t>(
                    version,
                    7
                );

            self->seat =
                static_cast<wl_seat *>(
                    wl_registry_bind(
                        registry,
                        name,
                        &wl_seat_interface,
                        bindVersion
                    )
                );

            qDebug()
                << "KDE KeyCast: wl_seat encontrado";
        }
    }

    static void registryGlobalRemove(
        void *,
        wl_registry *,
        uint32_t
    )
    {
    }

    static const wl_registry_listener registryListener;

    static void keyboardKeymap(
        void *data,
        wl_keyboard *,
        uint32_t format,
        int fd,
        uint32_t size
    )
    {
        KeyCast *self =
            static_cast<KeyCast *>(data);

        if (format != WL_KEYBOARD_KEYMAP_FORMAT_XKB_V1) {

            qWarning(
                "KDE KeyCast: formato de keymap Wayland no soportado"
            );

            ::close(fd);
            return;
        }

        if (size == 0) {

            qWarning(
                "KDE KeyCast: keymap Wayland vacío"
            );

            ::close(fd);
            return;
        }

        void *mapping =
            mmap(
                nullptr,
                size,
                PROT_READ,
                MAP_PRIVATE,
                fd,
                0
            );

        if (mapping == MAP_FAILED) {

            qWarning(
                "KDE KeyCast: mmap del keymap falló: %s",
                std::strerror(errno)
            );

            ::close(fd);
            return;
        }

        xkb_context *context =
            xkb_context_new(
                XKB_CONTEXT_NO_FLAGS
            );

        if (!context) {

            munmap(mapping, size);
            ::close(fd);

            qWarning(
                "KDE KeyCast: no se pudo crear xkb_context"
            );

            return;
        }

        /*
         * Wayland entrega un keymap XKB v1 como texto terminado en NUL.
         */
        xkb_keymap *newKeymap =
            xkb_keymap_new_from_string(
                context,
                static_cast<const char *>(mapping),
                XKB_KEYMAP_FORMAT_TEXT_V1,
                XKB_KEYMAP_COMPILE_NO_FLAGS
            );

        munmap(mapping, size);
        ::close(fd);

        if (!newKeymap) {

            xkb_context_unref(context);

            qWarning(
                "KDE KeyCast: no se pudo compilar el keymap XKB de Wayland"
            );

            return;
        }

        xkb_state *newState =
            xkb_state_new(newKeymap);

        if (!newState) {

            xkb_keymap_unref(newKeymap);
            xkb_context_unref(context);

            qWarning(
                "KDE KeyCast: no se pudo crear xkb_state"
            );

            return;
        }

        if (self->xkbState)
            xkb_state_unref(self->xkbState);

        if (self->xkbKeymap)
            xkb_keymap_unref(self->xkbKeymap);

        self->xkbKeymap =
            newKeymap;

        self->xkbState =
            newState;

        xkb_context_unref(context);

        /*
         * Al cambiar el keymap debemos borrar nuestro estado físico
         * anterior. El siguiente evento volverá a construirlo.
         */
        self->pressedModifiers.clear();
        self->combinationActive = false;
        self->currentCombination.clear();

        qDebug()
            << "KDE KeyCast: XKB keymap actualizado desde Wayland";
    }

    static void keyboardEnter(
        void *,
        wl_keyboard *,
        uint32_t,
        wl_surface *,
        wl_array *
    )
    {
    }

    static void keyboardLeave(
        void *,
        wl_keyboard *,
        uint32_t,
        wl_surface *
    )
    {
    }

    static void keyboardKey(
        void *,
        wl_keyboard *,
        uint32_t,
        uint32_t,
        uint32_t,
        uint32_t
    )
    {
        /*
         * NO usamos los key events de Wayland para el overlay.
         *
         * showmethekey-cli nos proporciona los eventos físicos.
         *
         * Wayland sólo se utiliza aquí como fuente del keymap y del
         * estado de modificadores/layout.
         */
    }

    static void keyboardModifiers(
        void *data,
        wl_keyboard *,
        uint32_t,
        uint32_t modsDepressed,
        uint32_t modsLatched,
        uint32_t modsLocked,
        uint32_t group
    )
    {
        KeyCast *self =
            static_cast<KeyCast *>(data);

        if (!self->xkbState)
            return;

        /*
         * Este callback es importante para cambios de layout y para
         * sincronizar Caps Lock / Shift / AltGr / Ctrl cuando KWin
         * informa del estado lógico del teclado.
         */
        xkb_state_update_mask(
            self->xkbState,
            modsDepressed,
            modsLatched,
            modsLocked,
            group,
            group,
            group
        );
    }

    static void keyboardRepeatInfo(
        void *,
        wl_keyboard *,
        int32_t,
        int32_t
    )
    {
    }

    static const wl_keyboard_listener keyboardListener;

    void setupWaylandKeyboard()
    {
        display =
            wl_display_connect(nullptr);

        if (!display) {

            qWarning(
                "KDE KeyCast: no se pudo conectar a Wayland"
            );

            return;
        }

        registry =
            wl_display_get_registry(display);

        if (!registry) {

            qWarning(
                "KDE KeyCast: no se pudo obtener wl_registry"
            );

            wl_display_disconnect(display);
            display = nullptr;

            return;
        }

        wl_registry_add_listener(
            registry,
            &registryListener,
            this
        );

        /*
         * Primer roundtrip:
         *
         * obtiene wl_seat.
         */
        if (wl_display_roundtrip(display) < 0) {

            qWarning(
                "KDE KeyCast: fallo en wl_display_roundtrip"
            );

            return;
        }

        if (!seat) {

            qWarning(
                "KDE KeyCast: Wayland no proporcionó ningún wl_seat"
            );

            return;
        }

        /*
         * Obtenemos el teclado del seat.
         */
        keyboard =
            wl_seat_get_keyboard(seat);

        if (!keyboard) {

            qWarning(
                "KDE KeyCast: no se pudo obtener wl_keyboard"
            );

            return;
        }

        wl_keyboard_add_listener(
            keyboard,
            &keyboardListener,
            this
        );

        /*
         * Segundo roundtrip:
         *
         * fuerza la llegada del keymap, modifiers iniciales y repeat
         * info.
         */
        if (wl_display_roundtrip(display) < 0) {

            qWarning(
                "KDE KeyCast: fallo obteniendo el keymap Wayland"
            );

            return;
        }

        /*
         * Necesitamos procesar futuras notificaciones Wayland sin
         * bloquear el event loop de Qt.
         */
        const int fd =
            wl_display_get_fd(display);

        if (fd >= 0) {

            waylandNotifier =
                new QSocketNotifier(
                    fd,
                    QSocketNotifier::Read,
                    this
                );

            connect(
                waylandNotifier,
                &QSocketNotifier::activated,
                this,
               [this](qintptr) {


                    if (!display)
                        return;

                    if (wl_display_dispatch_pending(
                            display) < 0) {

                        return;
                    }

                    wl_display_flush(display);
                }
            );
        }

        qDebug()
            << "KDE KeyCast: keymap XKB obtenido directamente de Wayland";
    }

    /*
     * ---------------------------------------------------------------
     * XKB state
     * ---------------------------------------------------------------
     */

    void updateXkbState(
        int linuxCode,
        bool pressed
    )
    {
        if (!xkbState)
            return;

        const xkb_keycode_t code =
            xkbKeycodeFromLinuxCode(
                linuxCode
            );

        if (code == XKB_KEYCODE_INVALID)
            return;

        xkb_state_update_key(
            xkbState,
            code,
            pressed
                ? XKB_KEY_DOWN
                : XKB_KEY_UP
        );
    }

    QString xkbDisplayName(
        int linuxCode,
        const QString &fallbackName
    )
    {
        if (!xkbState ||
            !xkbKeymap ||
            linuxCode < 0) {

            return displayName(fallbackName);
        }

        const xkb_keycode_t code =
            xkbKeycodeFromLinuxCode(
                linuxCode
            );

        if (code == XKB_KEYCODE_INVALID)
            return displayName(fallbackName);

        /*
         * IMPORTANTE:
         *
         * xkb_state_key_get_one_sym() obtiene el keysym lógico
         * teniendo en cuenta el estado ACTUAL:
         *
         *   Shift
         *   Caps Lock
         *   AltGr
         *   layout
         *   etc.
         *
         * No usamos xkb_state_key_get_utf8() aquí porque Control puede
         * transformar C en U+0003.
         */
        const xkb_keysym_t sym =
            xkb_state_key_get_one_sym(
                xkbState,
                code
            );

        const QString result =
            keysymToText(sym);

        if (!result.isEmpty())
            return result;

        return displayName(fallbackName);
    }

    /*
     * ---------------------------------------------------------------
     * Visual queue
     * ---------------------------------------------------------------
     */
       void adjustWindowWidth()
    {
        
    if (visualQueue.isEmpty())
        return;

    QFont font =
        QApplication::font();

    font.setPixelSize(18);
    font.setBold(true);

    QFontMetrics fm(font);

    const int spacing = 8;
    const int sideMargin = 12;
    const int minimumBoxWidth =
        compactMode ? 42 : 44;

    int contentWidth = 0;

    for (const VisualEntry &entry : visualQueue) {

        QStringList visualKeys;

        for (const QString &key : entry.keys) {

            QString visualKey = key;

            if (compactMode)
                visualKey =
                    compactDisplayName(key);

            visualKeys.append(visualKey);
        }

        QString text =
            visualKeys.join(
                QStringLiteral(" + ")
            );

        if (entry.repeatCount > 1) {
            text +=
                QStringLiteral(" ×%1")
                    .arg(entry.repeatCount);
        }

        const int textWidth =
            fm.horizontalAdvance(text);

        const int boxWidth =
            qMax(
                minimumBoxWidth,
                textWidth + 24
            );

        if (contentWidth > 0)
            contentWidth += spacing;

        contentWidth += boxWidth;
    }

    const int desiredWidth =
        contentWidth +
        sideMargin * 2;

    /*
     * Evitamos ventanas ridículamente pequeñas o grandes.
     */
    const int minWidth = 70;
    const int maxWidth = 1000;

    const int newWidth =
        qBound(
            minWidth,
            desiredWidth,
            maxWidth
        );

    const QRect oldGeometry =
        geometry();

    if (oldGeometry.width() == newWidth)
        return;

    /*
     * Mantenemos el CENTRO de la ventana.
     *
     * Esto es importante:
     *
     *     antes       después
     *
     *       [----]       [--------]
     *          ^              ^
     *        centro          centro
     *
     * La ventana crece por ambos lados.
     */
    const int centerX =
        oldGeometry.left() +
        oldGeometry.width() / 2;

    const int newLeft =
        centerX -
        newWidth / 2;

    setGeometry(
        newLeft,
        oldGeometry.top(),
        newWidth,
        oldGeometry.height()
    );
    }




    void trimVisualQueue()
    {
        const qint64 now =
            QDateTime::currentMSecsSinceEpoch();

        bool changed = false;

        while (!visualQueue.isEmpty() &&
            visualQueue.first().expires <= now) {

            visualQueue.removeFirst();
            changed = true;
            }

        if (changed)
            adjustWindowWidth();

        }
    

    void addVisualEntry(
    const QStringList &keys,
    bool combination
)
{
    if (keys.isEmpty())
        return;

    const qint64 now =
        QDateTime::currentMSecsSinceEpoch();

    /*
     * ---------------------------------------------------------------
     * Repeticiones
     * ---------------------------------------------------------------
     *
     * Si la última entrada representa exactamente la misma tecla,
     * incrementamos su contador en lugar de crear otra tarjeta.
     *
     * Ejemplo:
     *
     *   Backspace
     *   Backspace
     *   Backspace
     *
     * se convierte en:
     *
     *   Backspace ×3
     *
     * Las combinaciones no se agrupan.
     */

    if (!combination &&
        !visualQueue.isEmpty()) {

        VisualEntry &last =
            visualQueue.last();

        if (!last.combination &&
            last.keys == keys) {

            last.repeatCount++;
            last.expires = now + 1800;

            adjustWindowWidth();
            update();

            return;
        }
    }

    VisualEntry entry;

    entry.keys = keys;
    entry.created = now;
    entry.expires = now + 1800;
    entry.combination = combination;
    entry.repeatCount = 1;

    visualQueue.append(entry);

    while (visualQueue.size() > 6)
        visualQueue.removeFirst();

    adjustWindowWidth();
    update();
}


    void addMouseButton(
        const QString &keyName
    )
    {
        addVisualEntry(
            QStringList{
                displayName(keyName)
            },
            false
        );
    }

    void finishCombination()
    {
        if (!combinationActive)
        return;

    if (!currentCombinationKey.isEmpty()) {

        if (!currentCombination.contains(
                currentCombinationKey)) {

            currentCombination.append(
                currentCombinationKey
            );
        }
    }

    if (currentCombination.size() >= 2) {

        addVisualEntry(
            currentCombination,
            true
        );
    }

    currentCombination.clear();
    currentCombinationKey.clear();

    combinationActive = false;
    }

    QStringList currentModifierNames() const
    {
        QStringList result;

        if (pressedModifiers.contains(
                QStringLiteral("KEY_LEFTCTRL")) ||
            pressedModifiers.contains(
                QStringLiteral("KEY_RIGHTCTRL"))) {

            result.append(
                QStringLiteral("Ctrl")
            );
        }

        if (pressedModifiers.contains(
                QStringLiteral("KEY_LEFTALT"))) {

            result.append(
                QStringLiteral("Alt")
            );
        }

        if (pressedModifiers.contains(
                QStringLiteral("KEY_RIGHTALT"))) {

            result.append(
                QStringLiteral("AltGr")
            );
        }

        if (pressedModifiers.contains(
                QStringLiteral("KEY_LEFTSHIFT")) ||
            pressedModifiers.contains(
                QStringLiteral("KEY_RIGHTSHIFT"))) {

            result.append(
                QStringLiteral("Shift")
            );
        }

        if (pressedModifiers.contains(
                QStringLiteral("KEY_LEFTMETA")) ||
            pressedModifiers.contains(
                QStringLiteral("KEY_RIGHTMETA"))) {

            result.append(
                QStringLiteral("Meta")
            );
        }

        return result;
    }

    bool hasNonShiftModifier() const
    {
        return
            pressedModifiers.contains(
                QStringLiteral("KEY_LEFTCTRL")
            ) ||
            pressedModifiers.contains(
                QStringLiteral("KEY_RIGHTCTRL")
            ) ||
            pressedModifiers.contains(
                QStringLiteral("KEY_LEFTALT")
            ) ||
            pressedModifiers.contains(
                QStringLiteral("KEY_RIGHTALT")
            ) ||
            pressedModifiers.contains(
                QStringLiteral("KEY_LEFTMETA")
            ) ||
            pressedModifiers.contains(
                QStringLiteral("KEY_RIGHTMETA")
            );
    }
    
    bool isShiftOnlyTextKey(
    const QString &visualName
) const
{
    /*
     * Shift modifica directamente caracteres de texto:
     *
     *     a -> A
     *     1 -> !
     *     ñ -> Ñ
     *     , -> ;
     *
     * En estos casos no mostramos Shift.
     *
     * Las teclas especiales como:
     *
     *     ↑
     *     ↓
     *     F1
     *     Home
     *
     * sí deben mostrar la combinación con Shift.
     */

    if (visualName.isEmpty())
        return false;

    if (visualName.size() != 1)
        return false;

    const QChar ch =
        visualName.at(0);

    /*
     * Las flechas y otros símbolos que usamos como representación
     * visual de teclas especiales NO son caracteres de texto para
     * este propósito.
     */
    switch (ch.unicode()) {

    case 0x2190: // ←
    case 0x2191: // ↑
    case 0x2192: // →
    case 0x2193: // ↓
    case 0x21E7: // ⇧
    case 0x21E8: // ⇨
    case 0x21E9: // ⇩
    case 0x21EA: // ⇪
    case 0x2302: // ⌂
    case 0x232B: // ⌫
    case 0x2326: // ⌦
        return false;

    default:
        break;
    }

    return true;
}


    void startCombinationIfNeeded()
    {
        if (combinationActive)
            return;

        combinationActive = true;

        currentCombination =
            currentModifierNames();
            
        currentCombinationKey.clear();
    }

    /*
     * ---------------------------------------------------------------
     * showmethekey-cli
     * ---------------------------------------------------------------
     */

    void readInput()
    {
        while (process.canReadLine()) {

            const QByteArray line =
                process.readLine().trimmed();

            if (line.isEmpty())
                continue;

            QJsonParseError error;

            const QJsonDocument doc =
                QJsonDocument::fromJson(
                    line,
                    &error
                );

            if (error.error !=
                    QJsonParseError::NoError ||
                !doc.isObject()) {

                continue;
            }

            const QJsonObject obj =
                doc.object();

            const QString eventName =
                obj.value(
                    QStringLiteral("event_name")
                ).toString();

            const QString keyName =
                obj.value(
                    QStringLiteral("key_name")
                ).toString();

            const int linuxKeyCode =
                obj.value(
                    QStringLiteral("key_code")
                ).toInt(-1);

            const QString state =
                obj.value(
                    QStringLiteral("state_name")
                ).toString();

            if (keyName.isEmpty())
                continue;

            if (linuxKeyCode < 0)
                continue;

            /*
             * -------------------------------------------------------
             * MOUSE
             * -------------------------------------------------------
             */

            if (eventName ==
                QStringLiteral("POINTER_BUTTON")) {

                if (state ==
                    QStringLiteral("PRESSED")) {

                    addMouseButton(keyName);
                }

                continue;
            }

            /*
             * -------------------------------------------------------
             * KEYBOARD
             * -------------------------------------------------------
             */

            if (eventName !=
                QStringLiteral("KEYBOARD_KEY")) {

                continue;
            }

            const bool pressed =
                state ==
                QStringLiteral("PRESSED");

            const bool released =
                state ==
                QStringLiteral("RELEASED");

            if (!pressed && !released)
                continue;

            const bool modifier =
                isModifier(keyName);
                
                if (pressed &&
                    keyName == QStringLiteral("KEY_C") &&
                    pressedModifiers.contains(
                        QStringLiteral("KEY_LEFTCTRL")
                    ) &&
                    pressedModifiers.contains(
                        QStringLiteral("KEY_LEFTSHIFT")
                    )) {

                    toggleCompactMode();
                    continue;
                }


            const bool shift =
                  keyName ==
                      QStringLiteral("KEY_LEFTSHIFT") ||
                  keyName ==
                      QStringLiteral("KEY_RIGHTSHIFT");

              const bool shiftActive =
                  pressedModifiers.contains(
                      QStringLiteral("KEY_LEFTSHIFT")
                  ) ||
                  pressedModifiers.contains(
                      QStringLiteral("KEY_RIGHTSHIFT")
                  );

            /*
 * -------------------------------------------------------
 * PRESS
 * -------------------------------------------------------
 */

if (pressed) {

    /*
     * Primero actualizamos XKB.
     *
     * Esto hace que la tecla normal se resuelva teniendo en cuenta:
     *
     *     Shift
     *     Caps Lock
     *     AltGr
     *     layout
     *     etc.
     */
    updateXkbState(
        linuxKeyCode,
        true
    );

    /*
     * -----------------------------------------------------------
     * MODIFICADOR
     * -----------------------------------------------------------
     */

    if (modifier) {

        pressedModifiers.insert(
            keyName
        );

        /*
         * Shift por sí solo no genera tarjeta.
         *
         * Esperamos a la siguiente tecla para decidir si es:
         *
         *     Shift + C       -> C
         *
         * o:
         *
         *     Shift + ↑       -> ⇧ + ↑
         */
        if (shift &&
            !hasNonShiftModifier()) {

            update();
            continue;
        }

        /*
         * Ctrl / Alt / AltGr / Meta empiezan una combinación.
         *
         * Si Shift se pulsa después de ellos, también entra aquí
         * porque hasNonShiftModifier() sigue siendo true.
         */
        if (hasNonShiftModifier()) {

            startCombinationIfNeeded();

            currentCombination =
                currentModifierNames();

            update();
            continue;
        }
    }

    /*
     * -----------------------------------------------------------
     * TECLA NORMAL
     * -----------------------------------------------------------
     */

    const QString name =
        xkbDisplayName(
            linuxKeyCode,
            keyName
        );

    const QString visualName =
        name.isEmpty()
            ? displayName(keyName)
            : name;

    /*
     * -----------------------------------------------------------
     * SHIFT + TEXTO
     * -----------------------------------------------------------
     *
     * XKB ya nos entrega el carácter transformado:
     *
     *     Shift + a -> A
     *     Shift + 1 -> !
     *     Shift + ñ -> Ñ
     *
     * Por tanto mostramos únicamente el carácter resultante.
     */
    if (shift &&
        !hasNonShiftModifier() &&
        isShiftOnlyTextKey(visualName)) {

        addVisualEntry(
            QStringList{
                visualName
            },
            false
        );

        continue;
    }

    /*
     * -----------------------------------------------------------
     * SHIFT + TECLA ESPECIAL
     * -----------------------------------------------------------
     *
     * Ejemplos:
     *
     *     Shift + ↑
     *     Shift + ↓
     *     Shift + Home
     *     Shift + End
     *     Shift + F1
     *
     * Estas sí deben aparecer como combinación.
     */
      if (shiftActive &&
          !isShiftOnlyTextKey(visualName)) {

          startCombinationIfNeeded();

          currentCombination =
              currentModifierNames();

          /*
           * Si Shift es el único modificador,
           * currentModifierNames() puede no conservarlo
           * en este punto porque el estado físico ya ha
           * cambiado. Lo añadimos explícitamente.
           */
          if (!hasNonShiftModifier() &&
              !currentCombination.contains(
                  QStringLiteral("Shift"))) {

              currentCombination.prepend(
                  QStringLiteral("Shift")
              );
          }

          currentCombinationKey =
              visualName;

          if (!currentCombinationKey.isEmpty() &&
              !currentCombination.contains(
                  currentCombinationKey)) {

              currentCombination.append(
                  currentCombinationKey
              );
          }

          update();
          continue;
      }

    /*
     * -----------------------------------------------------------
     * CTRL / ALT / ALTGR / META + TECLA
     * -----------------------------------------------------------
     */

    if (hasNonShiftModifier()) {

        startCombinationIfNeeded();

        currentCombinationKey =
            visualName;

        currentCombination =
            currentModifierNames();

        if (!currentCombinationKey.isEmpty() &&
            !currentCombination.contains(
                currentCombinationKey)) {

            currentCombination.append(
                currentCombinationKey
            );
        }

        update();
        continue;
    }

    /*
     * -----------------------------------------------------------
     * TECLA NORMAL SIN MODIFICADORES
     * -----------------------------------------------------------
     */

    addVisualEntry(
        QStringList{
            visualName
        },
        false
    );

    continue;
}


/*
 * ---------------------------------------------------------------
 * RELEASE
 * ---------------------------------------------------------------
 */

if (released) {

    /*
     * -----------------------------------------------------------
     * MODIFICADOR
     * -----------------------------------------------------------
     */

    if (modifier) {

        /*
         * Primero actualizamos XKB.
         */
        updateXkbState(
            linuxKeyCode,
            false
        );

        /*
         * Después retiramos el modificador físico.
         */
        pressedModifiers.remove(
            keyName
        );

        /*
         * Si todavía queda algún modificador, mantenemos
         * la combinación activa.
         */
          /*
           * Si todavía queda algún modificador físico,
           * NO reconstruimos la combinación desde cero.
           *
           * Es importante conservar los modificadores que ya
           * participaron en la combinación.
           *
           * Ejemplo:
           *
           *     Ctrl + Shift + ↓
           *
           * Si se suelta Ctrl primero, la combinación debe seguir
           * siendo:
           *
           *     Ctrl + Shift + ↓
           *
           * hasta que se libere el último modificador.
           */
          if (combinationActive &&
              !pressedModifiers.isEmpty()) {

              update();
              continue;
          }

        /*
         * Ya no queda ningún modificador:
         *
         * ahora sí cerramos la combinación.
         */
        if (combinationActive &&
            pressedModifiers.isEmpty()) {

            finishCombination();
        }

        update();
        continue;
    }

    /*
     * -----------------------------------------------------------
     * TECLA NORMAL
     * -----------------------------------------------------------
     *
     * No mostramos nada al liberar una tecla.
     *
     * Pero actualizamos XKB para mantener el estado correcto.
     */
    updateXkbState(
        linuxKeyCode,
        false
    );

    update();
}

        }
    }
    
    void toggleCompactMode()
{
    compactMode = !compactMode;

    qDebug()
        << "KDE KeyCast: compact mode ="
        << compactMode;
        
    adjustWindowWidth();
    update();
}


    /*
     * ---------------------------------------------------------------
     * Members
     * ---------------------------------------------------------------
     */

    wl_display *display = nullptr;
    wl_registry *registry = nullptr;
    wl_seat *seat = nullptr;
    wl_keyboard *keyboard = nullptr;

    QSocketNotifier *waylandNotifier = nullptr;

    xkb_keymap *xkbKeymap = nullptr;
    xkb_state *xkbState = nullptr;

    QProcess process;
    QTimer timer;

    QVector<VisualEntry> visualQueue;

    bool combinationActive = false;
    QStringList currentCombination;
    QString currentCombinationKey;

    QSet<QString> pressedModifiers;
    
    bool compactMode = false;

};

/*
 * ---------------------------------------------------------------
 * Wayland listener definitions
 * ---------------------------------------------------------------
 */

const wl_registry_listener KeyCast::registryListener = {
    KeyCast::registryGlobal,
    KeyCast::registryGlobalRemove
};

const wl_keyboard_listener KeyCast::keyboardListener = {
    KeyCast::keyboardKeymap,
    KeyCast::keyboardEnter,
    KeyCast::keyboardLeave,
    KeyCast::keyboardKey,
    KeyCast::keyboardModifiers,
    KeyCast::keyboardRepeatInfo
};

int main(int argc, char **argv)
{
    QApplication app(argc, argv);

    KeyCast window;

    return app.exec();
}
