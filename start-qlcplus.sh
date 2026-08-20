#!/usr/bin/env bash
# Startet das installierte QLC+ aus dem Toolbox-Container "qlcplus-build".
# Gebaut wurde im Container (Qt5-Widgets-UI), da der Fedora-Atomic-Host
# selbst keine Qt5-Devel-Pakete per dnf annehmen kann. Per "cmake --install"
# liegt qlcplus inkl. aller Plugins (MIDI, DMX USB/uDMX, ArtNet, ...) im
# Container unter /usr, daher genügt der reine Programmname.
#
# Hinweis: Falls du deinen DMX-USB-Controller oder MIDI-Gerät nicht siehst,
# obwohl das Gerät angeschlossen ist, prüfe ob es von den udev-Regeln unter
# /etc/udev/rules.d/z65-*.rules (Host!) erfasst wird (siehe idVendor/idProduct
# per "lsusb"). Ggf. eine passende Regel ergänzen und
# "sudo udevadm control --reload-rules && sudo udevadm trigger" ausführen.
set -euo pipefail
exec toolbox run -c qlcplus-build qlcplus "$@"
