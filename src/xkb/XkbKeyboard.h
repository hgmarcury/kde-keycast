#pragma once

#include <QString>
#include <QByteArray>

#include <xkbcommon/xkbcommon.h>

namespace KDEKeyCast {

class XkbKeyboard
{
public:
    XkbKeyboard() = default;

    XkbKeyboard(const XkbKeyboard &) = delete;
    XkbKeyboard &operator=(const XkbKeyboard &) = delete;

    ~XkbKeyboard()
    {
        reset();
    }

    void reset()
    {
        if (state_) {
            xkb_state_unref(state_);
            state_ = nullptr;
        }

        if (keymap_) {
            xkb_keymap_unref(keymap_);
            keymap_ = nullptr;
        }

        if (context_) {
            xkb_context_unref(context_);
            context_ = nullptr;
        }
    }

    bool isValid() const noexcept
    {
        return context_ && keymap_ && state_;
    }

    xkb_state *state() const noexcept
    {
        return state_;
    }

    xkb_keymap *keymap() const noexcept
    {
        return keymap_;
    }

    bool loadFromString(const QString &keymapText)
    {
        reset();

        context_ = xkb_context_new(XKB_CONTEXT_NO_FLAGS);
        if (!context_)
            return false;

        const QByteArray data = keymapText.toUtf8();

        keymap_ = xkb_keymap_new_from_string(
            context_,
            data.constData(),
            XKB_KEYMAP_FORMAT_TEXT_V1,
            XKB_KEYMAP_COMPILE_NO_FLAGS);

        if (!keymap_) {
            reset();
            return false;
        }

        state_ = xkb_state_new(keymap_);

        if (!state_) {
            reset();
            return false;
        }

        return true;
    }

    QString keyName(xkb_keycode_t code) const
    {
        if (!state_)
            return {};

        const xkb_keysym_t sym =
            xkb_state_key_get_one_sym(state_, code);

        if (sym == XKB_KEY_NoSymbol)
            return {};

        char name[128] = {};
        if (xkb_keysym_get_name(
                sym,
                name,
                sizeof(name)) <= 0) {
            return {};
        }

        return QString::fromLatin1(name);
    }

private:
    xkb_context *context_ = nullptr;
    xkb_keymap *keymap_ = nullptr;
    xkb_state *state_ = nullptr;
};

} // namespace KDEKeyCast
