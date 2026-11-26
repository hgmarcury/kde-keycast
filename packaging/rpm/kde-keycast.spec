
Name:           kde-keycast
Version:        1.1.0
%global commit 39bff0ff8e34fa076295f653035bc9cef81b4368
Release:        1
Summary:        Keyboard and mouse visualizer for KDE Plasma
License:        GPL-3.0-or-later AND Apache-2.0
URL:            https://github.com/hgmarcury/kde-keycast
Source0:        https://github.com/hgmarcury/kde-keycast/archive/%{commit}.tar.gz
Patch0:         0001-enable-pie-for-executables.patch
Patch1:         0002-use-portable-bash-shebang.patch

BuildRequires:  cmake
BuildRequires:  gcc-c++
BuildRequires:  extra-cmake-modules
BuildRequires:  kwin6-devel
BuildRequires:  qt6-base-devel
BuildRequires:  qt6-tools-devel
BuildRequires:  qt6-linguist-devel
BuildRequires:  qt6-declarative-devel
BuildRequires:  kf6-kconfig-devel
BuildRequires:  kf6-kcoreaddons-devel
BuildRequires:  kf6-kwindowsystem-devel
BuildRequires:  libinput-devel
BuildRequires:  systemd-devel
BuildRequires:  libevdev-devel
BuildRequires:  libxkbcommon-devel
BuildRequires:  wayland-devel
BuildRequires:  pkgconf-pkg-config

Requires:       pkexec
Requires:       qt6-tools-qdbus
Requires:       kf6-kconfig
Requires:       shadow

%description
KDE KeyCast displays keyboard and mouse input events on screen.
It includes native KWin integration for KDE Plasma on Wayland,
with a KWin script and a cursor-tracking effect.

%prep
%autosetup -n kde-keycast-%{commit} -p1

%build
%cmake \
    -DCMAKE_INSTALL_PREFIX=%{_prefix} \
    -DKDE_KEYCAST_REQUIRE_KWIN_EFFECT=ON \
    -DKDE_INSTALL_USE_QT_SYS_PATHS=ON \
    -DKDE_INSTALL_LIBEXECDIR=%{_libexecdir} \
    -DLIBINPUT_INCLUDE_DIR=%{_includedir}/libinput

%cmake_build

%install
%cmake_install

# Keep the vendored backend license under a distinct filename.
mkdir -p %{buildroot}%{_datadir}/licenses/%{name}
install -m 644 vendor/showmethekey-cli/LICENSE \
    %{buildroot}%{_datadir}/licenses/%{name}/LICENSE.vendor-showmethekey

# The package manager owns installation and removal.
rm -f %{buildroot}%{_bindir}/uninstall-kde-keycast

%post
sessions=$(/usr/bin/loginctl list-sessions --no-legend 2>/dev/null) || sessions=

printf '%s\n' "$sessions" | while read -r session rest; do
    [ -n "$session" ] || continue

    properties=$(/usr/bin/loginctl show-session "$session" \
        -p User -p Desktop -p Type -p Active -p State 2>/dev/null) || continue

    session_user=$(printf '%s\n' "$properties" | /usr/bin/sed -n 's/^User=//p')
    session_desktop=$(printf '%s\n' "$properties" | /usr/bin/sed -n 's/^Desktop=//p')
    session_type=$(printf '%s\n' "$properties" | /usr/bin/sed -n 's/^Type=//p')
    session_active=$(printf '%s\n' "$properties" | /usr/bin/sed -n 's/^Active=//p')
    session_state=$(printf '%s\n' "$properties" | /usr/bin/sed -n 's/^State=//p')

    case "$session_user" in
        ''|*[!0-9]*) continue ;;
    esac

    [ "$session_desktop" = "KDE" ] || continue
    [ "$session_active" = "yes" ] || continue
    [ "$session_state" = "active" ] || continue

    case "$session_type" in
        wayland|x11) ;;
        *) continue ;;
    esac

    runtime_dir="/run/user/$session_user"
    [ -S "$runtime_dir/bus" ] || continue

    echo "==> Requesting KWin reconfiguration for session $session (UID $session_user)..."

    if /usr/sbin/runuser -u "$session_user" -- /usr/bin/env \
        LC_ALL=C.UTF-8 \
        XDG_RUNTIME_DIR="$runtime_dir" \
        DBUS_SESSION_BUS_ADDRESS="unix:path=$runtime_dir/bus" \
        /usr/bin/qdbus6 org.kde.KWin /KWin reconfigure; then
        echo "==> KWin reconfiguration requested successfully."
    else
        echo "WARNING: Could not reconfigure KWin in session $session."
    fi
