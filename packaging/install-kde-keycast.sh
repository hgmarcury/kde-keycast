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
STATE_DIR="/var/lib/kde-keycast"
USER_DATA_REGISTRY="$STATE_DIR/appimage-user-data-dir"

REGISTRY_INSTALLED=false
REGISTRY_TEMP=""
STATE_DIR_CREATED=false

APPIMAGE_INSTALL_COMMITTED=false
NATIVE_MIGRATION_FAILED=false

NATIVE_MIGRATION_TARGETS=()
NATIVE_MIGRATION_BACKUP_DIRS=()
NATIVE_MIGRATION_BACKUPS=()
NATIVE_MIGRATION_IDS=()
NATIVE_MIGRATION_MOVED=()
NATIVE_MIGRATION_MUTATION_STARTED=false
NATIVE_MIGRATION_MUTATION_STARTED=false
NATIVE_MIGRATION_COMMITTED=false

rollback_appimage_user_data_registry() {
    echo "==> Reverting AppImage user-data registration..."

    local failed=0

    if [[ -n "$REGISTRY_TEMP" ]]; then
        if sudo rm -f -- "$REGISTRY_TEMP"; then
            REGISTRY_TEMP=""
        else
            echo "WARNING: Could not remove temporary XDG registration."
            failed=1
        fi
    fi

    if [[ "$REGISTRY_INSTALLED" == true ]]; then
        if sudo rm -f -- "$USER_DATA_REGISTRY"; then
            REGISTRY_INSTALLED=false
        else
            echo "WARNING: Could not remove the new XDG registration."
            failed=1
        fi
    fi

    if [[ "$STATE_DIR_CREATED" == true &&
        "$REGISTRY_INSTALLED" != true &&
        "$REGISTRY_TEMP" == "" ]]; then
        if sudo rmdir -- "$STATE_DIR"; then
            STATE_DIR_CREATED=false
        else
            echo "WARNING: Could not remove the newly created state directory."
            failed=1
        fi
    fi

    return "$failed"
}

