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

        delegate: ModCard {
            width: gridView.cellWidth - 4
            height: 144
            isGridMode: true
            isSelected: index === root.selectedRow
            themeBridge: root.themeBridge
            onClicked: {
                root.selectedRow = index;
                root.itemActivated(index);
            }
            onDoubleClicked: {
                root.itemToggled(index);
            }
        }

        Rectangle {
            anchors.right: parent.right
            anchors.rightMargin: 1
            width: 5
            radius: 2.5
            color: (root.themeBridge && root.themeBridge.textSecondary) ? root.themeBridge.textSecondary : "#A6ADC8"
            opacity: 0.45
            visible: gridView.contentHeight > gridView.height
            y: gridView.visibleArea.yPosition * gridView.height
            height: Math.max(24, gridView.visibleArea.heightRatio * gridView.height)
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

        delegate: ModCard {
            width: listView.width - 12
            height: 58
            isGridMode: false
            isSelected: index === root.selectedRow
            themeBridge: root.themeBridge
            onClicked: {
                root.selectedRow = index;
                root.itemActivated(index);
            }
            onDoubleClicked: {
                root.itemToggled(index);
            }
        }

        Rectangle {
            anchors.right: parent.right
            anchors.rightMargin: 1
            width: 5
            radius: 2.5
            color: (root.themeBridge && root.themeBridge.textSecondary) ? root.themeBridge.textSecondary : "#A6ADC8"
            opacity: 0.45
            visible: listView.contentHeight > listView.height
            y: listView.visibleArea.yPosition * listView.height
            height: Math.max(24, listView.visibleArea.heightRatio * listView.height)
        }
    }

    // Empty state placeholder with pure SVG icon
    Column {
        anchors.centerIn: parent
        spacing: 10
        visible: (root.isGridMode ? gridView.count === 0 : listView.count === 0)

        Image {
            anchors.horizontalCenter: parent.horizontalCenter
            width: 36
            height: 36
            sourceSize: Qt.size(36, 36)
            smooth: true
            source: "data:image/svg+xml;utf8,<svg xmlns='http://www.w3.org/2000/svg' width='36' height='36' viewBox='0 0 24 24' fill='none' stroke='%23A6ADC8' stroke-width='2' stroke-linecap='round' stroke-linejoin='round'><circle cx='11' cy='11' r='7'/><line x1='21' y1='21' x2='16.65' y2='16.65'/></svg>"
        }

        Text {
            anchors.horizontalCenter: parent.horizontalCenter
            text: "No resources found"
            color: (root.themeBridge && root.themeBridge.textSecondary) ? root.themeBridge.textSecondary : "#A6ADC8"
            font.pixelSize: 14
        }
    }
}
