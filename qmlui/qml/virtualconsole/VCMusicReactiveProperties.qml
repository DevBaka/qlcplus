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
    // should feed: "profile", "ambientSet", "group", or "" when the
    // panel wasn't opened from here at all. editingProfileIndex/
    // editingAmbientSetIndex/editingGroupIndex says which one's list
    // it's feeding.
    property string addTarget: ""
    property int editingProfileIndex: -1
    property int editingAmbientSetIndex: -1
    property int editingGroupIndex: -1
    property int editingSuperChaserIndex: -1

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
            else if (addTarget === "ambientSet" && editingAmbientSetIndex >= 0)
                widgetRef.addFunctionToAmbientSet(editingAmbientSetIndex, id)
            else if (addTarget === "group" && editingGroupIndex >= 0)
                widgetRef.addMemberToGroup(editingGroupIndex, id)
            else if (addTarget === "superChaser" && editingSuperChaserIndex >= 0)
                widgetRef.addTrackToSuperChaser(editingSuperChaserIndex, id)
        }
    }

    function closeFunctionPanel()
    {
        rightSidePanel.width -= sideLoader.width
        sideLoader.source = ""
        sideLoader.visible = false
        addTarget = ""
        editingProfileIndex = -1
        editingAmbientSetIndex = -1
        editingGroupIndex = -1
        editingSuperChaserIndex = -1
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

    function toggleAmbientSetFunctionPanel(setIndex)
    {
        if (sideLoader.visible && addTarget === "ambientSet" && editingAmbientSetIndex === setIndex)
        {
            closeFunctionPanel()
            return
        }

        if (!sideLoader.visible)
            rightSidePanel.width += UISettings.sidePanelWidth
        sideLoader.visible = true
        sideLoader.modelProvider = widgetRef
        sideLoader.source = "qrc:/FunctionManager.qml"
        addTarget = "ambientSet"
        editingAmbientSetIndex = setIndex
    }

    function toggleGroupMemberPanel(groupIndex)
    {
        if (sideLoader.visible && addTarget === "group" && editingGroupIndex === groupIndex)
        {
            closeFunctionPanel()
            return
        }

        if (!sideLoader.visible)
            rightSidePanel.width += UISettings.sidePanelWidth
        sideLoader.visible = true
        sideLoader.modelProvider = widgetRef
        sideLoader.source = "qrc:/FunctionManager.qml"
        addTarget = "group"
        editingGroupIndex = groupIndex
    }

    function toggleSuperChaserTrackPanel(superChaserIndex)
    {
        if (sideLoader.visible && addTarget === "superChaser" && editingSuperChaserIndex === superChaserIndex)
        {
            closeFunctionPanel()
            return
        }

        if (!sideLoader.visible)
            rightSidePanel.width += UISettings.sidePanelWidth
        sideLoader.visible = true
        sideLoader.modelProvider = widgetRef
        sideLoader.source = "qrc:/FunctionManager.qml"
        addTarget = "superChaser"
        editingSuperChaserIndex = superChaserIndex
    }

    // Groups are this widget's own concept, not real engine Functions -
    // FunctionManager.qml (the sideLoader picker above) can't list them,
    // so adding an existing Group to a Profile/Ambient Set uses a plain
    // CustomComboBox instead (one per Profile/Ambient Set row, right
    // where the "+list" (function) button is - see addGroupComboModel()
    // for the shared model shape). A bare Popup was tried here first and
    // never actually appeared - this properties panel is loaded into a
    // side-panel Loader, not directly under an ApplicationWindow, and a
    // Popup's default overlay reparenting doesn't reliably resolve
    // through that. CustomComboBox is used instead specifically because
    // it's already proven to work in this exact kind of side-loaded
    // Properties panel (see e.g. VCSliderProperties.qml).
    function addGroupComboModel()
    {
        var list = [ { mLabel: qsTr("+ Group"), mValue: -1 } ]
        if (widgetRef && widgetRef.groups)
        {
            for (var i = 0; i < widgetRef.groups.length; i++)
                list.push({ mLabel: widgetRef.groups[i].name, mValue: widgetRef.groups[i].id })
        }
        return list
    }

    // Same idea, for Super Chasers - see the class comment above
    // superChasers() (vcmusicreactive.h) for why a Super Chaser is the
    // right tool for "chasers that keep stepping in time with the music
    // as a bundle", not a Show (see the "Show" badge/section below).
    function addSuperChaserComboModel()
    {
        var list = [ { mLabel: qsTr("+ Super Chaser"), mValue: -1 } ]
        if (widgetRef && widgetRef.superChasers)
        {
            for (var i = 0; i < widgetRef.superChasers.length; i++)
                list.push({ mLabel: widgetRef.superChasers[i].name, mValue: widgetRef.superChasers[i].id })
        }
        return list
    }

    // For a Super Chaser's own track list, tracks can be a plain Chaser/
    // Scene (via the "list" button, see toggleSuperChaserTrackPanel()) or
    // an existing Group (via this combo) - but never another Super
    // Chaser, so this is deliberately the same shape as addGroupComboModel()
    // and nothing more.
    function addGroupTrackComboModel()
    {
        return addGroupComboModel()
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
            id: groupsSection
            sectionLabel: qsTr("Groups")

            sectionContents:
                ColumnLayout
                {
                    width: parent.width
                    spacing: 6

                    RobotoText
                    {
                        Layout.fillWidth: true
                        wrapText: true
                        fontSize: UISettings.textSizeDefault * 0.85
                        label: qsTr("A Group bundles Scenes/Chasers so they can be added to a Profile's Chaser list or an Idle Set's function list as a single unit. Parallel: every member runs together, same as adding them individually. Rotate: only one member runs at a time, switching to the next automatically every set number of seconds - independent of the beat.")
                    }

                    RowLayout
                    {
                        Layout.fillWidth: true

                        RobotoText
                        {
                            Layout.fillWidth: true
                            label: qsTr("Add a new Group")
                        }
                        IconButton
                        {
                            width: gridItemsHeight
                            height: gridItemsHeight
                            faSource: FontAwesome.fa_plus
                            tooltip: qsTr("Add Group")
                            onClicked: if (widgetRef) widgetRef.addGroup("")
                        }
                    }

                    Repeater
                    {
                        model: widgetRef ? widgetRef.groups : null

                        ColumnLayout
                        {
                            id: groupDelegate
                            Layout.fillWidth: true
                            spacing: 2

                            property int groupIndex: index

                            Rectangle
                            {
                                Layout.fillWidth: true
                                height: 1
                                color: UISettings.bgLight
                            }

                            RowLayout
                            {
                                Layout.fillWidth: true

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
                                    checked: sideLoader.visible && addTarget === "group" && editingGroupIndex === groupDelegate.groupIndex
                                    tooltip: qsTr("Open the list, then double-click a Scene or Chaser to add it to this Group")
                                    onClicked: toggleGroupMemberPanel(groupDelegate.groupIndex)
                                }
                                IconButton
                                {
                                    width: gridItemsHeight
                                    height: gridItemsHeight
                                    faSource: FontAwesome.fa_trash
                                    tooltip: qsTr("Delete Group")
                                    onClicked: if (widgetRef) widgetRef.removeGroupAt(groupDelegate.groupIndex)
                                }
                            }

                            RowLayout
                            {
                                Layout.fillWidth: true
                                Layout.leftMargin: 20

                                CustomCheckBox
                                {
                                    checked: modelData.rotate
                                    onClicked: if (widgetRef) widgetRef.setGroupRotate(groupDelegate.groupIndex, checked)
                                    tooltip: qsTr("On: only one member runs at a time, switching automatically. Off: every member runs together (Parallel).")
                                }
                                RobotoText
                                {
                                    label: qsTr("Rotate every")
                                }
                                CustomSpinBox
                                {
                                    Layout.preferredWidth: 90
                                    height: gridItemsHeight
                                    enabled: modelData.rotate
                                    from: 1
                                    to: 3600
                                    suffix: qsTr("s")
                                    value: modelData.rotateIntervalSec
                                    onValueModified: if (widgetRef) widgetRef.setGroupRotateIntervalSec(groupDelegate.groupIndex, value)
                                }
                                Repeater
                                {
                                    // Quick-pick presets for the common
                                    // values, per the original request -
                                    // the spin box above still covers
                                    // "or whatever you've set" (any value).
                                    model: [10, 30, 60, 180]

                                    GenericButton
                                    {
                                        width: 40
                                        height: gridItemsHeight
                                        label: modelData + qsTr("s")
                                        onClicked: if (widgetRef) widgetRef.setGroupRotateIntervalSec(groupDelegate.groupIndex, modelData)
                                    }
                                }
                            }

                            ListView
                            {
                                id: groupMemberListView
                                Layout.fillWidth: true
                                Layout.leftMargin: 20
                                implicitHeight: Math.max(0, count * gridItemsHeight)
                                clip: true
                                model: modelData.members

                                delegate:
                                    RowLayout
                                    {
                                        width: groupMemberListView.width
                                        height: gridItemsHeight

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
                                            onClicked: if (widgetRef) widgetRef.removeMemberFromGroup(groupDelegate.groupIndex, index)
                                        }
                                    }
                            }
                        }
                    }
                }
        }

        SectionBox
        {
            id: superChasersSection
            sectionLabel: qsTr("Super Chasers")

            sectionContents:
                ColumnLayout
                {
                    width: parent.width
                    spacing: 6

                    RobotoText
                    {
                        Layout.fillWidth: true
                        wrapText: true
                        fontSize: UISettings.textSizeDefault * 0.85
                        label: qsTr("A Super Chaser bundles several Chasers/Scenes/Groups as parallel \"tracks\" that all keep stepping in time with the music (Daslight 5's \"Super Scene\") - add it to a Profile's Chaser list or an Idle Set just like a Group. Unlike a \"Show\" (below), a Super Chaser has no fixed timeline - every enabled track just runs for as long as the Super Chaser itself is active, beat/kick-synced exactly like a plain Profile Chaser.")
                    }

                    RowLayout
                    {
                        Layout.fillWidth: true

                        RobotoText
                        {
                            Layout.fillWidth: true
                            label: qsTr("Add a new Super Chaser")
                        }
                        IconButton
                        {
                            width: gridItemsHeight
                            height: gridItemsHeight
                            faSource: FontAwesome.fa_plus
                            tooltip: qsTr("Add Super Chaser")
                            onClicked: if (widgetRef) widgetRef.addSuperChaser("")
                        }
                    }

                    Repeater
                    {
                        model: widgetRef ? widgetRef.superChasers : null

                        ColumnLayout
                        {
                            id: superChaserDelegate
                            Layout.fillWidth: true
                            spacing: 2

                            property int superChaserIndex: index

                            Rectangle
                            {
                                Layout.fillWidth: true
                                height: 1
                                color: UISettings.bgLight
                            }

                            RowLayout
                            {
                                Layout.fillWidth: true

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
                                    checked: sideLoader.visible && addTarget === "superChaser" && editingSuperChaserIndex === superChaserDelegate.superChaserIndex
                                    tooltip: qsTr("Open the list, then double-click a Scene or Chaser to add it as a track")
                                    onClicked: toggleSuperChaserTrackPanel(superChaserDelegate.superChaserIndex)
                                }
                                IconButton
                                {
                                    width: gridItemsHeight
                                    height: gridItemsHeight
                                    faSource: FontAwesome.fa_trash
                                    tooltip: qsTr("Delete Super Chaser")
                                    onClicked: if (widgetRef) widgetRef.removeSuperChaserAt(superChaserDelegate.superChaserIndex)
                                }
                            }

                            RowLayout
                            {
                                Layout.fillWidth: true
                                Layout.leftMargin: 20

                                RobotoText { label: qsTr("Add a Group as a track:") }
                                CustomComboBox
                                {
                                    Layout.preferredWidth: 160
                                    height: gridItemsHeight
                                    textRole: "mLabel"
                                    valueRole: "mValue"
                                    model: addGroupTrackComboModel()
                                    currentIndex: 0
                                    onActivated: (idx) =>
                                    {
                                        var groupId = model[idx].mValue
                                        if (groupId >= 0 && widgetRef)
                                            widgetRef.addGroupTrackToSuperChaser(superChaserDelegate.superChaserIndex, groupId)
                                        currentIndex = 0
                                    }
                                }
                            }

                            ListView
                            {
                                id: superChaserTrackListView
                                Layout.fillWidth: true
                                Layout.leftMargin: 20
                                implicitHeight: Math.max(0, count * gridItemsHeight)
                                clip: true
                                model: modelData.tracks

                                delegate:
                                    RowLayout
                                    {
                                        width: superChaserTrackListView.width
                                        height: gridItemsHeight

                                        CustomCheckBox
                                        {
                                            checked: modelData.enabled
                                            onClicked: if (widgetRef) widgetRef.setTrackEnabledInSuperChaser(superChaserDelegate.superChaserIndex, index, checked)
                                            tooltip: qsTr("Enabled")
                                        }
                                        IconButton
                                        {
                                            visible: modelData.isGroup === true
                                            width: gridItemsHeight
                                            height: gridItemsHeight
                                            faSource: FontAwesome.fa_object_group
                                            enabled: false
                                            tooltip: qsTr("Group")
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
                                            onClicked: if (widgetRef) widgetRef.removeTrackFromSuperChaser(superChaserDelegate.superChaserIndex, index)
                                        }
                                    }
                            }
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
                                CustomComboBox
                                {
                                    Layout.preferredWidth: 110
                                    height: gridItemsHeight
                                    textRole: "mLabel"
                                    valueRole: "mValue"
                                    model: addGroupComboModel()
                                    currentIndex: 0
                                    onActivated: (idx) =>
                                    {
                                        var groupId = model[idx].mValue
                                        if (groupId >= 0 && widgetRef)
                                            widgetRef.addGroupToProfile(profileDelegate.profileIndex, groupId)
                                        currentIndex = 0
                                    }
                                }
                                CustomComboBox
                                {
                                    Layout.preferredWidth: 130
                                    height: gridItemsHeight
                                    textRole: "mLabel"
                                    valueRole: "mValue"
                                    model: addSuperChaserComboModel()
                                    currentIndex: 0
                                    onActivated: (idx) =>
                                    {
                                        var superChaserId = model[idx].mValue
                                        if (superChaserId >= 0 && widgetRef)
                                            widgetRef.addSuperChaserToProfile(profileDelegate.profileIndex, superChaserId)
                                        currentIndex = 0
                                    }
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
                                        IconButton
                                        {
                                            visible: modelData.isGroup === true
                                            width: gridItemsHeight
                                            height: gridItemsHeight
                                            faSource: FontAwesome.fa_object_group
                                            enabled: false
                                            tooltip: qsTr("Group")
                                        }
                                        IconButton
                                        {
                                            visible: modelData.isSuperChaser === true
                                            width: gridItemsHeight
                                            height: gridItemsHeight
                                            faSource: FontAwesome.fa_layer_group
                                            enabled: false
                                            tooltip: qsTr("Super Chaser")
                                        }
                                        IconButton
                                        {
                                            visible: modelData.isShow === true
                                            width: gridItemsHeight
                                            height: gridItemsHeight
                                            faSource: FontAwesome.fa_film
                                            enabled: false
                                            tooltip: qsTr("Show (fixed timeline, not beat-synced)")
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
            sectionLabel: qsTr("Idle (runs during a detected break)")

            sectionContents:
                ColumnLayout
                {
                    width: parent.width
                    spacing: 6

                    RobotoText
                    {
                        Layout.fillWidth: true
                        wrapText: true
                        fontSize: UISettings.textSizeDefault * 0.85
                        label: qsTr("Each Idle Set is a group of Scenes/Chasers that run together once the track goes quiet. Tick a Set active in the live widget to turn it on - with more than one active, a different one gets picked every time a break starts, so idle sections don't all look the same.")
                    }

                    RowLayout
                    {
                        Layout.fillWidth: true

                        RobotoText
                        {
                            Layout.fillWidth: true
                            label: qsTr("Add a new Idle Set")
                        }
                        IconButton
                        {
                            width: gridItemsHeight
                            height: gridItemsHeight
                            faSource: FontAwesome.fa_plus
                            tooltip: qsTr("Add Idle Set")
                            onClicked: if (widgetRef) widgetRef.addAmbientSet("")
                        }
                    }

                    Repeater
                    {
                        model: widgetRef ? widgetRef.ambientSets : null

                        ColumnLayout
                        {
                            id: ambientSetDelegate
                            Layout.fillWidth: true
                            spacing: 2

                            property int setIndex: index

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
                                    onClicked: if (widgetRef) widgetRef.setAmbientSetActive(ambientSetDelegate.setIndex, checked)
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
                                    checked: sideLoader.visible && addTarget === "ambientSet" && editingAmbientSetIndex === ambientSetDelegate.setIndex
                                    tooltip: qsTr("Open the list, then double-click a Scene or Chaser to add it to this Set")
                                    onClicked: toggleAmbientSetFunctionPanel(ambientSetDelegate.setIndex)
                                }
                                CustomComboBox
                                {
                                    Layout.preferredWidth: 110
                                    height: gridItemsHeight
                                    textRole: "mLabel"
                                    valueRole: "mValue"
                                    model: addGroupComboModel()
                                    currentIndex: 0
                                    onActivated: (idx) =>
                                    {
                                        var groupId = model[idx].mValue
                                        if (groupId >= 0 && widgetRef)
                                            widgetRef.addGroupToAmbientSet(ambientSetDelegate.setIndex, groupId)
                                        currentIndex = 0
                                    }
                                }
                                CustomComboBox
                                {
                                    Layout.preferredWidth: 130
                                    height: gridItemsHeight
                                    textRole: "mLabel"
                                    valueRole: "mValue"
                                    model: addSuperChaserComboModel()
                                    currentIndex: 0
                                    onActivated: (idx) =>
                                    {
                                        var superChaserId = model[idx].mValue
                                        if (superChaserId >= 0 && widgetRef)
                                            widgetRef.addSuperChaserToAmbientSet(ambientSetDelegate.setIndex, superChaserId)
                                        currentIndex = 0
                                    }
                                }
                                IconButton
                                {
                                    width: gridItemsHeight
                                    height: gridItemsHeight
                                    faSource: FontAwesome.fa_trash
                                    tooltip: qsTr("Delete Idle Set")
                                    onClicked: if (widgetRef) widgetRef.removeAmbientSetAt(ambientSetDelegate.setIndex)
                                }
                            }

                            ListView
                            {
                                id: ambientSetFunctionListView
                                Layout.fillWidth: true
                                Layout.leftMargin: 20
                                implicitHeight: Math.max(0, count * gridItemsHeight)
                                clip: true
                                model: modelData.functions

                                delegate:
                                    RowLayout
                                    {
                                        width: ambientSetFunctionListView.width
                                        height: gridItemsHeight

                                        CustomCheckBox
                                        {
                                            checked: modelData.enabled
                                            onClicked: if (widgetRef) widgetRef.setFunctionEnabledInAmbientSet(ambientSetDelegate.setIndex, index, checked)
                                            tooltip: qsTr("Enabled")
                                        }
                                        IconButton
                                        {
                                            visible: modelData.isGroup === true
                                            width: gridItemsHeight
                                            height: gridItemsHeight
                                            faSource: FontAwesome.fa_object_group
                                            enabled: false
                                            tooltip: qsTr("Group")
                                        }
                                        IconButton
                                        {
                                            visible: modelData.isSuperChaser === true
                                            width: gridItemsHeight
                                            height: gridItemsHeight
                                            faSource: FontAwesome.fa_layer_group
                                            enabled: false
                                            tooltip: qsTr("Super Chaser")
                                        }
                                        IconButton
                                        {
                                            visible: modelData.isShow === true
                                            width: gridItemsHeight
                                            height: gridItemsHeight
                                            faSource: FontAwesome.fa_film
                                            enabled: false
                                            tooltip: qsTr("Show (fixed timeline, not beat-synced)")
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
                                            onClicked: if (widgetRef) widgetRef.removeFunctionFromAmbientSet(ambientSetDelegate.setIndex, index)
                                        }
                                    }
                            }
                        }
                    }
                }
        }
    }
}
