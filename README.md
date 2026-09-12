# KDE KeyCast

Pequeño indicador visual de teclado para Linux/KDE.

## Estado actual

- Captura eventos de teclado mediante showmethekey-cli.
- Utiliza XKB para resolver correctamente las teclas según el layout.
- Muestra caracteres transformados por Shift.
- Muestra combinaciones con Ctrl, Alt, AltGr, Meta y Shift cuando corresponde.
- Shift + texto se muestra como el carácter resultante.
- Shift + teclas especiales se muestra como combinación.
- Las flechas y otras teclas especiales tienen representación visual.
- Incluye modo compacto.

## Compilación

    cmake --build build -j$(nproc)

## Estado de Git

El repositorio contiene únicamente los archivos fuente necesarios.
Los backups históricos locales están excluidos mediante .gitignore.
