/*
  Q Light Controller Plus
  CustomPopupDialog.qml

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

Dialog
{
    id: control
    anchors.centerIn: parent
    width: mainView.width / 3
    parent: mainView

    modal: true
    closePolicy: Popup.CloseOnEscape
    title: ""
    standardButtons: Dialog.Ok | Dialog.Cancel
    // guarded: 'mainView' is only in lexical scope when this dialog is instantiated
    // from within MainView.qml's own component tree (e.g. the Properties side panel).
    // When instantiated from elsewhere (e.g. a VC widget's live item, which overrides
    // 'parent' to Overlay.overlay instead), 'mainView' is undefined and dimming is
    // simply skipped rather than throwing a ReferenceError.
    onVisibleChanged: if (typeof mainView !== "undefined") mainView.setDimScreen(visible)

    property string message: ""

    signal clicked(int role)

    function setButtonStatus(index, status)
    {
        buttonBoxControl.itemAt(index).enabled = status
    }

    header:
        Label
        {
            text: control.title
            color: "white"
            visible: control.title
            elide: Label.ElideRight
            font.family: UISettings.robotoFontName
            font.pixelSize: UISettings.textSizeDefault
            font.bold: true
            padding: 12
            background:
                Rectangle
                {
                    color: UISettings.sectionHeader
                    x: 2
                    y: 2
                    width: parent.width - 4
                    height: parent.height - 2
                }
        }

    background:
        Rectangle
        {
            color: UISettings.bgMedium
            border.color: UISettings.bgLight
            border.width: 2

            focus: true
            Keys.onPressed: (event) =>
            {
                if (event.key === Qt.Key_Return || event.key === Qt.Key_Enter)
                {
                    control.accept()
                    event.accepted = true
                }
            }
        }

    contentItem:
        Text
        {
            visible: message
            anchors.centerIn: parent
            font.family: UISettings.robotoFontName
            font.pixelSize: UISettings.textSizeDefault
            color: UISettings.fgMain
            horizontalAlignment: Text.AlignHCenter
            verticalAlignment: Text.AlignVCenter
            wrapMode: Text.Wrap
            text: message
        }

    footer:
        DialogButtonBox
        {
            id: buttonBoxControl
            visible: count > 0

            contentItem.implicitHeight: UISettings.iconSizeDefault

            onClicked: function(button)
            {
                var role = button.DialogButtonBox.buttonRole
                if (role === DialogButtonBox.YesRole)
                    control.clicked(Dialog.Yes)
                else if (role === DialogButtonBox.NoRole)
                    control.clicked(Dialog.No)
                else if (role === DialogButtonBox.AcceptRole)
                    control.clicked(Dialog.Ok)
                else if (role === DialogButtonBox.ApplyRole)
                    control.clicked(Dialog.Apply)
                else if (role === DialogButtonBox.RejectRole)
                    control.clicked(Dialog.Cancel)
            }

            background:
                Rectangle
                {
                    implicitHeight: UISettings.iconSizeDefault
                    color: UISettings.bgMedium
                    x: 2
                    y: 2
                    width: parent.width - 4
                }

            delegate:
                Button
                {
                    id: buttonControl
                    implicitWidth: UISettings.bigItemHeight * 2
                    //implicitWidth: width

                    hoverEnabled: true

                    contentItem:
                        Text
                        {
                            text: buttonControl.text
                            font.family: UISettings.robotoFontName
                            font.pixelSize: UISettings.textSizeDefault
                            opacity: enabled ? 1.0 : 0.3
                            color: UISettings.fgMain
                            horizontalAlignment: Text.AlignHCenter
                            verticalAlignment: Text.AlignVCenter
                            elide: Text.ElideRight
                        }

                    background:
                        Rectangle
                        {
                            color: buttonControl.hovered ?
                                       buttonControl.down ? UISettings.highlightPressed : UISettings.highlight : UISettings.bgLight
                            opacity: enabled ? 1 : 0.3
                            border.color: UISettings.bgStrong
                            border.width: 2
                        }
                }
        }
}
