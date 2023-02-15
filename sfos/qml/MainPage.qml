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
                title: qsTr(APP_NAME)
            }

            PullDownMenu {
                id: menu

                MenuItem {
                    text: qsTr("About %1").arg(APP_NAME)
                    onClicked: pageStack.push(Qt.resolvedUrl("AboutPage.qml"))
                }

                MenuItem {
                    text: qsTr("Settings")
                    onClicked: pageStack.push(Qt.resolvedUrl("SettingsPage.qml"))
                }

                MenuItem {
                    visible: gui.castingActive
                    text: qsTr("Cancel casting")
                    onClicked: gui.cancelCasting()
                }
            }

            Row {
                spacing: Theme.paddingLarge
                height: Theme.itemSizeMedium
                x: Theme.horizontalPageMargin

                CastingIndicator {
                    anchors.verticalCenter: parent.verticalCenter
                    size: Theme.itemSizeMedium
                }

                Label {
                    wrapMode: Text.WordWrap
                    anchors.verticalCenter: parent.verticalCenter
                    horizontalAlignment: Text.AlignLeft
                    width: root.width * 0.5
                    color: Theme.highlightColor
                    text: gui.castingActive ? qsTr("Casting active") : qsTr("Casting inactive")
                }
            }

            Spacer {}

            SectionHeader {
                text: qsTr("Streaming default configuration")
            }

            ComboBox {
                id: formatComboBox
                label: qsTr("Format")
                currentIndex: gui.streamFormatIdx
                menu: ContextMenu {
                    Repeater {
                        model: gui.getStreamFormats()
                        MenuItem { text: modelData }
                    }
                }
                onCurrentIndexChanged: {
                    gui.streamFormatIdx = currentIndex
                }
            }

            ComboBox {
                label: qsTr("Video source")
                currentIndex: gui.videoSourceIdx
                menu: ContextMenu {
                    Repeater {
                        model: gui.getVideoSources()
                        MenuItem { text: modelData }
                    }
                }
                onCurrentIndexChanged: {
                    gui.videoSourceIdx = currentIndex
                }
            }

            ComboBox {
                enabled: gui.videoSourceIdx !== 0
                label: qsTr("Video orientation")
                currentIndex: gui.videoOrientationIdx
                menu: ContextMenu {
                    Repeater {
                        model: gui.getVideoOrientations()
                        MenuItem { text: modelData }
                    }
                }
                onCurrentIndexChanged: {
                    gui.videoOrientationIdx = currentIndex
                }
            }

            ComboBox {
                label: qsTr("Audio source")
                currentIndex: gui.audioSourceIdx
                menu: ContextMenu {
                    Repeater {
                        model: gui.getAudioSources()
                        MenuItem { text: modelData }
                    }
                }
                onCurrentIndexChanged: {
                    gui.audioSourceIdx = currentIndex
                }
            }

            TextSwitch {
                enabled: gui.audioSourceIdx === 2
                automaticCheck: false
                checked: gui.audioSourceMuted
                text: qsTr("Audio source muted")

                onClicked: {
                    gui.audioSourceMuted = !gui.audioSourceMuted
                }
            }

            Slider {
                enabled: gui.audioSourceIdx !== 0
                opacity: enabled ? 1.0 : Theme.opacityLow
                width: parent.width
                minimumValue: -10
                maximumValue: 10
                stepSize: 1
                handleVisible: true
                value: gui.audioVolume
                valueText: value
                label: qsTr("Audio volume boost")

                onValueChanged: {
                    gui.audioVolume = value
                }
            }

            SectionHeader {
                text: qsTr("Stream URL")
            }

            PaddedLabel {
                text: qsTr("Use this URL to start streaming with default configuration.")
            }

            Column {
                width: root.width
                Repeater {
                    model: gui.serverActive ? gui.getStreamUrls() : null
                    CopyableLabel {
                        width: root.width
                        text: modelData
                    }
                }
            }

            SectionHeader {
                text: qsTr("Web-interface URL")
            }

            PaddedLabel {
                text: qsTr("Use this URL to open web-interface which let you customize certain streaming parameters.")
            }

            Column {
                width: root.width
                Repeater {
                    model: gui.serverActive ? gui.getWebUrls() : null
                    CopyableLabel {
                        width: root.width
                        text: modelData
                    }
                }
            }
        }
    }

    VerticalScrollDecorator {
        flickable: flick
    }
}
