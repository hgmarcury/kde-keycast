#!/usr/bin/env bash
# SPDX-FileCopyrightText: 2026 Marc G.
# SPDX-License-Identifier: GPL-3.0-or-later

set -euo pipefail

SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
APPDIR="$(cd "$SCRIPT_DIR/../.." && pwd)"

USER_DATA_DIR="${XDG_DATA_HOME:-$HOME/.local/share}"
APPLICATIONS_DIR="$USER_DATA_DIR/applications"
ICONS_DIR="$USER_DATA_DIR/icons/hicolor/scalable/apps"

APPIMAGE_PATH="${1:-}"

if [[ -z "$APPIMAGE_PATH" ]]; then
    echo "ERROR: AppImage path not specified."
    exit 1
fi

APPIMAGE_PATH="$(readlink -f "$APPIMAGE_PATH")"

if [[ ! -f "$APPIMAGE_PATH" ]]; then
    echo "ERROR: AppImage not found:"
    echo "  $APPIMAGE_PATH"
    exit 1
fi

DESKTOP_TEMPLATE="$APPDIR/usr/share/applications/kde-keycast.desktop"

if [[ ! -f "$DESKTOP_TEMPLATE" ]]; then
    echo "ERROR: Desktop file not found:"
    echo "  $DESKTOP_TEMPLATE"
    exit 1
fi

LIGHT_ICON="$APPDIR/usr/share/icons/hicolor/scalable/apps/keycast-light.svg"
DARK_ICON="$APPDIR/usr/share/icons/hicolor/scalable/apps/keycast-dark.svg"

if [[ ! -f "$LIGHT_ICON" || ! -f "$DARK_ICON" ]]; then
    echo "ERROR: KDE KeyCast icons not found."
    exit 1
fi

BACKEND_SOURCE="$APPDIR/usr/lib/x86_64-linux-gnu/libexec/kde-keycast/showmethekey-cli"
BACKEND_TARGET="/usr/lib/kde-keycast-appimage/showmethekey-cli"

POLICY_SOURCE="$APPDIR/usr/share/polkit-1/actions/com.github.hgmarcury.kde-keycast-appimage.policy"
POLICY_TARGET="/usr/share/polkit-1/actions/com.github.hgmarcury.kde-keycast-appimage.policy"

CONFIGURE_SOURCE="$APPDIR/usr/bin/configure-kde-keycast-permissions"
CONFIGURE_TARGET="/usr/lib/kde-keycast-appimage/configure-kde-keycast-permissions"

REMOVE_SOURCE="$APPDIR/usr/bin/remove-kde-keycast-permissions"
REMOVE_TARGET="/usr/lib/kde-keycast-appimage/remove-kde-keycast-permissions"

RULE_SOURCE="$APPDIR/usr/share/kde-keycast/polkit/10-com.github.hgmarcury.kde-keycast.rules"
RULE_TARGET="/usr/lib/kde-keycast-appimage/10-com.github.hgmarcury.kde-keycast.rules"

BACKEND_INSTALLED=false
POLICY_INSTALLED=false
CONFIGURE_INSTALLED=false
REMOVE_INSTALLED=false
RULE_INSTALLED=false
RULE_UPDATED=false
RULE_BACKUP=""

cleanup_on_exit() {
    local status=$?
    trap - EXIT

    if [[ "$status" -ne 0 ]]; then
        echo "ERROR: AppImage installation failed."

        if [[ "$BACKEND_INSTALLED" == true ||
              "$POLICY_INSTALLED" == true ||
              "$CONFIGURE_INSTALLED" == true ||
              "$REMOVE_INSTALLED" == true ||
              "$RULE_INSTALLED" == true ||
              "$RULE_UPDATED" == true ]]; then
            rollback_appimage_system_files ||
                echo "WARNING: Some AppImage-specific files could not be reverted."
        fi
    fi

    exit "$status"
}
trap cleanup_on_exit EXIT

