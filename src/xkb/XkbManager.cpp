#include "XkbManager.h"

#include <QDebug>

#include <cstdlib>

namespace {

xkb_keycode_t xkbKeycodeFromLinuxCode(int linuxCode)
{
    if (linuxCode < 0)
        return XKB_KEYCODE_INVALID;

    return static_cast<xkb_keycode_t>(linuxCode + 8);
}

} // namespace

XkbManager::XkbManager() = default;

XkbManager::~XkbManager()
{
    resetComposeState();

    if (xkbState) {
        xkb_state_unref(xkbState);
        xkbState = nullptr;
    }

    if (xkbKeymap) {
        xkb_keymap_unref(xkbKeymap);
        xkbKeymap = nullptr;
    }
}

xkb_keycode_t XkbManager::keycodeFromLinuxCode(int linuxCode)
{
    return xkbKeycodeFromLinuxCode(linuxCode);
}

bool XkbManager::loadKeymap(const char *keymapText)
{
    if (!keymapText)
        return false;

    xkb_context *context =
        xkb_context_new(XKB_CONTEXT_NO_FLAGS);

    if (!context) {
        qWarning()
            << "KDE KeyCast: no se pudo crear el contexto XKB";
        return false;
    }

    xkb_keymap *newKeymap =
        xkb_keymap_new_from_string(
            context,
            keymapText,
            XKB_KEYMAP_FORMAT_TEXT_V1,
            XKB_KEYMAP_COMPILE_NO_FLAGS
        );

    if (!newKeymap) {
        qWarning()
            << "KDE KeyCast: no se pudo crear el keymap XKB";

        xkb_context_unref(context);
        return false;
    }

    xkb_state *newState =
        xkb_state_new(newKeymap);

    if (!newState) {
        qWarning()
            << "KDE KeyCast: no se pudo crear el estado XKB";

        xkb_keymap_unref(newKeymap);
        xkb_context_unref(context);
        return false;
    }

    if (xkbState)
        xkb_state_unref(xkbState);

    if (xkbKeymap)
        xkb_keymap_unref(xkbKeymap);

    xkbKeymap = newKeymap;
    xkbState = newState;

    resetComposeState();

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

    xkbComposeTable =
        xkb_compose_table_new_from_locale(
            context,
            locale.constData(),
            XKB_COMPOSE_COMPILE_NO_FLAGS
        );

    if (!xkbComposeTable) {
        qWarning()
            << "KDE KeyCast: no se pudo crear la tabla Compose XKB";
    } else {
        xkbComposeState =
            xkb_compose_state_new(
                xkbComposeTable,
                XKB_COMPOSE_STATE_NO_FLAGS
            );

        if (!xkbComposeState) {
            qWarning()
                << "KDE KeyCast: no se pudo crear el estado Compose XKB";
        }
    }

    xkb_context_unref(context);

    composeSequence.clear();

    return true;
}

bool XkbManager::isReady() const
{
    return xkbKeymap != nullptr &&
           xkbState != nullptr;
}

bool XkbManager::ledActive(const char *name) const
{
    if (!xkbState || !name)
        return false;

    return xkb_state_led_name_is_active(
        xkbState,
        name
    );
}

