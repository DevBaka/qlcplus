/*
  Q Light Controller Plus
  EffectSceneCard.qml

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

import "."

Rectangle
{
    id: card
    width: parent ? parent.width : 220
    // implicitHeight (not a plain height binding) so a ColumnLayout parent
    // (see EffectGroupColumn.qml) sizes this row correctly via
    // Layout.preferredHeight's implicitHeight fallback - a raw height:
    // binding is not reliably honored once an item is Layout-managed.
    implicitHeight: 58
    clip: true // a Rectangle doesn't clip its children by default - without
               // this, the name+detail column can render a couple of
               // pixels taller than the card itself and bleed into
               // whatever's directly below it (the next card)
    radius: 4
    color: sceneData.running ? UISettings.highlightPressed : UISettings.bgMedium
    border.width: 1
    border.color: sceneData.running ? UISettings.selection : UISettings.borderColorDark

    property var sceneData: ({})

    signal editRequested(int id, bool isShow)
    signal deleteRequested(int id)
    signal moveRequested(int id)

    RowLayout
    {
        anchors.fill: parent
        anchors.margins: 6
        spacing: 6

        IconButton
        {
            width: 30
            height: 30
            faSource: card.sceneData.running ? FontAwesome.fa_stop : FontAwesome.fa_play
            faColor: card.sceneData.running ? "#ff5555" : "#55dd55"
            tooltip: card.sceneData.running ? qsTr("Stoppen") : qsTr("Abspielen")
            onClicked: functionManager.effectToggleRun(card.sceneData.id)
        }

        Rectangle
        {
            width: 3
            height: parent.height - 8
            radius: 1
            color: card.sceneData.isShow ? "#4aa3ff" : "#ffb400"
        }

        ColumnLayout
        {
            Layout.fillWidth: true
            Layout.minimumWidth: 0
            spacing: 2

            // plain Text here, not RobotoText: RobotoText's root Rectangle
            // has a fixed height of UISettings.iconSizeDefault (a touch-
            // target size, DPI-scaled) regardless of font size - fine for
            // a single-line list item, but two of them stacked need far
            // more room than their actual text, which is what was causing
            // the card content to overflow past its own bounds
            Text
            {
                Layout.fillWidth: true
                Layout.minimumWidth: 0
                text: card.sceneData.name ? card.sceneData.name : ""
                color: UISettings.fgMain
                font.family: UISettings.robotoFontName
                font.pixelSize: UISettings.textSizeDefault
                font.bold: true
                elide: Text.ElideRight
            }
            RowLayout
            {
                spacing: 4
                IconButton
                {
                    width: 14
                    height: 14
                    enabled: false
                    faSource: card.sceneData.isShow ? FontAwesome.fa_film : FontAwesome.fa_shuffle
                }
                Text
                {
                    text: (card.sceneData.typeLabel ? card.sceneData.typeLabel : "") + " · " +
                          (card.sceneData.detail ? card.sceneData.detail : "")
                    color: UISettings.fgMain
                    font.family: UISettings.robotoFontName
                    font.pixelSize: UISettings.textSizeDefault * 0.8
                }
            }
        }

        IconButton
        {
            width: 26
            height: 26
            faSource: FontAwesome.fa_folder_open
            tooltip: qsTr("In andere Gruppe verschieben")
            onClicked: card.moveRequested(card.sceneData.id)
        }
        IconButton
        {
            width: 26
            height: 26
            faSource: FontAwesome.fa_pen
            tooltip: qsTr("Bearbeiten")
            onClicked: card.editRequested(card.sceneData.id, card.sceneData.isShow === true)
        }
        IconButton
        {
            width: 26
            height: 26
            faSource: FontAwesome.fa_trash
            tooltip: qsTr("Löschen")
            onClicked: card.deleteRequested(card.sceneData.id)
        }
    }
}
