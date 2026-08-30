# QLC+ unter Windows bauen (lokal, ohne Installer)

Diese Anleitung beschreibt den auf diesem Rechner eingerichteten Build – als
Windows-Pendant zum `start-qlcplus.sh`-Workflow (dort: Fedora-Toolbox/
Distrobox-Container für QLC+4/QLC+5 getrennt; hier: zwei getrennte lokale
CMake-Build-Verzeichnisse aus demselben Grund).

## Voraussetzungen (einmalig)

- **Qt 6.11.1**, MinGW-Kit, unter `C:\Qt\6.11.1\mingw_64`, inkl. folgender
  nachinstallierter Zusatzmodule (über den Qt Maintenance Tool,
  `C:\Qt\MaintenanceTool.exe`):
  - `qt.qt6.6111.addons.qtserialport` – für den QtSerialPort-Pfad des
    dmxusb-Plugins
  - `qt.qt6.6111.addons.qtwebsockets` – für webaccess/qmlui
  - `qt.qt6.6111.addons.qt3d` – für QLC+5 (3D-Bühnenvorschau, `qmlui`)
- **CMake** und **Ninja** (`C:\Qt\Tools\CMake_64`, `C:\Qt\Tools\Ninja`)
- **MinGW 13.1.0** Compiler (`C:\Qt\Tools\mingw1310_64`)
- **pkgconf** als `pkg-config.exe` im PATH (installiert via
  `winget install pkgconf.pkgconf`) – vom Root-`CMakeLists.txt` zwingend
  vorausgesetzt (`find_package(PkgConfig REQUIRED)`), auch wenn nicht jedes
  gefundene `.pc`-Paket tatsächlich gebraucht wird.
- **libusb-1.0** (MinGW64-Binärrelease von libusb.org, entpackt nach
  `C:\libusb`: `include\libusb.h`, `lib\libusb-1.0.a` +
  `lib\libusb-1.0.dll.a`, `bin\libusb-1.0.dll`) plus eine selbst angelegte
  `C:\libusb\lib\pkgconfig\libusb-1.0.pc`, damit `pkg-config` sie findet –
  fürs **udmx**-Plugin (Anyma-uDMX-kompatible Interfaces). Braucht `PKG_CONFIG_PATH`
  wie unten gezeigt.

Alles zusammen in den PATH holen (für jede neue Shell nötig, da die
Bash-Sitzung Umgebungsvariablen nicht zwischen Aufrufen behält):

```bash
export PATH="/c/Program Files/pkgconf 3.0.5:/c/Qt/Tools/mingw1310_64/bin:/c/Qt/Tools/Ninja:/c/Qt/Tools/CMake_64/bin:$PATH"
export PKG_CONFIG_PATH="/c/libusb/lib/pkgconfig"
```

## Bekannte, absichtlich deaktivierte Plugins

In [plugins/CMakeLists.txt](plugins/CMakeLists.txt) ist ein Plugin
auskommentiert, weil seine Abhängigkeit hier nicht verfügbar ist:

- **dmxusb** – braucht das proprietäre FTDI-D2XX-SDK
  (`C:/projects/D2XXSDK`, Lizenz-Download bei ftdichip.com). Betrifft
  Enttec-artige/FTDI-basierte USB-DMX-Interfaces.

**udmx** (Anyma-uDMX-kompatible Interfaces, z. B. mit libusbK-Treiber) ist
seit dem libusb-Setup oben wieder aktiv.

Ohne dmxusb fehlt aktuell FTDI-basierte USB-DMX-Ausgabe; Art-Net, sACN, OSC,
MIDI, HID, ENTTEC-Wing, Peperoni, Loopback und uDMX funktionieren. Wieder
aktivieren: den `#`-Kommentar vor `add_subdirectory(dmxusb)` entfernen,
dann das D2XX-SDK besorgen und neu konfigurieren.

**Wichtig nach jedem Neu-Konfigurieren/Build**: `udmx.dll` liegt nach dem
Bauen unter `build/plugins/udmx/src/udmx.dll` (bzw. `build-qml/...`) und
muss wie die anderen Plugin-DLLs manuell in den `Plugins`-Ordner neben die
`.exe` kopiert werden – zusätzlich braucht es `libusb-1.0.dll` (aus
`C:\libusb\bin`) direkt neben der `.exe` (nicht im `Plugins`-Unterordner).