done

:

%preun
if [ "$1" -eq 0 ]; then
    APPIMAGE_DIR="/usr/lib/kde-keycast-appimage"
    APPIMAGE_POLICY="/usr/share/polkit-1/actions/com.github.hgmarcury.kde-keycast-appimage.policy"

    present=0
    valid=0

    APPIMAGE_EXECUTABLES="
    $APPIMAGE_DIR/showmethekey-cli
    $APPIMAGE_DIR/configure-kde-keycast-permissions
    $APPIMAGE_DIR/remove-kde-keycast-permissions
    "

    APPIMAGE_DATA_FILES="
    $APPIMAGE_DIR/10-com.github.hgmarcury.kde-keycast.rules
    $APPIMAGE_POLICY
    "

    for file in $APPIMAGE_EXECUTABLES; do
        if [ -e "$file" ] || [ -L "$file" ]; then
            present=$((present + 1))
            if [ -f "$file" ] && [ ! -L "$file" ] && [ -x "$file" ]; then
                valid=$((valid + 1))
            fi
        fi
    done

    for file in $APPIMAGE_DATA_FILES; do
        if [ -e "$file" ] || [ -L "$file" ]; then
            present=$((present + 1))
            if [ -f "$file" ] && [ ! -L "$file" ]; then
                valid=$((valid + 1))
            fi
        fi
    done

    if [ "$present" -eq 0 ]; then
        echo "==> No AppImage installation detected; removing native permissions..."
        /usr/bin/remove-kde-keycast-permissions --apply
    elif [ "$present" -eq 5 ] && [ "$valid" -eq 5 ]; then
        echo "==> AppImage installation detected; preserving shared permissions."
    else
        echo "ERROR: An incomplete or unverified AppImage installation was detected."
        echo "Shared permissions have been preserved."
        echo "Resolve the AppImage installation before removing KDE KeyCast RPM."
        exit 1
    fi
fi

%postun
if [ "$1" -eq 0 ]; then
    if [ -d /var/lib/kde-keycast/permissions ]; then
        rmdir --ignore-fail-on-non-empty /var/lib/kde-keycast/permissions
    fi

    if [ -d /var/lib/kde-keycast ]; then
        rmdir --ignore-fail-on-non-empty /var/lib/kde-keycast
    fi
fi

%files
%license LICENSE
%license THIRD-PARTY-NOTICES
%license %{_datadir}/licenses/%{name}/LICENSE.vendor-showmethekey
%doc README.md
%doc CONTRIBUTING.md

%{_bindir}/kde-keycast
%{_bindir}/configure-kde-keycast-permissions
%{_bindir}/remove-kde-keycast-permissions

%{_libexecdir}/kde-keycast/showmethekey-cli

%dir %{_datadir}/kwin/scripts/kde-keycast
%dir %{_datadir}/kwin/scripts/kde-keycast/contents
%dir %{_datadir}/kwin/scripts/kde-keycast/contents/locale
%dir %{_datadir}/kwin/scripts/kde-keycast/contents/locale/es
%dir %{_datadir}/kwin/scripts/kde-keycast/contents/locale/es/LC_MESSAGES
%dir %{_datadir}/kwin/scripts/kde-keycast/contents/ui

%{_datadir}/kwin/scripts/kde-keycast/metadata.json
%{_datadir}/kwin/scripts/kde-keycast/contents/ui/main.qml
%{_datadir}/kwin/scripts/kde-keycast/contents/locale/es/LC_MESSAGES/kde-keycast.po
%lang(es) %{_datadir}/kwin/scripts/kde-keycast/contents/locale/es/LC_MESSAGES/kde-keycast.mo

%{_libdir}/qt6/plugins/kwin/effects/plugins/kde-keycast-cursor.so
%{_libdir}/qt6/plugins/kwin/effects/plugins/kde-keycast-cursor.installed

%{_datadir}/polkit-1/actions/com.github.hgmarcury.kde-keycast.policy
%{_datadir}/kde-keycast/polkit/10-com.github.hgmarcury.kde-keycast.rules

%{_datadir}/applications/kde-keycast.desktop

%{_datadir}/icons/hicolor/scalable/apps/keycast-light.svg
%{_datadir}/icons/hicolor/scalable/apps/keycast-dark.svg

%{_datadir}/metainfo/com.github.hgmarcury.kde-keycast.metainfo.xml

%{_datadir}/kde-keycast/translations/kde-keycast_es.qm

%changelog
* Wed Nov 25 2026 Marc G. <hgmarcury@gmail.com> - 1.1.0-1
- Initial openSUSE Tumbleweed RPM packaging
