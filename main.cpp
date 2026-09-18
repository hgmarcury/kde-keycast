#include "src/mouse/WaylandPointer.h"
#include <linux/input-event-codes.h>
#include <functional>
#include <algorithm>
#include <cerrno>
#include <cstring>
#include <QApplication>
#include <QWidget>
#include <QPainter>
#include <QPainterPath>
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
#include <QSettings>
#include <QMoveEvent>
#include <QDir>
#include <QFile>
#include <QRegularExpression>
#include <QtDBus/QtDBus>
#include <QContextMenuEvent>
#include <QMenu>
#include <QWidgetAction>
#include <QHBoxLayout>
#include <QLabel>
#include <QCheckBox>
#include <QAbstractButton>
#include <QDBusConnection>
#include <QDBusMessage>
#include <QWidgetAction>
#include <QLabel>
#include <QSlider>
#include <QDialog>
#include <QVBoxLayout>
#include <QPushButton>

#include <wayland-client.h>

#include <xkbcommon/xkbcommon.h>
#include <xkbcommon/xkbcommon-compose.h>
#include <limits>

#include <sys/mman.h>
#include <unistd.h>

#include <cerrno>
#include <cstring>

struct VisualEntry {
    QStringList keys;
    QStringList keySecondaryLegends;
    qint64 created = 0;
    qint64 expires = 0;
    bool combination = false;
    int repeatCount = 1;

    // Código Linux de la tecla física cuando la entrada representa
    // una única tecla. Las combinaciones utilizan -1.
    int linuxKeyCode = -1;
    bool composePending = false;
    bool keyboardEntry = false;
    bool maskKeyboard = false;
};



class WaylandPointer
{
public:
    WaylandPointer() = default;

    ~WaylandPointer()
    {
        stop();
    }

    bool start(std::function<void(const QString &, bool)> callback)
    {
        if (display)
            return true;

        onButton = std::move(callback);

        display = wl_display_connect(nullptr);
        if (!display) {
            qWarning() << "WaylandPointer: no se pudo conectar a Wayland";
            return false;
        }

        registry = wl_display_get_registry(display);
        if (!registry) {
            stop();
            return false;
        }

        static const wl_registry_listener listener = {
            registryGlobal,
            registryGlobalRemove
        };

        wl_registry_add_listener(registry, &listener, this);

        if (wl_display_roundtrip(display) < 0) {
            stop();
            return false;
        }

        if (!seat) {
            qWarning() << "WaylandPointer: no se encontró wl_seat";
            stop();
            return false;
        }

        pointer = wl_seat_get_pointer(seat);
        if (!pointer) {
            qWarning() << "WaylandPointer: no se pudo obtener wl_pointer";
            stop();
            return false;
        }

        static const wl_pointer_listener pointerListener = {
            pointerEnter,
            pointerLeave,
            pointerMotion,
            pointerButton,
            pointerAxis,
            pointerFrame,
            pointerAxisSource,
            pointerAxisStop,
            pointerAxisDiscrete,
            pointerAxisValue120,
            pointerAxisRelativeDirection
        };

        wl_pointer_add_listener(pointer, &pointerListener, this);

        if (wl_display_roundtrip(display) < 0) {
            stop();
            return false;
        }

        fd = wl_display_get_fd(display);
        if (fd < 0) {
            stop();
            return false;
        }

        qDebug() << "WaylandPointer: activo";
        return true;
    }

    void dispatch()
    {
        if (!display)
            return;

        if (wl_display_dispatch_pending(display) < 0) {
            stop();
            return;
        }

        if (wl_display_flush(display) < 0) {
            if (errno != EAGAIN)
                stop();
        }
    }

    void stop()
    {
        if (pointer) {
            wl_pointer_destroy(pointer);
            pointer = nullptr;
        }

        if (seat) {
            wl_seat_destroy(seat);
            seat = nullptr;
        }

        if (registry) {
            wl_registry_destroy(registry);
            registry = nullptr;
        }

        if (display) {
            wl_display_disconnect(display);
            display = nullptr;
        }

        fd = -1;
    }

    int socketFd() const
    {
        return fd;
    }

private:
    static void registryGlobal(
        void *data,
        wl_registry *registry,
        uint32_t name,
        const char *interface,
        uint32_t version)
    {
        auto *self = static_cast<WaylandPointer *>(data);

        if (std::strcmp(interface, wl_seat_interface.name) == 0) {
            const uint32_t bindVersion = std::min(
                version,
                9u);

            self->seat = static_cast<wl_seat *>(
                wl_registry_bind(
                    registry,
                    name,
                    &wl_seat_interface,
                    bindVersion));
        }
    }

    static void registryGlobalRemove(
        void *,
        wl_registry *,
        uint32_t)
    {
    }

    static void pointerEnter(
        void *,
        wl_pointer *,
        uint32_t,
        wl_surface *,
        wl_fixed_t,
        wl_fixed_t)
    {
    }

    static void pointerLeave(
        void *,
        wl_pointer *,
        uint32_t,
        wl_surface *)
    {
    }

    static void pointerMotion(
        void *,
        wl_pointer *,
        uint32_t,
        wl_fixed_t,
        wl_fixed_t)
    {
    }

    static void pointerButton(
        void *data,
        wl_pointer *,
        uint32_t,
        uint32_t,
        uint32_t button,
        uint32_t state)
    {
        auto *self = static_cast<WaylandPointer *>(data);

        if (!self->onButton)
            return;

        QString name;

        switch (button) {
        case BTN_LEFT:
            name = QStringLiteral("BTN_LEFT");
            break;
        case BTN_RIGHT:
            name = QStringLiteral("BTN_RIGHT");
            break;
        case BTN_MIDDLE:
            name = QStringLiteral("BTN_MIDDLE");
            break;
        case BTN_SIDE:
            name = QStringLiteral("BTN_SIDE");
            break;
        case BTN_EXTRA:
            name = QStringLiteral("BTN_EXTRA");
            break;
        default:
            name = QStringLiteral("BTN_%1").arg(button);
            break;
        }

        self->onButton(
            name,
            state == WL_POINTER_BUTTON_STATE_PRESSED);
    }

    static void pointerAxis(
        void *data,
        wl_pointer *,
        uint32_t,
        uint32_t axis,
        wl_fixed_t value)
    {
        auto *self = static_cast<WaylandPointer *>(data);

        if (!self->onButton)
            return;

        /*
         * Wayland no expone la rueda como un "button".
         * Convertimos cada movimiento de rueda en un evento visual.
         *
         * El signo sigue la convención estándar de Wayland:
         * positivo = desplazamiento hacia abajo/derecha según el eje.
         */
        if (axis == WL_POINTER_AXIS_VERTICAL_SCROLL) {
            if (value > 0) {
                self->onButton(
                    QStringLiteral("BTN_WHEEL_DOWN"),
                    true);
            } else if (value < 0) {
                self->onButton(
                    QStringLiteral("BTN_WHEEL_UP"),
                    true);
            }
        } else if (axis == WL_POINTER_AXIS_HORIZONTAL_SCROLL) {
            if (value > 0) {
                self->onButton(
                    QStringLiteral("BTN_WHEEL_RIGHT"),
                    true);
            } else if (value < 0) {
                self->onButton(
                    QStringLiteral("BTN_WHEEL_LEFT"),
                    true);
            }
        }
    }

    static void pointerFrame(
        void *,
        wl_pointer *)
    {
    }

    static void pointerAxisSource(
        void *,
        wl_pointer *,
        uint32_t)
    {
    }

    static void pointerAxisStop(
        void *,
        wl_pointer *,
        uint32_t,
        uint32_t)
    {
    }

    static void pointerAxisDiscrete(
        void *,
        wl_pointer *,
        uint32_t,
        int32_t)
    {
    }

    static void pointerAxisValue120(
        void *,
        wl_pointer *,
        uint32_t,
        int32_t)
    {
    }

    static void pointerAxisRelativeDirection(
        void *,
        wl_pointer *,
        uint32_t,
        uint32_t)
    {
    }

    wl_display *display = nullptr;
    wl_registry *registry = nullptr;
    wl_seat *seat = nullptr;
    wl_pointer *pointer = nullptr;

    int fd = -1;

    std::function<void(const QString &, bool)> onButton;
};

class KeyCast : public QWidget
{

    Q_OBJECT


    Q_CLASSINFO(
           "D-Bus Interface",
           "org.kde.KDEKeyCast"
       )


public:
    KeyCast()
    {
        setAttribute(Qt::WA_TranslucentBackground);
        setAttribute(Qt::WA_NoSystemBackground);
        setAttribute(Qt::WA_OpaquePaintEvent, false);
        setAttribute(Qt::WA_TransparentForMouseEvents, false);

        setWindowFlags(
            Qt::FramelessWindowHint |
            Qt::Tool
        );

        setWindowTitle(QStringLiteral("KDE KeyCast"));

        QSettings settings(
            QStringLiteral("KDE"),
            QStringLiteral("KDEKeyCast")
        );

        compactMode =
            settings.value(
                QStringLiteral("compactMode"),
                false
            ).toBool();

        alwaysOnTop =
            settings.value(
                QStringLiteral("alwaysOnTop"),
                true
            ).toBool();

        displayDurationMs =
            settings.value(
                QStringLiteral("displayDurationMs"),
                1800
            ).toLongLong();

        displayDurationMs =
            qBound<qint64>(
                static_cast<qint64>(500),
                displayDurationMs,
                static_cast<qint64>(5000)
            );

        lightMode =
            settings.value(
                QStringLiteral("lightMode"),
                false
            ).toBool();

        hideKeys =
            settings.value(
                QStringLiteral("hideKeys"),
                false
            ).toBool();

        onlyShortcuts =
            settings.value(
                QStringLiteral("onlyShortcuts"),
                false
            ).toBool();

        QDBusConnection dbus =
            QDBusConnection::sessionBus();

        if (!dbus.registerService(
                QStringLiteral("org.kde.KDEKeyCast"))) {

            qWarning()
                << "KDE KeyCast: no se pudo registrar el servicio D-Bus";
        }

        if (!dbus.registerObject(
                QStringLiteral("/KDEKeyCast"),
                this,
                QDBusConnection::ExportScriptableSlots)) {

            qWarning()
                << "KDE KeyCast: no se pudo registrar el objeto D-Bus";
        }

        const int initialWidth =
            compactMode ? 300 : 400;

        setFixedSize(
            initialWidth,
            106
        );

        setupWaylandKeyboard();


        {
            const QString projectBackend =
                QDir(QCoreApplication::applicationDirPath())
                    .absoluteFilePath(
                        QStringLiteral(
                            "../vendor/showmethekey-cli/showmethekey-cli"
                        )
                    );

            const QString systemBackend =
                QStringLiteral(
                    "/usr/bin/showmethekey-cli"
                );

            if (QFileInfo::exists(projectBackend) &&
                QFileInfo(projectBackend).isExecutable()) {

                process.setProgram(projectBackend);

                qDebug()
                    << "KDE KeyCast: usando backend extendido:"
                    << projectBackend;

            } else {

                process.setProgram(systemBackend);

                qDebug()
                    << "KDE KeyCast: usando backend del sistema:"
                    << systemBackend;
            }
        }

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

                trimVisualQueue();
                update();
            }
        );

        timer.start();

        /*
         * Sincronización del estado real de multimedia.
         *
         * 250 ms es suficientemente rápido para que el cambio sea
         * prácticamente inmediato y evita hacer consultas
         * continuamente.
         */
        mediaStateTimer.setInterval(250);

        connect(
            &mediaStateTimer,
            &QTimer::timeout,
            this,
            [this] {
                updateMediaState();
            }
        );

        updateMediaState();
        mediaStateTimer.start();

        show();

    }

    ~KeyCast()
    {
        waylandPointer.stop();

        if (process.state() != QProcess::NotRunning) {

                process.terminate();

                if (!process.waitForFinished(1000)) {
                    process.kill();
                    process.waitForFinished(1000);
                }
            }

        QSettings settings(
            QStringLiteral("KDE"),
            QStringLiteral("KDEKeyCast")
        );

        settings.setValue(
            QStringLiteral("compactMode"),
            compactMode
        );

        settings.setValue(
            QStringLiteral("alwaysOnTop"),
            alwaysOnTop
        );

        settings.setValue(
            QStringLiteral("displayDurationMs"),
            displayDurationMs
        );

        settings.setValue(
            QStringLiteral("lightMode"),
            lightMode
        );

        settings.setValue(
            QStringLiteral("hideKeys"),
            hideKeys
        );

        settings.setValue(
            QStringLiteral("onlyShortcuts"),
            onlyShortcuts
        );

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

        resetComposeState();

        if (xkbState)
            xkb_state_unref(xkbState);

        if (xkbKeymap)
            xkb_keymap_unref(xkbKeymap);
    }
    public slots:

        Q_SCRIPTABLE bool isAlwaysOnTop() const
        {
            return alwaysOnTop;
        }

        Q_SCRIPTABLE void setKeepAboveState(bool state)
        {
            qDebug()
                    << "KDE KeyCast: setKeepAboveState recibido ="
                    << state;


            alwaysOnTop = state;

            QSettings settings(
                QStringLiteral("KDE"),
                QStringLiteral("KDEKeyCast")
            );

            settings.setValue(
                QStringLiteral("alwaysOnTop"),
                alwaysOnTop
            );

            qDebug()
                << "KDE KeyCast: keepAbove ="
                << alwaysOnTop;

            update();
        }

        QRect closeButtonHitRect() const
        {
            return QRect(
                width() - 30,
                3,
                24,
                24
            );
        }

        void paintCloseButton(QPainter &p)
        {
            const QRectF rect(
                width() - 28,
                5,
                18,
                18
            );

            const QColor background(32, 34, 40, 175);
            const QColor border(255, 255, 255, 48);
            const QColor shadow(0, 0, 0, 85);
            const QColor iconColor(248, 249, 251, 238);

            p.save();

            p.setRenderHint(QPainter::Antialiasing, true);

            p.setPen(Qt::NoPen);
            p.setBrush(shadow);

            p.drawRoundedRect(
                rect.adjusted(0, 1, 0, 2),
                7,
                7
            );

            p.setBrush(background);

            p.drawRoundedRect(
                rect,
                7,
                7
            );

            p.setBrush(Qt::NoBrush);
            p.setPen(QPen(border, 1.0));

            p.drawRoundedRect(
                rect.adjusted(0.5, 0.5, -0.5, -0.5),
                6.5,
                6.5
            );

            const QPointF center = rect.center();

            p.setPen(
                QPen(
                    iconColor,
                    1.5,
                    Qt::SolidLine,
                    Qt::RoundCap,
                    Qt::RoundJoin
                )
            );

            p.drawLine(
                QPointF(center.x() - 3.5, center.y() - 3.5),
                QPointF(center.x() + 3.5, center.y() + 3.5)
            );

            p.drawLine(
                QPointF(center.x() + 3.5, center.y() - 3.5),
                QPointF(center.x() - 3.5, center.y() + 3.5)
            );

            p.restore();
        }

