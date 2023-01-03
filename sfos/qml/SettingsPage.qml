/* Copyright (C) 2022 Michal Kosciesza <michal@mkiol.net>
 *
 * This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at http://mozilla.org/MPL/2.0/.
 */

import QtQuick 2.0
import Sailfish.Silica 1.0

Page {
    id: root

    allowedOrientations: Orientation.All

    SilicaFlickable {
        id: flick
        anchors.fill: parent
        contentHeight: column.height

        Column {
            id: column

            width: root.width
            spacing: Theme.paddingMedium

            PageHeader {
                title: qsTr("Settings")
            }

            PaddedLabel {
                text: qsTr("To apply changes, restart the application.")
            }

            Spacer {}

            ComboBox {
                label: qsTr("Network interface")
                currentIndex: gui.ifnameIdx
                menu: ContextMenu {
                    Repeater {
                        model: gui.getIfnames()
                        MenuItem { text: modelData }
                    }
                }
                onCurrentIndexChanged: {
                    gui.ifnameIdx = currentIndex
                }

                description: qsTr("Network interface on which server accepts requests.")
            }

            TextField {
                anchors {
                    left: parent.left; right: parent.right
                }
                label: qsTr("Port number")
                text: gui.port
                inputMethodHints: Qt.ImhDigitsOnly | Qt.ImhNoPredictiveText
                placeholderText: qsTr("Enter port number")
                onTextChanged: {
                    gui.port = parseInt(text, 10)
                }

                description: qsTr("TCP port number on which server accepts requests. " +
                                  "Port 0 means random port.")
            }

            TextField {
                anchors {
                    left: parent.left; right: parent.right
                }
                label: qsTr("URL path")
                text: gui.urlPath
                inputMethodHints: Qt.ImhNoAutoUppercase | Qt.ImhNoPredictiveText
                placeholderText: qsTr("Enter URL path")
                onTextChanged: {
                    gui.urlPath = text
                }

                description: qsTr("Path portion of stream or web-interface URL. " +
                                  "Only requests with correct path are accepted.")
            }
        }
    }

    VerticalScrollDecorator {
        flickable: flick
    }
}
