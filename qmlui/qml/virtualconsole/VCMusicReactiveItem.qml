/*
  Q Light Controller Plus
  VCMusicReactiveItem.qml

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

import org.qlcplus.classes 1.0
import "."

VCWidgetItem
{
    id: musicReactiveRoot
    property VCMusicReactive musicReactiveObj: null
    // Diagnostic readout below is hidden by default so the widget looks
    // clean in normal/presentation use - flip this to true (e.g. from
    // the QML debugger, or wire a control to it later) if the trigger
    // behaviour ever needs re-calibrating against live numbers again.
    property bool showDebug: true

    property variant levelValues: musicReactiveObj ? musicReactiveObj.levels : null
    property var bandNames: [ qsTr("Master"), qsTr("Kick"), qsTr("Mids") ]
    property var bandColors: [ "#4488FF", "#FF4444", "#44DD88" ]

    clip: true

    onMusicReactiveObjChanged: setCommonProperties(musicReactiveObj)

    ColumnLayout
    {
        anchors.fill: parent
        anchors.margins: 4
        spacing: 4

        RowLayout
        {
            Layout.fillWidth: true

            IconButton
            {
                width: UISettings.iconSizeDefault
                height: UISettings.iconSizeDefault
                faSource: FontAwesome.fa_microphone
                checkable: true
                checked: musicReactiveObj ? musicReactiveObj.captureEnabled : false
                // Explicitly negate the C++ property rather than reading
                // this button's own "checked" back: checked is bound to
                // musicReactiveObj.captureEnabled, and Button's built-in
                // auto-toggle-on-click races with that binding (same
                // reason editModeButton in VCRightPanel.qml does
                // "!checked" instead of relying on it).
                onClicked: if (musicReactiveObj) musicReactiveObj.captureEnabled = !musicReactiveObj.captureEnabled
                tooltip: qsTr("Enable audio capture")
            }

            // Manual safety net for live use: with Auto off, the
            // automatic Ambient/Active decision is suspended and the
            // status badge below becomes a button the operator can tap
            // to force the right state by hand, regardless of what the
            // audio analysis currently thinks - the Beat-driven chaser
            // and the meters keep working the same either way.
            Rectangle
            {
                width: autoLabel.width + 12
                height: UISettings.iconSizeDefault
                radius: 4
                color: musicReactiveObj && musicReactiveObj.autoMode ? UISettings.bgStrong : "#AA6622"
                border.width: 1
                border.color: UISettings.bgLight

                RobotoText
                {
                    id: autoLabel
                    anchors.centerIn: parent
                    label: musicReactiveObj && musicReactiveObj.autoMode ? qsTr("AUTO") : qsTr("MANUAL")
                    fontBold: true
                }

                MouseArea
                {
                    anchors.fill: parent
                    onClicked: if (musicReactiveObj) musicReactiveObj.autoMode = !musicReactiveObj.autoMode
                }
            }

            RobotoText
            {
                Layout.fillWidth: true
                label: musicReactiveObj && musicReactiveObj.currentProfileIndex >= 0 &&
                       musicReactiveObj.currentProfileIndex < musicReactiveObj.profiles.length ?
                           musicReactiveObj.profiles[musicReactiveObj.currentProfileIndex].name : qsTr("No profile active")
                fontBold: true
            }

            Rectangle
            {
                width: statusLabel.width + 12
                height: UISettings.iconSizeDefault
                radius: 4
                color: musicReactiveObj && musicReactiveObj.inBreak ? "#3355AA" : "#227733"
                border.width: musicReactiveObj && !musicReactiveObj.autoMode ? 1 : 0
                border.color: "#FFDD33"

                RobotoText
                {
                    id: statusLabel
                    anchors.centerIn: parent
                    label: musicReactiveObj && musicReactiveObj.inBreak ? qsTr("AMBIENT") : qsTr("ACTIVE")
                    fontBold: true
                }

                // Tappable only in Manual mode - see the AUTO/MANUAL
                // badge above. Forces the opposite state by hand.
                MouseArea
                {
                    anchors.fill: parent
                    enabled: musicReactiveObj && !musicReactiveObj.autoMode
                    onClicked: musicReactiveObj.setManualBreak(!musicReactiveObj.inBreak)
                }
            }
        }

        // Diagnostic-only readout - see showDebug above. Capped height
        // with clipping, not left to grow with the text: this line kept
        // getting longer as more counters were added for debugging the
        // Profile/Chaser advance issue, and being an uncapped wrapping
        // Text in a ColumnLayout it just ate more and more of the
        // widget's fixed total height every time - which starved the
        // Profiles/Chasers panel below (Layout.fillHeight: true, so it
        // only gets whatever's left over) down to less than one row's
        // worth on a widget saved at a modest height, making an actually
        // fully-populated Profile look like it silently lost a Chaser.
        // Same fix shape as the meters row below it.
        Item
        {
            Layout.fillWidth: true
            Layout.preferredHeight: musicReactiveRoot.showDebug ? 60 : 0
            Layout.maximumHeight: 60
            clip: true
            visible: musicReactiveRoot.showDebug

            RobotoText
            {
                anchors.left: parent.left
                anchors.right: parent.right
                anchors.top: parent.top
                fontSize: UISettings.textSizeDefault * 0.75
                label: musicReactiveObj ? musicReactiveObj.debugText : ""
                wrapText: true
            }
        }

        // Fixed share of the widget's height, not fillHeight: the
        // meters used to be the only content below the header, so they
        // soaked up 100% of any extra space when the widget was resized
        // taller - leaving the Profiles/Chasers panel below (which
        // needs that room far more) with nothing. 130px is enough to
        // read the bars comfortably without starving the panel.
        RowLayout
        {
            Layout.fillWidth: true
            Layout.preferredHeight: 130
            Layout.maximumHeight: 130
            spacing: 6

            Repeater
            {
                model: 3

                ColumnLayout
                {
                    Layout.fillHeight: true
                    Layout.fillWidth: true

                    Rectangle
                    {
                        id: meterRect
                        Layout.fillWidth: true
                        Layout.fillHeight: true
                        color: UISettings.bgStrong
                        border.width: 1
                        border.color: UISettings.bgLight

                        Rectangle
                        {
                            anchors.left: parent.left
                            anchors.right: parent.right
                            anchors.bottom: parent.bottom
                            height: levelValues ? parent.height *
                                     (Math.max(0, Math.min(255, levelValues[index] || 0)) / 255.0) : 0
                            color: bandColors[index]
                        }

                        // Draggable trigger threshold marker: shows what
                        // the (gained) meter must reach for this band to
                        // fire - only Kick's marker currently drives
                        // anything, but all three are shown/adjustable
                        // for visual consistency and future use.
                        property var thresholdValues: musicReactiveObj ? musicReactiveObj.thresholds : null
                        property real thresholdPct: thresholdValues && thresholdValues.length > index ?
                                                    thresholdValues[index] : 60

                        Rectangle
                        {
                            id: thresholdMarker
                            anchors.left: parent.left
                            anchors.right: parent.right
                            height: 2
                            color: "#FFDD33"
                            y: meterRect.height * (1 - meterRect.thresholdPct / 100) - height / 2
                        }

                        MouseArea
                        {
                            anchors.left: parent.left
                            anchors.right: parent.right
                            y: thresholdMarker.y - 8
                            height: 16
                            cursorShape: Qt.SizeVerCursor
                            preventStealing: true

                            onPositionChanged: (mouse) =>
                            {
                                if (pressed && musicReactiveObj)
                                {
                                    var posInMeter = mapToItem(meterRect, mouse.x, mouse.y)
                                    var pct = Math.round((1 - posInMeter.y / meterRect.height) * 100)
                                    musicReactiveObj.setThreshold(index, Math.max(0, Math.min(100, pct)))
                                }
                            }
                        }
                    }

                    RobotoText
                    {
                        Layout.alignment: Qt.AlignHCenter
                        label: bandNames[index]
                    }
                }
            }
        }

        // Live performance panel: Profiles (left) turn whole groups of
        // Chasers on/off - tick more than one and they automatically
        // rotate which is in control every few bars. Chasers (right)
        // are every Chaser belonging to a currently-active Profile,
        // flattened, so individual ones (e.g. "moving heads") can be
        // switched on/off live regardless of Profile membership -
        // colored border shows what's actually outputting right now.
        // Profiles are built up (which Chasers belong to them) in the
        // widget's Properties panel, not here - this is operation only.
        RowLayout
        {
            Layout.fillWidth: true
            Layout.fillHeight: true
            // Guarantee at least ~3 Chaser rows' worth of room
            // regardless of how tall the header/debug text/meters above
            // end up being - this is the panel the whole Profile system
            // is actually operated from, it should never be the one
            // that silently loses out to everything else's height.
            Layout.minimumHeight: 140
            spacing: 6

            Rectangle
            {
                Layout.fillWidth: true
                Layout.fillHeight: true
                color: UISettings.bgStrong
                border.width: 1
                border.color: UISettings.bgLight

                ColumnLayout
                {
                    anchors.fill: parent
                    anchors.margins: 3
                    spacing: 2

                    RobotoText { label: qsTr("Profiles"); fontBold: true }

                    ListView
                    {
                        Layout.fillWidth: true
                        Layout.fillHeight: true
                        clip: true
                        model: musicReactiveObj ? musicReactiveObj.profiles : null

                        // Not just cosmetic: on a widget saved at a modest
                        // height (header + debug text + meters can easily
                        // eat most of a small widget's total height), this
                        // list otherwise silently clips to whatever rows
                        // happen to fit, with no indication more exist -
                        // see the "2nd Chaser looked like it vanished"
                        // investigation in VCMusicReactive::visibleChasers().
                        // A scrollbar means every entry stays reachable
                        // without the operator having to resize the widget.
                        ScrollBar.vertical: CustomScrollBar { }

                        delegate:
                            Rectangle
                            {
                                width: ListView.view.width
                                height: UISettings.iconSizeDefault + 6
                                radius: 3
                                color: "transparent"
                                border.width: 2
                                border.color: modelData.active ?
                                                  (musicReactiveObj && musicReactiveObj.currentProfileIndex === index ? "#33CC55" : "#DD9922") :
                                                  UISettings.bgLight

                                RowLayout
                                {
                                    anchors.fill: parent
                                    anchors.margins: 3

                                    CustomCheckBox
                                    {
                                        checked: modelData.active
                                        onClicked: if (musicReactiveObj) musicReactiveObj.setProfileActive(index, checked)
                                        tooltip: qsTr("Active (participates in rotation)")
                                    }
                                    RobotoText
                                    {
                                        Layout.fillWidth: true
                                        label: modelData.name
                                    }
                                }
                            }
                    }
                }
            }

            Rectangle
            {
                Layout.fillWidth: true
                Layout.fillHeight: true
                color: UISettings.bgStrong
                border.width: 1
                border.color: UISettings.bgLight

                ColumnLayout
                {
                    anchors.fill: parent
                    anchors.margins: 3
                    spacing: 2

                    RobotoText { label: qsTr("Chasers"); fontBold: true }

                    ListView
                    {
                        Layout.fillWidth: true
                        Layout.fillHeight: true
                        clip: true
                        model: musicReactiveObj ? musicReactiveObj.visibleChasers : null

                        // See the identical comment on the Profiles
                        // ListView above - this is the list where a
                        // squeezed height previously made a fully-working,
                        // fully-loaded 2nd Chaser look like it wasn't
                        // there at all.
                        ScrollBar.vertical: CustomScrollBar { }

                        delegate:
                            Rectangle
                            {
                                width: ListView.view.width
                                height: UISettings.iconSizeDefault + 6
                                radius: 3
                                color: "transparent"
                                border.width: 2
                                border.color: modelData.running ? "#33CC55" : (modelData.enabled ? "#DD9922" : UISettings.bgLight)

                                RowLayout
                                {
                                    anchors.fill: parent
                                    anchors.margins: 3

                                    CustomCheckBox
                                    {
                                        checked: modelData.enabled
                                        onClicked: if (musicReactiveObj)
                                                       musicReactiveObj.setChaserEnabledInProfile(modelData.profileIndex, modelData.chaserIndex, checked)
                                        tooltip: qsTr("Enabled")
                                    }
                                    RobotoText
                                    {
                                        Layout.fillWidth: true
                                        label: modelData.name + " (" + modelData.profileName + ")"
                                    }
                                }
                            }
                    }
                }
            }
        }
    }
}