protected:

    void mousePressEvent(QMouseEvent *event) override
    {
        if (event->button() == Qt::LeftButton) {

            if (closeButtonHitRect().contains(event->pos())) {
                QApplication::quit();
                event->accept();
                return;
            }

            if (windowHandle()) {

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

        paintCloseButton(p);

        paintStatusIndicators(p);

        if (visualQueue.isEmpty())
            return;

        QFont mainFont = QApplication::font();
        mainFont.setPixelSize(20);
        mainFont.setBold(true);

        QFont smallFont = QApplication::font();
        smallFont.setPixelSize(11);
        smallFont.setBold(true);

        const int spacing = 10;
        const int sideMargin = 12;
        const int boxHeight = 58;
        const int minimumBoxWidth = compactMode ? 42 : 46;

        const int top =
            (height() - boxHeight) / 2 - 2;

        struct DrawEntry {
            int index;
            int width;
            QString primary;
            QStringList secondary;
            QStringList keySecondary;
        };

        QVector<DrawEntry> visibleEntries;

        int usedWidth = 0;

        for (int i = visualQueue.size() - 1; i >= 0; --i) {

            const VisualEntry &entry =
                visualQueue[i];

            QStringList visualKeys;


            const bool isShiftWheel =
                entry.combination &&
                entry.keys.size() == 2 &&
                entry.keys.at(0) == QStringLiteral("Shift") &&
                (
                    entry.keys.at(1).startsWith(QStringLiteral("Wheel ")) ||
                    entry.keys.at(1).startsWith(QStringLiteral("◎"))
                );

            if (entry.maskKeyboard &&
                (entry.keyboardEntry ||
                 (entry.combination && !isShiftWheel))) {

                for (const QString &key : entry.keys) {
                    Q_UNUSED(key);

                    visualKeys.append(
                        QStringLiteral("•")
                    );
                }

            } else {

                for (const QString &key : entry.keys) {
                    QString visualKey = key;

                    if (compactMode)
                        visualKey =
                            compactDisplayName(key);

                    visualKeys.append(
                        visualKey
                    );
                }
            }

            QString primary =
                visualKeys.join(
                    QStringLiteral(" + ")
                );

            QStringList secondary;

            /*
             * Serigrafía específica de teclas especiales en
             * combinaciones.
             *
             * Las teclas normales (C, S, A, etc.) permanecen limpias.
             */
            QStringList keySecondary;

            if (visualKeys.size() > 1) {

                for (const QString &key : visualKeys) {

                    const QString symbol =
                        compactDisplayName(key);

                    if (symbol != key &&
                        !symbol.isEmpty()) {

                        keySecondary.append(symbol);

                    } else {

                        keySecondary.append(QString());
                    }
                }
            }

            /*
             * Las leyendas ya no se obtienen de una tabla manual.
             *
             * XKB nos proporciona los niveles reales del layout
             * activo de KDE.
             *
             * Las teclas normales conservan sus leyendas también
             * en compacto.
             *
             * Las teclas especiales solo muestran serigrafía
             * adicional en modo normal.
             */
            if (visualKeys.size() == 1 &&
                entry.linuxKeyCode >= 0) {

                secondary =
                    xkbSecondaryLegends(
                        entry.linuxKeyCode,
                        primary
                    );

                /*
                 * ---------------------------------------------------
                 * Serigrafía física de teclas especiales
                 * ---------------------------------------------------
                 *
                 * XKB proporciona los niveles reales de las teclas
                 * de caracteres, pero teclas como Home, End, Print,
                 * Delete, etc. no tienen necesariamente niveles
                 * Unicode que podamos utilizar como serigrafía.
                 *
                 * En modo normal conservamos el nombre como leyenda
                 * principal y añadimos aquí únicamente su símbolo
                 * físico, que será dibujado como superíndice.
                 *
                 * En compacto NO se añade porque el símbolo ya es
                 * la leyenda principal de la tecla.
                 */
                if (!compactMode) {

                    static const QHash<QString, QString> specialSymbols = {

                        {"Space", "␣"},

                        {"Home", "⇱"},
                        {"End", "⇲"},
                        {"Page Up", "⇞"},
                        {"Page Down", "⇟"},
                        {"PgUp", "⇞"},
                        {"PgDn", "⇟"},

                        {"Insert", "Ins"},
                        {"Ins", "Ins"},

                        {"Delete", "⌦"},
                        {"Del", "⌦"},

                        {"Print", "⎙"},
                        {"PrtSc", "⎙"},

                        {"SysRq", "SysRq"},

                        {"Pause", "Ⅱ"},
        {"Pause Break", "Ⅱ"},

                        {"Scroll Lock", "ScrLk"},
                        {"ScrLk", "ScrLk"},

                        {"Num Lock", "Num"},
                        {"Num", "Num"},

                        {"Caps Lock", "⇪"},

                        {"Esc", "Esc"},
                        {"Enter", "↵"},
                        {"Backspace", "⌫"},
                        {"Tab", "⇥"}
                    };

                    if (specialSymbols.contains(primary)) {

                        const QString symbol =
                            specialSymbols.value(primary);

                        if (!secondary.contains(symbol))
                            secondary.prepend(symbol);
                    }
                }
            }

            if (entry.keyboardEntry &&
                entry.maskKeyboard) {

                secondary.clear();
                keySecondary.clear();
            }

            QFontMetrics fm(mainFont);

            int boxWidth = 0;

            if (entry.combination &&
                entry.keys.size() > 1) {

                const int comboSeparatorGap = 8;

                const int comboSeparatorWidth =
                    fm.horizontalAdvance(
                        QStringLiteral("+")
                    );

                for (int k = 0;
                     k < visualKeys.size();
                     ++k) {

                    const int componentWidth =
                        qMax(
                            minimumBoxWidth,
                            fm.horizontalAdvance(
                                visualKeys.at(k)
                            ) + 28
                        );

                    boxWidth += componentWidth;

                    if (k + 1 <
                        visualKeys.size()) {

                        boxWidth +=
                            comboSeparatorGap * 2 +
                            comboSeparatorWidth;
                    }
                }

            } else {

                QString widthText = primary;

                if (!secondary.isEmpty())
                    widthText += QStringLiteral("  ");

                const int textWidth =
                    fm.horizontalAdvance(
                        widthText
                    );

                boxWidth =
                    qMax(
                        minimumBoxWidth,
                        textWidth + 28
                    );
            }

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
                    primary,
                    secondary,
                    keySecondary
                }
            );

            usedWidth += requiredWidth;
        }

        if (visibleEntries.isEmpty())
            return;

        int x =
            (width() - usedWidth) / 2;

        const qint64 now =
            QDateTime::currentMSecsSinceEpoch();

        /*
         * -----------------------------------------------------------
         * Paleta
         * -----------------------------------------------------------
         */

        const QColor shadowColor(
            0, 0, 0, 190
        );

        const QColor deepShadow(
            0, 0, 0, 90
        );

        const QColor keyTop =
            lightMode
                ? QColor(255, 255, 255, 255)
                : QColor(64, 66, 72, 255);

        const QColor keyMiddle =
            lightMode
                ? QColor(242, 243, 245, 255)
                : QColor(43, 46, 52, 255);

        const QColor keyBottom =
            lightMode
                ? QColor(224, 226, 230, 255)
                : QColor(23, 25, 30, 255);

        const QColor edge =
            lightMode
                ? QColor(0, 0, 0, 55)
                : QColor(255, 255, 255, 68);

        const QColor edgeBottom =
            lightMode
                ? QColor(0, 0, 0, 105)
                : QColor(0, 0, 0, 150);

        const QColor highlight =
            lightMode
                ? QColor(255, 255, 255, 150)
                : QColor(255, 255, 255, 34);

        const QColor textColor =
            lightMode
                ? QColor(28, 30, 34, 255)
                : QColor(250, 250, 252, 255);

        const QColor secondaryColor =
            lightMode
                ? QColor(75, 78, 84, 185)
                : QColor(205, 207, 212, 185);

        const QColor combinationBorder =
            lightMode
                ? QColor(0, 0, 0, 65)
                : QColor(255, 255, 255, 88);
        /*
         * -----------------------------------------------------------
         * Keycaps
         * -----------------------------------------------------------
         */

        /*
         * -----------------------------------------------------------
         * Keycaps
         * -----------------------------------------------------------
         *
         * Las teclas individuales se dibujan como hasta ahora.
         *
         * Las combinaciones se representan como keycaps
         * independientes:
         *
         *     [ Ctrl ] + [ C ]
         *
         * en lugar de:
         *
         *     [ Ctrl + C ]
         *
         * El símbolo + queda fuera de las keycaps.
         */

        for (const DrawEntry &draw : visibleEntries) {

            const VisualEntry &entry =
                visualQueue[draw.index];

            qreal repeatBadgeX = x - 5;

            const qint64 age =
                now - entry.created;

            const qint64 remaining =
                entry.expires - now;

            qreal opacity = 1.0;

            if (age < 100) {

                opacity =
                    qBound(
                        0.0,
                        age / 100.0,
                        1.0
                    );

            } else if (remaining < 140) {

                opacity =
                    qBound(
                        0.0,
                        remaining / 140.0,
                        1.0
                    );
            }

            p.save();

            p.setOpacity(opacity);

            const qreal lift =
                age < 120
                    ? 3.0 *
                      (1.0 -
                       qBound(
                           0.0,
                           age / 120.0,
                           1.0
                       ))
                    : 0.0;

            /*
             * -------------------------------------------------------
             * Lambda común para dibujar una keycap.
             * -------------------------------------------------------
             */

            auto drawKeycap =
                [&](const QRectF &rect,
                    const QString &text,
                    const QStringList &legends,
                    bool combinationCap) {

                QRectF keyRect = rect;
                keyRect.translate(0, -lift);

                /*
                 * Sombra profunda.
                 */
                QRectF deepRect =
                    keyRect.adjusted(
                        0,
                        5,
                        0,
                        7
                    );

                p.setPen(Qt::NoPen);
                p.setBrush(deepShadow);

                p.drawRoundedRect(
                    deepRect,
                    10,
                    10
                );

                /*
                 * Sombra principal.
                 */
                QRectF shadowRect =
                    keyRect.adjusted(
                        0,
                        3,
                        0,
                        5
                    );

                p.setBrush(shadowColor);

                p.drawRoundedRect(
                    shadowRect,
                    10,
                    10
                );

                /*
                 * Cuerpo 3D.
                 */
                QLinearGradient gradient(
                    keyRect.topLeft(),
                    keyRect.bottomLeft()
                );

                gradient.setColorAt(
                    0.0,
                    keyTop
                );

                gradient.setColorAt(
                    0.48,
                    keyMiddle
                );

                gradient.setColorAt(
                    1.0,
                    keyBottom
                );

                p.setBrush(gradient);
                p.setPen(Qt::NoPen);

                p.drawRoundedRect(
                    keyRect,
                    10,
                    10
                );

                /*
                 * Borde.
                 */
                const QColor borderColor =
                    combinationCap
                        ? combinationBorder
                        : edge;

                p.setBrush(Qt::NoBrush);

                p.setPen(
                    QPen(
                        borderColor,
                        1.0
                    )
                );

                p.drawRoundedRect(
                    keyRect.adjusted(
                        0.5,
                        0.5,
                        -0.5,
                        -0.5
                    ),
                    10,
                    10
                );

                /*
                 * Línea superior.
                 */
                p.setPen(
                    QPen(
                        highlight,
                        1.0
                    )
                );

                p.drawLine(
                    keyRect.left() + 8,
                    keyRect.top() + 2,
                    keyRect.right() - 8,
                    keyRect.top() + 2
                );

                /*
                 * Borde inferior.
                 */
                p.setPen(
                    QPen(
                        edgeBottom,
                        1.0
                    )
                );

                p.drawLine(
                    keyRect.left() + 7,
                    keyRect.bottom() - 1,
                    keyRect.right() - 7,
                    keyRect.bottom() - 1
                );

                /*
                 * Serigrafía secundaria.
                 */
                if (!legends.isEmpty()) {

                    QFont legendFont =
                        QApplication::font();

                    legendFont.setPixelSize(12);
                    legendFont.setBold(true);

                    p.setFont(legendFont);
                    p.setPen(secondaryColor);

                    QFontMetrics legendMetrics(
                        legendFont
                    );

                    const int legendCount =
                        qMin(2, legends.size());

                    const int rightMargin = 7;
                    const int topMargin = 4;
                    const int lineSpacing = 1;

                    const int lineHeight =
                        legendMetrics.height();

                    for (int i = 0;
                         i < legendCount;
                         ++i) {

                        const QString &legend =
                            legends.at(i);

                        const int textWidth =
                            legendMetrics.horizontalAdvance(
                                legend
                            );

                        QRectF legendRect(
                            keyRect.right()
                                - rightMargin
                                - textWidth,
                            keyRect.top()
                                + topMargin
                                + i *
                                  (lineHeight +
                                   lineSpacing),
                            textWidth,
                            lineHeight
                        );

                        p.drawText(
                            legendRect,
                            Qt::AlignRight |
                            Qt::AlignTop,
                            legend
                        );
                    }
                }

                /*
                 * Leyenda principal.
                 */
                p.setFont(mainFont);
                p.setPen(textColor);

                QRectF primaryRect =
                    keyRect;

                if (!legends.isEmpty()) {

                    primaryRect.adjust(
                        0,
                        3,
                        0,
                        2
                    );
                }

                p.drawText(
                    primaryRect,
                    Qt::AlignCenter,
                    text
                );
            };

            /*
             * -------------------------------------------------------
             * COMBINACIÓN
             * -------------------------------------------------------
             */

            if (entry.combination &&
                entry.keys.size() > 1) {

                QFontMetrics mainMetrics(mainFont);

                const QString separator =
                    QStringLiteral("+");

                const int separatorWidth =
                    mainMetrics.horizontalAdvance(
                        separator
                    );

                const int separatorGap = 8;

                QVector<QString> components;
                QVector<int> componentWidths;
                QVector<QStringList> componentLegends;

                int totalWidth = 0;

                for (int i = 0;
                     i < entry.keys.size();
                     ++i) {

                         const bool maskCombination =
                             entry.keyboardEntry &&
                             entry.maskKeyboard;

                             const QString component =
                                 maskCombination
                                     ? QStringLiteral("•")
                                     : (
                                         compactMode
                                             ? compactDisplayName(
                                                 entry.keys.at(i)
                                               )
                                             : entry.keys.at(i)
                                       );

                    const int textWidth =
                        mainMetrics.horizontalAdvance(
                            component
                        );

                    const int componentWidth =
                        qMax(
                            minimumBoxWidth,
                            textWidth + 28
                        );

                    components.append(component);
                    componentWidths.append(componentWidth);

                    QStringList legends;

                    if (!entry.keySecondaryLegends.isEmpty() &&
                        i < entry.keySecondaryLegends.size()) {

                        const QString legend =
                            entry.keySecondaryLegends.at(i);

                        if (!legend.isEmpty())
                            legends =
                                legend.split(
                                    QStringLiteral("\n"),
                                    Qt::SkipEmptyParts
                                );

                    } else if (i < draw.keySecondary.size()) {

                        const QString legend =
                            draw.keySecondary.at(i);

                        if (!legend.isEmpty())
                            legends.append(legend);
                    }

                    componentLegends.append(legends);

                    totalWidth += componentWidth;

                    if (i + 1 < entry.keys.size()) {

                        totalWidth +=
                            separatorGap * 2 +
                            separatorWidth;
                    }
                }

                qreal componentX =
                    x +
                    (draw.width - totalWidth) / 2.0;

                /*
                 * Si por alguna razón el ancho calculado supera
                 * ligeramente el espacio reservado, lo centramos
                 * igualmente dentro de la entrada.
                 */
                for (int i = 0;
                     i < components.size();
                     ++i) {

                    if (i == components.size() - 1)
                        repeatBadgeX = componentX - 5;

                    QRectF componentRect(
                        componentX,
                        top,
                        componentWidths.at(i),
                        boxHeight
                    );

                    drawKeycap(
                        componentRect,
                        components.at(i),
                        componentLegends.at(i),
                        true
                    );

                    componentX +=
                        componentWidths.at(i);

                    if (i + 1 <
                        components.size()) {

                        /*
                         * Separador +.
                         */
                        componentX += separatorGap;

                        p.setFont(mainFont);
                        p.setPen(secondaryColor);

                        QRectF plusRect(
                            componentX,
                            top,
                            separatorWidth,
                            boxHeight
                        );

                        p.drawText(
                            plusRect,
                            Qt::AlignCenter,
                            separator
                        );

                        componentX +=
                            separatorWidth +
                            separatorGap;
                    }
                }

            } else {

                /*
                 * ---------------------------------------------------
                 * TECLA INDIVIDUAL
                 * ---------------------------------------------------
                 */

                QRectF r(
                    x,
                    top,
                    draw.width,
                    boxHeight
                );

                drawKeycap(
                    r,
                    draw.primary,
                    draw.secondary,
                    false
                );
            }

            /*
             * -------------------------------------------------------
             * Burbuja de repetición
             * -------------------------------------------------------
             */

            if (entry.repeatCount > 1) {

                const QString countText =
                    QStringLiteral("×%1")
                        .arg(entry.repeatCount);

                QFont badgeFont =
                    QApplication::font();

                badgeFont.setPixelSize(11);
                badgeFont.setBold(true);

                QFontMetrics badgeMetrics(
                    badgeFont
                );

                const int badgeWidth =
                    qMax(
                        24,
                        badgeMetrics.horizontalAdvance(
                            countText
                        ) + 12
                    );

                const int badgeHeight = 19;

                const qreal badgeScale =
                    age < 140
                        ? 1.0 +
                          0.18 *
                          (1.0 -
                           qBound(
                               0.0,
                               age / 140.0,
                               1.0
                           ))
                        : 1.0;

                const qreal scaledBadgeWidth =
                    badgeWidth * badgeScale;

                const qreal scaledBadgeHeight =
                    badgeHeight * badgeScale;

                QRectF badgeRect(
                    repeatBadgeX,
                    top - 7,
                    scaledBadgeWidth,
                    scaledBadgeHeight
                );

                QRectF badgeShadow =
                    badgeRect.adjusted(
                        0,
                        2,
                        0,
                        3
                    );

                p.setPen(Qt::NoPen);

                p.setBrush(
                    QColor(
                        0,
                        0,
                        0,
                        170
                    )
                );

                p.drawRoundedRect(
                    badgeShadow,
                    8.5,
                    8.5
                );

                QLinearGradient badgeGradient(
                    badgeRect.topLeft(),
                    badgeRect.bottomLeft()
                );

                badgeGradient.setColorAt(
                    0.0,
                    QColor(
                        82,
                        84,
                        90,
                        245
                    )
                );

                badgeGradient.setColorAt(
                    1.0,
                    QColor(
                        42,
                        44,
                        49,
                        245
                    )
                );

                p.setBrush(badgeGradient);

                p.drawRoundedRect(
                    badgeRect,
                    8.5,
                    8.5
                );

                p.setBrush(Qt::NoBrush);

                p.setPen(
                    QPen(
                        QColor(
                            255,
                            255,
                            255,
                            70
                        ),
                        1.0
                    )
                );

                p.drawRoundedRect(
                    badgeRect.adjusted(
                        0.5,
                        0.5,
                        -0.5,
                        -0.5
                    ),
                    8,
                    8
                );

                p.setFont(badgeFont);

                p.setPen(
                    QColor(
                        248,
                        248,
                        250,
                        235
                    )
                );

                p.drawText(
                    badgeRect,
                    Qt::AlignCenter,
                    countText
                );
            }

            p.restore();

            x +=
                draw.width +
                spacing;
        }
    }

    void paintStatusIndicators(QPainter &p)
    {
        enum class StatusIcon {
            HideKeys,
            Pause,
            ClickThrough,
            AlwaysOnTop,
            OnlyShortcuts,
            CapsLock,
            NumLock,
            ScrollLock
        };

        QVector<StatusIcon> items;

        if (hideKeys)
            items.append(StatusIcon::HideKeys);

        if (pauseInput)
            items.append(StatusIcon::Pause);

        if (clickThrough)
            items.append(StatusIcon::ClickThrough);

        if (alwaysOnTop)
            items.append(StatusIcon::AlwaysOnTop);

        if (onlyShortcuts)
            items.append(StatusIcon::OnlyShortcuts);

        const bool capsLock =
            xkbState &&
            xkb_state_led_name_is_active(
                xkbState,
                XKB_LED_NAME_CAPS
            ) > 0;

        const bool numLock =
            xkbState &&
            xkb_state_led_name_is_active(
                xkbState,
                XKB_LED_NAME_NUM
            ) > 0;

        const bool scrollLock =
            xkbState &&
            xkb_state_led_name_is_active(
                xkbState,
                XKB_LED_NAME_SCROLL
            ) > 0;

        if (capsLock)
            items.append(StatusIcon::CapsLock);

        if (numLock)
            items.append(StatusIcon::NumLock);

        if (scrollLock)
            items.append(StatusIcon::ScrollLock);

        if (items.isEmpty())
            return;

        p.save();

        p.setRenderHint(QPainter::Antialiasing, true);

        const int size = 18;
        const int spacing = 5;
        const int rightMargin = 33;
        const int topMargin = 5;

        const QColor background(32, 34, 40, 175);
        const QColor border(255, 255, 255, 48);
        const QColor shadow(0, 0, 0, 85);
        const QColor iconColor(248, 249, 251, 238);

        const int totalWidth =
            items.size() * size +
            (items.size() - 1) * spacing;

        int x = width() - rightMargin - totalWidth;

        auto drawIcon =
            [&](QPainter &painter,
                const QRectF &rect,
                StatusIcon icon)
        {
            const QPointF c = rect.center();

            QPen pen(
                iconColor,
                1.35,
                Qt::SolidLine,
                Qt::RoundCap,
                Qt::RoundJoin
            );

            painter.setPen(pen);
            painter.setBrush(Qt::NoBrush);

            switch (icon) {

            case StatusIcon::HideKeys: {
                // Candado
                const QRectF body(
                    c.x() - 4.0,
                    c.y() - 0.5,
                    8.0,
                    6.5
                );

                painter.drawRoundedRect(body, 1.5, 1.5);

                QPainterPath shackle;
                shackle.moveTo(c.x() - 3.0, c.y() - 0.5);
                shackle.lineTo(c.x() - 3.0, c.y() - 2.5);
                shackle.cubicTo(
                    c.x() - 3.0, c.y() - 5.5,
                    c.x() + 3.0, c.y() - 5.5,
                    c.x() + 3.0, c.y() - 2.5
                );
                shackle.lineTo(c.x() + 3.0, c.y() - 0.5);

                painter.drawPath(shackle);
                break;
            }

            case StatusIcon::Pause:
                painter.drawRoundedRect(
                    QRectF(c.x() - 3.8, c.y() - 5.0, 2.4, 10.0),
                    1.0,
                    1.0
                );
                painter.drawRoundedRect(
                    QRectF(c.x() + 1.4, c.y() - 5.0, 2.4, 10.0),
                    1.0,
                    1.0
                );
                break;

            case StatusIcon::ClickThrough: {
                // Ratón
                const QRectF mouse(
                    c.x() - 4.0,
                    c.y() - 5.5,
                    8.0,
                    11.0
                );

                painter.drawRoundedRect(mouse, 3.5, 3.5);
                painter.drawLine(
                    QPointF(c.x(), c.y() - 5.0),
                    QPointF(c.x(), c.y() + 5.0)
                );
                painter.drawLine(
                    QPointF(c.x(), c.y() - 2.5),
                    QPointF(c.x(), c.y() - 0.5)
                );
                break;
            }

            case StatusIcon::AlwaysOnTop: {
                // Pin
                QPolygonF pin;
                pin << QPointF(c.x(),     c.y() - 5.0)
                    << QPointF(c.x() + 3.2, c.y() - 1.5)
                    << QPointF(c.x() + 1.2, c.y() + 0.5)
                    << QPointF(c.x() + 1.2, c.y() + 3.0)
                    << QPointF(c.x() - 1.2, c.y() + 3.0)
                    << QPointF(c.x() - 1.2, c.y() + 0.5)
                    << QPointF(c.x() - 3.2, c.y() - 1.5);

                painter.drawPolygon(pin);

                painter.drawLine(
                    QPointF(c.x(), c.y() + 3.0),
                    QPointF(c.x(), c.y() + 5.5)
                );
                break;
            }

            case StatusIcon::OnlyShortcuts: {
                // ↯
                QPolygonF bolt;
                bolt << QPointF(c.x() + 1.5, c.y() - 6.0)
                     << QPointF(c.x() - 2.5, c.y() - 0.5)
                     << QPointF(c.x(),      c.y() - 0.5)
                     << QPointF(c.x() - 1.5, c.y() + 6.0)
                     << QPointF(c.x() + 2.5, c.y() + 0.5)
                     << QPointF(c.x(),      c.y() + 0.5);

                painter.drawPolygon(bolt);
                break;
            }

            case StatusIcon::CapsLock: {
                // ⇪
                painter.drawRoundedRect(
                    QRectF(c.x() - 5.0, c.y() + 0.5, 10.0, 4.5),
                    1.0,
                    1.0
                );

                painter.drawLine(
                    QPointF(c.x(), c.y() + 0.5),
                    QPointF(c.x(), c.y() - 5.0)
                );

                painter.drawLine(
                    QPointF(c.x(), c.y() - 5.0),
                    QPointF(c.x() - 2.5, c.y() - 2.5)
                );

                painter.drawLine(
                    QPointF(c.x(), c.y() - 5.0),
                    QPointF(c.x() + 2.5, c.y() - 2.5)
                );
                break;
            }

            case StatusIcon::NumLock: {
                // Pequeño teclado numérico
                const qreal cell = 2.2;
                const qreal gap = 1.0;
                const qreal total = cell * 3 + gap * 2;
                const qreal startX = c.x() - total / 2.0;
                const qreal startY = c.y() - total / 2.0;

                for (int row = 0; row < 3; ++row) {
                    for (int col = 0; col < 3; ++col) {
                        painter.drawRoundedRect(
                            QRectF(
                                startX + col * (cell + gap),
                                startY + row * (cell + gap),
                                cell,
                                cell
                            ),
                            0.5,
                            0.5
                        );
                    }
                }
                break;
            }

            case StatusIcon::ScrollLock: {
                // ⇳
                painter.drawLine(
                    QPointF(c.x(), c.y() - 5.5),
                    QPointF(c.x(), c.y() + 5.5)
                );

                painter.drawLine(
                    QPointF(c.x(), c.y() - 5.5),
                    QPointF(c.x() - 2.3, c.y() - 3.0)
                );

                painter.drawLine(
                    QPointF(c.x(), c.y() - 5.5),
                    QPointF(c.x() + 2.3, c.y() - 3.0)
                );

                painter.drawLine(
                    QPointF(c.x(), c.y() + 5.5),
                    QPointF(c.x() - 2.3, c.y() + 3.0)
                );

                painter.drawLine(
                    QPointF(c.x(), c.y() + 5.5),
                    QPointF(c.x() + 2.3, c.y() + 3.0)
                );
                break;
            }
            }
        };

        for (const StatusIcon icon : items) {

            const QRectF rect(
                x,
                topMargin,
                size,
                size
            );

            // Sombra
            p.setPen(Qt::NoPen);
            p.setBrush(shadow);
            p.drawRoundedRect(
                rect.adjusted(0, 1, 0, 2),
                7,
                7
            );

            // Fondo fijo para ambos modos
            p.setBrush(background);
            p.drawRoundedRect(
                rect,
                7,
                7
            );

            // Borde fijo
            p.setBrush(Qt::NoBrush);
            p.setPen(QPen(border, 1.0));
            p.drawRoundedRect(
                rect.adjusted(0.5, 0.5, -0.5, -0.5),
                6.5,
                6.5
            );

            // Icono
            drawIcon(
                p,
                rect.adjusted(2.0, 2.0, -2.0, -2.0),
                icon
            );

            x += size + spacing;
        }

        p.restore();
    }

    void contextMenuEvent(QContextMenuEvent *event) override;

