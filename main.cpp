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
#include <QSettings>
#include <QMoveEvent>
#include <QDir>
#include <QFile>
#include <QRegularExpression>
#include <QtDBus/QtDBus>

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

    // Código Linux de la tecla física cuando la entrada representa
    // una única tecla. Las combinaciones utilizan -1.
    int linuxKeyCode = -1;
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

        const int initialWidth =
            compactMode ? 300 : 400;

        setFixedSize(
            initialWidth,
            106
        );


        if (auto *screen = QGuiApplication::primaryScreen()) {
            const QRect area = screen->availableGeometry();

            move(
                area.left() + (area.width() - width()) / 2,
                area.top() + area.height() - height() - 80
            );
        }

        const QVariant savedX =
            settings.value(QStringLiteral("windowX"));

        const QVariant savedY =
            settings.value(QStringLiteral("windowY"));

        if (savedX.isValid() && savedY.isValid()) {
            move(
                savedX.toInt(),
                savedY.toInt()
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
        QSettings settings(
            QStringLiteral("KDE"),
            QStringLiteral("KDEKeyCast")
        );

        settings.setValue(
            QStringLiteral("windowX"),
            x()
        );

        settings.setValue(
            QStringLiteral("windowY"),
            y()
        );

        settings.setValue(
            QStringLiteral("compactMode"),
            compactMode
        );

        settings.setValue(
            QStringLiteral("alwaysOnTop"),
            alwaysOnTop
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

        QFont mainFont = QApplication::font();
        mainFont.setPixelSize(19);
        mainFont.setBold(true);

        QFont smallFont = QApplication::font();
        smallFont.setPixelSize(10);
        smallFont.setBold(true);

        const int spacing = 8;
        const int sideMargin = 12;
        const int boxHeight = 54;
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

            for (const QString &key : entry.keys) {

                QString visualKey = key;

                /*
                 * En modo normal las teclas especiales conservan
                 * su nombre como leyenda principal.
                 *
                 * En compacto utilizan su símbolo.
                 */
                if (compactMode)
                    visualKey =
                        compactDisplayName(key);

                visualKeys.append(visualKey);
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

            QString widthText = primary;

            if (!secondary.isEmpty())
                widthText += QStringLiteral("  ");

            QFontMetrics fm(mainFont);

            const int textWidth =
                fm.horizontalAdvance(widthText);

            const int boxWidth =
                qMax(
                    minimumBoxWidth,
                    textWidth + 28
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
            0, 0, 0, 175
        );

        const QColor deepShadow(
            0, 0, 0, 90
        );

        const QColor keyTop(
            58, 60, 65, 255
        );

        const QColor keyMiddle(
            42, 44, 49, 255
        );

        const QColor keyBottom(
            27, 29, 33, 255
        );

        const QColor edge(
            255, 255, 255, 52
        );

        const QColor edgeBottom(
            0, 0, 0, 150
        );

        const QColor highlight(
            255, 255, 255, 28
        );

        const QColor textColor(
            250, 250, 252, 255
        );

        const QColor secondaryColor(
            205, 207, 212, 185
        );

        const QColor combinationBorder(
            255, 255, 255, 72
        );

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
                    8,
                    8
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
                    8,
                    8
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
                    8,
                    8
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
                    8,
                    8
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

                    const QString component =
                        compactMode
                            ? compactDisplayName(
                                entry.keys.at(i)
                              )
                            : entry.keys.at(i);

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

                    if (i < draw.keySecondary.size()) {

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
                    x - 5,
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

        if (primary.size() == 1) {

            const QChar ch =
                primary.at(0);

            characterKey =
                ch.isLetterOrNumber() ||
                QStringLiteral(
                    "!@#$%^&*()_+=-{}[]:;\"'<>,.?/\\\\|~`"
                ).contains(ch);
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
     * Posición de la ventana
     * ---------------------------------------------------------------
     */

    void saveWindowPosition()
    {
        QSettings settings(
            QStringLiteral("KDE"),
            QStringLiteral("KDEKeyCast")
        );

        settings.setValue(
            QStringLiteral("windowPosition"),
            pos()
        );
    }

    void restoreWindowPosition()
    {
        QSettings settings(
            QStringLiteral("KDE"),
            QStringLiteral("KDEKeyCast")
        );

        const QVariant value =
            settings.value(
                QStringLiteral("windowPosition")
            );

        if (value.isValid()) {
            const QPoint savedPosition =
                value.toPoint();

            const QRect available =
                QGuiApplication::primaryScreen()
                    ? QGuiApplication::primaryScreen()
                        ->availableGeometry()
                    : QRect();

            if (available.isValid()) {

                const QRect restored(
                    savedPosition,
                    size()
                );

                if (available.intersects(restored)) {
                    move(savedPosition);
                    return;
                }
            }
        }

        /*
         * Primera ejecución:
         * posición inferior centrada.
         */
        if (auto *screen =
                QGuiApplication::primaryScreen()) {

            const QRect area =
                screen->availableGeometry();

            move(
                area.left() +
                    (area.width() - width()) / 2,
                area.top() +
                    area.height() -
                    height() -
                    80
            );
        }
    }

    /*
     * ---------------------------------------------------------------
     * Visual queue
     * ---------------------------------------------------------------
     */
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
    entry.linuxKeyCode =
        keys.size() == 1 && !combination
            ? linuxKeyCode
            : -1;

    visualQueue.append(entry);

    while (visualQueue.size() > 6)
        visualQueue.removeFirst();

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
                keyName == QStringLiteral("KEY_T") &&
                pressedModifiers.contains(
                    QStringLiteral("KEY_LEFTCTRL")
                ) &&
                pressedModifiers.contains(
                    QStringLiteral("KEY_LEFTSHIFT")
                )) {

                toggleAlwaysOnTop();
            }

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

    addVisualEntry(
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
    
    void toggleAlwaysOnTop()
    {
        alwaysOnTop = !alwaysOnTop;

        const QPoint oldPosition = pos();

        const Qt::WindowFlags flags =
            Qt::FramelessWindowHint |
            Qt::Tool |
            (alwaysOnTop
                ? Qt::WindowStaysOnTopHint
                : Qt::WindowType(0));

        setWindowFlags(flags);
        move(oldPosition);
        show();

        qDebug()
            << "KDE KeyCast: always on top ="
            << alwaysOnTop;
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
    bool alwaysOnTop = true;

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
