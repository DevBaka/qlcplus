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

    // Which control (see midiControlId in each row's modelData) is
    // currently waiting for a MIDI/keyboard press to land, or -1 if
    // none - set the instant a row is clicked in MIDI edit mode, cleared
    // either by inputSourceLearned actually completing it (see the
    // Connections block below) or by clicking that same row again to
    // cancel (see cancelOrStartMidiLearn()). Purely local UI state, not
    // persisted - virtualConsole itself only exposes a matching "is ANY
    // detection running" flag as a private bool, not per-control and not
    // to QML, so this is tracked here instead.
    property int pendingLearnControlId: -1

    // Shared by every row's MIDI-learn click handler (Profiles/Chasers/
    // Idle all call this the same way) - starts learning $controlId,
    // first cancelling whatever detection might already be pending
    // (either this same control, acting as a toggle-to-cancel, or a
    // different one the operator clicked into by mistake and now wants
    // to abandon - virtualConsole.enableInputSourceAutoDetection()
    // silently refuses a second detection while one's already running,
    // so leaving the old one active would make the new click do nothing
    // at all, with no feedback as to why).
    function cancelOrStartMidiLearn(controlId)
    {
        var wasPending = pendingLearnControlId
        if (wasPending >= 0)
        {
            virtualConsole.disableAutoDetection()
            pendingLearnControlId = -1
            if (wasPending === controlId)
                return // clicking the row that was already waiting just cancels it
        }
        if (musicReactiveObj)
        {
            musicReactiveObj.learnMidiForControl(controlId)
            pendingLearnControlId = controlId
        }
    }

    clip: true

    onMusicReactiveObjChanged: setCommonProperties(musicReactiveObj)

    // Opened automatically the instant a MIDI/keyboard learn started
    // from this widget's own MIDI edit mode (see learnMidiForControl()
    // calls below) actually completes - see the Connections block below
    // and virtualConsole's inputSourceLearned signal. Same dialog every
    // other QLC+ widget's Properties panel "External controls" tab
    // already uses to configure per-state (on/off) feedback colour and
    // blink for controllers that support it (Akai APC Mini and
    // similar) - reused as-is, nothing custom built for the dialog
    // itself, only for popping it open right away here.
    PopupCustomFeedback
    {
        id: feedbackPopup
        // CustomPopupDialog.qml (which this is built on) parents/sizes
        // itself off "mainView", an id that only resolves from QML
        // instantiated directly within MainView.qml's own component
        // scope (e.g. the Properties side panel, loaded as a genuine
        // child of that tree) - a VC widget's own live item, created
        // through a completely different runtime path, doesn't have
        // that id in scope at all ("mainView is not defined", confirmed
        // live). Overlay.overlay is the actual Qt Quick Controls
        // mechanism for "the nearest usable full-window overlay" and
        // doesn't depend on any app-specific id being in scope -
        // overriding just these two properties is enough; everything
        // else in the dialog (centering, etc.) already just works once
        // parent is valid.
        parent: Overlay.overlay
        width: parent ? parent.width / 3 : 300
    }

    Connections
    {
        target: virtualConsole
        function onInputSourceLearned(widget, controlId, uni, ch)
        {
            if (!musicReactiveObj || widget !== musicReactiveObj)
                return
            musicReactiveRoot.pendingLearnControlId = -1
            feedbackPopup.widgetObjRef = musicReactiveObj
            feedbackPopup.universe = uni
            feedbackPopup.channel = ch
            feedbackPopup.open()
        }
    }

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

            // MIDI edit mode: while on, clicking a checkbox/row in the
            // Profiles/Chasers/Idle panels below starts MIDI/keyboard
            // learn for THAT item instead of toggling it - point at
            // what you want, then hit the pad/key. Where the actual
            // MIDI note ends up mapped, and configuring per-state
            // feedback colour/blink for controllers that support it
            // (Akai APC Mini and similar), is the same generic
            // "External controls" tab every other QLC+ widget already
            // has in its own Properties panel - this button is just a
            // quicker way to reach it than hunting the right item in
            // that flat list by name.
            IconButton
            {
                width: UISettings.iconSizeDefault
                height: UISettings.iconSizeDefault
                faSource: FontAwesome.fa_plug
                checkable: true
                checked: musicReactiveObj ? musicReactiveObj.midiEditMode : false
                onClicked:
                {
                    if (!musicReactiveObj)
                        return
                    // Leaving MIDI edit mode with a learn still pending
                    // would otherwise leave that row's yellow "waiting"
                    // highlight stuck on indefinitely (it isn't gated by
                    // midiEditMode itself, unlike the plug icons) and the
                    // detection itself dangling in virtualConsole with no
                    // way back to it - cancel cleanly instead.
                    if (musicReactiveObj.midiEditMode && musicReactiveRoot.pendingLearnControlId >= 0)
                    {
                        virtualConsole.disableAutoDetection()
                        musicReactiveRoot.pendingLearnControlId = -1
                    }
                    musicReactiveObj.midiEditMode = !musicReactiveObj.midiEditMode
                }
                tooltip: qsTr("MIDI edit mode: click a checkbox below to learn a MIDI/keyboard control for it")
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
                                border.width: modelData.midiControlId === musicReactiveRoot.pendingLearnControlId ? 3 : 2
                                border.color: modelData.midiControlId === musicReactiveRoot.pendingLearnControlId ? "#FFDD33" :
                                                  modelData.active ?
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
                                    IconButton
                                    {
                                        visible: musicReactiveObj && musicReactiveObj.midiEditMode && modelData.midiControlId >= 0
                                        width: UISettings.iconSizeDefault * 0.7
                                        height: width
                                        faSource: FontAwesome.fa_plug
                                        faColor: modelData.midiControlId === musicReactiveRoot.pendingLearnControlId ? "#FFDD33" :
                                                     (musicReactiveObj && musicReactiveObj.isControlLearned(modelData.midiControlId)) ? "limegreen" : UISettings.fgMedium
                                        enabled: false
                                        // Learned state, not just "a control slot exists for this" -
                                        // every entry gets a control id the moment it's created,
                                        // whether or not anything was ever actually assigned to it.
                                        tooltip: modelData.midiControlId === musicReactiveRoot.pendingLearnControlId
                                                     ? qsTr("Waiting for a MIDI/keyboard press... click again to cancel")
                                                     : (musicReactiveObj && musicReactiveObj.isControlLearned(modelData.midiControlId))
                                                     ? qsTr("A MIDI/keyboard control is already learned for this - click to re-learn")
                                                     : qsTr("No MIDI/keyboard control learned yet - click to learn one")
                                    }
                                }

                                // On top of everything above, only while
                                // midiEditMode is on - see the button
                                // next to the mic icon at the top.
                                MouseArea
                                {
                                    anchors.fill: parent
                                    enabled: musicReactiveObj && musicReactiveObj.midiEditMode
                                    cursorShape: enabled ? Qt.PointingHandCursor : Qt.ArrowCursor
                                    onClicked: musicReactiveRoot.cancelOrStartMidiLearn(modelData.midiControlId)
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
                                border.width: modelData.midiControlId === musicReactiveRoot.pendingLearnControlId ? 3 : 2
                                border.color: modelData.midiControlId === musicReactiveRoot.pendingLearnControlId ? "#FFDD33" :
                                                  modelData.running ? "#33CC55" : (modelData.enabled ? "#DD9922" : UISettings.bgLight)

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
                                    IconButton
                                    {
                                        visible: musicReactiveObj && musicReactiveObj.midiEditMode && modelData.midiControlId >= 0
                                        width: UISettings.iconSizeDefault * 0.7
                                        height: width
                                        faSource: FontAwesome.fa_plug
                                        faColor: modelData.midiControlId === musicReactiveRoot.pendingLearnControlId ? "#FFDD33" :
                                                     (musicReactiveObj && musicReactiveObj.isControlLearned(modelData.midiControlId)) ? "limegreen" : UISettings.fgMedium
                                        enabled: false
                                        // Learned state, not just "a control slot exists for this" -
                                        // every entry gets a control id the moment it's created,
                                        // whether or not anything was ever actually assigned to it.
                                        tooltip: modelData.midiControlId === musicReactiveRoot.pendingLearnControlId
                                                     ? qsTr("Waiting for a MIDI/keyboard press... click again to cancel")
                                                     : (musicReactiveObj && musicReactiveObj.isControlLearned(modelData.midiControlId))
                                                     ? qsTr("A MIDI/keyboard control is already learned for this - click to re-learn")
                                                     : qsTr("No MIDI/keyboard control learned yet - click to learn one")
                                    }
                                }

                                MouseArea
                                {
                                    anchors.fill: parent
                                    enabled: musicReactiveObj && musicReactiveObj.midiEditMode
                                    cursorShape: enabled ? Qt.PointingHandCursor : Qt.ArrowCursor
                                    onClicked: musicReactiveRoot.cancelOrStartMidiLearn(modelData.midiControlId)
                                }
                            }
                    }
                }
            }

            // Idle - every Scene/Chaser belonging to any ACTIVE Idle Set,
            // flattened across Sets (same rotation idea as Profiles ->
            // Chasers, but for the break/ambient state): tick individual
            // ones on/off live regardless of which Set is currently in
            // control - colored border shows what's actually running
            // right now (only meaningful once a break is in progress).
            // Which Sets exist, which Scenes/Chasers belong to one, and
            // which Sets are active/participate in rotation are all
            // built up in the widget's Properties panel, not here.
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

                    RobotoText { label: qsTr("Idle"); fontBold: true }

                    ListView
                    {
                        Layout.fillWidth: true
                        Layout.fillHeight: true
                        clip: true
                        model: musicReactiveObj ? musicReactiveObj.visibleAmbientFunctions : null

                        ScrollBar.vertical: CustomScrollBar { }

                        delegate:
                            Rectangle
                            {
                                width: ListView.view.width
                                height: UISettings.iconSizeDefault + 6
                                radius: 3
                                color: "transparent"
                                border.width: modelData.midiControlId === musicReactiveRoot.pendingLearnControlId ? 3 : 2
                                border.color: modelData.midiControlId === musicReactiveRoot.pendingLearnControlId ? "#FFDD33" :
                                                  modelData.running ? "#33CC55" : (modelData.enabled ? "#DD9922" : UISettings.bgLight)

                                RowLayout
                                {
                                    anchors.fill: parent
                                    anchors.margins: 3

                                    CustomCheckBox
                                    {
                                        checked: modelData.enabled
                                        onClicked: if (musicReactiveObj)
                                                       musicReactiveObj.setFunctionEnabledInAmbientSet(modelData.setIndex, modelData.functionIndex, checked)
                                        tooltip: qsTr("Enabled")
                                    }
                                    RobotoText
                                    {
                                        Layout.fillWidth: true
                                        label: modelData.name + " (" + modelData.setName + ")"
                                    }
                                    IconButton
                                    {
                                        visible: musicReactiveObj && musicReactiveObj.midiEditMode && modelData.midiControlId >= 0
                                        width: UISettings.iconSizeDefault * 0.7
                                        height: width
                                        faSource: FontAwesome.fa_plug
                                        faColor: modelData.midiControlId === musicReactiveRoot.pendingLearnControlId ? "#FFDD33" :
                                                     (musicReactiveObj && musicReactiveObj.isControlLearned(modelData.midiControlId)) ? "limegreen" : UISettings.fgMedium
                                        enabled: false
                                        // Learned state, not just "a control slot exists for this" -
                                        // every entry gets a control id the moment it's created,
                                        // whether or not anything was ever actually assigned to it.
                                        tooltip: modelData.midiControlId === musicReactiveRoot.pendingLearnControlId
                                                     ? qsTr("Waiting for a MIDI/keyboard press... click again to cancel")
                                                     : (musicReactiveObj && musicReactiveObj.isControlLearned(modelData.midiControlId))
                                                     ? qsTr("A MIDI/keyboard control is already learned for this - click to re-learn")
                                                     : qsTr("No MIDI/keyboard control learned yet - click to learn one")
                                    }
                                }

                                MouseArea
                                {
                                    anchors.fill: parent
                                    enabled: musicReactiveObj && musicReactiveObj.midiEditMode
                                    cursorShape: enabled ? Qt.PointingHandCursor : Qt.ArrowCursor
                                    onClicked: musicReactiveRoot.cancelOrStartMidiLearn(modelData.midiControlId)
                                }
                            }
                    }
                }
            }
        }
    }
}
