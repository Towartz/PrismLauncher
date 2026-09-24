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
import QtQuick.Controls
import QtQuick.Shapes

Rectangle {
    id: root
    property var themeBridge: (typeof theme !== "undefined") ? theme : null
    color: (themeBridge && themeBridge.windowBackground) ? themeBridge.windowBackground : "#1E1E2E"

    property bool isGridMode: true
    property int selectedRow: -1

    signal itemActivated(int row)
    signal itemToggled(int row)

    function selectRow(row) {
        selectedRow = row;
        if (isGridMode) {
            gridView.currentIndex = row;
        } else {
            listView.currentIndex = row;
        }
    }

    // Grid View
    GridView {
        id: gridView
        anchors.fill: parent
        anchors.margins: 6
        cellWidth: Math.max(240, Math.floor(width / Math.max(1, Math.floor(width / 260))))
        cellHeight: 152
        visible: root.isGridMode
        clip: true
        model: resourceModel

        ScrollBar.vertical: ScrollBar {
            policy: ScrollBar.AsNeeded
        }

        delegate: ModCard {
            width: gridView.cellWidth - 4
            height: 144
            isGridMode: true
            isSelected: index === root.selectedRow
            theme: root.themeBridge
            onClicked: {
                root.selectedRow = index;
                root.itemActivated(index);
            }
            onDoubleClicked: {
                root.itemToggled(index);
            }
        }
    }

    // List View
    ListView {
        id: listView
        anchors.fill: parent
        anchors.margins: 6
        spacing: 4
        visible: !root.isGridMode
        clip: true
        model: resourceModel

        ScrollBar.vertical: ScrollBar {
            policy: ScrollBar.AsNeeded
        }

        delegate: ModCard {
            width: listView.width - 12
            height: 58
            isGridMode: false
            isSelected: index === root.selectedRow
            theme: root.themeBridge
            onClicked: {
                root.selectedRow = index;
                root.itemActivated(index);
            }
            onDoubleClicked: {
                root.itemToggled(index);
            }
        }
    }

    // Empty state placeholder
    Column {
        anchors.centerIn: parent
        spacing: 10
        visible: (root.isGridMode ? gridView.count === 0 : listView.count === 0)

        Shape {
            anchors.horizontalCenter: parent.horizontalCenter
            width: 36
            height: 36
            layer.enabled: true
            layer.samples: 4

            ShapePath {
                strokeColor: (root.themeBridge && root.themeBridge.textSecondary) ? root.themeBridge.textSecondary : "#A6ADC8"
                strokeWidth: 2.8
                fillColor: "transparent"
                capStyle: ShapePath.RoundCap

                PathAngleArc {
                    centerX: 15
                    centerY: 15
                    radiusX: 10
                    radiusY: 10
                    startAngle: 0
                    sweepAngle: 360
                }
            }

            ShapePath {
                strokeColor: (root.themeBridge && root.themeBridge.textSecondary) ? root.themeBridge.textSecondary : "#A6ADC8"
                strokeWidth: 2.8
                fillColor: "transparent"
                capStyle: ShapePath.RoundCap
                startX: 22.5
                startY: 22.5
                PathLine { x: 31; y: 31 }
            }
        }

        Text {
            anchors.horizontalCenter: parent.horizontalCenter
            text: "No resources found"
            color: (root.themeBridge && root.themeBridge.textSecondary) ? root.themeBridge.textSecondary : "#A6ADC8"
            font.pixelSize: 14
        }
    }
}