rollback_appimage_system_files() {
    echo "==> Reverting AppImage-specific system files..."

    local failed=0
    local -a targets=()

    # Restore a recognized legacy rule before removing newly installed files.
    if [[ "$RULE_UPDATED" == true ]]; then
        if [[ -n "$RULE_BACKUP" ]] &&
           sudo test -f "$RULE_BACKUP" &&
           sudo mv -f -- "$RULE_BACKUP" "$RULE_TARGET"; then
            echo "==> Previous AppImage Polkit rule restored."
            RULE_UPDATED=false
            RULE_BACKUP=""
        else
            echo "WARNING: Could not restore the previous Polkit rule."
            echo "  Backup: $RULE_BACKUP"
            failed=1
        fi
    fi

    # Never delete the target if restoring its previous version failed.
    if [[ "$RULE_INSTALLED" == true &&
          "$RULE_UPDATED" != true ]]; then
        targets+=("$RULE_TARGET")
    fi
    [[ "$REMOVE_INSTALLED" == true ]] && targets+=("$REMOVE_TARGET")
    [[ "$CONFIGURE_INSTALLED" == true ]] && targets+=("$CONFIGURE_TARGET")
    [[ "$POLICY_INSTALLED" == true ]] && targets+=("$POLICY_TARGET")
    [[ "$BACKEND_INSTALLED" == true ]] && targets+=("$BACKEND_TARGET")

    # One rm invocation attempts every selected path, even if one fails.
    if ((${#targets[@]})) && ! sudo rm -f -- "${targets[@]}"; then
        echo "WARNING: Could not remove all newly installed files."
        failed=1
    fi

    local target_dir
    target_dir="$(dirname "$BACKEND_TARGET")"
    if sudo test -d "$target_dir"; then
        if ! sudo rmdir --ignore-fail-on-non-empty "$target_dir"; then
            echo "WARNING: Could not remove directory: $target_dir"
            failed=1
        fi
    fi

    return "$failed"
}

install_appimage_system_files() {
    echo "==> Installing AppImage-specific backend, scripts and Polkit files..."

    local source target mode staging
    local -a files=(
        "$BACKEND_SOURCE|$BACKEND_TARGET|0755|BACKEND_INSTALLED"
        "$POLICY_SOURCE|$POLICY_TARGET|0644|POLICY_INSTALLED"
        "$CONFIGURE_SOURCE|$CONFIGURE_TARGET|0755|CONFIGURE_INSTALLED"
        "$REMOVE_SOURCE|$REMOVE_TARGET|0755|REMOVE_INSTALLED"
        "$RULE_SOURCE|$RULE_TARGET|0644|RULE_INSTALLED"
    )

    for entry in "${files[@]}"; do
        IFS='|' read -r source target mode flag <<< "$entry"

        if [[ ! -f "$source" ]]; then
            echo "ERROR: Required AppImage file not found:"
            echo "  $source"
            rollback_appimage_system_files
            return 1
        fi

        staging="$(mktemp)" || {
            echo "ERROR: Could not create a temporary staging file."
            rollback_appimage_system_files
            return 1
        }

        if ! install -m "$mode" "$source" "$staging"; then
            echo "ERROR: Could not stage:"
            echo "  $source"
            rm -f "$staging"
            rollback_appimage_system_files
            return 1
        fi

        # Treat dangling symlinks as existing targets too; never follow or overwrite them.
        if sudo test -e "$target" || sudo test -L "$target"; then
            if sudo test -L "$target"; then
                echo "ERROR: Existing system target is a symbolic link:"
                echo "  $target"
                echo "No existing file was overwritten."
                rm -f "$staging"
                rollback_appimage_system_files
                return 1
            fi

            if sudo cmp -s "$staging" "$target"; then
                echo "==> Reusing existing file: $target"
                rm -f "$staging"
                continue
            fi

            # Only upgrade the exact legacy KDE KeyCast rule we recognize.
            # Any symlink, non-regular file, or unknown content remains untouched.
            if [[ "$target" == "$RULE_TARGET" ]] &&
               sudo test -f "$target" &&
               ! sudo test -L "$target"; then
                legacy_rule="$(mktemp)" || {
                    echo "ERROR: Could not create a legacy-rule comparison file."
                    rm -f "$staging"
                    rollback_appimage_system_files
                    return 1
                }

                cat > "$legacy_rule" <<'LEGACY_RULE'
/*
 * SPDX-FileCopyrightText: 2026 Marc G.
 * SPDX-License-Identifier: GPL-3.0-or-later
 */

polkit.addRule(function(action, subject) {
    if (action.id === "com.github.hgmarcury.kde-keycast.backend" &&
        subject.isInGroup("kde-keycast") &&
        subject.local &&
        subject.active) {
        return polkit.Result.YES;
    }
});
LEGACY_RULE

                if sudo cmp -s "$legacy_rule" "$target"; then
                    echo "==> Recognized legacy KDE KeyCast Polkit rule."
                    echo "==> Backing it up before upgrading..."

                    RULE_BACKUP="$(sudo mktemp "${RULE_TARGET}.backup.XXXXXX")" || {
                        echo "ERROR: Could not create the Polkit rule backup."
                        rm -f "$legacy_rule" "$staging"
                        rollback_appimage_system_files
                        return 1
                    }

                    if ! sudo cp -p -- "$target" "$RULE_BACKUP"; then
                        echo "ERROR: Could not preserve the existing Polkit rule."
                        sudo rm -f -- "$RULE_BACKUP"
                        RULE_BACKUP=""
                        rm -f "$legacy_rule" "$staging"
                        rollback_appimage_system_files
                        return 1
                    fi

                    # Set this before replacing the target so rollback can
                    # restore the backup if any subsequent step fails.
                    RULE_UPDATED=true
                    rule_staging="$(sudo mktemp "${RULE_TARGET}.new.XXXXXX")" || {
                        echo "ERROR: Could not create a replacement rule file."
                        rm -f "$legacy_rule" "$staging"
                        rollback_appimage_system_files
                        return 1
                    }

                    if ! sudo install -o root -g root -m "$mode" \
                        "$staging" "$rule_staging"; then
                        echo "ERROR: Could not stage the updated Polkit rule."
                        sudo rm -f -- "$rule_staging"
                        rm -f "$legacy_rule" "$staging"
                        rollback_appimage_system_files
                        return 1
                    fi

                    if ! sudo mv -f -- "$rule_staging" "$RULE_TARGET"; then
                        echo "ERROR: Could not install the updated Polkit rule."
                        sudo rm -f -- "$rule_staging"
                        rm -f "$legacy_rule" "$staging"
                        rollback_appimage_system_files
                        return 1
                    fi

                    echo "==> Updated the legacy KDE KeyCast Polkit rule."
                    rm -f "$legacy_rule" "$staging"
                    continue
                fi

                rm -f "$legacy_rule"
            fi

            echo "ERROR: An existing system file differs from the AppImage:"
            echo "  $target"
            echo "No existing file was overwritten."
            rm -f "$staging"
            rollback_appimage_system_files
            return 1
        fi

        # The target did not exist above; mark it before installation so
        # rollback can remove it even if install partially creates it.
        case "$flag" in
            BACKEND_INSTALLED)   BACKEND_INSTALLED=true ;;
            POLICY_INSTALLED)    POLICY_INSTALLED=true ;;
            CONFIGURE_INSTALLED) CONFIGURE_INSTALLED=true ;;
            REMOVE_INSTALLED)    REMOVE_INSTALLED=true ;;
            RULE_INSTALLED)      RULE_INSTALLED=true ;;
        esac

        if ! sudo install -D -o root -g root -m "$mode" "$staging" "$target"; then
            echo "ERROR: Could not install:"
            echo "  $target"
            rm -f "$staging"
            if ! rollback_appimage_system_files; then
                echo "WARNING: Rollback incomplete; cleanup will retry on exit."
            fi
            return 1
        fi

        rm -f "$staging"
    done
}