private:

    /*
     * -----------------------------------------------------------
     * ESTADO REAL MULTIMEDIA
     * -----------------------------------------------------------
     *
     * El icono no representa simplemente la última tecla pulsada.
     * Se sincroniza con:
     *
     *   MPRIS -> PlaybackStatus
     *   PipeWire -> estado de mute del sink por defecto
     *
     * De esta forma también detectamos cambios hechos con el ratón,
     * botones del reproductor, auriculares, etc.
     */
    /*
     * -----------------------------------------------------------
     * ESTADOS REALES DEL SISTEMA
     * -----------------------------------------------------------
     *
     * Las tarjetas multimedia/hardware no dependen únicamente de
     * la última tecla pulsada. Consultamos el estado real:
     *
     *   MPRIS       -> reproducción / repeat / shuffle
     *   PipeWire    -> volumen / mute / micrófono
     *   NetworkMgr  -> Wi-Fi / WWAN
     *   BlueZ       -> Bluetooth
     *   rfkill      -> estado de radios
     *   backlight   -> brillo
     *   KWin        -> touchpad
     *
     * Esto permite que el overlay se actualice incluso cuando el
     * usuario cambia el estado desde Plasma, el reproductor, etc.
     */

    QString mprisPlaybackStatus =
        QStringLiteral("Stopped");

    QString mprisLoopStatus =
        QStringLiteral("None");

    bool mprisShuffle = false;

    bool realAudioMuted = false;
    int realAudioVolumePercent = 0;

    bool realMicMuted = false;
    int realMicVolumePercent = 0;

    bool wifiEnabled = false;
    bool wifiHardwareEnabled = true;
    bool wifiStateKnown = false;

    bool wwanEnabled = false;
    bool wwanHardwareEnabled = true;
    bool wwanStateKnown = false;

    bool bluetoothPowered = false;
    bool bluetoothStateKnown = false;

    bool allRadiosEnabled = true;
    bool rfkillStateKnown = false;

    int brightnessPercent = 0;
    bool brightnessStateKnown = false;

    bool touchpadEnabled = true;
    bool touchpadStateKnown = false;

    QTimer mediaStateTimer;

    void updateMediaState()
    {
        /*
         * -------------------------------------------------------
         * Helper D-Bus genérico
         * -------------------------------------------------------
         */

        auto readDBusProperty =
            [](const QDBusConnection &bus,
               const QString &service,
               const QString &path,
               const QString &interface,
               const QString &property) -> QVariant {

                QDBusInterface props(
                    service,
                    path,
                    QStringLiteral(
                        "org.freedesktop.DBus.Properties"
                    ),
                    bus
                );

                if (!props.isValid())
                    return QVariant();

                const QDBusMessage reply =
                    props.call(
                        QStringLiteral("Get"),
                        interface,
                        property
                    );

                if (reply.type() !=
                    QDBusMessage::ReplyMessage) {

                    return QVariant();
                }

                const QList<QVariant> args =
                    reply.arguments();

                if (args.isEmpty())
                    return QVariant();

                const QVariant outer =
                    args.first();

                if (!outer.canConvert<QDBusVariant>())
                    return QVariant();

                return qvariant_cast<QDBusVariant>(
                    outer
                ).variant();
            };

        /*
         * -------------------------------------------------------
         * MPRIS
         * -------------------------------------------------------
         */

        QString playbackStatus =
            QStringLiteral("Stopped");

        QString loopStatus =
            QStringLiteral("None");

        bool shuffle = false;

        QString activePlayer;

        const QDBusConnection sessionBus =
            QDBusConnection::sessionBus();

        QDBusConnectionInterface *busInterface =
            sessionBus.interface();

        auto playerProperty =
            [&](const QString &service,
                const QString &property) -> QVariant {

                return readDBusProperty(
                    sessionBus,
                    service,
                    QStringLiteral(
                        "/org/mpris/MediaPlayer2"
                    ),
                    QStringLiteral(
                        "org.mpris.MediaPlayer2.Player"
                    ),
                    property
                );
            };

        auto readPlayerStatus =
            [&](const QString &service) -> QString {

                return playerProperty(
                    service,
                    QStringLiteral("PlaybackStatus")
                ).toString();
            };

        /*
         * playerctld representa el reproductor activo/reciente
         * cuando está disponible.
         */
        if (busInterface &&
            busInterface->isServiceRegistered(
                QStringLiteral(
                    "org.mpris.MediaPlayer2.playerctld"
                ))) {

            const QString status =
                readPlayerStatus(
                    QStringLiteral(
                        "org.mpris.MediaPlayer2.playerctld"
                    )
                );

            if (status == QStringLiteral("Playing") ||
                status == QStringLiteral("Paused") ||
                status == QStringLiteral("Stopped")) {

                activePlayer =
                    QStringLiteral(
                        "org.mpris.MediaPlayer2.playerctld"
                    );

                playbackStatus = status;
            }
        }

        /*
         * Fallback: buscamos un reproductor reproduciendo.
         */
        if (activePlayer.isEmpty() &&
            busInterface) {

            const QStringList services =
                busInterface->registeredServiceNames();

            QString pausedPlayer;

            for (const QString &service : services) {

                if (!service.startsWith(
                        QStringLiteral(
                            "org.mpris.MediaPlayer2."
                        )
                    )) {

                    continue;
                }

                if (service ==
                    QStringLiteral(
                        "org.mpris.MediaPlayer2.playerctld"
                    )) {

                    continue;
                }

                const QString status =
                    readPlayerStatus(service);

                if (status == QStringLiteral("Playing")) {

                    activePlayer = service;
                    playbackStatus = status;
                    break;
                }

                if (status == QStringLiteral("Paused") &&
                    pausedPlayer.isEmpty()) {

                    pausedPlayer = service;
                }
            }

            if (activePlayer.isEmpty() &&
                !pausedPlayer.isEmpty()) {

                activePlayer = pausedPlayer;
                playbackStatus =
                    QStringLiteral("Paused");
            }
        }

        if (!activePlayer.isEmpty()) {

            const QString detectedLoop =
                playerProperty(
                    activePlayer,
                    QStringLiteral("LoopStatus")
                ).toString();

            if (detectedLoop == QStringLiteral("None") ||
                detectedLoop == QStringLiteral("Track") ||
                detectedLoop == QStringLiteral("Playlist")) {

                loopStatus = detectedLoop;
            }

            const QVariant shuffleValue =
                playerProperty(
                    activePlayer,
                    QStringLiteral("Shuffle")
                );

            if (shuffleValue.isValid())
                shuffle = shuffleValue.toBool();
        }

        mprisPlaybackStatus =
            playbackStatus;

        mprisLoopStatus =
            loopStatus;

        mprisShuffle =
            shuffle;

        /*
         * -------------------------------------------------------
         * PIPEWIRE / VOLUMEN + MUTE
         * -------------------------------------------------------
         *
         * wpctl get-volume devuelve el volumen como 0.0 = 0%,
         * 1.0 = 100%, y puede superar 1.0 si hay amplificación.
         *
         * También añade [MUTED] cuando el nodo está muteado.
         */

        auto readWpctlVolume =
            [](const QString &node,
               int &percent,
               bool &muted) -> bool {

                QProcess process;

                process.start(
                    QStringLiteral("wpctl"),
                    QStringList{
                        QStringLiteral("get-volume"),
                        node
                    }
                );

                if (!process.waitForFinished(120))
                    return false;

                if (process.exitStatus() !=
                    QProcess::NormalExit ||
                    process.exitCode() != 0) {

                    return false;
                }

                const QString output =
                    QString::fromLocal8Bit(
                        process.readAllStandardOutput()
                    ).trimmed();

                if (output.isEmpty())
                    return false;

                static const QRegularExpression re(
                    QStringLiteral(
                        R"(Volume:\s*([0-9]+(?:\.[0-9]+)?))"
                    )
                );

                const QRegularExpressionMatch match =
                    re.match(output);

                if (!match.hasMatch())
                    return false;

                bool ok = false;

                const double volume =
                    match.captured(1).toDouble(&ok);

                if (!ok)
                    return false;

                percent =
                    qRound(volume * 100.0);

                muted =
                    output.contains(
                        QStringLiteral("[MUTED]")
                    );

                return true;
            };

        int sinkVolume = realAudioVolumePercent;
        bool sinkMuted = realAudioMuted;

        if (readWpctlVolume(
                QStringLiteral(
                    "@DEFAULT_AUDIO_SINK@"
                ),
                sinkVolume,
                sinkMuted)) {

            realAudioVolumePercent =
                sinkVolume;

            realAudioMuted =
                sinkMuted;
        }

        int sourceVolume =
            realMicVolumePercent;

        bool sourceMuted =
            realMicMuted;

        if (readWpctlVolume(
                QStringLiteral(
                    "@DEFAULT_AUDIO_SOURCE@"
                ),
                sourceVolume,
                sourceMuted)) {

            realMicVolumePercent =
                sourceVolume;

            realMicMuted =
                sourceMuted;
        }

        /*
         * -------------------------------------------------------
         * NETWORKMANAGER / WIFI + WWAN
         * -------------------------------------------------------
         *
         * NetworkManager diferencia:
         *
         *   WirelessEnabled
         *   WirelessHardwareEnabled
         *
         * y lo mismo para WWAN.
         *
         * El segundo indica además si existe un bloqueo RF físico.
         */

        const QDBusConnection systemBus =
            QDBusConnection::systemBus();

        const QString nmService =
            QStringLiteral(
                "org.freedesktop.NetworkManager"
            );

        const QString nmPath =
            QStringLiteral(
                "/org/freedesktop/NetworkManager"
            );

        const QString nmInterface =
            QStringLiteral(
                "org.freedesktop.NetworkManager"
            );

        const QVariant networking =
            readDBusProperty(
                systemBus,
                nmService,
                nmPath,
                nmInterface,
                QStringLiteral(
                    "NetworkingEnabled"
                )
            );

        const QVariant nmWifi =
            readDBusProperty(
                systemBus,
                nmService,
                nmPath,
                nmInterface,
                QStringLiteral(
                    "WirelessEnabled"
                )
            );

        const QVariant nmWifiHw =
            readDBusProperty(
                systemBus,
                nmService,
                nmPath,
                nmInterface,
                QStringLiteral(
                    "WirelessHardwareEnabled"
                )
            );

        if (networking.isValid() &&
            nmWifi.isValid() &&
            nmWifiHw.isValid()) {

            wifiEnabled =
                networking.toBool() &&
                nmWifi.toBool();

            wifiHardwareEnabled =
                nmWifiHw.toBool();

            wifiStateKnown = true;
        }

        const QVariant nmWwan =
            readDBusProperty(
                systemBus,
                nmService,
                nmPath,
                nmInterface,
                QStringLiteral(
                    "WwanEnabled"
                )
            );

        const QVariant nmWwanHw =
            readDBusProperty(
                systemBus,
                nmService,
                nmPath,
                nmInterface,
                QStringLiteral(
                    "WwanHardwareEnabled"
                )
            );

        if (networking.isValid() &&
            nmWwan.isValid() &&
            nmWwanHw.isValid()) {

            wwanEnabled =
                networking.toBool() &&
                nmWwan.toBool();

            wwanHardwareEnabled =
                nmWwanHw.toBool();

            wwanStateKnown = true;
        }

        /*
         * -------------------------------------------------------
         * BLUETOOTH / BLUEZ
         * -------------------------------------------------------
         *
         * Consultamos los adaptadores hci* y su propiedad Powered.
         */

        bool foundBluetooth =
            false;

        bool anyBluetoothPowered =
            false;

        const QStringList bluetoothAdapters =
            QDir(
                QStringLiteral(
                    "/sys/class/bluetooth"
                )
            ).entryList(
                QStringList{
                    QStringLiteral("hci*")
                },
                QDir::Dirs |
                QDir::NoDotAndDotDot
            );

        for (const QString &adapter :
             bluetoothAdapters) {

            const QVariant powered =
                readDBusProperty(
                    systemBus,
                    QStringLiteral("org.bluez"),
                    QStringLiteral(
                        "/org/bluez/"
                    ) + adapter,
                    QStringLiteral(
                        "org.bluez.Adapter1"
                    ),
                    QStringLiteral("Powered")
                );

            if (!powered.isValid())
                continue;

            foundBluetooth = true;

            if (powered.toBool())
                anyBluetoothPowered = true;
        }

        if (foundBluetooth) {

            bluetoothPowered =
                anyBluetoothPowered;

            bluetoothStateKnown =
                true;
        }

        /*
         * -------------------------------------------------------
         * RFKILL
         * -------------------------------------------------------
         *
         * Si cualquier radio registrado está bloqueado, el estado
         * global no está completamente habilitado.
         */

        bool foundRfkill =
            false;

        bool blockedRadio =
            false;

        const QDir rfkillDir(
            QStringLiteral(
                "/sys/class/rfkill"
            )
        );

        const QStringList rfkillEntries =
            rfkillDir.entryList(
                QStringList{
                    QStringLiteral("rfkill*")
                },
                QDir::Dirs |
                QDir::NoDotAndDotDot
            );

        for (const QString &entry :
             rfkillEntries) {

            const QString base =
                rfkillDir.filePath(entry);

            QFile hardFile(
                base +
                QStringLiteral("/hard")
            );

            QFile softFile(
                base +
                QStringLiteral("/soft")
            );

            if (!hardFile.open(QIODevice::ReadOnly))
                continue;

            if (!softFile.open(QIODevice::ReadOnly))
                continue;

            const int hard =
                hardFile.readAll()
                    .trimmed()
                    .toInt();

            const int soft =
                softFile.readAll()
                    .trimmed()
                    .toInt();

            foundRfkill = true;

            if (hard != 0 ||
                soft != 0) {

                blockedRadio = true;
            }
        }

        if (foundRfkill) {

            allRadiosEnabled =
                !blockedRadio;

            rfkillStateKnown =
                true;
        }

        /*
         * -------------------------------------------------------
         * BRILLO
         * -------------------------------------------------------
         *
         * /sys/class/backlight/<device>/brightness
         * /sys/class/backlight/<device>/max_brightness
         */

        const QDir backlightDir(
            QStringLiteral(
                "/sys/class/backlight"
            )
        );

        const QStringList backlights =
            backlightDir.entryList(
                QDir::Dirs |
                QDir::NoDotAndDotDot
            );

        int selectedBrightness =
            -1;

        int selectedMaximum =
            -1;

        for (const QString &device :
             backlights) {

            const QString base =
                backlightDir.filePath(device);

            QFile brightnessFile(
                base +
                QStringLiteral("/brightness")
            );

            QFile maximumFile(
                base +
                QStringLiteral("/max_brightness")
            );

            if (!brightnessFile.open(
                    QIODevice::ReadOnly
                )) {

                continue;
            }

            if (!maximumFile.open(
                    QIODevice::ReadOnly
                )) {

                continue;
            }

            bool okBrightness = false;
            bool okMaximum = false;

            const int current =
                QString::fromLatin1(
                    brightnessFile.readAll()
                ).trimmed().toInt(
                    &okBrightness
                );

            const int maximum =
                QString::fromLatin1(
                    maximumFile.readAll()
                ).trimmed().toInt(
                    &okMaximum
                );

            if (!okBrightness ||
                !okMaximum ||
                maximum <= 0) {

                continue;
            }

            selectedBrightness = current;
            selectedMaximum = maximum;

            /*
             * Normalmente solo existe un backlight. Si hay varios,
             * usamos el primero válido.
             */
            break;
        }

        if (selectedMaximum > 0) {

            brightnessPercent =
                qRound(
                    100.0 *
                    static_cast<double>(
                        selectedBrightness
                    ) /
                    static_cast<double>(
                        selectedMaximum
                    )
                );

            brightnessPercent =
                qBound(
                    0,
                    brightnessPercent,
                    100
                );

            brightnessStateKnown =
                true;
        }

        /*
         * -------------------------------------------------------
         * TOUCHPAD / KWIN
         * -------------------------------------------------------
         *
         * KWin expone los dispositivos de entrada por D-Bus.
         * Buscamos los que tienen touchpad == true y leemos
         * enabled.
         */

        QDBusInterface kwinManager(
            QStringLiteral("org.kde.KWin"),
            QStringLiteral(
                "/org/kde/KWin/InputDevice"
            ),
            QStringLiteral(
                "org.freedesktop.DBus.Properties"
            ),
            sessionBus
        );

        if (kwinManager.isValid()) {

            const QDBusMessage reply =
                kwinManager.call(
                    QStringLiteral("Get"),
                    QStringLiteral(
                        "org.kde.KWin.InputDeviceManager"
                    ),
                    QStringLiteral(
                        "devicesSysNames"
                    )
                );

            if (reply.type() ==
                    QDBusMessage::ReplyMessage &&
                !reply.arguments().isEmpty()) {

                const QVariant outer =
                    reply.arguments().first();

                if (outer.canConvert<QDBusVariant>()) {

                    const QVariant devicesVariant =
                        qvariant_cast<QDBusVariant>(
                            outer
                        ).variant();

                    const QStringList devices =
                        devicesVariant.toStringList();

                    bool foundTouchpad =
                        false;

                    bool enabled =
                        true;

                    for (const QString &device :
                         devices) {

                        const QString path =
                            QStringLiteral(
                                "/org/kde/KWin/InputDevice/"
                            ) +
                            device;

                        const QVariant isTouchpad =
                            readDBusProperty(
                                sessionBus,
                                QStringLiteral(
                                    "org.kde.KWin"
                                ),
                                path,
                                QStringLiteral(
                                    "org.kde.KWin.InputDevice"
                                ),
                                QStringLiteral(
                                    "touchpad"
                                )
                            );

                        if (!isTouchpad.isValid() ||
                            !isTouchpad.toBool()) {

                            continue;
                        }

                        const QVariant deviceEnabled =
                            readDBusProperty(
                                sessionBus,
                                QStringLiteral(
                                    "org.kde.KWin"
                                ),
                                path,
                                QStringLiteral(
                                    "org.kde.KWin.InputDevice"
                                ),
                                QStringLiteral(
                                    "enabled"
                                )
                            );

                        if (!deviceEnabled.isValid())
                            continue;

                        foundTouchpad = true;

                        if (!deviceEnabled.toBool()) {
                            enabled = false;
                            break;
                        }
                    }

                    if (foundTouchpad) {

                        touchpadEnabled =
                            enabled;

                        touchpadStateKnown =
                            true;
                    }
                }
            }
        }

        /*
         * -------------------------------------------------------
         * ACTUALIZAR ENTRADAS VISIBLES
         * -------------------------------------------------------
         *
         * Las tarjetas que siguen en pantalla se sincronizan con
         * el estado real aunque el cambio haya ocurrido fuera de
         * KeyCast.
         */

        const QString volumeText =
            QStringLiteral(
                "%1 %2%"
            ).arg(
                realAudioMuted
                    ? QStringLiteral("🔇")
                    : QStringLiteral("🔊")
            ).arg(
                realAudioVolumePercent
            );

        const QString micText =
            QStringLiteral(
                "%1 %2%"
            ).arg(
                realMicMuted
                    ? QStringLiteral("🔇")
                    : QStringLiteral("🎙")
            ).arg(
                realMicVolumePercent
            );

        const QString playbackText =
            mprisPlaybackStatus ==
                QStringLiteral("Playing")
                ? QStringLiteral("Ⅱ")
                : QStringLiteral("▶");

        QString repeatText =
            QStringLiteral("↻");

        if (mprisLoopStatus ==
            QStringLiteral("Playlist")) {

            repeatText =
                QStringLiteral("↻∞");

        } else if (mprisLoopStatus ==
                   QStringLiteral("None")) {

            repeatText =
                QStringLiteral("↻");
        }

        const QString shuffleText =
            mprisShuffle
                ? QStringLiteral("🔀")
                : QStringLiteral("⇄");

        const QString brightnessText =
            QStringLiteral(
                "☀ %1%"
            ).arg(
                brightnessPercent
            );

        const QString wifiText =
            wifiEnabled &&
            wifiHardwareEnabled
                ? QStringLiteral("📶")
                : QStringLiteral("⊘");

        const QString wwanText =
            wwanEnabled &&
            wwanHardwareEnabled
                ? QStringLiteral("📡")
                : QStringLiteral("⊘");

        const QString bluetoothText =
            bluetoothPowered
                ? QStringLiteral("ᛒ")
                : QStringLiteral("⊘");

        const QString rfkillText =
            allRadiosEnabled
                ? QStringLiteral("📡")
                : QStringLiteral("⊘");

        const QString touchpadText =
            touchpadEnabled
                ? QStringLiteral("⌁")
                : QStringLiteral("⊘");

        for (auto &entry :
             visualQueue) {

            /*
             * Mute / volumen
             */
            if (entry.linuxKeyCode == 113 ||
                entry.linuxKeyCode == 114 ||
                entry.linuxKeyCode == 115) {

                entry.keys =
                    QStringList{
                        volumeText
                    };
            }

            /*
             * Mic mute
             */
            if (entry.linuxKeyCode == 248) {

                entry.keys =
                    QStringList{
                        micText
                    };
            }

            /*
             * Play/Pause
             */
            if (entry.linuxKeyCode == 164 ||
                entry.linuxKeyCode == 207) {

                entry.keys =
                    QStringList{
                        playbackText
                    };
            }

            /*
             * Repeat
             */
            if (entry.linuxKeyCode == 439) {

                entry.keys =
                    QStringList{
                        repeatText
                    };
            }

            /*
             * Shuffle
             */
            if (entry.linuxKeyCode == 410) {

                entry.keys =
                    QStringList{
                        shuffleText
                    };
            }

            /*
             * Brillo:
             *
             * 224 = KEY_BRIGHTNESSDOWN
             * 225 = KEY_BRIGHTNESSUP
             * 243 = KEY_BRIGHTNESS_CYCLE
             * 592 = KEY_BRIGHTNESS_MIN
             * 593 = KEY_BRIGHTNESS_MAX
             */
            if (entry.linuxKeyCode == 224 ||
                entry.linuxKeyCode == 225 ||
                entry.linuxKeyCode == 243 ||
                entry.linuxKeyCode == 592 ||
                entry.linuxKeyCode == 593) {

                entry.keys =
                    QStringList{
                        brightnessText
                    };
            }

            /*
             * Wi-Fi
             */
            if (entry.linuxKeyCode == 238 &&
                wifiStateKnown) {

                entry.keys =
                    QStringList{
                        wifiText
                    };
            }

            /*
             * WWAN
             */
            if (entry.linuxKeyCode == 246 &&
                wwanStateKnown) {

                entry.keys =
                    QStringList{
                        wwanText
                    };
            }

            /*
             * Bluetooth
             */
            if (entry.linuxKeyCode == 237 &&
                bluetoothStateKnown) {

                entry.keys =
                    QStringList{
                        bluetoothText
                    };
            }

            /*
             * RFKill
             */
            if (entry.linuxKeyCode == 247 &&
                rfkillStateKnown) {

                entry.keys =
                    QStringList{
                        rfkillText
                    };
            }

            /*
             * Touchpad:
             *
             * 530 = TOGGLE
             * 531 = ON
             * 532 = OFF
             */
            if ((entry.linuxKeyCode == 530 ||
                 entry.linuxKeyCode == 531 ||
                 entry.linuxKeyCode == 532) &&
                touchpadStateKnown) {

                entry.keys =
                    QStringList{
                        touchpadText
                    };
            }
        }
    }

