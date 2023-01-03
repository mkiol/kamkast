/* Copyright (C) 2022 Michal Kosciesza <michal@mkiol.net>
 *
 * This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at http://mozilla.org/MPL/2.0/.
 */

import QtQuick 2.0
import Sailfish.Silica 1.0

Rectangle {
    property real size: Theme.itemSizeMedium

    width: size
    height: size
    radius: width * 0.5

    color: gui.castingActive ? Theme.rgba(Theme.highlightColor, Theme.opacityLow) : "transparent"
    border { color: Theme.highlightColor; width: Theme.paddingSmall }

    Rectangle {
        anchors.centerIn: parent
        width: parent.width * 0.5
        height: parent.height * 0.5
        radius: width * 0.5
        color: gui.castingActive ? Theme.highlightColor : "transparent"
        border { color: Theme.highlightColor; width: Theme.paddingSmall * 0.5 }
    }

    Rectangle {
        x: parent.width * 0.2
        y: parent.width * 0.2
        width: parent.width * 0.1
        height: parent.height * 0.1
        radius: width * 0.5
        color: Theme.highlightColor
    }
}
