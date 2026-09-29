# KDE KeyCast AppImage

This directory contains the files used to build the KDE KeyCast AppImage.

The AppImage must include:

- kde-keycast
- the bundled showmethekey-cli fork
- required runtime libraries
- KDE KeyCast desktop integration
- KDE KeyCast icon

The KWin script is installed separately in the user's KDE configuration,
because it must be registered with KWin.