private:

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
    char debugName[128] {};

    xkb_keysym_get_name(
        sym,
        debugName,
        sizeof(debugName)
    );

    qDebug()
        << "keysymToText:"
        << Qt::hex
        << static_cast<uint32_t>(sym)
        << Qt::dec
        << debugName;


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

    case XKB_KEY_ISO_Left_Tab:
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
     * Teclas XF86 / multimedia / hardware
     * ---------------------------------------------------------------
     *
     * Estas teclas tienen keysym XKB pero no representan un carácter
     * Unicode. Usamos leyendas cortas para que encajen bien en las
     * teclas del overlay.
     */
    switch (sym) {

    case XKB_KEY_XF86HomePage:
        return QStringLiteral("⌂");

    case XKB_KEY_XF86Back:
        return QStringLiteral("↩");

    case XKB_KEY_XF86MonBrightnessUp:
        return QStringLiteral("☀+");

    case XKB_KEY_XF86MonBrightnessDown:
        return QStringLiteral("☀−");

    case XKB_KEY_XF86MonBrightnessCycle:
        return QStringLiteral("☀↻");

    case XKB_KEY_XF86AudioLowerVolume:
        return QStringLiteral("🔉");

    case XKB_KEY_XF86AudioRaiseVolume:
        return QStringLiteral("🔊");

    case XKB_KEY_XF86AudioMute:
        return QStringLiteral("🔇");

    case XKB_KEY_XF86AudioMicMute:
        return QStringLiteral("🎙×");

    case XKB_KEY_XF86AudioPlay:
        return QStringLiteral("▶");

    case XKB_KEY_XF86AudioPause:
        return QStringLiteral("Ⅱ");

    case XKB_KEY_XF86AudioStop:
        return QStringLiteral("■");

    case XKB_KEY_XF86AudioPrev:
        return QStringLiteral("◀|");

    case XKB_KEY_XF86AudioNext:
        return QStringLiteral("|▶");

    case XKB_KEY_XF86AudioRewind:
        return QStringLiteral("◀◀");

    case XKB_KEY_XF86AudioForward:
        return QStringLiteral("▶▶");

    case XKB_KEY_XF86AudioRepeat:
        return QStringLiteral("↻");

    case XKB_KEY_XF86AudioRandomPlay:
        return QStringLiteral("🔀");

    case XKB_KEY_XF86Bluetooth:
        return QStringLiteral("ᛒ");

    case XKB_KEY_XF86WLAN:
        return QStringLiteral("⌁");

    case XKB_KEY_XF86TouchpadToggle:
        return QStringLiteral("▣");

    case XKB_KEY_XF86TouchpadOn:
        return QStringLiteral("▣ ON");

    case XKB_KEY_XF86TouchpadOff:
        return QStringLiteral("▣ OFF");

    case XKB_KEY_XF86CameraFocus:
        return QStringLiteral("◎");

    case XKB_KEY_XF86CameraZoomIn:
        return QStringLiteral("⌕+");

    case XKB_KEY_XF86CameraZoomOut:
        return QStringLiteral("⌕−");

    case XKB_KEY_XF86CameraAccessEnable:
        return QStringLiteral("●");

    case XKB_KEY_XF86CameraAccessDisable:
        return QStringLiteral("⊘");

    case XKB_KEY_XF86CameraAccessToggle:
        return QStringLiteral("◉");

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
    {"BTN_WHEEL_UP", "Mouse Wheel Up"},
    {"BTN_WHEEL_DOWN", "Mouse Wheel Down"},
    {"BTN_WHEEL_LEFT", "Mouse Wheel Left"},
    {"BTN_WHEEL_RIGHT", "Mouse Wheel Right"},
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
        {"Mouse Wheel Up", "◎↑"},
        {"Mouse Wheel Down", "◎↓"},
        {"Mouse Wheel Left", "◎←"},
        {"Mouse Wheel Right", "◎→"},
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

        self->resetComposeState();

        QByteArray locale =
            qEnvironmentVariable("LC_ALL").toLocal8Bit();

        if (locale.isEmpty())
            locale =
                qEnvironmentVariable("LC_CTYPE").toLocal8Bit();

        if (locale.isEmpty())
            locale =
                qEnvironmentVariable("LANG").toLocal8Bit();

        if (locale.isEmpty())
            locale =
                QByteArrayLiteral("C");

        self->xkbComposeTable =
            xkb_compose_table_new_from_locale(
                context,
                locale.constData(),
                XKB_COMPOSE_COMPILE_NO_FLAGS
            );

        if (!self->xkbComposeTable) {

            qWarning()
                << "KDE KeyCast: no se pudo crear la tabla Compose para"
                << locale;

        } else {

            self->xkbComposeState =
                xkb_compose_state_new(
                    self->xkbComposeTable,
                    XKB_COMPOSE_STATE_NO_FLAGS
                );

            if (!self->xkbComposeState) {

                xkb_compose_table_unref(
                    self->xkbComposeTable
                );

                self->xkbComposeTable = nullptr;

                qWarning()
                    << "KDE KeyCast: no se pudo crear el estado Compose";
            }
        }

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

    bool handleComposeKey(
        int linuxKeyCode,
        const QString &visualName
    )
    {
        if (!xkbComposeState ||
            visualName.isEmpty()) {

            return false;
        }

        if (!xkbState)
            return false;

        const xkb_keycode_t keycode =
            xkbKeycodeFromLinuxCode(
                linuxKeyCode
            );

        if (keycode == XKB_KEYCODE_INVALID)
            return false;

        const xkb_keysym_t keysym =
            xkb_state_key_get_one_sym(
                xkbState,
                keycode
            );

        if (keysym == XKB_KEY_NoSymbol)
            return false;

        const xkb_compose_feed_result result =
            xkb_compose_state_feed(
                xkbComposeState,
                keysym
            );

        if (result ==
            XKB_COMPOSE_FEED_IGNORED) {

            return false;
        }

        const xkb_compose_status status =
            xkb_compose_state_get_status(
                xkbComposeState
            );

        /*
         * -----------------------------------------------------------
         * COMPOSING
         * -----------------------------------------------------------
         */

        if (status ==
            XKB_COMPOSE_COMPOSING) {

            composeSequence.append(
                visualName
            );
            composeKeyLegends.append(
                xkbSecondaryLegends(
                    linuxKeyCode,
                    visualName
                )
            );

            /*
             * Primera tecla de la composición.
             */
            if (composeSequence.size() == 1) {

                addKeyboardVisualEntry(
                    QStringList{
                        visualName
                    },
                    false,
                    linuxKeyCode
                );

                const int index =
                    visualQueue.size() - 1;

                if (index >= 0) {

                    visualQueue[index].composePending =
                        true;

                    visualQueue[index].expires =
                        std::numeric_limits<qint64>::max();
                }

            /*
             * Si Compose necesita más teclas,
             * no añadimos otra tarjeta todavía.
             */
            }

            update();

            return true;
        }

        /*
         * -----------------------------------------------------------
         * COMPOSED
         * -----------------------------------------------------------
         */

        if (status ==
            XKB_COMPOSE_COMPOSED) {

            composeSequence.append(
                visualName
            );
            composeKeyLegends.append(
                            xkbSecondaryLegends(
                                linuxKeyCode,
                                visualName
                            )
                        );



            const int index =
                findComposePendingEntry();

            /*
             * Ejemplo:
             *
             *     ´ + ´ -> ´
             *
             * La entrada pendiente ya representa ´.
             * addVisualEntry() se encargará de convertirla
             * en ´ ×2.
             */
            if (composeSequence.size() == 2 &&
                composeSequence.at(0) ==
                    composeSequence.at(1)) {

                if (index >= 0)
                    visualQueue[index].composePending =
                        false;

                resetComposeSequence();
                composeKeyLegends.clear();

                addKeyboardVisualEntry(
                    QStringList{
                        visualName
                    },
                    false,
                    linuxKeyCode
                );

                update();

                return true;
            }

            /*
             * Composición real:
             *
             *     ´ + a -> á
             *
             * Visualmente queremos conservar la secuencia.
             */
            if (index >= 0) {

                VisualEntry &entry =
                    visualQueue[index];

                entry.keys =
                    composeSequence;

                entry.keySecondaryLegends.clear();

                for (const QStringList &legends :
                     composeKeyLegends) {

                    entry.keySecondaryLegends.append(
                        legends.join(
                            QStringLiteral("\n")
                        )
                    );
                }


                entry.combination = true;
                entry.repeatCount = 1;
                entry.linuxKeyCode = -1;
                entry.composePending = false;

                entry.created =
                    QDateTime::currentMSecsSinceEpoch();

                entry.expires =
                    entry.created + displayDurationMs;
            }

            resetComposeSequence();

            update();

            return true;
        }

        /*
         * -----------------------------------------------------------
         * CANCELLED
         * -----------------------------------------------------------
         */

        if (status ==
            XKB_COMPOSE_CANCELLED) {

            const int index =
                findComposePendingEntry();

            /*
             * La dead key deja de ser "pendiente".
             * Se conserva como pulsación normal.
             */
            if (index >= 0) {

                VisualEntry &entry =
                    visualQueue[index];

                entry.composePending = false;

                entry.expires =
                    QDateTime::currentMSecsSinceEpoch()
                    + displayDurationMs;
            }

            resetComposeSequence();

            /*
             * La tecla que canceló la composición
             * vuelve al flujo normal.
             */
            return false;
        }

        return false;
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

        self->update();
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


    void handleWaylandPointerEvent(
        const KDEKeyCast::InputEvent &event)
    {
        if (!event.isMouse())
            return;

        if (event.action ==
            KDEKeyCast::InputAction::Wheel) {

            if (event.text.isEmpty())
                return;

            /*
             * Reutilizamos el mismo camino visual que ya utiliza
             * el proyecto para los eventos de ratón.
             *
             * No creamos un renderer diferente para la rueda:
             * teclado, botones y rueda terminan en VisualEntry.
             */
            addMouseButton(event.text);
            return;
        }

        /*
         * Los botones físicos se incorporarán al mismo pipeline.
         * De momento sólo mostramos pulsaciones.
         */
        if (event.action ==
            KDEKeyCast::InputAction::Pressed) {

            if (!event.keyName.isEmpty())
                addMouseButton(event.keyName);
        }
    }

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
            qWarning()
                << "Failed to obtain Wayland keyboard";
            return;
        }

        if (!waylandPointer.start(
                [this](const QString &keyName, bool pressed) {
                    if (pressed)
                        addMouseButton(keyName);
                })) {
            qWarning()
                << "Failed to initialise Wayland pointer";
        }

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

    QStringList xkbSecondaryLegends(
        int linuxCode,
        const QString &primary
    ) const
    {
        QStringList result;

        /*
         * En modo compacto las teclas especiales ya utilizan
         * su símbolo como leyenda principal.
         *
         * No debemos intentar obtener niveles XKB adicionales
         * para ellas porque teclas como Pause pueden devolver
         * nombres internos como Pause / Break / Sys_Req.
         */
        if (compactMode &&
            (primary == QStringLiteral("Ⅱ") ||
             primary == QStringLiteral("⎙") ||
             primary == QStringLiteral("⇥"))) {

            return result;
        }

        if (!xkbState ||
            !xkbKeymap ||
            linuxCode < 0) {

            return result;
        }

        const xkb_keycode_t code =
            xkbKeycodeFromLinuxCode(linuxCode);

        if (code == XKB_KEYCODE_INVALID)
            return result;

        /*
         * -----------------------------------------------------------
         * Teclas especiales con serigrafía física fija
         * -----------------------------------------------------------
         *
         * Print y Pause tienen varias entradas XKB asociadas
         * dependiendo del layout/implementación. Para el overlay
         * queremos representar únicamente la serigrafía física de
         * la tecla, no nombres internos como Sys_Req, Break, etc.
         */
        /*
         * Estas teclas tienen símbolos físicos conocidos.
         *
         * En modo compacto el símbolo ya es la leyenda principal,
         * por lo que NO debemos devolverlo también como leyenda
         * secundaria.
         *
         * En modo normal sí se conserva como serigrafía.
         */
        if (primary == QStringLiteral("PrtSc") ||
            primary == QStringLiteral("Print")) {

            if (!compactMode)
                result.append(QStringLiteral("⎙"));

            return result;
        }

        if (primary == QStringLiteral("Pause")) {

            if (!compactMode)
                result.append(QStringLiteral("Ⅱ"));

            return result;
        }

        /*
         * Tab puede tener keysyms auxiliares ISO_* en algunos keymaps.
         * No son serigrafía física de la tecla.
         */
        if (primary == QStringLiteral("Tab")) {

            if (!compactMode)
                result.append(QStringLiteral("⇥"));

            return result;
        }

        /*
         * Determinamos el layout actualmente activo.
         */
        const xkb_layout_index_t layout =
            xkb_state_key_get_layout(
                xkbState,
                code
            );

        if (layout == XKB_LAYOUT_INVALID)
            return result;

        const xkb_level_index_t levels =
            xkb_keymap_num_levels_for_key(
                xkbKeymap,
                code,
                layout
            );

        /*
         * No mostramos una cantidad absurda de leyendas.
         * Un teclado convencional normalmente tiene 2-4 niveles.
         */
        const xkb_level_index_t maxLevels =
            qMin<xkb_level_index_t>(
                levels,
                4
            );

        for (xkb_level_index_t level = 0;
             level < maxLevels;
             ++level) {

            const xkb_keysym_t *syms = nullptr;

            const int count =
                xkb_keymap_key_get_syms_by_level(
                    xkbKeymap,
                    code,
                    layout,
                    level,
                    &syms
                );

            if (count <= 0 || !syms)
                continue;

            for (int i = 0; i < count; ++i) {

                /*
                 * Las dead keys son estados de composición,
                 * no leyendas físicas de la keycap.
                 */
                char symName[128] = {};

                xkb_keysym_get_name(
                    syms[i],
                    symName,
                    sizeof(symName)
                );

                if (QString::fromLatin1(symName)
                        .startsWith(QStringLiteral("dead_"))) {
                    continue;
                }

                const QString text =
                    keysymToText(syms[i]);

                if (text.isEmpty())
                    continue;

                /*
                 * Evitamos duplicar el carácter principal.
                 */
                if (text == primary)
                    continue;

                if (result.contains(text))
                    continue;

                result.append(text);
            }
        }

        /*
         * Las teclas especiales solo llevan serigrafía adicional
         * en modo normal.
         *
         * Los caracteres normales sí conservan sus leyendas en
         * compacto.
         */
        bool characterKey = false;

        const bool compactSpecialKey =
            compactMode &&
            (primary == QStringLiteral("␣") ||
             primary == QStringLiteral("↵") ||
             primary == QStringLiteral("⌫") ||
             primary == QStringLiteral("⌦") ||
             primary == QStringLiteral("⇥") ||
             primary == QStringLiteral("⇪") ||
             primary == QStringLiteral("⇧") ||
             primary == QStringLiteral("⌃") ||
             primary == QStringLiteral("⌥") ||
             primary == QStringLiteral("⎇") ||
             primary == QStringLiteral("⇱") ||
             primary == QStringLiteral("⇲") ||
             primary == QStringLiteral("⇞") ||
             primary == QStringLiteral("⇟") ||
             primary == QStringLiteral("⌘"));

        if (!compactSpecialKey &&
            primary.size() == 1) {

            const QChar ch =
                primary.at(0);

            characterKey =
                ch.isLetterOrNumber() ||
                ch.isPunct() ||
                ch.isSymbol();
        }

        if (!characterKey && compactMode)
            result.clear();

        /*
         * -----------------------------------------------------------
         * Serigrafía de teclas especiales
         * -----------------------------------------------------------
         *
         * En modo normal:
         *
         *     Space       <- principal
         *          ␣      <- serigrafía
         *
         * En compacto:
         *
         *     ␣           <- principal
         *
         * De esta forma ambos modos tienen una identidad visual
         * claramente diferenciada.
         */

        if (!characterKey && !compactMode) {

            const QString symbol =
                compactDisplayName(primary);

            if (symbol != primary) {

                const QStringList symbolicKeys = {
                    QStringLiteral("⌃"),
                    QStringLiteral("⌥"),
                    QStringLiteral("⎇"),
                    QStringLiteral("⇧"),
                    QStringLiteral("⌘"),
                    QStringLiteral("↵"),
                    QStringLiteral("⌫"),
                    QStringLiteral("⇥"),
                    QStringLiteral("␣"),
                    QStringLiteral("⇪"),
                    QStringLiteral("⌦")
                };

                if (symbolicKeys.contains(symbol))
                    result.prepend(symbol);
            }
        }

        /*
         * La serigrafía física debe ser discreta.
         *
         * Como máximo mostramos dos leyendas secundarias.
         */
        while (result.size() > 2)
            result.removeLast();

        return result;
    }

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

    void resetComposeState()
    {
        if (xkbComposeState) {

            xkb_compose_state_unref(
                xkbComposeState
            );

            xkbComposeState = nullptr;
        }

        if (xkbComposeTable) {

            xkb_compose_table_unref(
                xkbComposeTable
            );

            xkbComposeTable = nullptr;
        }

        composeSequence.clear();
    }

    void resetComposeSequence()
    {
        if (xkbComposeState)
            xkb_compose_state_reset(
                xkbComposeState
            );

        composeSequence.clear();
        composeKeyLegends.clear();
    }

    int findComposePendingEntry() const
    {
        for (int i = visualQueue.size() - 1;
             i >= 0;
             --i) {

            if (visualQueue[i].composePending)
                return i;
        }

        return -1;
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

    void trimVisualQueue()
    {
        const qint64 now =
            QDateTime::currentMSecsSinceEpoch();

        while (!visualQueue.isEmpty() &&
               visualQueue.first().expires <= now) {

            visualQueue.removeFirst();
        }
    }


    void addVisualEntry(
    const QStringList &keys,
    bool combination,
    int linuxKeyCode = -1
)
{


    if (pauseInput)
            return;

    if (keys.isEmpty())
        return;

    if (onlyShortcuts && !combination)
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
            last.expires = now + displayDurationMs;

            update();

            return;
        }
    }

    VisualEntry entry;

    entry.keys = keys;
    entry.created = now;
    entry.expires = now + displayDurationMs;
    entry.combination = combination;
    entry.repeatCount = 1;
    entry.linuxKeyCode =
        keys.size() == 1 && !combination
            ? linuxKeyCode
            : -1;

    visualQueue.append(entry);

    while (visualQueue.size() > 6)
        visualQueue.removeFirst();

    update();
}

