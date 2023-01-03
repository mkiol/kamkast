/* Copyright (C) 2022 Michal Kosciesza <michal@mkiol.net>
 *
 * This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at http://mozilla.org/MPL/2.0/.
 */

import QtQuick 2.0
import Sailfish.Silica 1.0

CoverBackground {
    id: root

    CastingIndicator {
        id: indicator
        anchors.centerIn: parent
        width: parent.width * 0.5
        height: parent.width * 0.5
    }

    CoverActionList {
        enabled: gui.castingActive
        CoverAction {
            iconSource: "image://theme/icon-cover-cancel"
            onTriggered: gui.cancelCasting()
        }
    }
}
