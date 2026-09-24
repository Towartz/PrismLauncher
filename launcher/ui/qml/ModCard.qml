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
    width: isGridMode ? 260 : parent.width
    height: isGridMode ? 144 : 58

    property bool isGridMode: true
    property bool isSelected: false
    property var themeBridge: null

    signal clicked()
    signal doubleClicked()

    Behavior on scale {
        NumberAnimation { duration: 140; easing.type: Easing.OutQuad }
    }

    Rectangle {
        id: bgCard
        anchors.fill: parent
        anchors.margins: 4
        radius: isGridMode ? 10 : 8
        color: isSelected ? ((themeBridge && themeBridge.cardSelected) ? themeBridge.cardSelected : "#313244") : (cardArea.containsMouse ? ((themeBridge && themeBridge.cardHover) ? themeBridge.cardHover : "#252636") : ((themeBridge && themeBridge.cardBackground) ? themeBridge.cardBackground : "#181825"))
        border.color: isSelected ? ((themeBridge && themeBridge.accentColor) ? themeBridge.accentColor : "#89B4FA") : (cardArea.containsMouse ? ((themeBridge && themeBridge.accentColor) ? themeBridge.accentColor : "#89B4FA") : ((themeBridge && themeBridge.borderColor) ? themeBridge.borderColor : "#313244"))
        border.width: isSelected ? 2 : 1

        Behavior on color {
            ColorAnimation { duration: 120 }
        }
        Behavior on border.color {
            ColorAnimation { duration: 120 }
        }

        // Grid Layout
        Column {
            visible: root.isGridMode
            anchors.fill: parent
            anchors.margins: 10
            spacing: 6

            Row {
                width: parent.width
                spacing: 10

                // Icon container
                Rectangle {
                    width: 44
                    height: 44
                    radius: 8
                    color: (themeBridge && themeBridge.windowBackground) ? themeBridge.windowBackground : "#11111B"
                    clip: true

                    Image {
                        id: gridIcon
                        anchors.fill: parent
                        anchors.margins: 2
                        source: (typeof iconUrl !== "undefined" && iconUrl !== "") ? iconUrl : ""
                        fillMode: Image.PreserveAspectFit
                        asynchronous: true
                        cache: true
                        visible: status === Image.Ready
                    }

                    // Pure SVG Cube Fallback Icon
                    Image {
                        anchors.centerIn: parent
                        width: 24
                        height: 24
                        sourceSize: Qt.size(24, 24)
                        smooth: true
                        visible: gridIcon.status !== Image.Ready
                        source: "data:image/svg+xml;utf8,<svg xmlns='http://www.w3.org/2000/svg' width='24' height='24' viewBox='0 0 24 24' fill='none' stroke='%2389B4FA' stroke-width='1.8' stroke-linecap='round' stroke-linejoin='round'><path d='M21 16V8a2 2 0 0 0-1-1.73l-7-4a2 2 0 0 0-2 0l-7 4A2 2 0 0 0 3 8v8a2 2 0 0 0 1 1.73l7 4a2 2 0 0 0 2 0l7-4A2 2 0 0 0 21 16z'/><polyline points='3.27 6.96 12 12.01 20.73 6.96'/><line x1='12' y1='22.08' x2='12' y2='12'/></svg>"
                    }
                }

                Column {
                    width: parent.width - 54
                    spacing: 3
                    anchors.verticalCenter: parent.verticalCenter

                    Row {
                        width: parent.width
                        spacing: 6

                        Text {
                            text: (typeof title !== "undefined" && title !== "") ? title : ((typeof display !== "undefined" && display !== "") ? display : "Resource")
                            color: (themeBridge && themeBridge.textPrimary) ? themeBridge.textPrimary : "#CDD6F4"
                            font.pixelSize: 13
                            font.bold: true
                            elide: Text.ElideRight
                            width: parent.width - (gridStatusRing.visible ? 26 : 0)
                        }

                        DownloadProgressRing {
                            id: gridStatusRing
                            width: 18
                            height: 18
                            anchors.verticalCenter: parent.verticalCenter
                            isInstalled: (typeof installed !== "undefined" && installed) || (typeof checkState !== "undefined" && checkState === 2)
                            ringColor: (themeBridge && themeBridge.successColor) ? themeBridge.successColor : "#A6E3A1"
                            visible: isInstalled
                        }
                    }

                    Text {
                        width: parent.width
                        visible: (typeof author !== "undefined" && author !== "")
                        text: (typeof author !== "undefined" && author !== "") ? ("by " + author) : ""
                        color: (themeBridge && themeBridge.textSecondary) ? themeBridge.textSecondary : "#A6ADC8"
                        font.pixelSize: 11
                        elide: Text.ElideRight
                    }
                }
            }

            // Description
            Text {
                width: parent.width
                height: 34
                text: (typeof description !== "undefined") ? description : ""
                color: (themeBridge && themeBridge.textSecondary) ? themeBridge.textSecondary : "#A6ADC8"
                font.pixelSize: 11
                wrapMode: Text.WordWrap
                elide: Text.ElideRight
                maximumLineCount: 2
            }

            // Provider & Side Pills
            Row {
                width: parent.width
                spacing: 4
                clip: true

                Rectangle {
                    visible: (typeof provider !== "undefined" && provider !== "")
                    height: 18
                    width: providerTagText.implicitWidth + 10
                    radius: 4
                    color: (themeBridge && themeBridge.badgeBackground) ? themeBridge.badgeBackground : "#2A2A3C"
                    border.color: (themeBridge && themeBridge.borderColor) ? themeBridge.borderColor : "#313244"
                    border.width: 1

                    Text {
                        id: providerTagText
                        anchors.centerIn: parent
                        text: (typeof provider !== "undefined") ? provider : ""
                        color: (themeBridge && themeBridge.accentColor) ? themeBridge.accentColor : "#89B4FA"
                        font.pixelSize: 10
                    }
                }

                Rectangle {
                    visible: (typeof side !== "undefined" && side !== "")
                    height: 18
                    width: sideTagText.implicitWidth + 10
                    radius: 4
                    color: (themeBridge && themeBridge.badgeBackground) ? themeBridge.badgeBackground : "#2A2A3C"
                    border.color: (themeBridge && themeBridge.borderColor) ? themeBridge.borderColor : "#313244"
                    border.width: 1

                    Text {
                        id: sideTagText
                        anchors.centerIn: parent
                        text: (typeof side !== "undefined") ? side : ""
                        color: (themeBridge && themeBridge.badgeText) ? themeBridge.badgeText : "#CAD3F5"
                        font.pixelSize: 10
                    }
                }
            }
        }

        // Compact List Layout
        Item {
            visible: !root.isGridMode
            anchors.fill: parent
            anchors.margins: 6

            Row {
                anchors.left: parent.left
                anchors.right: listBadgeRow.left
                anchors.rightMargin: 8
                anchors.verticalCenter: parent.verticalCenter
                spacing: 10

                Rectangle {
                    width: 38
                    height: 38
                    radius: 6
                    color: (themeBridge && themeBridge.windowBackground) ? themeBridge.windowBackground : "#11111B"
                    anchors.verticalCenter: parent.verticalCenter
                    clip: true

                    Image {
                        id: listIcon
                        anchors.fill: parent
                        anchors.margins: 2
                        source: (typeof iconUrl !== "undefined" && iconUrl !== "") ? iconUrl : ""
                        fillMode: Image.PreserveAspectFit
                        asynchronous: true
                        cache: true
                        visible: status === Image.Ready
                    }

                    // Pure SVG Cube Fallback Icon
                    Image {
                        anchors.centerIn: parent
                        width: 22
                        height: 22
                        sourceSize: Qt.size(22, 22)
                        smooth: true
                        visible: listIcon.status !== Image.Ready
                        source: "data:image/svg+xml;utf8,<svg xmlns='http://www.w3.org/2000/svg' width='22' height='22' viewBox='0 0 24 24' fill='none' stroke='%2389B4FA' stroke-width='1.8' stroke-linecap='round' stroke-linejoin='round'><path d='M21 16V8a2 2 0 0 0-1-1.73l-7-4a2 2 0 0 0-2 0l-7 4A2 2 0 0 0 3 8v8a2 2 0 0 0 1 1.73l7 4a2 2 0 0 0 2 0l7-4A2 2 0 0 0 21 16z'/><polyline points='3.27 6.96 12 12.01 20.73 6.96'/><line x1='12' y1='22.08' x2='12' y2='12'/></svg>"
                    }
                }

                Column {
                    width: parent.width - 48
                    anchors.verticalCenter: parent.verticalCenter
                    spacing: 2

                    Text {
                        width: parent.width
                        text: (typeof title !== "undefined" && title !== "") ? title : ((typeof display !== "undefined" && display !== "") ? display : "Resource")
                        color: (themeBridge && themeBridge.textPrimary) ? themeBridge.textPrimary : "#CDD6F4"
                        font.pixelSize: 13
                        font.bold: true
                        elide: Text.ElideRight
                    }

                    Text {
                        width: parent.width
                        text: (typeof description !== "undefined") ? description : ""
                        color: (themeBridge && themeBridge.textSecondary) ? themeBridge.textSecondary : "#A6ADC8"
                        font.pixelSize: 11
                        elide: Text.ElideRight
                    }
                }
            }

            Row {
                id: listBadgeRow
                anchors.right: parent.right
                anchors.verticalCenter: parent.verticalCenter
                spacing: 6

                Rectangle {
                    radius: 4
                    height: 18
                    width: listProviderLabel.implicitWidth + 10
                    color: (themeBridge && themeBridge.badgeBackground) ? themeBridge.badgeBackground : "#2A2A3C"
                    visible: (typeof provider !== "undefined" && provider !== "")

                    Text {
                        id: listProviderLabel
                        anchors.centerIn: parent
                        text: (typeof provider !== "undefined") ? provider : ""
                        color: (themeBridge && themeBridge.badgeText) ? themeBridge.badgeText : "#CAD3F5"
                        font.pixelSize: 10
                    }
                }

                DownloadProgressRing {
                    width: 20
                    height: 20
                    anchors.verticalCenter: parent.verticalCenter
                    isInstalled: (typeof installed !== "undefined" && installed) || (typeof checkState !== "undefined" && checkState === 2)
                    ringColor: (themeBridge && themeBridge.successColor) ? themeBridge.successColor : "#A6E3A1"
                    visible: isInstalled
                }
            }
        }

        MouseArea {
            id: cardArea
            anchors.fill: parent
            hoverEnabled: true
            acceptedButtons: Qt.LeftButton
            onClicked: root.clicked()
            onDoubleClicked: root.doubleClicked()
            onEntered: root.scale = 1.015
            onExited: root.scale = 1.0
        }
    }
}
