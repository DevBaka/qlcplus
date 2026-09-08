/*
  Q Light Controller Plus
  PopupAIEffectGenerator.qml

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
    width: mainView.width * 0.6
    title: qsTr("KI Effekt Generator")
    standardButtons: Dialog.Close
    closePolicy: Popup.CloseOnEscape

    property string resultText: ""
    property bool resultIsError: false

    onOpened:
    {
        resultText = ""
        functionManager.aiRefreshModels()
    }

    function generate()
    {
        if (functionManager.aiBusy || promptField.text.trim().length === 0)
            return
        resultText = ""
        functionManager.aiGenerate(promptField.text)
    }

    function quickSetup()
    {
        if (functionManager.aiBusy)
            return
        resultText = ""
        functionManager.aiQuickSetup()
    }

    Connections
    {
        target: functionManager

        function onAiFinished(summary)
        {
            resultIsError = false
            resultText = summary
        }
        function onAiFailed(error)
        {
            resultIsError = true
            resultText = error
        }
    }

    contentItem:
        ColumnLayout
        {
            width: popupRoot.width
            spacing: 8

            RobotoText
            {
                Layout.fillWidth: true
                wrapText: true
                label: qsTr("Beschreibe eine Szene oder einen Chaser in eigenen Worten, oder klicke auf eines der Beispiele. Die KI kennt alle vorhandenen Fixtures und ihre Anordnung im 2D-Designer.")
                fontItalic: true
            }

            // "no ideas yet?" shortcut - instantly builds a curated starter pack
            // without going through Ollama, so it works even with no specific idea
            Rectangle
            {
                Layout.fillWidth: true
                height: quickSetupRow.implicitHeight + 16
                radius: 4
                color: UISettings.bgMedium
                border.width: 1
                border.color: UISettings.borderColorDark

                RowLayout
                {
                    id: quickSetupRow
                    anchors.fill: parent
                    anchors.margins: 8
                    spacing: 10

                    Text
                    {
                        text: "✨"
                        font.pixelSize: UISettings.textSizeDefault * 1.4
                    }

                    Text
                    {
                        Layout.fillWidth: true
                        wrapMode: Text.Wrap
                        text: qsTr("Noch keine Idee? Erstellt sofort ein fertiges Set aus mehreren Beispiel-Effekten (Farben, Chase, Regenbogen, Zufallspunkte, Strobe, ggf. gespiegelte Moving-Head-Bewegung) - ganz ohne Texteingabe.")
                        color: UISettings.fgMain
                        font.family: UISettings.robotoFontName
                        font.pixelSize: UISettings.textSizeDefault * 0.9
                        font.italic: true
                    }

                    GenericButton
                    {
                        label: qsTr("Inspirations-Set erstellen")
                        width: contentWidth + 20
                        enabled: !functionManager.aiBusy
                        onClicked: popupRoot.quickSetup()
                    }
                }
            }

            // preset "chips"
            Flow
            {
                Layout.fillWidth: true
                spacing: 4

                Repeater
                {
                    model: functionManager.aiPresetPrompts

                    Rectangle
                    {
                        width: presetLabel.implicitWidth + 16
                        height: presetLabel.implicitHeight + 10
                        radius: height / 2
                        color: presetArea.containsMouse ? UISettings.highlight : UISettings.bgMedium
                        border.width: 1
                        border.color: UISettings.borderColorDark

                        Text
                        {
                            id: presetLabel
                            anchors.centerIn: parent
                            text: modelData.length > 42 ? (modelData.substring(0, 42) + "…") : modelData
                            color: UISettings.fgMain
                            font.family: UISettings.robotoFontName
                            font.pixelSize: UISettings.textSizeDefault * 0.9
                        }

                        MouseArea
                        {
                            id: presetArea
                            anchors.fill: parent
                            hoverEnabled: true
                            onClicked: promptField.text = modelData
                        }
                    }
                }
            }

            RowLayout
            {
                Layout.fillWidth: true
                spacing: 8

                RobotoText { label: qsTr("KI-Modell") }

                CustomComboBox
                {
                    id: modelCombo
                    Layout.fillWidth: true
                    height: UISettings.listItemHeight
                    textRole: ""
                    model: functionManager.aiAvailableModels
                    onCurrentTextChanged:
                        if (currentText.length) functionManager.aiSelectedModel = currentText

                    Component.onCompleted:
                    {
                        var idx = functionManager.aiAvailableModels.indexOf(functionManager.aiSelectedModel)
                        if (idx >= 0)
                            currentIndex = idx
                    }
                }

                IconButton
                {
                    width: UISettings.iconSizeMedium
                    height: UISettings.iconSizeMedium
                    faSource: FontAwesome.fa_rotate
                    tooltip: qsTr("Modellliste aktualisieren")
                    onClicked: functionManager.aiRefreshModels()
                }
            }

            Rectangle
            {
                Layout.fillWidth: true
                height: UISettings.bigItemHeight * 1.4
                color: UISettings.bgLighter
                border.width: 1
                border.color: UISettings.borderColorDark
                radius: 3

                ScrollView
                {
                    anchors.fill: parent
                    anchors.margins: 4
                    clip: true

                    TextArea
                    {
                        id: promptField
                        wrapMode: TextArea.Wrap
                        selectByMouse: true
                        font.family: UISettings.robotoFontName
                        font.pixelSize: UISettings.textSizeDefault
                        color: UISettings.fgMain
                        placeholderText: qsTr("z.B. \"alle Fixtures auf Rot\" oder \"erstelle einen Rainbow-Effekt\"")

                        Keys.onReturnPressed: (event) =>
                        {
                            if (event.modifiers & Qt.ControlModifier)
                            {
                                popupRoot.generate()
                                event.accepted = true
                            }
                        }
                    }
                }
            }

            RowLayout
            {
                Layout.fillWidth: true
                spacing: 8

                BusyIndicator
                {
                    visible: functionManager.aiBusy
                    running: functionManager.aiBusy
                    width: UISettings.iconSizeMedium
                    height: UISettings.iconSizeMedium
                }

                RobotoText
                {
                    Layout.fillWidth: true
                    label: functionManager.aiStatus
                    wrapText: true
                }

                GenericButton
                {
                    id: genButton
                    label: qsTr("Generieren")
                    enabled: !functionManager.aiBusy && promptField.text.trim().length > 0
                    onClicked: popupRoot.generate()
                }
            }

            Rectangle
            {
                Layout.fillWidth: true
                visible: popupRoot.resultText.length > 0
                height: resultLabel.implicitHeight + 12
                radius: 3
                color: popupRoot.resultIsError ? "#4d1f1f" : "#1f4d2a"
                border.width: 1
                border.color: popupRoot.resultIsError ? "#a33" : "#3a3"

                Text
                {
                    id: resultLabel
                    x: 6
                    y: 6
                    width: parent.width - 12
                    wrapMode: Text.Wrap
                    text: popupRoot.resultText
                    color: UISettings.fgMain
                    font.family: UISettings.robotoFontName
                    font.pixelSize: UISettings.textSizeDefault
                }
            }
        } // ColumnLayout
}