if [[ ! -x "$BACKEND_SOURCE" ]]; then
    echo "ERROR: AppImage backend not found:"
    echo "  $BACKEND_SOURCE"
    exit 1
fi

# Detect and validate an existing native installation.
NATIVE_APP="$(command -v kde-keycast || true)"
NATIVE_POLICY="/usr/share/polkit-1/actions/com.github.hgmarcury.kde-keycast.policy"
NATIVE_BINDIR=""
NATIVE_UNINSTALLER=""
NATIVE_CONFIGURER=""
NATIVE_PERMISSION_REMOVER=""
NATIVE_BACKEND=""
NATIVE_BACKENDS=()
NATIVE_TRACE=false
NATIVE_VALID=false

if [[ -n "$NATIVE_APP" ]]; then
    NATIVE_APP="$(readlink -f "$NATIVE_APP")"
    NATIVE_BINDIR="$(dirname "$NATIVE_APP")"
    NATIVE_UNINSTALLER="$NATIVE_BINDIR/uninstall-kde-keycast"
    NATIVE_CONFIGURER="$NATIVE_BINDIR/configure-kde-keycast-permissions"
    NATIVE_PERMISSION_REMOVER="$NATIVE_BINDIR/remove-kde-keycast-permissions"
    NATIVE_TRACE=true