void addKeyboardVisualEntry(
    const QStringList &keys,
    bool combination,
    int linuxKeyCode = -1
)
{
    addVisualEntry(
        keys,
        combination,
        linuxKeyCode
    );

    if (!visualQueue.isEmpty()) {
        visualQueue.last().keyboardEntry = true;
        visualQueue.last().maskKeyboard = hideKeys;
        update();
    }
}

    void addMouseButton(
        const QString &keyName
    )
    {
        const QString visualName =
            displayName(keyName);

        qDebug()
            << "KDE KeyCast addMouseButton:"
            << "input=" << keyName
            << "display=" << visualName;

        addVisualEntry(
            QStringList{
                visualName
            },
            false
        );
    }

    void addMouseWheel(
        const QString &axisName,
        double axisValue
    )
    {

        if (axisName.isEmpty() ||
            qFuzzyIsNull(axisValue)) {
            return;
        }

        const bool shiftPressed =
            pressedModifiers.contains(
                QStringLiteral("KEY_LEFTSHIFT")
            ) ||
            pressedModifiers.contains(
                QStringLiteral("KEY_RIGHTSHIFT")
            );

        QString label;

        /*
         * -----------------------------------------------------------
         * Dirección visual de la rueda
         * -----------------------------------------------------------
         */

        if (axisName ==
            QStringLiteral("VERTICAL_SCROLL")) {

            if (shiftPressed) {

                label =
                    axisValue < 0.0
                        ? (compactMode
                            ? QStringLiteral("◎←")
                            : QStringLiteral("Wheel ←"))
                        : (compactMode
                            ? QStringLiteral("◎→")
                            : QStringLiteral("Wheel →"));

            } else {

                label =
                    axisValue < 0.0
                        ? (compactMode
                            ? QStringLiteral("◎↑")
                            : QStringLiteral("Wheel ↑"))
                        : (compactMode
                            ? QStringLiteral("◎↓")
                            : QStringLiteral("Wheel ↓"));
            }

        } else if (
            axisName ==
            QStringLiteral("HORIZONTAL_SCROLL")) {

            label =
                axisValue < 0.0
                    ? (compactMode
                        ? QStringLiteral("◎←")
                        : QStringLiteral("Wheel ←"))
                    : (compactMode
                        ? QStringLiteral("◎→")
                        : QStringLiteral("Wheel →"));

        } else {
            return;
        }

        qDebug()
            << "KDE KeyCast wheel:"
            << axisName
            << axisValue
            << label
            << "shift="
            << shiftPressed;

        if (pauseInput)
            return;
        /*
         * -----------------------------------------------------------
         * SHIFT + WHEEL
         *
         * Una única entrada de combinación:
         *
         *     [ Shift ] + [ Wheel ← ×3 ]
         *
         * El pintado de combinaciones ya proporciona la serigrafía
         * ⇧ de Shift.
         * -----------------------------------------------------------
         */

        if (shiftPressed) {

            const QStringList keys{
                QStringLiteral("Shift"),
                label
            };

            /*
             * Buscar la combinación Shift + Wheel que ya está visible.
             */
            for (int i = visualQueue.size() - 1;
                 i >= 0;
                 --i) {

                VisualEntry &entry =
                    visualQueue[i];

                if (!entry.combination ||
                    entry.keys.size() != 2 ||
                    entry.keys.first() !=
                        QStringLiteral("Shift")) {

                    continue;
                }

                const QString oldWheel =
                    entry.keys.at(1);

                const bool oldIsWheel =
                    oldWheel.startsWith(
                        QStringLiteral("Wheel ")
                    ) ||
                    oldWheel.startsWith(
                        QStringLiteral("◎")
                    );

                if (!oldIsWheel)
                    continue;

                /*
                 * MISMA DIRECCIÓN:
                 *
                 * solamente incrementamos ×2, ×3...
                 */
                if (oldWheel == label) {

                    entry.repeatCount++;

                    entry.expires =
                        QDateTime::currentMSecsSinceEpoch()
                        + displayDurationMs;

                    update();

                    return;
                }

                /*
                 * DIRECCIÓN DISTINTA:
                 *
                 * sustituimos únicamente la rueda.
                 *
                 * Shift permanece en la misma entrada.
                 */
                entry.keys[1] = label;

                entry.repeatCount = 1;

                entry.created =
                    QDateTime::currentMSecsSinceEpoch();

                entry.expires =
                    entry.created + displayDurationMs;

                update();

                return;
            }

            /*
             * Primera rueda con Shift.
             */
            VisualEntry entry;

            entry.keys = keys;
            entry.created =
                QDateTime::currentMSecsSinceEpoch();

            entry.expires =
                entry.created + displayDurationMs;

            entry.combination = true;
            entry.repeatCount = 1;
            entry.linuxKeyCode = -1;

            visualQueue.append(entry);

            while (visualQueue.size() > 6)
                visualQueue.removeFirst();

            update();

            return;
        }

        /*
         * -----------------------------------------------------------
         * RUEDA NORMAL
         * -----------------------------------------------------------
         */

        addVisualEntry(
            QStringList{label},
            false
        );

        qDebug()
            << "KDE KeyCast wheel:"
            << axisName
            << axisValue
            << label
            << "shift="
            << shiftPressed;
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

        addKeyboardVisualEntry(
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

            /*
             * -------------------------------------------------------
             * MOUSE WHEEL
             * -------------------------------------------------------
             *
             * POINTER_AXIS no tiene key_name ni key_code.
             * Por eso debe procesarse antes de los filtros
             * específicos del teclado/botones.
             */

            if (eventName ==
                QStringLiteral("POINTER_AXIS")) {

                    const QString axisName =
                        obj.value(
                            QStringLiteral("axis_name")
                        ).toString();

                    const double axisValue =
                        obj.value(
                            QStringLiteral("axis_value")
                        ).toDouble();

                    addMouseWheel(
                        axisName,
                        axisValue
                    );

                continue;
            }

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
                    keyName == QStringLiteral("KEY_M") &&
                    pressedModifiers.contains(
                        QStringLiteral("KEY_LEFTCTRL")
                    ) &&
                    pressedModifiers.contains(
                        QStringLiteral("KEY_LEFTSHIFT")
                    )) {

                    toggleCompactMode();

                }

            if (pressed &&
                keyName == QStringLiteral("KEY_H") &&
                pressedModifiers.contains(
                    QStringLiteral("KEY_LEFTCTRL")
                ) &&
                pressedModifiers.contains(
                    QStringLiteral("KEY_LEFTSHIFT")
                )) {

                toggleHideKeys();
            }

            if (pressed &&
                keyName == QStringLiteral("KEY_P") &&
                pressedModifiers.contains(QStringLiteral("KEY_LEFTCTRL")) &&
                pressedModifiers.contains(QStringLiteral("KEY_LEFTSHIFT"))) {

                togglePauseInput();
                continue;
            }

            if (pressed &&
                keyName == QStringLiteral("KEY_C") &&
                pressedModifiers.contains(
                    QStringLiteral("KEY_LEFTCTRL")
                ) &&
                pressedModifiers.contains(
                    QStringLiteral("KEY_LEFTSHIFT")
                )) {

                toggleClickThrough();
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

    QString visualName =
        name.isEmpty()
            ? displayName(keyName)
            : name;

    /*
     * -----------------------------------------------------------
     * XKB COMPOSE / DEAD KEYS
     * -----------------------------------------------------------
     */

    if (!modifier &&
        !hasNonShiftModifier()) {

        if (handleComposeKey(
                linuxKeyCode,
                visualName
            )) {

            continue;
        }
    }
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

        addKeyboardVisualEntry(
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
     * TECLAS CON ESTADO REAL
     * -----------------------------------------------------------
     *
     * La tarjeta se crea con el estado actual. El timer de
     * updateMediaState() la seguirá sincronizando mientras esté
     * visible.
     */

    if (linuxKeyCode == 113 ||
        linuxKeyCode == 114 ||
        linuxKeyCode == 115) {

        updateMediaState();

        addVisualEntry(
            QStringList{
                realAudioMuted
                    ? QStringLiteral("🔇 %1%")
                          .arg(realAudioVolumePercent)
                    : QStringLiteral("🔊 %1%")
                          .arg(realAudioVolumePercent)
            },
            false,
            linuxKeyCode
        );

        continue;
    }

    if (linuxKeyCode == 248) {

        updateMediaState();

        addVisualEntry(
            QStringList{
                realMicMuted
                    ? QStringLiteral("🔇 %1%")
                          .arg(realMicVolumePercent)
                    : QStringLiteral("🎙 %1%")
                          .arg(realMicVolumePercent)
            },
            false,
            linuxKeyCode
        );

        continue;
    }

    if (linuxKeyCode == 164 ||
        linuxKeyCode == 207) {

        updateMediaState();

        addVisualEntry(
            QStringList{
                mprisPlaybackStatus ==
                    QStringLiteral("Playing")
                    ? QStringLiteral("Ⅱ")
                    : QStringLiteral("▶")
            },
            false,
            linuxKeyCode
        );

        continue;
    }

    if (linuxKeyCode == 439) {

        updateMediaState();

        addVisualEntry(
            QStringList{
                mprisLoopStatus ==
                    QStringLiteral("Playlist")
                    ? QStringLiteral("↻∞")
                    : QStringLiteral("↻")
            },
            false,
            linuxKeyCode
        );

        continue;
    }

    if (linuxKeyCode == 410) {

        updateMediaState();

        addVisualEntry(
            QStringList{
                mprisShuffle
                    ? QStringLiteral("🔀")
                    : QStringLiteral("⇄")
            },
            false,
            linuxKeyCode
        );

        continue;
    }

    if (linuxKeyCode == 224 ||
        linuxKeyCode == 225 ||
        linuxKeyCode == 243 ||
        linuxKeyCode == 592 ||
        linuxKeyCode == 593) {

        updateMediaState();

        addVisualEntry(
            QStringList{
                QStringLiteral("☀ %1%")
                    .arg(brightnessPercent)
            },
            false,
            linuxKeyCode
        );

        continue;
    }

    if (linuxKeyCode == 238) {

        updateMediaState();

        addVisualEntry(
            QStringList{
                wifiEnabled &&
                wifiHardwareEnabled
                    ? QStringLiteral("📶")
                    : QStringLiteral("⊘")
            },
            false,
            linuxKeyCode
        );

        continue;
    }

    if (linuxKeyCode == 246) {

        updateMediaState();

        addVisualEntry(
            QStringList{
                wwanEnabled &&
                wwanHardwareEnabled
                    ? QStringLiteral("📡")
                    : QStringLiteral("⊘")
            },
            false,
            linuxKeyCode
        );

        continue;
    }

    if (linuxKeyCode == 237) {

        updateMediaState();

        addVisualEntry(
            QStringList{
                bluetoothPowered
                    ? QStringLiteral("ᛒ")
                    : QStringLiteral("⊘")
            },
            false,
            linuxKeyCode
        );

        continue;
    }

    if (linuxKeyCode == 247) {

        updateMediaState();

        addVisualEntry(
            QStringList{
                allRadiosEnabled
                    ? QStringLiteral("📡")
                    : QStringLiteral("⊘")
            },
            false,
            linuxKeyCode
        );

        continue;
    }

    if (linuxKeyCode == 530 ||
        linuxKeyCode == 531 ||
        linuxKeyCode == 532) {

        updateMediaState();

        addVisualEntry(
            QStringList{
                touchpadEnabled
                    ? QStringLiteral("⌁")
                    : QStringLiteral("⊘")
            },
            false,
            linuxKeyCode
        );

        continue;
    }

    /*
     * -----------------------------------------------------------
     * TECLA NORMAL SIN MODIFICADORES
     * -----------------------------------------------------------
     */

    addKeyboardVisualEntry(
        QStringList{
            visualName
        },
        false,
        linuxKeyCode
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

        if (keyName == QStringLiteral("KEY_LEFTSHIFT") ||
            keyName == QStringLiteral("KEY_RIGHTSHIFT")) {

            for (int i = visualQueue.size() - 1;
                 i >= 0;
                 --i) {

                const VisualEntry &entry =
                    visualQueue[i];

                if (!entry.combination ||
                    entry.keys.size() != 2 ||
                    entry.keys.at(0) !=
                        QStringLiteral("Shift")) {
                    continue;
                }

                const QString wheel =
                    entry.keys.at(1);

                if (wheel.startsWith(
                        QStringLiteral("Wheel ")
                    ) ||
                    wheel.startsWith(
                        QStringLiteral("◎"))) {

                    visualQueue.remove(i);
                    break;
                }
            }

            update();
        }

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

    void addDisplayDurationAction(QMenu &menu)
           {
               auto *action =
                   new QWidgetAction(&menu);

               auto *container =
                   new QWidget(&menu);

               auto *layout =
                   new QHBoxLayout(container);

               layout->setContentsMargins(
                   10,
                   2,
                   10,
                   2
               );

               layout->setSpacing(8);

               auto *label =
                   new QLabel(
                       QStringLiteral("Display duration"),
                       container
                   );

               auto *slider =
                   new QSlider(
                       Qt::Horizontal,
                       container
                   );

               auto *valueLabel =
                   new QLabel(container);

               /*
                * 5..50 = 0.5..5.0 segundos
                */
               slider->setRange(
                   5,
                   50
               );

               slider->setValue(
                   static_cast<int>(
                       displayDurationMs / 100
                   )
               );

               slider->setFixedWidth(92);

               valueLabel->setFixedWidth(34);
               valueLabel->setAlignment(
                   Qt::AlignRight | Qt::AlignVCenter
               );

               auto updateValue =
                   [slider, valueLabel]() {

                       const double seconds =
                           slider->value() / 10.0;

                       valueLabel->setText(
                           QStringLiteral("%1 s")
                               .arg(
                                   seconds,
                                   0,
                                   'f',
                                   1
                               )
                       );
                   };

               updateValue();

               /*
                * Mismo lenguaje visual que nuestros switches.
                */
               const QColor track =
                   lightMode
                       ? QColor(205, 208, 213)
                       : QColor(68, 71, 78);

               const QColor active =
                   lightMode
                       ? QColor(70, 72, 78)
                       : QColor(220, 222, 226);

               const QColor handle =
                   lightMode
                       ? QColor(45, 47, 52)
                       : QColor(245, 246, 248);

               slider->setStyleSheet(
                   QString(
                       "QSlider::groove:horizontal {"
                       " height: 4px;"
                       " background: %1;"
                       " border-radius: 2px;"
                       "}"
                       "QSlider::sub-page:horizontal {"
                       " background: %2;"
                       " border-radius: 2px;"
                       "}"
                       "QSlider::add-page:horizontal {"
                       " background: %1;"
                       " border-radius: 2px;"
                       "}"
                       "QSlider::handle:horizontal {"
                       " width: 12px;"
                       " height: 12px;"
                       " margin: -4px 0;"
                       " border-radius: 6px;"
                       " background: %3;"
                       "}"
                   ).arg(
                       track.name(),
                       active.name(),
                       handle.name()
                   )
               );

               connect(
                   slider,
                   &QSlider::valueChanged,
                   this,
                   [this, updateValue](int value) {

                       displayDurationMs =
                           static_cast<qint64>(value) *
                           100;

                       updateValue();

                       QSettings settings(
                           QStringLiteral("KDE"),
                           QStringLiteral("KDEKeyCast")
                       );

                       settings.setValue(
                           QStringLiteral("displayDurationMs"),
                           displayDurationMs
                       );
                   }
               );

               layout->addWidget(label);
               layout->addWidget(slider);
               layout->addWidget(valueLabel);

               action->setDefaultWidget(container);

               menu.addAction(action);
           }

    void toggleAlwaysOnTop()
    {
        QDBusMessage message =
            QDBusMessage::createMethodCall(
                QStringLiteral("org.kde.kglobalaccel"),
                QStringLiteral("/component/kwin"),
                QStringLiteral(
                    "org.kde.kglobalaccel.Component"
                ),
                QStringLiteral("invokeShortcut")
            );

        message <<
            QStringLiteral(
                "kde-keycast-toggle-keep-above"
            );

        QDBusConnection::sessionBus().asyncCall(
            message
        );
    }

    void toggleCompactMode()
    {
        compactMode = !compactMode;

        const int newWidth =
            compactMode ? 300 : 400;

        const QRect oldGeometry =
            geometry();

        const int centerX =
            oldGeometry.left() +
            oldGeometry.width() / 2;

        setFixedWidth(newWidth);

        move(
            centerX - newWidth / 2,
            oldGeometry.top()
        );

        qDebug()
            << "KDE KeyCast: compact mode ="
            << compactMode
            << "width =" << newWidth;

        update();
    }

    void toggleLightMode()
    {
        lightMode = !lightMode;

        qDebug()
            << "KDE KeyCast: light mode ="
            << lightMode;

        update();
    }

    void toggleHideKeys()
    {
        hideKeys = !hideKeys;

        qDebug()
            << "KDE KeyCast: hide keys ="
            << hideKeys;

        update();
    }

    void togglePauseInput()
    {
        pauseInput = !pauseInput;

        qDebug()
            << "KDE KeyCast: pause input ="
            << pauseInput;

        update();
    }

    void toggleClickThrough()
    {
        clickThrough = !clickThrough;

        if (windowHandle()) {
            windowHandle()->setFlag(
                Qt::WindowTransparentForInput,
                clickThrough
            );
        }

        qDebug()
            << "KDE KeyCast: click-through ="
            << clickThrough;

        update();
    }

    void toggleOnlyShortcuts()
    {
        onlyShortcuts = !onlyShortcuts;

        /*
         * Al activar el modo eliminamos inmediatamente
         * las entradas individuales que ya estén visibles.
         *
         * Las combinaciones se conservan.
         */
        if (onlyShortcuts) {

            for (int i = visualQueue.size() - 1;
                 i >= 0;
                 --i) {

                if (!visualQueue[i].combination)
                    visualQueue.remove(i);
            }
        }

        QSettings settings(
            QStringLiteral("KDE"),
            QStringLiteral("KDEKeyCast")
        );

        settings.setValue(
            QStringLiteral("onlyShortcuts"),
            onlyShortcuts
        );

        update();
    }

    void showShortcutHelp()
    {
        QDialog dialog(this);

        dialog.setWindowTitle(
            QStringLiteral("Keyboard shortcuts")
        );

        dialog.setModal(true);
        dialog.setFixedWidth(360);

        auto *layout =
            new QVBoxLayout(&dialog);

        layout->setContentsMargins(
            20,
            18,
            20,
            18
        );

        layout->setSpacing(8);

        auto addShortcut =
            [&](const QString &shortcut,
                const QString &description)
        {
            auto *row =
                new QHBoxLayout();

            row->setSpacing(16);

            auto *keyLabel =
                new QLabel(shortcut, &dialog);

            auto *descriptionLabel =
                new QLabel(description, &dialog);

            keyLabel->setMinimumWidth(110);
            keyLabel->setAlignment(
                Qt::AlignLeft |
                Qt::AlignVCenter
            );

            keyLabel->setStyleSheet(
                QStringLiteral(
                    "font-weight: 600;"
                )
            );

            descriptionLabel->setAlignment(
                Qt::AlignLeft |
                Qt::AlignVCenter
            );

            row->addWidget(keyLabel);
            row->addWidget(descriptionLabel, 1);

            layout->addLayout(row);
        };

        addShortcut(
            QStringLiteral("Ctrl + Shift + M"),
            QStringLiteral("Compact mode")
        );

        addShortcut(
            QStringLiteral("Ctrl + Shift + T"),
            QStringLiteral("Always on top")
        );

        addShortcut(
            QStringLiteral("Ctrl + Shift + H"),
            QStringLiteral("Hide keys")
        );

        addShortcut(
            QStringLiteral("Ctrl + Shift + C"),
            QStringLiteral("Click-through")
        );

        addShortcut(
            QStringLiteral("Ctrl + Shift + P"),
            QStringLiteral("Pause input")
        );

        layout->addSpacing(8);

        auto *closeButton =
            new QPushButton(
                QStringLiteral("Close"),
                &dialog
            );

        closeButton->setDefault(true);

        connect(
            closeButton,
            &QPushButton::clicked,
            &dialog,
            &QDialog::accept
        );

        layout->addWidget(
            closeButton,
            0,
            Qt::AlignRight
        );

        dialog.exec();
    }

    void showAbout()
    {
        QDialog dialog(this);

        dialog.setWindowTitle(
            QStringLiteral("About KDE KeyCast")
        );

        dialog.setModal(true);
        dialog.setFixedWidth(380);

        auto *layout =
            new QVBoxLayout(&dialog);

        layout->setContentsMargins(
            24,
            22,
            24,
            20
        );

        layout->setSpacing(10);

        auto *title =
            new QLabel(
                QStringLiteral("KDE KeyCast"),
                &dialog
            );

        title->setAlignment(
            Qt::AlignCenter
        );

        title->setStyleSheet(
            QStringLiteral(
                "font-size: 18px;"
                "font-weight: 600;"
            )
        );

        layout->addWidget(title);

        auto *description =
            new QLabel(
                QStringLiteral(
                    "Keyboard and mouse visualizer "
                    "for KDE Plasma on Wayland."
                ),
                &dialog
            );

        description->setAlignment(
            Qt::AlignCenter
        );

        description->setWordWrap(true);

        layout->addWidget(description);

        layout->addSpacing(6);

        auto *details =
            new QLabel(
                QStringLiteral(
                    "Built with Qt 6 and libxkbcommon."
                ),
                &dialog
            );

        details->setAlignment(
            Qt::AlignCenter
        );

        details->setStyleSheet(
            QStringLiteral(
                "color: palette(mid);"
            )
        );

        layout->addWidget(details);

        layout->addSpacing(8);

        auto *closeButton =
            new QPushButton(
                QStringLiteral("Close"),
                &dialog
            );

        closeButton->setDefault(true);

        connect(
            closeButton,
            &QPushButton::clicked,
            &dialog,
            &QDialog::accept
        );

        layout->addWidget(
            closeButton,
            0,
            Qt::AlignRight
        );

        dialog.exec();
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

    WaylandPointer waylandPointer;

    QSocketNotifier *waylandNotifier = nullptr;

    xkb_keymap *xkbKeymap = nullptr;
    xkb_state *xkbState = nullptr;
    xkb_compose_table *xkbComposeTable = nullptr;
    xkb_compose_state *xkbComposeState = nullptr;

    QStringList composeSequence;
    QVector<QStringList> composeKeyLegends;

    QProcess process;
    QTimer timer;

    QVector<VisualEntry> visualQueue;

    bool combinationActive = false;
    bool shiftVisualActive = false;
    QStringList currentCombination;
    QString currentCombinationKey;

    QSet<QString> pressedModifiers;

    bool shiftWheelVisualActive = false;

    bool compactMode = false;
    bool alwaysOnTop = true;
    bool lightMode = false;
    bool hideKeys = false;
    bool pauseInput = false;
    bool clickThrough = false;
    qint64 displayDurationMs = 1800;
    bool onlyShortcuts = false;

};

class SwitchButton : public QAbstractButton
{
public:
    explicit SwitchButton(QWidget *parent = nullptr)
        : QAbstractButton(parent)
    {
        setCheckable(true);
        setCursor(Qt::PointingHandCursor);
        setFixedSize(38, 22);
        setFocusPolicy(Qt::NoFocus);
    }

protected:
    void paintEvent(QPaintEvent *) override
    {
        QPainter p(this);

        p.setRenderHint(
            QPainter::Antialiasing,
            true
        );

        /*
         * -------------------------------------------------------
         * Track
         * -------------------------------------------------------
         */
        QColor trackColor =
            isChecked()
                ? QColor(61, 174, 233)
                : QColor(110, 114, 121);

        if (underMouse()) {
            trackColor =
                isChecked()
                    ? QColor(79, 186, 237)
                    : QColor(125, 129, 136);
        }

        const QRectF trackRect(
            0.5,
            0.5,
            width() - 1.0,
            height() - 1.0
        );

        p.setPen(Qt::NoPen);
        p.setBrush(trackColor);

        p.drawRoundedRect(
            trackRect,
            height() / 2.0,
            height() / 2.0
        );


        /*
         * -------------------------------------------------------
         * Thumb shadow
         * -------------------------------------------------------
         */
        const qreal thumbSize =
            height() - 4.0;

        const qreal thumbY =
            2.0;

        const qreal thumbX =
            isChecked()
                ? width() - thumbSize - 2.0
                : 2.0;

        const QRectF shadowRect(
            thumbX,
            thumbY + 1.0,
            thumbSize,
            thumbSize
        );

        p.setBrush(
            QColor(
                0,
                0,
                0,
                45
            )
        );

        p.drawEllipse(
            shadowRect
        );


        /*
         * -------------------------------------------------------
         * Thumb
         * -------------------------------------------------------
         */
        const QRectF thumbRect(
            thumbX,
            thumbY,
            thumbSize,
            thumbSize
        );

        p.setBrush(
            QColor(
                255,
                255,
                255,
                255
            )
        );

        p.drawEllipse(
            thumbRect
        );
    }
};

static QWidgetAction *addSwitchAction(
    QMenu &menu,
    const QString &text,
    bool checked,
    const std::function<void(bool)> &onToggled
)
{
    auto *action =
        new QWidgetAction(&menu);

    auto *row =
        new QWidget();

    auto *layout =
        new QHBoxLayout(row);

    layout->setContentsMargins(
        8,
        3,
        8,
        3
    );

    layout->setSpacing(10);


    auto *label =
        new QLabel(text);

    label->setSizePolicy(
        QSizePolicy::Expanding,
        QSizePolicy::Preferred
    );


    auto *switchButton =
        new SwitchButton();

    switchButton->setChecked(
        checked
    );


    layout->addWidget(
        label
    );

    layout->addWidget(
        switchButton
    );


    QObject::connect(
        switchButton,
        &QAbstractButton::toggled,
        &menu,
        onToggled
    );


    action->setDefaultWidget(
        row
    );

    menu.addAction(
        action
    );

    return action;
}

void KeyCast::contextMenuEvent(
    QContextMenuEvent *event
)
{
    QMenu menu(this);

    addSwitchAction(
        menu,
        QStringLiteral("Compact mode"),
        compactMode,
        [this](bool) {
            toggleCompactMode();
        }
    );

    addSwitchAction(
        menu,
        QStringLiteral("Light mode"),
        lightMode,
        [this](bool) {
            toggleLightMode();
        }
    );

    addSwitchAction(
        menu,
        QStringLiteral("Hide keys"),
        hideKeys,
        [this](bool) {
            toggleHideKeys();
        }
    );

    addSwitchAction(
        menu,
        QStringLiteral("Only shortcuts"),
        onlyShortcuts,
        [this](bool) {
            toggleOnlyShortcuts();
        }
    );

    addSwitchAction(
        menu,
        QStringLiteral("Pause input"),
        pauseInput,
        [this](bool) { togglePauseInput(); }
    );

    addSwitchAction(
        menu,
        QStringLiteral("Click-through"),
        clickThrough,
        [this](bool) { toggleClickThrough(); }
    );

    addSwitchAction(
        menu,
        QStringLiteral("Always on top"),
        alwaysOnTop,
        [this](bool) {
            toggleAlwaysOnTop();
        }
    );

    addDisplayDurationAction(menu);

    menu.addSeparator();

    QAction *shortcutsAction =
        menu.addAction(
            QStringLiteral("Keyboard shortcuts")
        );

    connect(
        shortcutsAction,
        &QAction::triggered,
        this,
        [this]() {
            showShortcutHelp();
        }
    );

    QAction *aboutAction =
        menu.addAction(
            QStringLiteral("About KDE KeyCast")
        );

    connect(
        aboutAction,
        &QAction::triggered,
        this,
        [this]() {
            showAbout();
        }
    );

    menu.addSeparator();

    QAction *quitAction =
        menu.addAction(
            QStringLiteral("Quit")
        );

    connect(
        quitAction,
        &QAction::triggered,
        this,
        []() {
            QApplication::quit();
        }
    );

    menu.exec(
        event->globalPos()
    );
}

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
#include "main.moc"