register_appimage_user_data_dir() {
    echo "==> Registering AppImage user-data directory..."

    local state_metadata
    local registry_metadata

    if [[ "$USER_DATA_DIR" != /* || ! -d "$USER_DATA_DIR" ]]; then
        echo "ERROR: XDG data directory must be an existing absolute directory."
        return 1
    fi

    USER_DATA_DIR="$(realpath -e -- "$USER_DATA_DIR")"
    APPLICATIONS_DIR="$USER_DATA_DIR/applications"
    ICONS_DIR="$USER_DATA_DIR/icons/hicolor/scalable/apps"

    if sudo test -e "$STATE_DIR"; then
        if ! sudo test -d "$STATE_DIR" ||
            sudo test -L "$STATE_DIR"; then
            echo "ERROR: KDE KeyCast state path is not a real directory."
            return 1
        fi

        state_metadata="$(sudo stat -c '%u:%g:%a' -- "$STATE_DIR")"

        if [[ "$state_metadata" != "0:0:755" ]]; then
            echo "ERROR: KDE KeyCast state directory has unexpected ownership or permissions."
            return 1
        fi
    else
        if ! sudo mkdir -- "$STATE_DIR"; then
            echo "ERROR: Could not create KDE KeyCast state directory."
            return 1
        fi

        STATE_DIR_CREATED=true

        if ! sudo chown root:root "$STATE_DIR" ||
            ! sudo chmod 0755 "$STATE_DIR"; then
            echo "ERROR: Could not set KDE KeyCast state directory permissions."
            return 1
        fi
    fi

    if sudo test -e "$USER_DATA_REGISTRY" ||
        sudo test -L "$USER_DATA_REGISTRY"; then

        if ! sudo test -f "$USER_DATA_REGISTRY" ||
            sudo test -L "$USER_DATA_REGISTRY"; then
            echo "ERROR: Existing XDG registration is not a regular file."
            return 1
        fi

        registry_metadata="$(sudo stat -c '%u:%g:%a' -- "$USER_DATA_REGISTRY")"

        if [[ "$registry_metadata" != "0:0:644" ]]; then
            echo "ERROR: Existing XDG registration has unexpected ownership or permissions."
            return 1
        fi

        if ! printf '%s\n' "$USER_DATA_DIR" |
            sudo cmp -s - "$USER_DATA_REGISTRY"; then
            echo "ERROR: An XDG data directory is already registered for KDE KeyCast."
            echo "  Current: $USER_DATA_DIR"
            return 1
        fi

        echo "==> Existing XDG registration verified."
        return 0
    fi

    REGISTRY_TEMP="$(sudo mktemp "$STATE_DIR/.appimage-user-data-dir.XXXXXX")"

    if ! printf '%s\n' "$USER_DATA_DIR" |
        sudo tee "$REGISTRY_TEMP" >/dev/null; then
        echo "ERROR: Could not write temporary XDG registration."
        return 1
    fi

    if ! sudo chown root:root "$REGISTRY_TEMP" ||
        ! sudo chmod 0644 "$REGISTRY_TEMP"; then
        echo "ERROR: Could not set XDG registration ownership or permissions."
        return 1
    fi

    if ! sudo ln -- "$REGISTRY_TEMP" "$USER_DATA_REGISTRY"; then
        echo "ERROR: Could not create XDG registration without overwriting an existing file."
        return 1
    fi

    REGISTRY_INSTALLED=true

    if ! sudo rm -f -- "$REGISTRY_TEMP"; then
        echo "ERROR: Could not remove temporary XDG registration."
        return 1
    fi

    REGISTRY_TEMP=""

    echo "==> AppImage XDG data directory registered."
}

ensure_user_integration_directory() {
    local dir="$1"
    local relative
    local current
    local part
    local -a parts=()

    [[ "$dir" == "$USER_DATA_DIR/"* ]] || return 1

    relative="${dir#"$USER_DATA_DIR"/}"
    current="$USER_DATA_DIR"

    IFS='/' read -r -a parts <<<"$relative"

    for part in "${parts[@]}"; do
        [[ -n "$part" && "$part" != "." && "$part" != ".." ]] || return 1

        current="$current/$part"

        if [[ -L "$current" ]]; then
            echo "ERROR: Refusing to use a symbolic-link directory:"
            echo "  $current"
            return 1
        fi

        if [[ ! -e "$current" ]]; then
            if ! mkdir -- "$current"; then
                echo "ERROR: Could not create directory:"
                echo "  $current"
                return 1
            fi
        fi

        if [[ ! -d "$current" || -L "$current" ]]; then
            echo "ERROR: Invalid integration directory:"
            echo "  $current"
            return 1
        fi
    done
}

install_appimage_user_integration() {
    echo "==> Installing KDE KeyCast application integration..."

    local i
    local target
    local staged
    local current_id
    local staged_id
    local transaction_real
    local -a targets=()

    USER_INTEGRATION_INSTALLED=false
    USER_INTEGRATION_MUTATION_STARTED=false
    USER_INTEGRATION_BACKUP_DIR=""

    USER_INTEGRATION_TARGETS=()
    USER_INTEGRATION_BACKUPS=()
    USER_INTEGRATION_EXISTED=()
    USER_INTEGRATION_INSTALLED_IDS=()
    USER_INTEGRATION_ORIGINAL_IDS=()
    USER_INTEGRATION_STAGED=()
    USER_INTEGRATION_STAGED_IDS=()

    if ! ensure_user_integration_directory "$APPLICATIONS_DIR" ||
        ! ensure_user_integration_directory "$ICONS_DIR"; then
        return 1
    fi

    if [[ ! -f "$DESKTOP_TEMPLATE" || -L "$DESKTOP_TEMPLATE" ||
        ! -f "$LIGHT_ICON" || -L "$LIGHT_ICON" ||
        ! -f "$DARK_ICON" || -L "$DARK_ICON" ]]; then
        echo "ERROR: A required desktop integration source is missing or invalid."
        return 1
    fi

    if ! USER_INTEGRATION_BACKUP_DIR="$(
        mktemp -d -- "$USER_DATA_DIR/.kde-keycast-integration.XXXXXX"
    )"; then
        echo "ERROR: Could not create the integration transaction directory."
        return 1
    fi

    if [[ ! -d "$USER_INTEGRATION_BACKUP_DIR" ||
        -L "$USER_INTEGRATION_BACKUP_DIR" ]]; then
        echo "ERROR: Invalid integration transaction directory."
        return 1
    fi

    chmod 0700 -- "$USER_INTEGRATION_BACKUP_DIR" || return 1

    transaction_real="$(realpath -e -- "$USER_INTEGRATION_BACKUP_DIR")" || return 1

    if [[ "$(dirname -- "$transaction_real")" != "$USER_DATA_DIR" ]]; then
        echo "ERROR: Integration transaction directory is outside the XDG data directory."
        return 1
    fi

    targets=(
        "$APPLICATIONS_DIR/kde-keycast.desktop"
        "$ICONS_DIR/keycast-light.svg"
        "$ICONS_DIR/keycast-dark.svg"
    )

    # Register every path before preparing or moving any file.
    for i in 0 1 2; do
        USER_INTEGRATION_TARGETS[i]="${targets[i]}"
        USER_INTEGRATION_BACKUPS[i]="$USER_INTEGRATION_BACKUP_DIR/original-$i"
        USER_INTEGRATION_STAGED[i]="$USER_INTEGRATION_BACKUP_DIR/new-$i"
        USER_INTEGRATION_EXISTED[i]=false
        USER_INTEGRATION_ORIGINAL_IDS[i]=""
        USER_INTEGRATION_INSTALLED_IDS[i]=""
        USER_INTEGRATION_STAGED_IDS[i]=""
    done

    # Prepare all new files inside the private transaction directory.
    if ! sed "s|^Exec=.*|Exec=$APPIMAGE_PATH|" \
        "$DESKTOP_TEMPLATE" >"${USER_INTEGRATION_STAGED[0]}"; then
        echo "ERROR: Could not prepare the desktop entry."
        return 1
    fi

    if ! chmod 0644 -- "${USER_INTEGRATION_STAGED[0]}" ||
        ! install -m 0644 -- "$LIGHT_ICON" "${USER_INTEGRATION_STAGED[1]}" ||
        ! install -m 0644 -- "$DARK_ICON" "${USER_INTEGRATION_STAGED[2]}"; then
        echo "ERROR: Could not prepare the application icons."
        return 1
    fi

    # Record staged identities before any destination is modified.
    for i in 0 1 2; do
        staged="${USER_INTEGRATION_STAGED[i]}"

        if [[ ! -f "$staged" || -L "$staged" ]] ||
            ! staged_id="$(stat -c '%d:%i' -- "$staged")"; then
            echo "ERROR: Could not verify a prepared integration file:"
            echo "  $staged"
            return 1
        fi

        USER_INTEGRATION_STAGED_IDS[i]="$staged_id"
        USER_INTEGRATION_INSTALLED_IDS[i]="$staged_id"
    done

    # Inspect all existing destinations before moving any of them.
    for i in 0 1 2; do
        target="${USER_INTEGRATION_TARGETS[i]}"

        if [[ -e "$target" || -L "$target" ]]; then
            if [[ ! -f "$target" || -L "$target" ]] ||
                ! current_id="$(stat -c '%d:%i' -- "$target")"; then
                echo "ERROR: Existing integration target is not a regular file:"
                echo "  $target"
                return 1
            fi

            USER_INTEGRATION_EXISTED[i]=true
            USER_INTEGRATION_ORIGINAL_IDS[i]="$current_id"
        fi
    done

    # From this point onward, rollback must inspect the destinations.
    USER_INTEGRATION_MUTATION_STARTED=true

    # Move existing files into the transaction directory.
    for i in 0 1 2; do
        [[ "${USER_INTEGRATION_EXISTED[i]}" == true ]] || continue

        target="${USER_INTEGRATION_TARGETS[i]}"
        staged="${USER_INTEGRATION_BACKUPS[i]}"
        current_id="${USER_INTEGRATION_ORIGINAL_IDS[i]}"

        if ! mv -nT -- "$target" "$staged" ||
            [[ -e "$target" || -L "$target" ]] ||
            [[ ! -f "$staged" || -L "$staged" ]] ||
            ! staged_id="$(stat -c '%d:%i' -- "$staged")" ||
            [[ "$staged_id" != "$current_id" ]]; then
            echo "ERROR: Could not safely back up the existing integration file:"
            echo "  $target"
            return 1
        fi
    done

    # Install the prepared files without overwriting unexpected destinations.
    for i in 0 1 2; do
        target="${USER_INTEGRATION_TARGETS[i]}"
        staged="${USER_INTEGRATION_STAGED[i]}"
        staged_id="${USER_INTEGRATION_INSTALLED_IDS[i]}"

        if ! mv -nT -- "$staged" "$target" ||
            [[ -e "$staged" || -L "$staged" ]] ||
            [[ ! -f "$target" || -L "$target" ]] ||
            ! current_id="$(stat -c '%d:%i' -- "$target")" ||
            [[ "$current_id" != "$staged_id" ]]; then
            echo "ERROR: Could not safely install the integration file:"
            echo "  $target"
            return 1
        fi
    done

    USER_INTEGRATION_INSTALLED=true
    echo "==> KDE KeyCast application integration installed."
}

rollback_appimage_user_integration() {
    echo "==> Reverting AppImage user integration..."

    local failed=0
    local i
    local target
    local backup
    local staged
    local quarantine
    local original_id
    local installed_id
    local current_id
    local quarantine_id
    local transaction_realpath

    # If no destination has been touched, remove only our prepared files.
    if [[ "$USER_INTEGRATION_MUTATION_STARTED" != true ]]; then
        if [[ -n "$USER_INTEGRATION_BACKUP_DIR" ]]; then
            if [[ ! -d "$USER_INTEGRATION_BACKUP_DIR" ||
                -L "$USER_INTEGRATION_BACKUP_DIR" ]] ||
                ! transaction_realpath="$(realpath -e -- "$USER_INTEGRATION_BACKUP_DIR")" ||
                [[ "$(dirname -- "$transaction_realpath")" != "$USER_DATA_DIR" ]]; then
                echo "WARNING: Invalid integration transaction directory."
                return 1
            fi

            for i in 0 1 2; do
                staged="$USER_INTEGRATION_BACKUP_DIR/new-$i"

                if [[ -e "$staged" || -L "$staged" ]]; then
                    if [[ -f "$staged" && ! -L "$staged" ]]; then
                        if ! rm -- "$staged"; then
                            echo "WARNING: Could not remove prepared integration file:"
                            echo "  $staged"
                            failed=1
                        fi
                    else
                        echo "WARNING: Unexpected prepared integration file:"
                        echo "  $staged"
                        failed=1
                    fi
                fi
            done

            if [[ "$failed" -eq 0 ]]; then
                if rmdir -- "$USER_INTEGRATION_BACKUP_DIR"; then
                    USER_INTEGRATION_BACKUP_DIR=""
                else
                    echo "WARNING: Could not remove the integration transaction directory."
                    failed=1
                fi
            fi
        fi

        return "$failed"
    fi

    # Never construct quarantine paths until the transaction directory
    # has been validated.
    if [[ -z "$USER_INTEGRATION_BACKUP_DIR" ||
        ! -d "$USER_INTEGRATION_BACKUP_DIR" ||
        -L "$USER_INTEGRATION_BACKUP_DIR" ]]; then
        echo "WARNING: Integration transaction directory is missing or invalid."
        return 1
    fi

    if ! transaction_realpath="$(realpath -e -- "$USER_INTEGRATION_BACKUP_DIR")" ||
        [[ "$(dirname -- "$transaction_realpath")" != "$USER_DATA_DIR" ]]; then
        echo "WARNING: Integration transaction directory is outside the XDG data directory."
        return 1
    fi

    # Restore destinations in reverse order.
    for ((i = ${#USER_INTEGRATION_TARGETS[@]} - 1; i >= 0; i--)); do
        target="${USER_INTEGRATION_TARGETS[i]:-}"
        backup="${USER_INTEGRATION_BACKUPS[i]:-}"
        original_id="${USER_INTEGRATION_ORIGINAL_IDS[i]:-}"
        installed_id="${USER_INTEGRATION_INSTALLED_IDS[i]:-}"
        quarantine="$USER_INTEGRATION_BACKUP_DIR/rollback-new-$i"

        [[ -n "$target" && -n "$backup" ]] || {
            failed=1
            continue
        }

        if [[ -e "$target" || -L "$target" ]]; then
            if [[ ! -f "$target" || -L "$target" ]] ||
                ! current_id="$(stat -c '%d:%i' -- "$target")"; then
                echo "WARNING: Unexpected integration target:"
                echo "  $target"
                failed=1
                continue
            fi

            # The original may still be in place if the backup move
            # had not yet happened when installation failed.
            if [[ "${USER_INTEGRATION_EXISTED[i]:-false}" == true &&
                "$current_id" == "$original_id" ]]; then
                if [[ -e "$backup" || -L "$backup" ]]; then
                    echo "WARNING: Both original target and backup exist:"
                    echo "  $target"
                    failed=1
                fi
                continue
            fi

            # Remove only the exact inode installed by this transaction.
            if [[ -n "$installed_id" && "$current_id" == "$installed_id" ]]; then
                if [[ -e "$quarantine" || -L "$quarantine" ]]; then
                    echo "WARNING: Quarantine path already exists:"
                    echo "  $quarantine"
                    failed=1
                    continue
                fi

                if ! mv -nT -- "$target" "$quarantine" ||
                    [[ -e "$target" || -L "$target" ]] ||
                    [[ ! -f "$quarantine" || -L "$quarantine" ]] ||
                    ! quarantine_id="$(stat -c '%d:%i' -- "$quarantine")" ||
                    [[ "$quarantine_id" != "$installed_id" ]]; then
                    echo "WARNING: Could not safely quarantine the installed file:"
                    echo "  $target"
                    failed=1
                    continue
                fi
            else
                echo "WARNING: Integration target has an unexpected identity:"
                echo "  $target"
                failed=1
                continue
            fi
        fi

        if [[ "${USER_INTEGRATION_EXISTED[i]:-false}" == true ]]; then
            if [[ -e "$backup" || -L "$backup" ]]; then
                if [[ ! -f "$backup" || -L "$backup" ]] ||
                    ! current_id="$(stat -c '%d:%i' -- "$backup")" ||
                    [[ "$current_id" != "$original_id" ]]; then
                    echo "WARNING: Original backup is missing, invalid, or changed:"
                    echo "  $backup"
                    failed=1
                    continue
                fi

                if [[ -e "$target" || -L "$target" ]]; then
                    echo "WARNING: Refusing to overwrite an existing integration target:"
                    echo "  $target"
                    failed=1
                    continue
                fi

                if ! mv -nT -- "$backup" "$target" ||
                    [[ -e "$backup" || -L "$backup" ]] ||
                    [[ ! -f "$target" || -L "$target" ]] ||
                    ! current_id="$(stat -c '%d:%i' -- "$target")" ||
                    [[ "$current_id" != "$original_id" ]]; then
                    echo "WARNING: Original file restoration could not be verified:"
                    echo "  $target"
                    failed=1
                    continue
                fi
            elif [[ ! -f "$target" || -L "$target" ]] ||
                ! current_id="$(stat -c '%d:%i' -- "$target")" ||
                [[ "$current_id" != "$original_id" ]]; then
                echo "WARNING: Original integration file could not be found:"
                echo "  $target"
                failed=1
                continue
            fi
        fi
    done

    # Remove staged files and quarantined new files only after all
    # original destinations have been restored successfully.
    if [[ "$failed" -eq 0 ]]; then
        for ((i = 0; i < ${#USER_INTEGRATION_STAGED[@]}; i++)); do
            staged="${USER_INTEGRATION_STAGED[i]:-}"
            installed_id="${USER_INTEGRATION_STAGED_IDS[i]:-}"

            [[ -n "$staged" && -n "$installed_id" ]] || continue

            if [[ -e "$staged" || -L "$staged" ]]; then
                if [[ -f "$staged" && ! -L "$staged" ]] &&
                    current_id="$(stat -c '%d:%i' -- "$staged")" &&
                    [[ "$current_id" == "$installed_id" ]]; then
                    rm -- "$staged" || failed=1
                else
                    echo "WARNING: Unexpected staged integration file:"
                    echo "  $staged"
                    failed=1
                fi
            fi

            quarantine="$USER_INTEGRATION_BACKUP_DIR/rollback-new-$i"

            if [[ -e "$quarantine" || -L "$quarantine" ]]; then
                if [[ -f "$quarantine" && ! -L "$quarantine" ]] &&
                    current_id="$(stat -c '%d:%i' -- "$quarantine")" &&
                    [[ "$current_id" == "$installed_id" ]]; then
                    rm -- "$quarantine" || failed=1
                else
                    echo "WARNING: Unexpected file in integration quarantine:"
                    echo "  $quarantine"
                    failed=1
                fi
            fi
        done
    fi

    if [[ "$failed" -eq 0 ]]; then
        if rmdir -- "$USER_INTEGRATION_BACKUP_DIR"; then
            USER_INTEGRATION_BACKUP_DIR=""
            USER_INTEGRATION_INSTALLED=false
            USER_INTEGRATION_MUTATION_STARTED=false
        else
            echo "WARNING: Could not remove the integration transaction directory."
            failed=1
        fi
    fi

    return "$failed"
}

commit_appimage_installation() {
    echo "==> Verifying AppImage installation before commit..."

    local i
    local target
    local backup
    local staged
    local current_id
    local expected_id
    local failed=0

    # Verify the complete transaction structure before using any entry.
    if [[ ${#USER_INTEGRATION_TARGETS[@]} -ne 3 ||
        ${#USER_INTEGRATION_BACKUPS[@]} -ne 3 ||
        ${#USER_INTEGRATION_STAGED[@]} -ne 3 ||
        ${#USER_INTEGRATION_EXISTED[@]} -ne 3 ||
        ${#USER_INTEGRATION_ORIGINAL_IDS[@]} -ne 3 ||
        ${#USER_INTEGRATION_INSTALLED_IDS[@]} -ne 3 ||
        ${#USER_INTEGRATION_STAGED_IDS[@]} -ne 3 ]]; then
        echo "ERROR: Incomplete user integration transaction."
        return 1
    fi

    # Validate the transaction directory before accessing its contents.
    if [[ -z "$USER_INTEGRATION_BACKUP_DIR" ||
        ! -d "$USER_INTEGRATION_BACKUP_DIR" ||
        -L "$USER_INTEGRATION_BACKUP_DIR" ]]; then
        echo "ERROR: Integration transaction directory is missing or invalid."
        return 1
    fi

    local transaction_realpath

    if ! transaction_realpath="$(realpath -e -- "$USER_INTEGRATION_BACKUP_DIR")" ||
        [[ "$(dirname -- "$transaction_realpath")" != "$USER_DATA_DIR" ]]; then
        echo "ERROR: Integration transaction directory is outside the XDG data directory."
        return 1
    fi

    for i in 0 1 2; do
        if [[ "${USER_INTEGRATION_BACKUPS[i]}" != "$USER_INTEGRATION_BACKUP_DIR/original-$i" ||
            "${USER_INTEGRATION_STAGED[i]}" != "$USER_INTEGRATION_BACKUP_DIR/new-$i" ||
            -z "${USER_INTEGRATION_INSTALLED_IDS[i]}" ||
            "${USER_INTEGRATION_STAGED_IDS[i]}" != "${USER_INTEGRATION_INSTALLED_IDS[i]}" ]]; then
            echo "ERROR: Invalid integration transaction entry."
            return 1
        fi

        if [[ "${USER_INTEGRATION_EXISTED[i]}" != true &&
            "${USER_INTEGRATION_EXISTED[i]}" != false ]]; then
            echo "ERROR: Invalid original-file transaction state."
            return 1
        fi

        if [[ "${USER_INTEGRATION_EXISTED[i]}" == true &&
            -z "${USER_INTEGRATION_ORIGINAL_IDS[i]}" ]]; then
            echo "ERROR: Missing original-file identity."
            return 1
        fi

        target="${USER_INTEGRATION_TARGETS[i]}"
        backup="${USER_INTEGRATION_BACKUPS[i]}"
        staged="${USER_INTEGRATION_STAGED[i]}"
        expected_id="${USER_INTEGRATION_INSTALLED_IDS[i]}"

        if [[ ! -f "$target" || -L "$target" ]] ||
            ! current_id="$(stat -c '%d:%i' -- "$target")" ||
            [[ "$current_id" != "$expected_id" ]]; then
            echo "ERROR: Installed integration file failed verification:"
            echo "  $target"
            return 1
        fi

        if [[ -e "$staged" || -L "$staged" ]]; then
            echo "ERROR: A staged integration file remains:"
            echo "  $staged"
            return 1
        fi

        if [[ "${USER_INTEGRATION_EXISTED[i]}" == true ]]; then
            expected_id="${USER_INTEGRATION_ORIGINAL_IDS[i]}"

            if [[ ! -f "$backup" || -L "$backup" ]] ||
                ! current_id="$(stat -c '%d:%i' -- "$backup")" ||
                [[ "$current_id" != "$expected_id" ]]; then
                echo "ERROR: Original integration backup failed verification:"
                echo "  $backup"
                return 1
            fi
        elif [[ -e "$backup" || -L "$backup" ]]; then
            echo "ERROR: Unexpected integration backup exists:"
            echo "  $backup"
            return 1
        fi
    done

    # Retain rollback capability until every user file has been verified.
    if [[ "$RULE_UPDATED" == true ]]; then
        if [[ -z "$RULE_BACKUP" ]] ||
            ! sudo test -f "$RULE_BACKUP" ||
            sudo test -L "$RULE_BACKUP"; then
            echo "ERROR: The previous Polkit rule backup is missing or invalid."
            return 1
        fi

        if ! sudo rm -- "$RULE_BACKUP"; then
            echo "ERROR: Could not remove the previous Polkit rule backup."
            return 1
        fi

        RULE_BACKUP=""
        RULE_UPDATED=false
    fi

    # From this point onward, the AppImage installation is committed.
    APPIMAGE_INSTALL_COMMITTED=true

    echo "==> AppImage installation committed."

    # Original files are no longer needed for rollback. Remove only
    # backups whose identities still match the recorded originals.
    for i in 0 1 2; do
        [[ "${USER_INTEGRATION_EXISTED[i]}" == true ]] || continue

        backup="${USER_INTEGRATION_BACKUPS[i]}"
        expected_id="${USER_INTEGRATION_ORIGINAL_IDS[i]}"

        if [[ -f "$backup" && ! -L "$backup" ]] &&
            current_id="$(stat -c '%d:%i' -- "$backup")" &&
            [[ "$current_id" == "$expected_id" ]]; then
            if ! rm -- "$backup"; then
                echo "WARNING: Could not remove original integration backup:"
                echo "  $backup"
                failed=1
            fi
        else
            echo "WARNING: Original integration backup changed; preserving it:"
            echo "  $backup"
            failed=1
        fi
    done

    if [[ "$failed" -eq 0 ]]; then
        if rmdir -- "$USER_INTEGRATION_BACKUP_DIR"; then
            USER_INTEGRATION_BACKUP_DIR=""
            USER_INTEGRATION_MUTATION_STARTED=false
        else
            echo "WARNING: Could not remove the integration transaction directory."
            echo "  $USER_INTEGRATION_BACKUP_DIR"
        fi
    else
        echo "WARNING: Some transaction backups were retained for safety."
    fi

    return 0
}

prepare_native_migration() {
    echo "==> Preparing native installation migration..."

    local target
    local parent
    local current
    local component
    local file_id
    local datadir
    local -a candidates=()
    local -a components=()

    NATIVE_MIGRATION_TARGETS=()
    NATIVE_MIGRATION_IDS=()

    if [[ "$NATIVE_VALID" != true ||
        -z "$NATIVE_APP" ||
        -z "$NATIVE_BINDIR" ||
        -z "$NATIVE_BACKEND" ||
        -z "$NATIVE_POLICY" ]]; then
        echo "ERROR: Native installation information is incomplete."
        return 1
    fi

    # Derive the data directory from the detected native policy path.
    datadir="$(dirname -- "$(dirname -- "$(dirname -- "$NATIVE_POLICY")")")"

    candidates=(
        "$NATIVE_APP"
        "$NATIVE_UNINSTALLER"
        "$NATIVE_CONFIGURER"
        "$NATIVE_PERMISSION_REMOVER"
        "$NATIVE_BACKEND"
        "$NATIVE_POLICY"
        "$datadir/applications/kde-keycast.desktop"
        "$datadir/icons/hicolor/scalable/apps/keycast-light.svg"
        "$datadir/icons/hicolor/scalable/apps/keycast-dark.svg"
        "$datadir/metainfo/com.github.hgmarcury.kde-keycast.metainfo.xml"
    )

    # The legacy rule is optional, but must be safe if present.
    target="$datadir/kde-keycast/polkit/10-com.github.hgmarcury.kde-keycast.rules"

    if sudo test -e "$target" || sudo test -L "$target"; then
        candidates+=("$target")
    fi

    for target in "${candidates[@]}"; do
        if [[ "$target" != /* ]]; then
            echo "ERROR: Native migration path is not absolute:"
            echo "  $target"
            return 1
        fi

        parent="$(dirname -- "$target")"
        current="/"
        IFS='/' read -r -a components <<<"${parent#/}"

        # Reject symbolic links or missing components in the parent path.
        for component in "${components[@]}"; do
            [[ -n "$component" ]] || continue
            current="${current%/}/$component"

            if ! sudo test -d "$current" ||
                sudo test -L "$current"; then
                echo "ERROR: Invalid native migration directory:"
                echo "  $current"
                return 1
            fi
        done

        if ! sudo test -f "$target" ||
            sudo test -L "$target"; then
            echo "ERROR: Missing, non-regular or symbolic-link native file:"
            echo "  $target"
            return 1
        fi

        if ! file_id="$(sudo stat -c '%d:%i' -- "$target")" ||
            [[ -z "$file_id" ]]; then
            echo "ERROR: Could not identify native file:"
            echo "  $target"
            return 1
        fi

        NATIVE_MIGRATION_TARGETS+=("$target")
        NATIVE_MIGRATION_IDS+=("$file_id")
    done

    if [[ ${#NATIVE_MIGRATION_TARGETS[@]} -ne ${#NATIVE_MIGRATION_IDS[@]} ]]; then
        echo "ERROR: Native migration inventory is inconsistent."
        return 1
    fi

    echo "==> Native migration preflight passed."
    echo "    Files validated: ${#NATIVE_MIGRATION_TARGETS[@]}"
}

rollback_native_migration() {
    echo "==> Restoring native installation files..."

    local i
    local target
    local backup
    local expected_id
    local current_id
    local failed=0
    local backup_dir

    for ((i = ${#NATIVE_MIGRATION_TARGETS[@]} - 1; i >= 0; i--)); do
        [[ "${NATIVE_MIGRATION_MOVED[i]:-false}" == true ]] || continue

        target="${NATIVE_MIGRATION_TARGETS[i]}"
        backup="${NATIVE_MIGRATION_BACKUPS[i]}"
        expected_id="${NATIVE_MIGRATION_IDS[i]}"

        if ! sudo test -f "$backup" ||
            sudo test -L "$backup" ||
            ! current_id="$(sudo stat -c '%d:%i' -- "$backup")" ||
            [[ "$current_id" != "$expected_id" ]]; then
            echo "ERROR: Native migration backup is missing or has changed:"
            echo "  $backup"
            failed=1
            continue
        fi

        if sudo test -e "$target" || sudo test -L "$target"; then
            echo "ERROR: Cannot restore native file because its target is occupied:"
            echo "  $target"
            failed=1
            continue
        fi

        if ! sudo mv -nT -- "$backup" "$target" ||
            sudo test -e "$backup" ||
            ! sudo test -f "$target" ||
            sudo test -L "$target" ||
            ! current_id="$(sudo stat -c '%d:%i' -- "$target")" ||
            [[ "$current_id" != "$expected_id" ]]; then
            echo "ERROR: Could not safely restore native file:"
            echo "  $target"
            failed=1
            continue
        fi

        NATIVE_MIGRATION_MOVED[i]=false
        echo "==> Restored: $target"
    done

    if [[ "$failed" -eq 0 ]]; then
        for backup_dir in "${NATIVE_MIGRATION_BACKUP_DIRS[@]}"; do
            if [[ -n "$backup_dir" ]] &&
                ! sudo rmdir -- "$backup_dir" 2>/dev/null; then
                if sudo test -d "$backup_dir" && ! sudo test -L "$backup_dir"; then
                    echo "WARNING: Native migration backup directory was retained:"
                    echo "  $backup_dir"
                fi
            fi
        done
        NATIVE_MIGRATION_MUTATION_STARTED=false
    else
        echo "WARNING: Some native migration backups were retained for recovery."
    fi

    return "$failed"
}

retire_native_installation() {
    if [[ "$NATIVE_MIGRATION_COMMITTED" == true ]]; then
        echo "WARNING: Native migration is already committed; rollback skipped."
        return 0
    fi
    echo "==> Retiring the native installation..."

    local i
    local target
    local parent
    local backup_dir
    local backup
    local current_id
    local expected_id
    local failed=0

    if [[ ${#NATIVE_MIGRATION_TARGETS[@]} -eq 0 ||
        ${#NATIVE_MIGRATION_TARGETS[@]} -ne ${#NATIVE_MIGRATION_IDS[@]} ]]; then
        echo "ERROR: Native migration inventory is incomplete."
        return 1
    fi

    NATIVE_MIGRATION_BACKUP_DIRS=()
    NATIVE_MIGRATION_BACKUPS=()
    NATIVE_MIGRATION_MOVED=()
    NATIVE_MIGRATION_COMMITTED=false

    # Prepare one private backup directory beside each native file.
    for ((i = 0; i < ${#NATIVE_MIGRATION_TARGETS[@]}; i++)); do
        target="${NATIVE_MIGRATION_TARGETS[i]}"
        parent="$(dirname -- "$target")"
        expected_id="${NATIVE_MIGRATION_IDS[i]}"

        if ! sudo test -f "$target" ||
            sudo test -L "$target" ||
            ! current_id="$(sudo stat -c '%d:%i' -- "$target")" ||
            [[ "$current_id" != "$expected_id" ]]; then
            echo "ERROR: Native file changed since preflight:"
            echo "  $target"
            failed=1
            break
        fi

        if ! backup_dir="$(sudo mktemp -d -- "$parent/.kde-keycast-native-migration.XXXXXX")"; then
            echo "ERROR: Could not create a native migration backup directory:"
            echo "  $parent"
            failed=1
            break
        fi

        NATIVE_MIGRATION_BACKUP_DIRS+=("$backup_dir")
        NATIVE_MIGRATION_BACKUPS+=("$backup_dir/$(basename -- "$target")")
        NATIVE_MIGRATION_MOVED+=(false)

        if ! sudo test -d "$backup_dir" ||
            sudo test -L "$backup_dir" ||
            [[ "$(sudo realpath -e -- "$backup_dir")" != "$backup_dir" ]]; then
            echo "ERROR: Invalid native migration backup directory:"
            echo "  $backup_dir"
            failed=1
            break
        fi
    done

    if [[ "$failed" -ne 0 ]]; then
        for backup_dir in "${NATIVE_MIGRATION_BACKUP_DIRS[@]}"; do
            sudo rmdir -- "$backup_dir" 2>/dev/null || true
        done
        return 1
    fi

    # Revalidate the entire inventory before moving the first file.
    for ((i = 0; i < ${#NATIVE_MIGRATION_TARGETS[@]}; i++)); do
        target="${NATIVE_MIGRATION_TARGETS[i]}"
        backup="${NATIVE_MIGRATION_BACKUPS[i]}"
        expected_id="${NATIVE_MIGRATION_IDS[i]}"

        if ! sudo test -f "$target" ||
            sudo test -L "$target" ||
            sudo test -f "$backup" ||
            sudo test -L "$backup" ||
            ! current_id="$(sudo stat -c '%d:%i' -- "$target")" ||
            [[ "$current_id" != "$expected_id" ]]; then
            echo "ERROR: Native migration pre-move verification failed:"
            echo "  $target"
            failed=1
            break
        fi
    done

    if [[ "$failed" -ne 0 ]]; then
        for backup_dir in "${NATIVE_MIGRATION_BACKUP_DIRS[@]}"; do
            sudo rmdir -- "$backup_dir" 2>/dev/null || true
        done
        return 1
    fi

    # From here onward, every move must be recoverable.
    NATIVE_MIGRATION_MUTATION_STARTED=true

    for ((i = 0; i < ${#NATIVE_MIGRATION_TARGETS[@]}; i++)); do
        target="${NATIVE_MIGRATION_TARGETS[i]}"
        backup="${NATIVE_MIGRATION_BACKUPS[i]}"
        expected_id="${NATIVE_MIGRATION_IDS[i]}"

        if ! current_id="$(sudo stat -c '%d:%i' -- "$target")" ||
            [[ "$current_id" != "$expected_id" ]] ||
            ! sudo mv -nT -- "$target" "$backup"; then
            echo "ERROR: Could not move native file:"
            echo "  $target"
            failed=1
            break
        fi

        NATIVE_MIGRATION_MOVED[i]=true

        if sudo test -e "$target" ||
            sudo test -L "$target" ||
            ! sudo test -f "$backup" ||
            sudo test -L "$backup" ||
            ! current_id="$(sudo stat -c '%d:%i' -- "$backup")" ||
            [[ "$current_id" != "$expected_id" ]]; then
            echo "ERROR: Native file movement failed verification:"
            echo "  $target"
            failed=1
            break
        fi

        echo "==> Retired: $target"
    done

    if [[ "$failed" -ne 0 ]]; then
        NATIVE_MIGRATION_FAILED=true
        rollback_native_migration || true
        return 1
    fi

    # All native files have been retired successfully.
    # From this point onward, rollback is no longer safe because
    # backup deletion may be partial.
    NATIVE_MIGRATION_COMMITTED=true
    NATIVE_MIGRATION_MUTATION_STARTED=false

    # All native files have been moved and verified. The migration is complete.
    echo "==> Native installation successfully retired."

    # Remove only backups whose identities still match the original files.
    for ((i = 0; i < ${#NATIVE_MIGRATION_BACKUPS[@]}; i++)); do
        backup="${NATIVE_MIGRATION_BACKUPS[i]}"
        expected_id="${NATIVE_MIGRATION_IDS[i]}"

        if sudo test -f "$backup" &&
            ! sudo test -L "$backup" &&
            current_id="$(sudo stat -c '%d:%i' -- "$backup")" &&
            [[ "$current_id" == "$expected_id" ]]; then
            if ! sudo rm -- "$backup"; then
                echo "WARNING: Could not remove migration backup:"
                echo "  $backup"
            fi
        else
            echo "WARNING: Migration backup changed; preserving it:"
            echo "  $backup"
        fi
    done

    for backup_dir in "${NATIVE_MIGRATION_BACKUP_DIRS[@]}"; do
        if ! sudo rmdir -- "$backup_dir" 2>/dev/null; then
            if sudo test -d "$backup_dir" && ! sudo test -L "$backup_dir"; then
                echo "WARNING: Native migration backup directory was retained:"
                echo "  $backup_dir"
            fi
        fi
    done

    return 0
}

cleanup_on_exit() {
    local status=$?
    trap - EXIT

    # Recover an interrupted native migration before leaving.
    if [[ "$NATIVE_MIGRATION_MUTATION_STARTED" == true &&
        "$NATIVE_MIGRATION_COMMITTED" != true ]]; then
        echo "WARNING: An unfinished native migration was detected."

        if ! rollback_native_migration; then
            echo "ERROR: Native migration recovery was incomplete."
            NATIVE_MIGRATION_FAILED=true
            status=1
        fi
    fi

    if [[ "$status" -ne 0 ]]; then
        if [[ "$APPIMAGE_INSTALL_COMMITTED" == true ]]; then
            if [[ "$NATIVE_MIGRATION_FAILED" == true ]]; then
                echo "ERROR: AppImage is installed, but native migration failed."
            else
                echo "ERROR: A post-installation step failed."
                echo "The AppImage installation remains committed."
            fi
        else
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

            if [[ "$REGISTRY_INSTALLED" == true ||
                -n "$REGISTRY_TEMP" ||
                "$STATE_DIR_CREATED" == true ]]; then
                rollback_appimage_user_data_registry ||
                    echo "WARNING: XDG registration rollback incomplete."
            fi

            if [[ "$USER_INTEGRATION_INSTALLED" == true ||
                -n "$USER_INTEGRATION_BACKUP_DIR" ||
                "$USER_INTEGRATION_MUTATION_STARTED" == true ||
                ${#USER_INTEGRATION_TARGETS[@]} -gt 0 ||
                ${#USER_INTEGRATION_STAGED[@]} -gt 0 ]]; then
                rollback_appimage_user_integration ||
                    echo "WARNING: User integration rollback incomplete."
            fi
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
        IFS='|' read -r source target mode flag <<<"$entry"

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

                cat >"$legacy_rule" <<'LEGACY_RULE'
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
        BACKEND_INSTALLED) BACKEND_INSTALLED=true ;;
        POLICY_INSTALLED) POLICY_INSTALLED=true ;;
        CONFIGURE_INSTALLED) CONFIGURE_INSTALLED=true ;;
        REMOVE_INSTALLED) REMOVE_INSTALLED=true ;;
        RULE_INSTALLED) RULE_INSTALLED=true ;;
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
    read -r -p "Migrate from the native installation to this AppImage? [y/N] " answer

    case "$answer" in
    [yY] | [yY][eE][sS] | [sS] | [sS][iI])
        MIGRATE_NATIVE=true
        ;;
    *)
        echo "Migration cancelled. No AppImage integration was installed."
        exit 1
        ;;
    esac
else
    MIGRATE_NATIVE=false
fi

if [[ "$MIGRATE_NATIVE" == true ]]; then
    if ! prepare_native_migration; then
        echo "ERROR: Native migration preflight failed."
        exit 1
    fi
fi

if ! install_appimage_system_files; then
    echo "ERROR: AppImage system-file installation failed."
    echo "The native installation, if present, has been preserved."
    exit 1
fi

if ! register_appimage_user_data_dir; then
    echo "ERROR: AppImage user-data registration failed."
    exit 1
fi

if ! install_appimage_user_integration; then
    echo "ERROR: AppImage user integration installation failed."
    exit 1
fi

if ! commit_appimage_installation; then
    echo "ERROR: AppImage installation could not be committed."
    exit 1
fi

if [[ "$MIGRATE_NATIVE" == true ]]; then
    if ! retire_native_installation; then
        echo "ERROR: Native installation could not be retired."
        exit 1
    fi
fi