void XkbManager::updateKey(int linuxCode, bool pressed)
{
    if (!xkbState)
        return;

    const xkb_keycode_t code =
        xkbKeycodeFromLinuxCode(linuxCode);

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

void XkbManager::updateModifiers(
    xkb_mod_mask_t modsDepressed,
    xkb_mod_mask_t modsLatched,
    xkb_mod_mask_t modsLocked,
    xkb_layout_index_t group
)
{
    if (!xkbState)
        return;

    xkb_state_update_mask(
        xkbState,
        modsDepressed,
        modsLatched,
        modsLocked,
        group,
        group,
        group
    );
}

QString XkbManager::keysymToText(xkb_keysym_t sym) const
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

    switch (sym) {
    case XKB_KEY_Escape:
        return QStringLiteral("Esc");

    case XKB_KEY_Return:
        return QStringLiteral("Enter");

    case XKB_KEY_BackSpace:
        return QStringLiteral("Backspace");

    case XKB_KEY_Tab:
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

    const uint32_t unicode =
        xkb_keysym_to_utf32(sym);

    if (unicode != 0 &&
        unicode != 0xFFFD &&
        unicode <= 0x10FFFF) {

        if (unicode < 0x20 ||
            (unicode >= 0x7F && unicode <= 0x9F)) {

            return QString();
        }

        const char32_t ucs4[] = {
            static_cast<char32_t>(unicode),
            U'\0'
        };

        const QString result =
            QString::fromUcs4(ucs4);

        if (!result.isEmpty())
            return result;
    }

    char name[128] {};

    if (xkb_keysym_get_name(
            sym,
            name,
            sizeof(name)) > 0) {

        QString result =
            QString::fromLatin1(name);

        if (result.startsWith(QStringLiteral("dead_"))) {
            const QString dead = result.mid(5);

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

xkb_keysym_t XkbManager::keySym(int linuxCode) const
{
    if (!xkbState)
        return XKB_KEY_NoSymbol;

    const xkb_keycode_t code =
        xkbKeycodeFromLinuxCode(linuxCode);

    if (code == XKB_KEYCODE_INVALID)
        return XKB_KEY_NoSymbol;

    return xkb_state_key_get_one_sym(
        xkbState,
        code
    );
}

QString XkbManager::displayName(
    int linuxCode,
    const QString &fallbackName
) const
{
    if (!xkbState ||
        !xkbKeymap ||
        linuxCode < 0) {

        return fallbackName;
    }

    const xkb_keycode_t code =
        xkbKeycodeFromLinuxCode(linuxCode);

    if (code == XKB_KEYCODE_INVALID)
        return fallbackName;

    const xkb_keysym_t sym =
        xkb_state_key_get_one_sym(
            xkbState,
            code
        );

    const QString result =
        keysymToText(sym);

    if (!result.isEmpty())
        return result;

    return fallbackName;
}

QStringList XkbManager::secondaryLegends(
    int linuxCode,
    const QString &primary,
    bool compactMode
) const
{
    QStringList result;

    if (compactMode) {
        if (primary == QStringLiteral("Ⅱ") ||
            primary == QStringLiteral("⎙") ||
            primary == QStringLiteral("⇥")) {

            return result;
        }
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
     * Conservamos las leyendas físicas especiales existentes.
     */
    if (primary == QStringLiteral("PrtSc") ||
        primary == QStringLiteral("Print")) {
        result.append(QStringLiteral("⎙"));
        return result;
    }

    if (primary == QStringLiteral("Pause")) {
        result.append(QStringLiteral("Ⅱ"));
        return result;
    }

    if (primary == QStringLiteral("Tab")) {
        result.append(QStringLiteral("⇥"));
        return result;
    }

    const xkb_layout_index_t layout =
        xkb_state_key_get_layout(
            xkbState,
            code
        );

    if (layout == XKB_LAYOUT_INVALID)
        return result;

    const int levels =
        xkb_keymap_num_levels_for_key(
            xkbKeymap,
            code,
            layout
        );

    const int maxLevels =
        qMin(levels, 4);

    for (int level = 0;
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
            char name[128] {};

            if (xkb_keysym_get_name(
                    syms[i],
                    name,
                    sizeof(name)) <= 0) {

                continue;
            }

            const QString xkbName =
                QString::fromLatin1(name);

            if (xkbName.startsWith(
                    QStringLiteral("dead_"))) {

                continue;
            }

            const QString legend =
                keysymToText(syms[i]);

            if (legend.isEmpty() ||
                legend == primary ||
                result.contains(legend)) {

                continue;
            }

            result.append(legend);

            if (result.size() >= 2)
                return result;
        }
    }

    return result;
}

void XkbManager::resetComposeState()
{
    if (xkbComposeState) {
        xkb_compose_state_unref(xkbComposeState);
        xkbComposeState = nullptr;
    }

    if (xkbComposeTable) {
        xkb_compose_table_unref(xkbComposeTable);
        xkbComposeTable = nullptr;
    }

    composeSequence.clear();
}

void XkbManager::resetComposeSequence()
{
    if (xkbComposeState)
        xkb_compose_state_reset(xkbComposeState);

    composeSequence.clear();
}

ComposeResult
XkbManager::feedComposeKey(xkb_keysym_t keysym)
{
    if (!xkbComposeState)
        return ComposeResult::Ignored;

    const xkb_compose_feed_result result =
        xkb_compose_state_feed(
            xkbComposeState,
            keysym
        );

    if (result ==
        XKB_COMPOSE_FEED_IGNORED) {

        return ComposeResult::Ignored;
    }

    const xkb_compose_status status =
        xkb_compose_state_get_status(
            xkbComposeState
        );

    switch (status) {

    case XKB_COMPOSE_COMPOSING:
        return ComposeResult::Composing;

    case XKB_COMPOSE_COMPOSED:
        return ComposeResult::Composed;

    case XKB_COMPOSE_CANCELLED:
        return ComposeResult::Cancelled;

    case XKB_COMPOSE_NOTHING:
    default:
        return ComposeResult::Ignored;
    }
}
