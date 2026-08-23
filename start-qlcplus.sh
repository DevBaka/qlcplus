#!/usr/bin/env bash
# Startet QLC+ (Version 4 oder 5) aus einem der lokalen Toolbox-Container.
# Gebaut wurde in Containern (Fedora-Toolbox), da der Fedora-Atomic-Host
# selbst keine Qt-Devel-Pakete per dnf annehmen kann.
#
#   QLC+ 4 (klassische Qt-Widgets-UI, Qt5) -> toolbox-Container
#           "qlcplus-build", Binary "qlcplus"
#   QLC+ 5 (neue QML-UI, braucht Qt6 - der Code nutzt Qt6-only APIs wie
#           QQuickWindow::setGraphicsApi und das neue Qt3D-Modullayout)
#           -> distrobox-Container "qlcplus5-build" (mit --nvidia, da die
#           3D/2D-Ansicht einen echten OpenGL-Kontext braucht - im
#           normalen toolbox-Container ohne GPU-Passthrough crasht
#           qlcplus-qml beim RHI/EGL-Init), Binary "qlcplus-qml"
#
# Beide teilen sich denselben Projektordner (bind-gemountet über $HOME),
# aber getrennte Container/Qt-Versionen, damit sich Qt5- und Qt6-Build
# nicht in die Quere kommen.
#
# Nutzung:
#   ./start-qlcplus.sh          # startet QLC+ 4 (Standard)
#   ./start-qlcplus.sh 4        # startet QLC+ 4 explizit
#   ./start-qlcplus.sh 5        # startet QLC+ 5 (QML-UI)
#   ./start-qlcplus.sh 5 --geometry ...   # weitere Argumente werden durchgereicht
#
# Neu bauen (nach Codeänderungen):
#   toolbox run -c qlcplus-build     bash -lc 'cd build-toolbox && cmake --build . -j$(nproc) && sudo cmake --install .'
#   distrobox enter qlcplus5-build -- bash -lc 'cd build-qml    && cmake --build . -j$(nproc) && sudo cmake --install .'
#
# Hinweis: Falls dein DMX-USB-Controller oder MIDI-Gerät nicht auftaucht,
# obwohl angeschlossen, prüfe ob es von den udev-Regeln unter
# /etc/udev/rules.d/z65-*.rules (Host!) erfasst wird (idVendor/idProduct
# per "lsusb"). Ggf. eine passende Regel ergänzen und
# "sudo udevadm control --reload-rules && sudo udevadm trigger" ausführen.
set -euo pipefail

version="4"
if [[ "${1:-}" == "4" || "${1:-}" == "5" ]]; then
    version="$1"
    shift
fi

case "$version" in
    4)
        echo "Starte QLC+ 4 (qlcplus, Container: qlcplus-build) ..." >&2
        exec toolbox run -c qlcplus-build qlcplus "$@"
        ;;
    5)
        # QT_QPA_PLATFORM=xcb: die native Wayland-EGL-Integration crasht mit
        # "EGL_BAD_MATCH" beim RHI/OpenGL-Init (Nvidia + Wayland + Container
        # ist fragil). Über XWayland (xcb) läuft es stabil.
        echo "Starte QLC+ 5 (qlcplus-qml, Container: qlcplus5-build, XWayland) ..." >&2
        exec distrobox enter qlcplus5-build -- env QT_QPA_PLATFORM=xcb qlcplus-qml "$@"
        ;;
esac
