
# KDE KeyCast

Indicador visual de teclado y ratón para Linux/KDE, pensado especialmente para presentaciones, tutoriales y screencasts.

KDE KeyCast muestra visualmente las teclas y combinaciones pulsadas, utilizando XKB para interpretar correctamente el teclado según el layout activo.

## Características

* Captura eventos de teclado mediante el backend incluido de ShowMeTheKey.
* Utiliza XKB para resolver correctamente las teclas según el layout del sistema.
* Muestra caracteres transformados por `Shift`.
* Muestra combinaciones con `Ctrl`, `Alt`, `AltGr`, `Meta` y `Shift` cuando corresponde.
* Representa las teclas especiales mediante nombres o símbolos apropiados.
* Detecta y muestra eventos de ratón, incluidos los desplazamientos de la rueda.
* Incluye un modo compacto para mostrar únicamente las combinaciones relevantes.
* Integración con KDE/Plasma mediante un icono en la bandeja del sistema.
* Incluye integración opcional con KWin.

## Estado del proyecto

KDE KeyCast se encuentra actualmente en desarrollo.

El código principal ha sido reorganizado en componentes independientes para facilitar su mantenimiento y futuras mejoras.

La distribución y el proceso de instalación continúan en desarrollo, especialmente para facilitar la instalación en distintas distribuciones KDE/Plasma.

## Dependencias

KDE KeyCast utiliza, entre otras, las siguientes tecnologías:

* Qt 6
* KDE Frameworks / KDE ECM
* Wayland
* XKB / libxkbcommon
* ShowMeTheKey como backend de captura de entrada

El backend de ShowMeTheKey utilizado por KDE KeyCast está incluido en el propio proyecto y contiene modificaciones específicas para detectar eventos de rueda del ratón.

KDE KeyCast utiliza siempre este backend incluido y no depende de una instalación externa de `showmethekey-cli`.

## Compilación

El proyecto utiliza CMake.

Una vez configurado el directorio de compilación:

```bash
cmake --build build -j$(nproc)
```

Para instalar en un directorio de prueba:

```bash
cmake --install build --prefix "$(pwd)/build/test-install"
```

Los requisitos concretos de desarrollo y el proceso de instalación para distintas distribuciones se documentarán conforme avance el sistema de empaquetado.

## AppImage

El proyecto incluye herramientas y scripts para generar una AppImage.

La documentación específica del proceso se encuentra en:

```text
packaging/appimage/README.md
```

La AppImage está pensada para incluir KDE KeyCast junto con el backend de ShowMeTheKey utilizado por el proyecto y las dependencias necesarias para su ejecución.

## Licencia

El código propio de KDE KeyCast está distribuido bajo:

**GNU General Public License v3.0 or later (GPL-3.0-or-later)**

El texto completo de la licencia se encuentra en:

```text
LICENSE
```

El backend incluido de ShowMeTheKey es un componente separado distribuido bajo:

**Apache License 2.0**

Su licencia original se conserva en:

```text
vendor/showmethekey-cli/LICENSE
```

El código del backend ha sido modificado para KDE KeyCast, incluyendo soporte para eventos físicos de rueda del ratón.

Las bibliotecas y demás componentes de terceros conservan sus respectivas licencias.

## Copyright

Copyright © 2026 Marc G.

KDE KeyCast no es un proyecto oficial de KDE ni está afiliado oficialmente con KDE e.V.

## Contribuciones

Las contribuciones, correcciones y sugerencias son bienvenidas.

Antes de realizar cambios importantes, se recomienda abrir una discusión o issue para explicar la propuesta.

## Proyecto

KDE KeyCast está desarrollado como un proyecto independiente para Linux/KDE.

El objetivo es proporcionar un indicador de entrada sencillo y adecuado para presentaciones, grabaciones y screencasts.
