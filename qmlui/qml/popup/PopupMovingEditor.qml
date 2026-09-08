/*
  Q Light Controller Plus
  PopupMovingEditor.qml

  Copyright (c) Massimo Callegari

  Licensed under the Apache License, Version 2.0 (the "License");
  you may not use this file except in compliance with the License.
  You may obtain a copy of the License at

      http://www.apache.org/licenses/LICENSE-2.0.txt

  Unless required by applicable law or agreed to in writing, software
  distributed under the License is distributed on an "AS IS" BASIS,
  WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
  See the License for the specific language governing permissions and
  limitations under the License.
*/

import QtQuick
import QtQuick.Layouts
import QtQuick.Controls
import QtQuick.Controls.Basic

import "."

CustomPopupDialog
{
    id: popupRoot
    width: mainView.width * 0.7
    title: qsTr("Moving Head Editor")
    standardButtons: Dialog.Close
    closePolicy: Popup.CloseOnEscape

    // {id, name, hasPan, hasTilt, use, mirror}
    property var fixtureRows: []
    // {pan, tilt, holdMs, fadeMs}
    property var points: []
    property int selectedPoint: -1
    // 0 = every fixture shows the same point at the same step (default,
    // unchanged behaviour). >0 = each fixture (in selection order) reads
    // the point list this many steps "behind" the previous one, wrapping
    // around - the same path then visibly ripples down the fixture line
    // as the resulting Chaser loops, a hand-drawn RGB-Matrix-style wave.
    property int deviceOffsetSteps: 0

    function selectedFixtureIds()
    {
        var ids = []
        for (var i = 0; i < fixtureRows.length; i++)
            if (fixtureRows[i].use && !fixtureRows[i].mirror)
                ids.push(fixtureRows[i].id)
        return ids
    }

    function mirrorFixtureIds()
    {
        var ids = []
        for (var i = 0; i < fixtureRows.length; i++)
            if (fixtureRows[i].use && fixtureRows[i].mirror)
                ids.push(fixtureRows[i].id)
        return ids
    }

    function anySelected()
    {
        return selectedFixtureIds().length > 0 || mirrorFixtureIds().length > 0
    }

    function previewPoint(pan, tilt)
    {
        var all = selectedFixtureIds()
        if (all.length)
            functionManager.movingEditorPreview(all, pan, tilt)

        var mirrored = mirrorFixtureIds()
        if (mirrored.length)
            functionManager.movingEditorPreview(mirrored, 1.0 - pan, tilt)
    }

    function addPoint(pan, tilt)
    {
        var p = points.slice()
        p.push({ pan: pan, tilt: tilt, holdMs: 500, fadeMs: 500 })
        points = p
        selectedPoint = points.length - 1
        previewPoint(pan, tilt)
    }

    function removePoint(idx)
    {
        var p = points.slice()
        p.splice(idx, 1)
        points = p
        if (selectedPoint >= points.length)
            selectedPoint = points.length - 1
    }

    function setPointTiming(idx, holdMs, fadeMs)
    {
        var p = points.slice()
        if (idx < 0 || idx >= p.length)
            return
        p[idx].holdMs = holdMs
        p[idx].fadeMs = fadeMs
        points = p
    }

    // Merges each adjacent pair of points into one (averaged position,
    // summed hold/fade so the merged point still covers the same total
    // time the two original ones did) - halves the step count, for when
    // a hand-drawn path ended up with more resolution than actually
    // needed. A trailing odd point out is kept as-is.
    function halvePoints()
    {
        if (points.length < 2)
            return
        var out = []
        var i = 0
        while (i < points.length)
        {
            if (i + 1 < points.length)
            {
                var a = points[i], b = points[i + 1]
                out.push({
                    pan: (a.pan + b.pan) / 2,
                    tilt: (a.tilt + b.tilt) / 2,
                    holdMs: a.holdMs + b.holdMs,
                    fadeMs: a.fadeMs + b.fadeMs
                })
                i += 2
            }
            else
            {
                out.push(points[i])
                i += 1
            }
        }
        points = out
        selectedPoint = Math.min(selectedPoint, points.length - 1)
    }

    // Duplicates every point in place (P1,P2 -> P1,P1,P2,P2) - doubles the
    // step count without changing the path's shape, e.g. to then hand-tweak
    // every other step individually, or just to have more steps to spread
    // a "Versatz zwischen Geräten" stagger across.
    function duplicatePoints()
    {
        if (points.length === 0)
            return
        var out = []
        for (var i = 0; i < points.length; i++)
        {
            out.push(points[i])
            out.push({ pan: points[i].pan, tilt: points[i].tilt, holdMs: points[i].holdMs, fadeMs: points[i].fadeMs })
        }
        points = out
        if (selectedPoint >= 0)
            selectedPoint = selectedPoint * 2
    }

    onOpened:
    {
        var rows = functionManager.movingEditorFixtures()
        for (var i = 0; i < rows.length; i++)
        {
            rows[i].use = false
            rows[i].mirror = false
        }
        fixtureRows = rows
        points = []
        selectedPoint = -1
        deviceOffsetSteps = 0
        nameField.text = qsTr("Moving Effekt")
    }

    onClosed: functionManager.movingEditorClearPreview()

    contentItem:
        ColumnLayout
        {
            width: popupRoot.width
            spacing: 8

            RobotoText
            {
                Layout.fillWidth: true
                wrapText: true
                fontItalic: true
                label: qsTr("Wähle Moving-Head-Fixtures aus, klicke dann in das Feld, um Punkte für den Bewegungspfad zu setzen - jeder Klick bewegt die ausgewählten Geräte sofort live zur Vorschau. \"Gespiegelt\" bewegt ein Gerät horizontal spiegelverkehrt zu den normalen (z.B. für symmetrische Paare links/rechts). \"Versatz zwischen Geräten\" lässt denselben Pfad zeitversetzt durch die Geräte-Reihe laufen (Wellen-Effekt, wie bei einem RGB-Matrix-Chase) statt dass sich alle Geräte synchron bewegen.")
            }

            RowLayout
            {
                Layout.fillWidth: true
                spacing: 8

                RobotoText { label: qsTr("Versatz zwischen Geräten (Schritte)") }
                CustomSpinBox
                {
                    from: 0
                    to: 50
                    value: popupRoot.deviceOffsetSteps
                    onValueModified: popupRoot.deviceOffsetSteps = value
                }
                Item { Layout.fillWidth: true }
            }

            RowLayout
            {
                Layout.fillWidth: true
                Layout.fillHeight: true
                spacing: 10

                // Fixture picker
                Rectangle
                {
                    Layout.preferredWidth: popupRoot.width * 0.28
                    Layout.fillHeight: true
                    color: UISettings.bgLighter
                    border.width: 1
                    border.color: UISettings.borderColorDark
                    radius: 3

                    ColumnLayout
                    {
                        anchors.fill: parent
                        anchors.margins: 4
                        spacing: 2

                        RobotoText { label: qsTr("Fixtures"); fontBold: true }

                        Rectangle { Layout.fillWidth: true; height: 1; color: UISettings.borderColorDark }

                        ScrollView
                        {
                            Layout.fillWidth: true
                            Layout.fillHeight: true
                            clip: true

                            ColumnLayout
                            {
                                width: parent.width
                                spacing: 2

                                Repeater
                                {
                                    model: popupRoot.fixtureRows

                                    delegate:
                                        RowLayout
                                        {
                                            Layout.fillWidth: true
                                            spacing: 4

                                            CustomCheckBox
                                            {
                                                checked: modelData.use === true
                                                onClicked:
                                                {
                                                    var rows = popupRoot.fixtureRows.slice()
                                                    rows[index].use = checked
                                                    popupRoot.fixtureRows = rows
                                                }
                                            }
                                            RobotoText
                                            {
                                                Layout.fillWidth: true
                                                label: modelData.name ? modelData.name : ""
                                            }
                                            RobotoText { label: qsTr("Gespiegelt") ; fontSize: UISettings.textSizeDefault * 0.8 }
                                            CustomCheckBox
                                            {
                                                checked: modelData.mirror === true
                                                enabled: modelData.use === true
                                                onClicked:
                                                {
                                                    var rows = popupRoot.fixtureRows.slice()
                                                    rows[index].mirror = checked
                                                    popupRoot.fixtureRows = rows
                                                }
                                            }
                                        }
                                }
                            }
                        }
                    }
                }

                // XY pad
                Rectangle
                {
                    id: pad
                    Layout.preferredWidth: popupRoot.width * 0.38
                    Layout.preferredHeight: Layout.preferredWidth
                    color: "#101010"
                    border.width: 2
                    border.color: UISettings.borderColorDark

                    RobotoText
                    {
                        anchors.centerIn: parent
                        visible: popupRoot.points.length === 0
                        label: qsTr("Klicken um einen Punkt zu setzen")
                        fontItalic: true
                        opacity: 0.5
                    }

                    // crosshair guides
                    Rectangle { anchors.horizontalCenter: parent.horizontalCenter; width: 1; height: parent.height; color: "#333333" }
                    Rectangle { anchors.verticalCenter: parent.verticalCenter; width: parent.width; height: 1; color: "#333333" }

                    // path lines between consecutive points
                    Canvas
                    {
                        id: pathCanvas
                        anchors.fill: parent
                        onPaint:
                        {
                            var ctx = getContext("2d")
                            ctx.clearRect(0, 0, width, height)
                            if (popupRoot.points.length < 2)
                                return
                            ctx.strokeStyle = "#4aa3ff"
                            ctx.lineWidth = 2
                            ctx.beginPath()
                            for (var i = 0; i < popupRoot.points.length; i++)
                            {
                                var px = popupRoot.points[i].pan * width
                                var py = popupRoot.points[i].tilt * height
                                if (i === 0)
                                    ctx.moveTo(px, py)
                                else
                                    ctx.lineTo(px, py)
                            }
                            ctx.stroke()
                        }
                        Connections
                        {
                            target: popupRoot
                            function onPointsChanged() { pathCanvas.requestPaint() }
                        }
                    }

                    MouseArea
                    {
                        anchors.fill: parent
                        onClicked: (mouse) =>
                        {
                            var pan = Math.max(0, Math.min(1, mouse.x / width))
                            var tilt = Math.max(0, Math.min(1, mouse.y / height))
                            popupRoot.addPoint(pan, tilt)
                        }
                    }

                    Repeater
                    {
                        model: popupRoot.points

                        delegate:
                            Rectangle
                            {
                                width: 16
                                height: 16
                                radius: 8
                                x: modelData.pan * pad.width - width / 2
                                y: modelData.tilt * pad.height - height / 2
                                color: index === popupRoot.selectedPoint ? "#ffb400" : "#4aa3ff"
                                border.width: 1
                                border.color: "white"

                                RobotoText
                                {
                                    anchors.centerIn: parent
                                    label: (index + 1).toString()
                                    fontSize: 9
                                    labelColor: "black"
                                }

                                MouseArea
                                {
                                    anchors.fill: parent
                                    acceptedButtons: Qt.LeftButton | Qt.RightButton
                                    onClicked: (mouse) =>
                                    {
                                        if (mouse.button === Qt.RightButton)
                                        {
                                            popupRoot.removePoint(index)
                                        }
                                        else
                                        {
                                            popupRoot.selectedPoint = index
                                            popupRoot.previewPoint(modelData.pan, modelData.tilt)
                                        }
                                        mouse.accepted = true
                                    }
                                }
                            }
                    }
                }

                // point list / timing
                Rectangle
                {
                    Layout.fillWidth: true
                    Layout.fillHeight: true
                    color: UISettings.bgLighter
                    border.width: 1
                    border.color: UISettings.borderColorDark
                    radius: 3

                    ColumnLayout
                    {
                        anchors.fill: parent
                        anchors.margins: 4
                        spacing: 2

                        RowLayout
                        {
                            Layout.fillWidth: true
                            RobotoText { Layout.fillWidth: true; label: qsTr("Pfad (in Reihenfolge)"); fontBold: true }
                            IconButton
                            {
                                width: 22; height: 22
                                faSource: FontAwesome.fa_compress
                                enabled: popupRoot.points.length > 1
                                tooltip: qsTr("Halbieren - je 2 benachbarte Punkte zu einem zusammenfassen")
                                onClicked: popupRoot.halvePoints()
                            }
                            IconButton
                            {
                                width: 22; height: 22
                                faSource: FontAwesome.fa_expand
                                enabled: popupRoot.points.length > 0
                                tooltip: qsTr("Verdoppeln - jeden Punkt duplizieren")
                                onClicked: popupRoot.duplicatePoints()
                            }
                        }
                        Rectangle { Layout.fillWidth: true; height: 1; color: UISettings.borderColorDark }

                        ScrollView
                        {
                            Layout.fillWidth: true
                            Layout.fillHeight: true
                            clip: true

                            ColumnLayout
                            {
                                width: parent.width
                                spacing: 4

                                Repeater
                                {
                                    model: popupRoot.points

                                    delegate:
                                        Rectangle
                                        {
                                            Layout.fillWidth: true
                                            height: 60
                                            color: index === popupRoot.selectedPoint ? UISettings.highlightPressed : "transparent"
                                            radius: 3

                                            ColumnLayout
                                            {
                                                anchors.fill: parent
                                                anchors.margins: 3
                                                spacing: 1

                                                RowLayout
                                                {
                                                    Layout.fillWidth: true
                                                    RobotoText { label: qsTr("Punkt") + " " + (index + 1) }
                                                    Rectangle { Layout.fillWidth: true }
                                                    IconButton
                                                    {
                                                        width: 20; height: 20
                                                        faSource: FontAwesome.fa_trash
                                                        onClicked: popupRoot.removePoint(index)
                                                    }
                                                }
                                                RowLayout
                                                {
                                                    Layout.fillWidth: true
                                                    RobotoText { label: qsTr("Halten (ms)"); fontSize: UISettings.textSizeDefault * 0.8 }
                                                    CustomSpinBox
                                                    {
                                                        from: 0
                                                        to: 60000
                                                        stepSize: 100
                                                        value: modelData.holdMs
                                                        onValueModified: popupRoot.setPointTiming(index, value, modelData.fadeMs)
                                                    }
                                                    RobotoText { label: qsTr("Fade (ms)"); fontSize: UISettings.textSizeDefault * 0.8 }
                                                    CustomSpinBox
                                                    {
                                                        from: 0
                                                        to: 60000
                                                        stepSize: 100
                                                        value: modelData.fadeMs
                                                        onValueModified: popupRoot.setPointTiming(index, modelData.holdMs, value)
                                                    }
                                                }
                                            }
                                        }
                                }
                            }
                        }
                    }
                }
            }

            RowLayout
            {
                Layout.fillWidth: true
                spacing: 8

                RobotoText { label: qsTr("Name") }
                CustomTextEdit
                {
                    id: nameField
                    Layout.fillWidth: true
                }
                GenericButton
                {
                    label: qsTr("Erstellen")
                    enabled: popupRoot.points.length > 0 && popupRoot.anySelected()
                    onClicked:
                    {
                        var newId = functionManager.movingEditorCreate("", nameField.text,
                                        popupRoot.selectedFixtureIds(), popupRoot.mirrorFixtureIds(), popupRoot.points,
                                        popupRoot.deviceOffsetSteps)
                        functionManager.movingEditorClearPreview()
                        if (newId !== undefined)
                            popupRoot.close()
                    }
                }
            }
        } // ColumnLayout
}
