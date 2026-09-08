/*
  Q Light Controller Plus
  EffectEditor.qml

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

Rectangle
{
    id: effectEditorRoot
    anchors.fill: parent
    color: "transparent"
    clip: true

    property var groupsModel: []
    // which group a "+ Super Scene/Show" click is currently targeting,
    // while the name popup is open
    property string pendingGroupPath: ""
    property int pendingCreateType: 0 // 0 = Super Scene (Chaser), 1 = Show (fixed timeline - see the
                                       // class comment above VCMusicReactive::superChasers() for why the
                                       // *beat-synced* "Super Chaser" concept lives there instead, not here
    property string pendingRenameGroup: ""
    property int pendingMoveFunctionId: -1

    function refresh()
    {
        var fresh = functionManager.effectGroups()
        // reassigning groupsModel always fully re-creates every card in
        // every Repeater (fresh JS array each call) - skip it when nothing
        // actually changed, so the 500ms poll Timer below doesn't
        // needlessly flicker/re-layout the whole page every tick
        if (JSON.stringify(fresh) === JSON.stringify(groupsModel))
            return
        groupsModel = fresh
    }

    Component.onCompleted: refresh()

    // effectGroups() only carries a running snapshot, not a live binding -
    // a Function starting/stopping (via a card's own play button, or from
    // anywhere else in the app) doesn't emit any of the structural signals
    // below. Poll while this page is visible so the play/stop icon and
    // running highlight stay correct without needing per-Function signal
    // plumbing through to QML.
    Timer
    {
        interval: 500
        running: true
        repeat: true
        onTriggered: effectEditorRoot.refresh()
    }

    Connections
    {
        target: functionManager
        function onFunctionsListChanged() { effectEditorRoot.refresh() }
        function onChaserCountChanged() { effectEditorRoot.refresh() }
        function onShowCountChanged() { effectEditorRoot.refresh() }
    }

    function openEditor(id, isShow)
    {
        if (isShow)
        {
            // a Show is always edited in its own dedicated top-level tab -
            // ShowManager.qml is a genuine standalone page, safe to load
            // directly as the whole context's content
            showManager.currentShowID = id
            mainView.switchToContext("SHOWMGR", "qrc:/ShowManager.qml")
        }
        else
        {
            // a Chaser ("Super Scene") editor (ChaserWidget.qml) is only
            // ever meant to be loaded inside the Fixtures & Functions
            // page's own RightPanel sideLoader (it references that panel's
            // "rightSidePanel" id lexically) - loading it directly as the
            // whole context's content, like a Show, throws a
            // ReferenceError. mainView.switchToFunctionEditor() does this
            // correctly: switches to FIXANDFUNC, then asks RightPanel to
            // open the editor the same way a tree double-click does.
            mainView.switchToFunctionEditor(id)
        }
    }

    ColumnLayout
    {
        anchors.fill: parent
        spacing: 0

        Rectangle
        {
            id: topBar
            Layout.fillWidth: true
            height: UISettings.iconSizeMedium
            z: 5
            gradient: Gradient
            {
                GradientStop { position: 0; color: UISettings.toolbarStartSub }
                GradientStop { position: 1; color: UISettings.toolbarEnd }
            }

            RowLayout
            {
                anchors.fill: parent
                anchors.margins: 2
                spacing: 8

                RobotoText
                {
                    label: qsTr("Effekt Editor")
                    fontBold: true
                    fontSize: UISettings.textSizeDefault * 1.1
                }
                RobotoText
                {
                    Layout.fillWidth: true
                    label: qsTr("Super Szenen & Shows - getrennt von der Virtuellen Konsole, wie bei Daslight 5. Beat-synchrone \"Super Chaser\" werden im Music-Reactive-Widget selbst verwaltet (Eigenschaften-Panel); die \"Super Show Timeline\" hier oben schaltet zeitgesteuert zwischen dessen Profilen um.")
                    fontItalic: true
                    fontSize: UISettings.textSizeDefault * 0.85
                }

                IconButton
                {
                    z: 2
                    width: topBar.height - 2
                    height: topBar.height - 2
                    faSource: FontAwesome.fa_folder_plus
                    tooltip: qsTr("Neue Gruppe")
                    onClicked:
                    {
                        newGroupPopup.editText = ""
                        newGroupPopup.open()
                    }
                }
                IconButton
                {
                    z: 2
                    width: topBar.height - 2
                    height: topBar.height - 2
                    faSource: FontAwesome.fa_location_crosshairs
                    tooltip: qsTr("Moving Head Editor")
                    onClicked: movingEditorPopup.open()
                }
                IconButton
                {
                    z: 2
                    width: topBar.height - 2
                    height: topBar.height - 2
                    faSource: FontAwesome.fa_timeline
                    tooltip: qsTr("Super Show Timeline (beat-synced Profile scheduling)")
                    onClicked: superShowPopup.open()
                }
                IconButton
                {
                    z: 2
                    width: topBar.height - 2
                    height: topBar.height - 2
                    bgColor: UISettings.bgMedium
                    faColor: "white"
                    faSource: FontAwesome.fa_robot
                    tooltip: qsTr("KI Effekt Generator")
                    onClicked: aiPopup.open()
                }
            }
        }

        ScrollView
        {
            Layout.fillWidth: true
            Layout.fillHeight: true
            clip: true
            contentWidth: columnsRow.implicitWidth
            ScrollBar.vertical.policy: ScrollBar.AlwaysOff

            Row
            {
                id: columnsRow
                spacing: 10
                padding: 10
                height: effectEditorRoot.height - topBar.height - 20

                Repeater
                {
                    model: effectEditorRoot.groupsModel

                    delegate:
                        EffectGroupColumn
                        {
                            height: columnsRow.height
                            groupData: modelData

                            onEditRequested: (id, isShow) => effectEditorRoot.openEditor(id, isShow)
                            onDeleteRequested: (id) =>
                            {
                                functionManager.effectDeleteFunction(id)
                                effectEditorRoot.refresh()
                            }
                            onMoveRequested: (id) =>
                            {
                                effectEditorRoot.pendingMoveFunctionId = id
                                moveToGroupPopup.open()
                            }
                            onNewSuperScene: (groupPath) =>
                            {
                                effectEditorRoot.pendingGroupPath = groupPath
                                effectEditorRoot.pendingCreateType = 0
                                newSceneNamePopup.editText = qsTr("Super Szene")
                                newSceneNamePopup.open()
                            }
                            onNewSuperChaser: (groupPath) =>
                            {
                                effectEditorRoot.pendingGroupPath = groupPath
                                effectEditorRoot.pendingCreateType = 1
                                newSceneNamePopup.editText = qsTr("Show")
                                newSceneNamePopup.open()
                            }
                            onRenameGroup: (path) =>
                            {
                                effectEditorRoot.pendingRenameGroup = path
                                renameGroupPopup.editText = path
                                renameGroupPopup.open()
                            }
                            onDeleteGroup: (path) =>
                            {
                                functionManager.effectDeleteGroup(path)
                                effectEditorRoot.refresh()
                            }
                        }
                }
            }
        }
    }

    PopupRenameItems
    {
        id: newGroupPopup
        title: qsTr("Neue Gruppe")
        showNumbering: false

        onAccepted:
        {
            if (functionManager.effectCreateGroup(editText))
                effectEditorRoot.refresh()
        }
    }

    PopupRenameItems
    {
        id: renameGroupPopup
        title: qsTr("Gruppe umbenennen")
        showNumbering: false

        onAccepted:
        {
            if (functionManager.effectRenameGroup(effectEditorRoot.pendingRenameGroup, editText))
                effectEditorRoot.refresh()
        }
    }

    PopupRenameItems
    {
        id: newSceneNamePopup
        title: effectEditorRoot.pendingCreateType === 1 ? qsTr("Neue Show") : qsTr("Neue Super Szene")
        showNumbering: false

        onAccepted:
        {
            var newId
            if (effectEditorRoot.pendingCreateType === 1)
                newId = functionManager.effectCreateSuperChaser(effectEditorRoot.pendingGroupPath, editText)
            else
                newId = functionManager.effectCreateSuperScene(effectEditorRoot.pendingGroupPath, editText)

            effectEditorRoot.refresh()

            if (newId !== undefined && newId !== 4294967295)
                effectEditorRoot.openEditor(newId, effectEditorRoot.pendingCreateType === 1)
        }
    }

    CustomPopupDialog
    {
        id: moveToGroupPopup
        width: mainView.width / 3
        title: qsTr("In Gruppe verschieben")
        standardButtons: Dialog.Ok | Dialog.Cancel

        onOpened: groupCombo.currentIndex = 0

        onAccepted:
        {
            if (effectEditorRoot.pendingMoveFunctionId < 0)
                return
            var idx = groupCombo.currentIndex
            var path = (idx >= 0 && idx < effectEditorRoot.groupsModel.length) ?
                        effectEditorRoot.groupsModel[idx].path : ""
            functionManager.effectMoveFunctionToGroup(effectEditorRoot.pendingMoveFunctionId, path)
            effectEditorRoot.refresh()
        }

        contentItem:
            RowLayout
            {
                width: moveToGroupPopup.width
                RobotoText { label: qsTr("Zielgruppe") }
                CustomComboBox
                {
                    id: groupCombo
                    Layout.fillWidth: true
                    height: UISettings.listItemHeight
                    textRole: ""
                    model:
                    {
                        var labels = []
                        for (var i = 0; i < effectEditorRoot.groupsModel.length; i++)
                            labels.push(effectEditorRoot.groupsModel[i].label)
                        return labels
                    }
                }
            }
    }

    PopupAIEffectGenerator
    {
        id: aiPopup
    }

    PopupMovingEditor
    {
        id: movingEditorPopup
    }

    PopupSuperShowEditor
    {
        id: superShowPopup
    }
}