Außerdem ist in [platforms/CMakeLists.txt](platforms/CMakeLists.txt) das
`windows`-Unterverzeichnis auskommentiert – das ist nur für das
NSIS-Installer-Packaging (`cmake --install`) relevant, das MSYS2-DLLs
voraussetzt, und hat auf das reine Bauen/Starten keinen Einfluss.

## QLC+ 4 (klassische Widgets-UI) bauen

```bash
cmake -G Ninja -S . -B build \
  -DCMAKE_PREFIX_PATH="C:/Qt/6.11.1/mingw_64/lib/cmake" \
  -DCMAKE_BUILD_TYPE=Release \
  -Dqmlui=OFF
cmake --build build
```

## QLC+ 5 (QML-UI, 3D) bauen

```bash
cmake -G Ninja -S . -B build-qml \
  -DCMAKE_PREFIX_PATH="C:/Qt/6.11.1/mingw_64/lib/cmake" \
  -DCMAKE_BUILD_TYPE=Release \
  -Dqmlui=ON
cmake --build build-qml
```

## Einmalig pro Build-Verzeichnis: Resourcen deployen

Weil `cmake --install` hier nie läuft, sucht QLC+ (`QLCFile::systemDirectory`,
schaut relativ neben die `.exe`) vergeblich nach Fixture-Profilen, RGB-
Scripts usw. – Symptom: Fixtures haben keine Kanal-Bezeichnungen/-Icons
(Dimmer/Rot/Grün/Blau/Strobe), weil das passende `.qxf` nicht gefunden wird.
Einmal pro Build-Verzeichnis (überlebt normale Rebuilds, nur nach einem
frischen `build`/`build-qml` nötig):

```bash
cd C:/Users/DJBaka/Documents/GitHub/qlcplus
for target in "build/main" "build-qml/qmlui"; do
  cp -r resources/fixtures "$target/Fixtures"
  cp -r resources/gobos "$target/Gobos"
  cp -r resources/rgbscripts "$target/RGBScripts"
  cp -r resources/inputprofiles "$target/InputProfiles"
  cp -r resources/miditemplates "$target/MidiTemplates"
  cp -r resources/modifierstemplates "$target/ModifiersTemplates"
  cp -r resources/colorfilters "$target/ColorFilters"
  cp -r resources/meshes "$target/Meshes"
  cp -r webaccess/res "$target/Web"
done
```

## Nach jedem Build: Runtime-DLLs deployen

Weil hier nie `cmake --install` läuft (siehe oben), müssen die Qt-DLLs und
die intern gebauten Shared Libraries manuell neben die jeweilige `.exe`
kopiert werden. Für QLC+4 (analog für QLC+5 mit `build-qml`/`qmlui`
statt `build`/`main`):

```bash
cd build/main
"/c/Qt/6.11.1/mingw_64/bin/windeployqt.exe" qlcplus.exe qlcplusengine.dll qlcplusui.dll qlcpluswebaccess.dll --release
cp ../engine/src/qlcplusengine.dll ../ui/src/qlcplusui.dll ../webaccess/src/qlcpluswebaccess.dll .
cp /c/libusb/bin/libusb-1.0.dll .
mkdir -p Plugins
cp ../plugins/*/src/*.dll ../plugins/*/*/*.dll Plugins/ 2>/dev/null
```

Für QLC+5 im QML-Build zusätzlich `--qmldir <repo>/qmlui/qml` an `windeployqt`
übergeben (siehe unten) – sonst bleibt das Fenster weiß, weil die
Qt-eigenen QML-Module (QtQuick, QtQuick.Controls, Qt3D.*, ...) fehlen.

Für QLC+5 fehlt `qlcplusui.dll` (die QML-UI nutzt stattdessen die QML/Qt3D-
Laufzeit), `webaccess` ist dort statisch mitgelinkt (keine eigene DLL), und
`windeployqt` braucht **zwingend** `--qmldir`, sonst fehlen die Qt-eigenen
QML-Module und das Fenster bleibt weiß:

```bash
cd build-qml/qmlui
"/c/Qt/6.11.1/mingw_64/bin/windeployqt.exe" qlcplus-qml.exe --qmldir ../../qmlui/qml --release
cp ../engine/src/qlcplusengine.dll .
cp /c/libusb/bin/libusb-1.0.dll .
mkdir -p Plugins
cp ../plugins/*/src/*.dll ../plugins/*/*/*.dll Plugins/ 2>/dev/null
```

## Starten

Aus dem Repo-Hauptverzeichnis:

```
start-qlcplus.bat        REM QLC+ 4
start-qlcplus.bat 5      REM QLC+ 5
```
