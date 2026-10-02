
# KDE KeyCast AppImage

Este directorio contiene los scripts y herramientas utilizados para generar una AppImage de KDE KeyCast.

## Contenido previsto

La AppImage debe incluir los componentes necesarios para ejecutar KDE KeyCast de forma independiente de la instalación del proyecto, incluyendo:

* `kde-keycast`
* el backend `showmethekey-cli` incluido y modificado para KDE KeyCast
* las bibliotecas de tiempo de ejecución necesarias
* la integración de escritorio de KDE KeyCast
* los iconos de KDE KeyCast

El backend incluido se utiliza directamente y no se sustituye por una instalación externa de `showmethekey-cli`.

## Construcción

El script principal para generar la AppImage es:

```text
packaging/appimage/build-appimage.sh
```

Las herramientas auxiliares utilizadas durante la construcción se mantienen fuera del repositorio cuando corresponde y no forman parte de las dependencias de ejecución de KDE KeyCast.

## Integración con KWin

La integración con KWin se instala separadamente en la configuración del usuario de KDE.

Esto es necesario porque el script de KWin debe registrarse en la configuración de KWin del usuario y no simplemente copiarse dentro de la AppImage.

## Dependencias y licencias

La AppImage puede contener bibliotecas de terceros necesarias para su ejecución. Estas bibliotecas mantienen sus respectivas licencias.

El código propio de KDE KeyCast está distribuido bajo GPL-3.0-or-later.

El backend incluido de ShowMeTheKey está distribuido bajo Apache License 2.0. Su licencia se conserva en:

```text
vendor/showmethekey-cli/LICENSE
```

Las herramientas utilizadas para construir la AppImage no deben confundirse con las dependencias de ejecución incluidas en la AppImage.
