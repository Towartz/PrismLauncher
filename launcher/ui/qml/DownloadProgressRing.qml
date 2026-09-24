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
import QtQuick.Shapes

Item {
    id: root
    width: 28
    height: 28

    property real progress: 0.0 // 0.0 to 1.0
    property bool isDownloading: false
    property bool isInstalled: false
    property color ringColor: "#3B82F6"
    property color trackColor: Qt.rgba(1, 1, 1, 0.15)

    Behavior on progress {
        NumberAnimation { duration: 180; easing.type: Easing.OutCubic }
    }

    // Outer track ring
    Shape {
        anchors.fill: parent
        visible: root.isDownloading
        layer.enabled: true
        layer.samples: 4

        ShapePath {
            strokeColor: root.trackColor
            strokeWidth: 3
            fillColor: "transparent"
            capStyle: ShapePath.RoundCap

            PathAngleArc {
                centerX: root.width / 2
                centerY: root.height / 2
                radiusX: (root.width - 4) / 2
                radiusY: (root.height - 4) / 2
                startAngle: -90
                sweepAngle: 360
            }
        }
    }

    // Animated progress arc
    Shape {
        anchors.fill: parent
        visible: root.isDownloading
        layer.enabled: true
        layer.samples: 4

        ShapePath {
            strokeColor: root.ringColor
            strokeWidth: 3
            fillColor: "transparent"
            capStyle: ShapePath.RoundCap

            PathAngleArc {
                centerX: root.width / 2
                centerY: root.height / 2
                radiusX: (root.width - 4) / 2
                radiusY: (root.height - 4) / 2
                startAngle: -90
                sweepAngle: Math.max(5, root.progress * 360)
            }
        }
    }

    // Installed vector checkmark indicator
    Rectangle {
        anchors.fill: parent
        radius: width / 2
        color: root.ringColor
        visible: root.isInstalled && !root.isDownloading

        Shape {
            anchors.centerIn: parent
            width: 14
            height: 14
            layer.enabled: true
            layer.samples: 4

            ShapePath {
                strokeColor: "#FFFFFF"
                strokeWidth: 2.2
                fillColor: "transparent"
                capStyle: ShapePath.RoundCap
                joinStyle: ShapePath.RoundJoin
                startX: 2.5
                startY: 7
                PathLine { x: 5.5; y: 10.5 }
                PathLine { x: 11.5; y: 3.5 }
            }
        }
    }
}