fi

if [[ -e "$NATIVE_POLICY" ||
      -e /usr/bin/uninstall-kde-keycast ||
      -e /usr/bin/configure-kde-keycast-permissions ||
      -e /usr/bin/remove-kde-keycast-permissions ]]; then
    NATIVE_TRACE=true
fi

if [[ -r "$NATIVE_POLICY" ]]; then
    mapfile -t NATIVE_BACKENDS < <(
        sed -n \
            's|.*<annotate key="org.freedesktop.policykit.exec.path">\([^<]*\)</annotate>.*|\1|p' \
            "$NATIVE_POLICY"
    )
fi

if [[ ${#NATIVE_BACKENDS[@]} -eq 1 ]]; then
    NATIVE_BACKEND="${NATIVE_BACKENDS[0]}"
fi

if [[ -n "$NATIVE_APP" &&
      -x "$NATIVE_APP" &&
      -x "$NATIVE_UNINSTALLER" &&
      -x "$NATIVE_CONFIGURER" &&
      -x "$NATIVE_PERMISSION_REMOVER" &&
      -r "$NATIVE_POLICY" &&
      ${#NATIVE_BACKENDS[@]} -eq 1 &&
      "$NATIVE_BACKEND" == /* &&
      -x "$NATIVE_BACKEND" ]]; then
    NATIVE_VALID=true
fi

if [[ "$NATIVE_TRACE" == true &&
      "$NATIVE_VALID" != true ]]; then
    echo "ERROR: An incomplete or unverified native KDE KeyCast installation was detected."
    echo "No AppImage system files have been installed or changed."
    echo "Inspect the native application, helpers, Polkit policy and backend before retrying."
    exit 1
fi

if [[ "$NATIVE_VALID" == true ]]; then
    echo
    echo "A valid native KDE KeyCast installation was detected:"
    echo "  Application: $NATIVE_APP"
    echo "  Backend:     $NATIVE_BACKEND"
    echo
    read -r -p "Uninstall it and migrate to this AppImage? [y/N] " answer

    case "$answer" in
        [yY]|[yY][eE][sS]|[sS]|[sS][iI])
            echo "==> Removing the native installation..."
            if ! sudo "$NATIVE_UNINSTALLER"; then
                echo "ERROR: Native uninstall failed."
                echo "AppImage system files have not been installed."
                exit 1
            fi

            echo "==> Native installation removed successfully."

            if ! install_appimage_system_files; then
                echo "ERROR: AppImage system-file installation failed."
                echo "The native installation has already been removed."
                exit 1
            fi
            ;;
        *)
            echo "Migration cancelled. No AppImage integration was installed."
            exit 1
            ;;
    esac
else
    if ! install_appimage_system_files; then
        exit 1
    fi
fi

echo "==> Installing KDE KeyCast application integration..."

mkdir -p \
    "$APPLICATIONS_DIR" \
    "$ICONS_DIR"

sed \
    "s|^Exec=.*|Exec=$APPIMAGE_PATH|" \
    "$DESKTOP_TEMPLATE" \
    > "$APPLICATIONS_DIR/kde-keycast.desktop"

chmod 0644 \
    "$APPLICATIONS_DIR/kde-keycast.desktop"

install \
    -m 0644 \
    "$LIGHT_ICON" \
    "$ICONS_DIR/keycast-light.svg"

install \
    -m 0644 \
    "$DARK_ICON" \
    "$ICONS_DIR/keycast-dark.svg"

# Keep the backup until all installation steps, including native migration,
# have completed successfully. If backup cleanup fails, the EXIT trap can
# still attempt to restore the previous rule.
if [[ "$RULE_UPDATED" == true ]]; then
    if ! sudo rm -f -- "$RULE_BACKUP"; then
        echo "ERROR: Could not remove the Polkit rule backup:"
        echo "  $RULE_BACKUP"
        exit 1
    fi
    RULE_UPDATED=false
    RULE_BACKUP=""
    echo "==> Previous Polkit rule backup removed."
fi

echo "==> KDE KeyCast application integration installed."
