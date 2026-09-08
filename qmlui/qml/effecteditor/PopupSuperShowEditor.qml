/*
  Q Light Controller Plus
  PopupSuperShowEditor.qml

  A small Daslight-5-style timeline: an ordered sequence of wall-clock
  "sections", each simply naming one of a Music Reactive widget's
  already-built, beat-synced Profiles and a duration - "run Profile A
  for 3 minutes, then switch to Profile B". Everything that happens
  WITHIN a section (which Chasers step on which Beat/Kick, Super
  Chasers recursing into their tracks, Groups rotating) is completely
  unchanged, already-proven Profile playback - this editor only
  schedules WHEN each Profile takes over. See the class comment above
  VCMusicReactive::superShows() for the full architecture rationale.

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

import org.qlcplus.classes 1.0
import "."

CustomPopupDialog
{
    id: popupRoot
    width: mainView.width * 0.8
    height: mainView.height * 0.75
    title: qsTr("Super Show Timeline")
    standardButtons: Dialog.Close
    closePolicy: Popup.CloseOnEscape

    property var widgetRef: null
    property int selectedShowIndex: -1

    // Single source of truth for "the Super Show being edited right now" -
    // every delegate below reads THIS instead of walking relative parent
    // chains (which break the moment a wrapper like ScrollView inserts
    // its own internal Flickable between a child and its declared
    // parent). Re-evaluates automatically whenever widgetRef.superShows,
    // widgetRef itself, or selectedShowIndex changes.
    property var selectedShow:
        (widgetRef && selectedShowIndex >= 0 && selectedShowIndex < widgetRef.superShows.length)
            ? widgetRef.superShows[selectedShowIndex] : null

    property bool isPlaying: widgetRef !== null && widgetRef.activeSuperShowIndex === selectedShowIndex

    function refreshWidgets()
    {
        var list = virtualConsole.widgetsList([VCWidget.MusicReactiveWidget])
        widgetCombo.model = list
        if (list.length && list[0].classRef !== undefined)
        {
            if (widgetRef === null)
                widgetRef = list[0].classRef
        }
        else
        {
            widgetRef = null
        }
    }

    function profileComboModel()
    {
        var out = []
        if (!widgetRef)
            return out
        var profiles = widgetRef.profiles
        for (var i = 0; i < profiles.length; i++)
            out.push({ mLabel: profiles[i].name, mValue: profiles[i].id })
        return out
    }

    function formatDuration(totalSec)
    {
        var s = Math.max(0, Math.round(totalSec))
        var h = Math.floor(s / 3600)
        var m = Math.floor((s % 3600) / 60)
        var sec = s % 60
        if (h > 0)
            return h + ":" + (m < 10 ? "0" : "") + m + ":" + (sec < 10 ? "0" : "") + sec
        return m + ":" + (sec < 10 ? "0" : "") + sec
    }

    function showTotalSec(show)
    {
        var total = 0
        if (!show)
            return 0
        for (var i = 0; i < show.sections.length; i++)
            total += show.sections[i].durationSec
        return total
    }

    onOpened: refreshWidgets()

    contentItem:
        RowLayout
        {
            width: popupRoot.width
            height: mainView.height * 0.65
            spacing: 0

            // Left: widget picker (only meaningful with >1 Music Reactive
            // widget in the project) + the Super Show list itself
            ColumnLayout
            {
                Layout.preferredWidth: 260
                Layout.fillHeight: true
                spacing: 6

                RobotoText { label: qsTr("Widget"); fontBold: true; visible: widgetCombo.model.length > 1 }
                CustomComboBox
                {
                    id: widgetCombo
                    Layout.fillWidth: true
                    model: []
                    visible: model.length > 1
                    textRole: "label"
                    onActivated: (idx) =>
                    {
                        if (model[idx].classRef !== undefined)
                        {
                            popupRoot.widgetRef = model[idx].classRef
                            popupRoot.selectedShowIndex = -1
                        }
                    }
                }

                Rectangle { Layout.fillWidth: true; height: 1; color: UISettings.borderColorDark }

                RowLayout
                {
                    Layout.fillWidth: true
                    RobotoText { Layout.fillWidth: true; label: qsTr("Super Shows"); fontBold: true }
                    IconButton
                    {
                        width: UISettings.iconSizeMedium
                        height: UISettings.iconSizeMedium
                        faSource: FontAwesome.fa_plus
                        tooltip: qsTr("Add a new Super Show")
                        enabled: widgetRef !== null
                        onClicked:
                        {
                            if (!widgetRef)
                                return
                            widgetRef.addSuperShow("")
                            popupRoot.selectedShowIndex = widgetRef.superShows.length - 1
                        }
                    }
                }

                ListView
                {
                    Layout.fillWidth: true
                    Layout.fillHeight: true
                    clip: true
                    model: widgetRef ? widgetRef.superShows : null

                    delegate:
                        Rectangle
                        {
                            width: ListView.view.width
                            height: UISettings.listItemHeight * 1.4
                            color: index === popupRoot.selectedShowIndex ? UISettings.highlight :
                                   (widgetRef && widgetRef.activeSuperShowIndex === index ? "#2a4d2a" : "transparent")
                            border.width: widgetRef && widgetRef.activeSuperShowIndex === index ? 1 : 0
                            border.color: "limegreen"

                            MouseArea
                            {
                                anchors.fill: parent
                                onClicked: popupRoot.selectedShowIndex = index
                            }

                            ColumnLayout
                            {
                                anchors.fill: parent
                                anchors.margins: 4
                                spacing: 2

                                RowLayout
                                {
                                    Layout.fillWidth: true
                                    RobotoText
                                    {
                                        Layout.fillWidth: true
                                        label: modelData.name + (widgetRef && widgetRef.activeSuperShowIndex === index ? " ▶" : "")
                                        fontBold: true
                                    }
                                    IconButton
                                    {
                                        width: UISettings.iconSizeDefault
                                        height: UISettings.iconSizeDefault
                                        faSource: FontAwesome.fa_trash
                                        tooltip: qsTr("Delete this Super Show")
                                        onClicked:
                                        {
                                            if (!widgetRef)
                                                return
                                            widgetRef.removeSuperShowAt(index)
                                            if (popupRoot.selectedShowIndex === index)
                                                popupRoot.selectedShowIndex = -1
                                            else if (popupRoot.selectedShowIndex > index)
                                                popupRoot.selectedShowIndex--
                                        }
                                    }
                                }
                                RobotoText
                                {
                                    label: modelData.sections.length + " " + qsTr("sections") + " · " +
                                           popupRoot.formatDuration(popupRoot.showTotalSec(modelData))
                                    fontSize: UISettings.textSizeDefault * 0.8
                                    fontItalic: true
                                }
                            }
                        }
                }
            }

            Rectangle { Layout.fillHeight: true; width: 1; color: UISettings.borderColorDark }

            // Right: the selected Super Show's own timeline editor
            ColumnLayout
            {
                Layout.fillWidth: true
                Layout.fillHeight: true
                Layout.leftMargin: 10
                spacing: 8
                visible: popupRoot.selectedShow !== null

                RowLayout
                {
                    Layout.fillWidth: true
                    spacing: 8

                    CustomTextInput
                    {
                        Layout.preferredWidth: 220
                        height: UISettings.iconSizeDefault
                        text: popupRoot.selectedShow ? popupRoot.selectedShow.name : ""
                        allowDoubleClick: true
                        font.bold: true
                        onTextConfirmed: (newText) => { if (widgetRef) widgetRef.renameSuperShow(popupRoot.selectedShowIndex, newText) }
                    }

                    CustomCheckBox
                    {
                        id: loopCheckBox
                        width: UISettings.iconSizeDefault
                        height: UISettings.iconSizeDefault
                        checked: popupRoot.selectedShow ? popupRoot.selectedShow.loop : false
                        tooltip: qsTr("Loop back to section 1 after the last section ends")
                        onClicked: if (widgetRef) widgetRef.setSuperShowLoop(popupRoot.selectedShowIndex, checked)
                    }
                    RobotoText { label: qsTr("Loop") }

                    Item { Layout.fillWidth: true }

                    IconButton
                    {
                        width: UISettings.iconSizeMedium
                        height: UISettings.iconSizeMedium
                        faSource: popupRoot.isPlaying ? FontAwesome.fa_stop : FontAwesome.fa_play
                        faColor: popupRoot.isPlaying ? "crimson" : "limegreen"
                        tooltip: popupRoot.isPlaying ? qsTr("Stop") : qsTr("Play")
                        enabled: popupRoot.selectedShow && popupRoot.selectedShow.sections.length > 0
                        onClicked:
                        {
                            if (!widgetRef)
                                return
                            if (popupRoot.isPlaying)
                                widgetRef.stopSuperShow()
                            else
                                widgetRef.startSuperShow(popupRoot.selectedShowIndex)
                        }
                    }

                    RobotoText
                    {
                        visible: popupRoot.isPlaying
                        label: widgetRef ? (popupRoot.formatDuration(widgetRef.superShowElapsedMs / 1000) +
                               " / " + popupRoot.formatDuration(popupRoot.showTotalSec(popupRoot.selectedShow))) : ""
                        fontBold: true
                    }
                }

                RobotoText
                {
                    Layout.fillWidth: true
                    wrapText: true
                    fontSize: UISettings.textSizeDefault * 0.85
                    fontItalic: true
                    label: qsTr("Each block below is one section: pick which of this widget's Profiles runs, and for how long, then the next block takes over automatically. Everything inside a Profile keeps stepping on the Beat/Kick exactly as configured in the widget's own Properties panel - this timeline only decides which Profile is current, and when.")
                }

                // Live playhead progress bar
                Rectangle
                {
                    Layout.fillWidth: true
                    height: 6
                    radius: 3
                    color: UISettings.bgStrong
                    visible: popupRoot.isPlaying

                    Rectangle
                    {
                        height: parent.height
                        radius: 3
                        color: "limegreen"
                        width:
                        {
                            var total = popupRoot.showTotalSec(popupRoot.selectedShow) * 1000
                            if (total <= 0 || !widgetRef)
                                return 0
                            return parent.width * Math.min(1.0, widgetRef.superShowElapsedMs / total)
                        }
                    }
                }

                ScrollView
                {
                    Layout.fillWidth: true
                    Layout.fillHeight: true
                    clip: true
                    ScrollBar.vertical.policy: ScrollBar.AlwaysOff

                    RowLayout
                    {
                        height: 140
                        spacing: 4

                        Repeater
                        {
                            model: popupRoot.selectedShow ? popupRoot.selectedShow.sections : []

                            Rectangle
                            {
                                id: sectionBlock
                                property int sectionIndex: index
                                property bool isActiveSection: popupRoot.isPlaying && widgetRef.activeSuperShowSectionIndex === index
                                Layout.preferredWidth: Math.max(120, modelData.durationSec / 2)
                                Layout.fillHeight: true
                                color: isActiveSection ? "#2a4d2a" : UISettings.bgMedium
                                border.width: isActiveSection ? 2 : 1
                                border.color: isActiveSection ? "limegreen" : UISettings.borderColorDark
                                radius: 3

                                ColumnLayout
                                {
                                    anchors.fill: parent
                                    anchors.margins: 6
                                    spacing: 4

                                    RowLayout
                                    {
                                        Layout.fillWidth: true
                                        RobotoText { Layout.fillWidth: true; label: (sectionBlock.sectionIndex + 1) + "."; fontBold: true }
                                        IconButton
                                        {
                                            width: UISettings.iconSizeDefault
                                            height: UISettings.iconSizeDefault
                                            faSource: FontAwesome.fa_trash
                                            tooltip: qsTr("Delete this section")
                                            onClicked: if (widgetRef) widgetRef.removeSectionFromSuperShow(popupRoot.selectedShowIndex, sectionBlock.sectionIndex)
                                        }
                                    }

                                    CustomComboBox
                                    {
                                        Layout.fillWidth: true
                                        textRole: "mLabel"
                                        valueRole: "mValue"
                                        model: popupRoot.profileComboModel()
                                        currentIndex:
                                        {
                                            for (var i = 0; i < model.length; i++)
                                                if (model[i].mValue === modelData.profileId)
                                                    return i
                                            return -1
                                        }
                                        onActivated: (idx) =>
                                        {
                                            if (widgetRef)
                                                widgetRef.setSectionProfile(popupRoot.selectedShowIndex, sectionBlock.sectionIndex, model[idx].mValue)
                                        }
                                    }

                                    RowLayout
                                    {
                                        Layout.fillWidth: true
                                        RobotoText { label: qsTr("sec") }
                                        CustomSpinBox
                                        {
                                            Layout.fillWidth: true
                                            from: 1
                                            to: 7200
                                            value: modelData.durationSec
                                            onValueModified: if (widgetRef) widgetRef.setSectionDuration(popupRoot.selectedShowIndex, sectionBlock.sectionIndex, value)
                                        }
                                    }
                                }
                            }
                        }

                        // "+ Section" append control
                        ColumnLayout
                        {
                            Layout.preferredWidth: 140
                            Layout.fillHeight: true
                            spacing: 4

                            RobotoText { label: qsTr("Add section:") }
                            CustomComboBox
                            {
                                id: addSectionCombo
                                Layout.fillWidth: true
                                textRole: "mLabel"
                                valueRole: "mValue"
                                model: popupRoot.profileComboModel()
                                currentIndex: 0
                            }
                            GenericButton
                            {
                                Layout.fillWidth: true
                                label: qsTr("+ 60s")
                                onClicked:
                                {
                                    if (!widgetRef || addSectionCombo.model.length === 0)
                                        return
                                    var profileId = addSectionCombo.model[addSectionCombo.currentIndex].mValue
                                    widgetRef.addSectionToSuperShow(popupRoot.selectedShowIndex, profileId, 60)
                                }
                            }
                        }
                    }
                }
            }

            RobotoText
            {
                Layout.fillWidth: true
                Layout.fillHeight: true
                visible: widgetRef === null
                label: qsTr("No Music Reactive widget found in this project - add one to the Virtual Console first.")
                textHAlign: Text.AlignHCenter
                wrapText: true
            }
        }
}
