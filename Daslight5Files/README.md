# Daslight 5 → QLC+ Fixture-Konverter

## Warum nicht direkt die .ssl2-Dateien?

Die `.ssl2`-Bibliotheksdateien von Daslight 5 sind proprietär verschlüsselt
(kein offenes/dokumentiertes Format) - sie lassen sich nicht direkt einlesen.

Der Daslight-Speicherstand (`.dvc`) enthält aber im `<PATCHS DATA="...">`-Element
eine **unverschlüsselte**, Base64+zlib-komprimierte Kopie **aller im Setup
verwendeten Fixture-Definitionen** (Kanäle, Modi, Presets - alles). Das Tool
liest diese Kopie aus und wandelt sie in QLC+-Fixturedateien (`.qxf`) um.

In deinem Speicherstand stecken sogar 12 Fixtures (mehr als die 10 exportierten
`.ssl2`-Dateien im Ordner `ssl files/`).

## Benutzung

```
python daslight_to_qlcplus.py Daslight5_savefile.dvc -o qlcplus_fixtures
```

Das erzeugt:

- pro Fixture eine `.qxf`-Datei, sortiert nach Hersteller-Unterordnern
  (wie QLC+'s eigene Fixture-Bibliothek), z. B.
  `qlcplus_fixtures/Varytec/Varytec-Giga-Bar-240-LED-RGB.qxf`
- eine `qlcplus_fixtures/daslight_patch.qxw` - ein fertiger QLC+-Workspace mit
  **allen 32 Fixtures aus deinem Daslight-Patch, an genau denselben
  DMX-Universen/-Adressen**, deinen Fixture-Gruppen ("All", "zq02040") und
  **deinen Szenen/Chasern** (37 Stück, siehe unten). Mit `--no-workspace`
  kannst du das weglassen und nur die `.qxf`-Dateien erzeugen lassen.

## Import in QLC+

Kopiere die erzeugten Hersteller-Ordner in deinen QLC+-Benutzer-Fixture-Ordner
und starte QLC+ neu:

```
%USERPROFILE%\QLC+\Fixtures
```

Alternativ: In QLC+ über **Fixture-Editor → Öffnen** einzeln importieren -
dort auch gleich Type/Physical prüfen und bei Bedarf per "Speichern unter"
in den Benutzerordner ablegen.

Danach `daslight_patch.qxw` in QLC+ öffnen (**Datei → Öffnen**) - die
Fixtures müssen dafür bereits im Benutzer-Fixture-Ordner liegen, sonst
erscheinen sie im Workspace als "fehlend". Von dort aus kannst du direkt mit
dem Bau von Szenen/Chasern beginnen (siehe nächster Abschnitt zu den
Szenen-Werten).

## Was das Tool automatisch erkennt

- Kanaltyp → QLC+-Gruppe (Intensity/Colour/Pan/Tilt/Gobo/Shutter/Speed/Beam/Effect)
- Einfache Vollbereichs-Kanäle (Rot/Grün/Blau/Weiß/UV/Dimmer/Pan/Tilt) werden
  als QLC+-Standard-"Preset" gesetzt (z. B. `IntensityRed`), inkl. zugehöriger
  16-Bit-Fine-Kanäle
- Alle anderen Kanäle (Farbrad, Goborad, Strobe, Makros, ...) werden als
  benannte Capabilities aus den Original-DMX-Wertebereichen übernommen
- Fixture-Typ (Moving Head / LED Bar / Color Changer / Laser / Other) wird aus
  Daslight-internen Kategorie-Codes und Pan/Tilt-Vorhandensein geschätzt
- Physical-Block (Maße, Gewicht, Leistung, Pan/Tilt-Bereich, Öffnungswinkel)
  wird übernommen (Maße von cm in mm umgerechnet)

## Was du danach in QLC+ noch prüfen solltest

- **Type**: automatisch geschätzt, bei Exoten (z. B. Effektgeräte) ggf. anpassen
- **Physical/Dimensions**: grobe Umrechnung, keine Garantie für exakte Maße
- Farbrad-/Gobo-Capabilities haben keine echten Wheel-Farben/-Bilder hinterlegt
- Kanäle, die das Skript nicht zuordnen konnte, stehen mit Gruppe "Nothing" da
  (im Testlauf mit deinem Speicherstand kam das aktuell bei keinem der 12
  Fixtures vor)

## Eigene Fixtures neu konvertieren

Wenn du in Daslight weitere Fixtures patchst und neu speicherst, einfach das
Skript erneut über die aktualisierte `.dvc`-Datei laufen lassen.

## Szenen: was übernommen wird und was nicht

Daslight speichert pro Szenen-Schritt und Fixture einen eigenen komprimierten
Datenblock, dessen Layout nirgends dokumentiert ist. Durch Reverse-Engineering
(Vergleich vieler Szenen wie "All On", "Blackout", "Red"/"Green"/"Blue" über
mehrere Fixture-Typen hinweg) konnte das Format entschlüsselt und mehrfach
gegen eindeutige Referenzfälle verifiziert werden, z. B.:

- Szene **"All On"** setzt exakt den Dimmer-Kanal auf 255 - nichts sonst.
- Szene **"Red"** setzt bei der 8-fach-LED-Bar exakt die 8 "Red"-Kanäle
  (jedes 3. von 24 Kanälen) auf 255, bei einem Farbrad-Fixture exakt den
  DMX-Default-Wert des "Red"-Presets aus dessen eigener Definition.

Layout (pro Fixture/Schritt): 2 Byte Header (enthält die Kanalzahl N),
gefolgt von N Byte-Paaren (ein Paar pro DMX-Kanal in genau der Reihenfolge
des Modus): `(0x00, Wert)` heißt "dieser Kanal wird auf `Wert` gesetzt",
`(0xff, 0xff)` heißt "in diesem Schritt nicht verändert". Danach folgen noch
weitere Daslight-interne Daten (vermutlich Kurven-/Animationsparameter ohne
QLC+-Äquivalent), die ignoriert werden, weil sie für eine statische
Szenen-Momentaufnahme nicht gebraucht werden.

**Ergebnis in deinem Speicherstand:** 37 von 66 Daslight-Szenen wurden so als
QLC+ `Scene`- bzw. `Chaser`-Funktion in `daslight_patch.qxw` übernommen
(Mehrschritt-Szenen wie "Circles" mit 124 Schritten werden als Chaser aus
einzelnen Scene-Funktionen gebaut). Die restlichen 29 sind bewusst **nicht**
übernommen worden - das sind keine statischen Wertesätze, sondern von
Daslight zur Laufzeit berechnete Effekte:

- **Super Scenes** (Bank "Super Scenes" und "SuperControls") - kombinieren
  andere Szenen/Effekte, wie von dir gewünscht ignoriert.
- **Generator-Effekte** (Rainbow, Random-Chases, Bewegungsmuster wie
  "Bouncing"/"FlowingOnBars"/"All Random") - werden von Daslight prozedural
  erzeugt, nicht als feste Werte gespeichert. Ein Äquivalent ließe sich in
  QLC+ nachbauen (z. B. mit EFX- oder RGB-Matrix-Funktionen), aber nicht
  automatisiert aus dieser Datei ableiten.

Beim Import wird dir genau aufgelistet, wie viele Szenen konvertiert bzw.
übersprungen wurden.

**Trotzdem bitte stichprobenartig gegenprüfen:** Die Werte sind verifiziert,
aber ohne eine echte Daslight-Instanz zum Livevergleich kann ich keine
100%ige Garantie für jede einzelne Szene geben. Schau dir ein paar Szenen in
QLC+ an und vergleiche sie mit Daslight.

**Timing bei Chasern ist eine Annahme:** Daslights `WAITTIME`/`FADETIME` sind
nicht dokumentiert; ich interpretiere sie als 1 Tick = 40 ms (WAITTIME=25 ->
1000 ms), das ist der mit Abstand häufigste Wert in deiner Datei und ergibt
einen plausiblen "1 Schritt pro Sekunde"-Standard - in QLC+ ggf. anpassen.

Fehlt dir eine der 29 Generator-Effekt-Szenen als echte QLC+-Funktion, sag
mir einfach in Worten, was sie tun soll (z. B. "Rainbow: alle PARs im
Regenbogen über 8s"), dann baue ich sie dir gezielt nach.
