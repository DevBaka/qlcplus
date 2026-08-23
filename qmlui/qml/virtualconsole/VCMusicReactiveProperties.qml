/*
  Q Light Controller Plus
  VCMusicReactiveProperties.qml

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

Rectangle
{
    id: propsRoot
    color: "transparent"
    height: musicReactivePropsColumn.height

    property VCMusicReactive widgetRef: null

    property int gridItemsHeight: UISettings.listItemHeight
    property var bandNames: [ qsTr("Master"), qsTr("Kick"), qsTr("Mids") ]

    // Which list a double-click in the (shared) function side panel
    // should feed: "profile", "ambient", or "" when the panel wasn't
    // opened from here at all. For "profile", editingProfileIndex says
    // which Profile's Chaser list it's feeding.
    property string addTarget: ""
    property int editingProfileIndex: -1

    // The function side panel (sideLoader, declared in
    // VCWidgetProperties.qml) sets allowEditing = false on whatever it
    // loads, which makes FunctionManager.qml emit doubleClicked(ID, type)
    // on a double-click instead of opening the function editor - the
    // supported way for a side-loaded picker to receive a pick. Plain
    // drag-and-drop onto a DropArea in a *different* Loader branch of
    // the tree turned out not to work reliably (no hover/drop events
    // ever fired) - this direct signal-based pick replaces it as the
    // real interaction, kept below.
    Connections
    {
        target: sideLoader.item
        enabled: sideLoader.visible && sideLoader.source == "qrc:/FunctionManager.qml"
        function onDoubleClicked(id, type)
        {
            if (!widgetRef)
                return
            if (addTarget === "profile" && editingProfileIndex >= 0)
                widgetRef.addChaserToProfile(editingProfileIndex, id)
            else if (addTarget === "ambient")
                widgetRef.addAmbientFunction(id)
        }
    }

    function closeFunctionPanel()
    {
        rightSidePanel.width -= sideLoader.width
        sideLoader.source = ""
        sideLoader.visible = false
        addTarget = ""
        editingProfileIndex = -1
    }

    function toggleFunctionPanel(target)
    {
        if (sideLoader.visible && addTarget === target)
        {
            closeFunctionPanel()
            return
        }

        if (!sideLoader.visible)
            rightSidePanel.width += UISettings.sidePanelWidth
        sideLoader.visible = true
        sideLoader.modelProvider = widgetRef
        sideLoader.source = "qrc:/FunctionManager.qml"
        addTarget = target
    }

    function toggleProfileFunctionPanel(profileIndex)
    {
        if (sideLoader.visible && addTarget === "profile" && editingProfileIndex === profileIndex)
        {
            closeFunctionPanel()
            return
        }

        if (!sideLoader.visible)
            rightSidePanel.width += UISettings.sidePanelWidth
        sideLoader.visible = true
        sideLoader.modelProvider = widgetRef
        sideLoader.source = "qrc:/FunctionManager.qml"
        addTarget = "profile"
        editingProfileIndex = profileIndex
    }

    Column
    {
        id: musicReactivePropsColumn
        width: parent.width
        spacing: 5

        SectionBox
        {
            sectionLabel: qsTr("Gain (meter volume, does not affect triggering)")

            sectionContents:
                GridLayout
                {
                    width: parent.width
                    columns: 3
                    columnSpacing: 5
                    rowSpacing: 4

                    Repeater
                    {
                        model: 3

                        RobotoText
                        {
                            height: gridItemsHeight
                            label: bandNames[index]
                        }
                    }
                    Repeater
                    {
                        model: 3

                        CustomSlider
                        {
                            Layout.fillWidth: true
                            height: gridItemsHeight
                            from: 0
                            to: 100
                            value: widgetRef && widgetRef.intensities.length > index ? widgetRef.intensities[index] : 100
                            onMoved: if (widgetRef) widgetRef.setIntensity(index, Math.round(value))
                        }
                    }
                }
        }

        SectionBox
        {
            id: profilesSection
            sectionLabel: qsTr("Profiles")

            sectionContents:
                ColumnLayout
                {
                    width: parent.width
                    spacing: 6

                    RowLayout
                    {
                        Layout.fillWidth: true

                        CustomCheckBox
                        {
                            checked: widgetRef ? widgetRef.advanceOnBeat : false
                            onClicked: if (widgetRef) widgetRef.advanceOnBeat = checked
                            tooltip: qsTr("On: advances every tracked Beat (smooth, always in tempo, but purely mechanical). Off: advances every detected Kick (precise, but the detector is sparse - can feel like it barely moves on some tracks)")
                        }
                        RobotoText
                        {
                            Layout.fillWidth: true
                            wrapText: true
                            label: widgetRef && widgetRef.advanceOnBeat ?
                                       qsTr("Chasers advance one step per Beat (tempo-locked)") :
                                       qsTr("Chasers advance one step per Kick (precise, can be sparse)")
                        }
                    }

                    RobotoText
                    {
                        Layout.fillWidth: true
                        wrapText: true
                        fontSize: UISettings.textSizeDefault * 0.85
                        label: qsTr("Each Profile is a group of Chasers that run together (e.g. one for PARs, one for moving heads). Tick a Profile active in the live widget to turn it on - with more than one active, they automatically rotate which one is in control every few bars, so the show keeps changing.")
                    }

                    RowLayout
                    {
                        Layout.fillWidth: true

                        RobotoText
                        {
                            Layout.fillWidth: true
                            label: qsTr("Add a new Profile")
                        }
                        IconButton
                        {
                            width: gridItemsHeight
                            height: gridItemsHeight
                            faSource: FontAwesome.fa_plus
                            tooltip: qsTr("Add Profile")
                            onClicked: if (widgetRef) widgetRef.addProfile("")
                        }
                    }

                    Repeater
                    {
                        model: widgetRef ? widgetRef.profiles : null

                        ColumnLayout
                        {
                            id: profileDelegate
                            Layout.fillWidth: true
                            spacing: 2

                            property int profileIndex: index

                            Rectangle
                            {
                                Layout.fillWidth: true
                                height: 1
                                color: UISettings.bgLight
                            }

                            RowLayout
                            {
                                Layout.fillWidth: true

                                CustomCheckBox
                                {
                                    checked: modelData.active
                                    onClicked: if (widgetRef) widgetRef.setProfileActive(profileDelegate.profileIndex, checked)
                                    tooltip: qsTr("Active (participates in rotation)")
                                }
                                RobotoText
                                {
                                    Layout.fillWidth: true
                                    label: modelData.name
                                    fontBold: true
                                }
                                IconButton
                                {
                                    width: gridItemsHeight
                                    height: gridItemsHeight
                                    faSource: FontAwesome.fa_list
                                    checkable: true
                                    checked: sideLoader.visible && addTarget === "profile" && editingProfileIndex === profileDelegate.profileIndex
                                    tooltip: qsTr("Open the list, then double-click a Chaser to add it to this Profile")
                                    onClicked: toggleProfileFunctionPanel(profileDelegate.profileIndex)
                                }
                                IconButton
                                {
                                    width: gridItemsHeight
                                    height: gridItemsHeight
                                    faSource: FontAwesome.fa_trash
                                    tooltip: qsTr("Delete Profile")
                                    onClicked: if (widgetRef) widgetRef.removeProfileAt(profileDelegate.profileIndex)
                                }
                            }

                            ListView
                            {
                                id: profileChaserListView
                                Layout.fillWidth: true
                                Layout.leftMargin: 20
                                implicitHeight: Math.max(0, count * gridItemsHeight)
                                clip: true
                                model: modelData.chasers

                                delegate:
                                    RowLayout
                                    {
                                        width: profileChaserListView.width
                                        height: gridItemsHeight

                                        CustomCheckBox
                                        {
                                            checked: modelData.enabled
                                            onClicked: if (widgetRef) widgetRef.setChaserEnabledInProfile(profileDelegate.profileIndex, index, checked)
                                            tooltip: qsTr("Enabled")
                                        }
                                        RobotoText
                                        {
                                            Layout.fillWidth: true
                                            label: modelData.name
                                        }
                                        IconButton
                                        {
                                            width: gridItemsHeight
                                            height: gridItemsHeight
                                            faSource: FontAwesome.fa_trash
                                            onClicked: if (widgetRef) widgetRef.removeChaserFromProfile(profileDelegate.profileIndex, index)
                                        }
                                    }
                            }
                        }
                    }
                }
        }

        SectionBox
        {
            id: ambientSection
            sectionLabel: qsTr("Ambient (runs during a detected break/idle)")

            sectionContents:
                ColumnLayout
                {
                    width: parent.width
                    spacing: 2

                    RowLayout
                    {
                        Layout.fillWidth: true

                        RobotoText
                        {
                            Layout.fillWidth: true
                            wrapText: true
                            label: qsTr("Open the list, then double-click a Scene or Chaser to add it")
                        }
                        IconButton
                        {
                            width: gridItemsHeight
                            height: gridItemsHeight
                            faSource: FontAwesome.fa_list
                            checkable: true
                            checked: sideLoader.visible && addTarget === "ambient"
                            tooltip: qsTr("Show the function list")
                            onClicked: toggleFunctionPanel("ambient")
                        }
                    }

                    ListView
                    {
                        id: ambientListView
                        Layout.fillWidth: true
                        implicitHeight: Math.max(gridItemsHeight, count * gridItemsHeight)
                        clip: true
                        model: widgetRef ? widgetRef.ambientFunctions : null

                        delegate:
                            RowLayout
                            {
                                width: ambientListView.width
                                height: gridItemsHeight

                                CustomCheckBox
                                {
                                    checked: modelData.enabled
                                    onClicked: if (widgetRef) widgetRef.setAmbientEnabled(index, checked)
                                    tooltip: qsTr("Enabled")
                                }
                                RobotoText
                                {
                                    Layout.fillWidth: true
                                    label: modelData.name
                                }
                                IconButton
                                {
                                    width: gridItemsHeight
                                    height: gridItemsHeight
                                    faSource: FontAwesome.fa_trash
                                    onClicked: if (widgetRef) widgetRef.removeAmbientAt(index)
                                }
                            }
                    }
                }
        }
    }
}
