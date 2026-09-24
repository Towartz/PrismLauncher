// SPDX-License-Identifier: GPL-3.0-only
/*
 *  Prism Launcher - Minecraft Launcher
 *  Copyright (C) 2026 Prism Launcher Contributors
 *
 *  This program is free software: you can redistribute it and/or modify
 *  it under the terms of the GNU General Public License as published by
 *  the Free Software Foundation, version 3.
 *
 *  This program is distributed in the hope that it will be useful,
 *  but WITHOUT ANY WARRANTY; without even the implied warranty of
 *  MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 *  GNU General Public License for more details.
 *
 *  You should have received a copy of the GNU General Public License
 *  along with this program.  If not, see <https://www.gnu.org/licenses/>.
 */

import QtQuick

Item {
    id: root
    width: 24
    height: 24

    property real progress: 0.0 // 0.0 to 1.0
    property bool isDownloading: false
    property bool isInstalled: false
    property color ringColor: "#3B82F6"
    property color trackColor: Qt.rgba(1, 1, 1, 0.15)

    Behavior on progress {
        NumberAnimation { duration: 180; easing.type: Easing.OutCubic }
    }

    // Installed / Selected SVG checkmark indicator
    Rectangle {
        anchors.fill: parent
        radius: width / 2
        color: root.ringColor
        visible: root.isInstalled && !root.isDownloading

        Image {
            anchors.centerIn: parent
            width: 14
            height: 14
            sourceSize: Qt.size(14, 14)
            smooth: true
            source: "data:image/svg+xml;utf8,<svg xmlns='http://www.w3.org/2000/svg' width='14' height='14' viewBox='0 0 14 14' fill='none' stroke='%23FFFFFF' stroke-width='2.2' stroke-linecap='round' stroke-linejoin='round'><polyline points='2.5 7.5 5.5 10.5 11.5 3.5'/></svg>"
        }
    }
}
