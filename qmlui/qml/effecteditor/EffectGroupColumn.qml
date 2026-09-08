/*
  Q Light Controller Plus
  EffectGroupColumn.qml

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
import QtQuick.Controls.Basic

import "."

Rectangle
{
    id: column
    width: 320
    height: parent ? parent.height : 400
    radius: 5
    color: UISettings.bgStrong
    border.width: 1
    border.color: UISettings.borderColorDark

    property var groupData: ({})

    signal editRequested(int id, bool isShow)
    signal deleteRequested(int id)
    signal moveRequested(int id)
    signal newSuperScene(string groupPath)
    signal newSuperChaser(string groupPath)
    signal renameGroup(string path)
    signal deleteGroup(string path)

    ColumnLayout
    {
        anchors.fill: parent
        anchors.margins: 6
        spacing: 6

        RowLayout
        {
            Layout.fillWidth: true
            spacing: 4

            RobotoText
            {
                Layout.fillWidth: true
                label: column.groupData.label ? column.groupData.label : ""
                fontBold: true
                fontSize: UISettings.textSizeDefault * 1.05
            }

            IconButton
            {
                visible: column.groupData.removable === true
                width: 22
                height: 22
                faSource: FontAwesome.fa_pen
                tooltip: qsTr("Gruppe umbenennen")
                onClicked: column.renameGroup(column.groupData.path)
            }
            IconButton
            {
                visible: column.groupData.removable === true
                width: 22
                height: 22
                faSource: FontAwesome.fa_trash
                enabled: column.groupData.scenes && column.groupData.scenes.length === 0
                tooltip: (column.groupData.scenes && column.groupData.scenes.length === 0) ?
                            qsTr("Leere Gruppe löschen") : qsTr("Erst alle Super Szenen/Shows aus dieser Gruppe verschieben")
                onClicked: column.deleteGroup(column.groupData.path)
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
                width: column.width - 12
                spacing: 6

                Repeater
                {
                    model: column.groupData.scenes ? column.groupData.scenes : []

                    delegate:
                        EffectSceneCard
                        {
                            Layout.fillWidth: true
                            sceneData: modelData
                            onEditRequested: (id, isShow) => column.editRequested(id, isShow)
                            onDeleteRequested: (id) => column.deleteRequested(id)
                            onMoveRequested: (id) => column.moveRequested(id)
                        }
                }

                RobotoText
                {
                    Layout.fillWidth: true
                    visible: !column.groupData.scenes || column.groupData.scenes.length === 0
                    label: qsTr("Noch leer")
                    fontItalic: true
                    textHAlign: Text.AlignHCenter
                }
            }
        }

        Rectangle { Layout.fillWidth: true; height: 1; color: UISettings.borderColorDark }

        RowLayout
        {
            Layout.fillWidth: true
            spacing: 4

            GenericButton
            {
                Layout.fillWidth: true
                label: qsTr("+ Super Szene")
                fontSize: UISettings.textSizeDefault * 0.85
                onClicked: column.newSuperScene(column.groupData.path)
            }
            GenericButton
            {
                Layout.fillWidth: true
                label: qsTr("+ Show")
                fontSize: UISettings.textSizeDefault * 0.85
                onClicked: column.newSuperChaser(column.groupData.path)
            }
        }
    }
}
