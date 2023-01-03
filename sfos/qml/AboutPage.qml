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
        anchors.fill: parent

        contentHeight: column.height

        Column {
            id: column

            width: root.width
            spacing: Theme.paddingLarge

            PageHeader {
                title: qsTr("About %1").arg(APP_NAME)
            }

            Image {
                anchors.horizontalCenter: parent.horizontalCenter
                height: root.isPortrait ? Theme.itemSizeHuge : Theme.iconSizeLarge
                width: root.isPortrait ? Theme.itemSizeHuge : Theme.iconSizeLarge
                source: gui.icon()
            }

            InfoLabel {
                text: APP_NAME
            }

            PaddedLabel {
                horizontalAlignment: Text.AlignHCenter
                font.pixelSize: Theme.fontSizeMedium
                color: Theme.highlightColor
                text: qsTr("Version %1").arg(APP_VERSION);
            }

            Flow {
                anchors.horizontalCenter: parent.horizontalCenter
                spacing: Theme.paddingLarge
                Button {
                    text: qsTr("Project website")
                    onClicked: Qt.openUrlExternally(APP_WEBPAGE)
                }
            }

            SectionHeader {
                text: qsTr("Authors")
            }

            PaddedLabel {
                horizontalAlignment: Text.AlignLeft
                textFormat: Text.RichText
                text: ("Copyright &copy; %1 %2")
                .arg(APP_COPYRIGHT_YEAR)
                .arg(APP_AUTHOR)
            }

            PaddedLabel {
                horizontalAlignment: Text.AlignLeft
                textFormat: Text.StyledText
                text: qsTr("%1 is developed as an open source project under %2.")
                .arg(APP_NAME)
                .arg("<a href=\"" + APP_LICENSE_URL + "\">" + APP_LICENSE + "</a>")
            }

            SectionHeader {
                text: qsTr("Libraries")
            }

            PaddedLabel {
                horizontalAlignment: Text.AlignLeft
                text: "GNU Libmicrohttpd · FFmpeg · LAME · x264 · {fmt} · mINI · cxxopts"
            }

            Spacer{}
        }

        VerticalScrollDecorator {}
    }

    focus: true
    Keys.onVolumeUpPressed: {
        rc.volUpPressed()
    }
    Keys.onVolumeDownPressed: {
        rc.volDownPressed()
    }
}
