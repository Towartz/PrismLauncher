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

Item {
    id: root
    width: isGridMode ? 260 : parent.width
    height: isGridMode ? 144 : 58

    property bool isGridMode: true
    property bool isSelected: false
    property var theme: (typeof theme !== "undefined") ? theme : null

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
        color: isSelected ? ((theme && theme.cardSelected) ? theme.cardSelected : "#313244") : (cardArea.containsMouse ? ((theme && theme.cardHover) ? theme.cardHover : "#252636") : ((theme && theme.cardBackground) ? theme.cardBackground : "#181825"))
        border.color: isSelected ? ((theme && theme.accentColor) ? theme.accentColor : "#89B4FA") : (cardArea.containsMouse ? ((theme && theme.accentColor) ? theme.accentColor : "#89B4FA") : ((theme && theme.borderColor) ? theme.borderColor : "#313244"))
        border.width: isSelected ? 2 : 1

        Behavior on color {
            ColorAnimation { duration: 120 }
        }
        Behavior on border.color {
            ColorAnimation { duration: 120 }
        }

        MouseArea {
            id: cardArea
            anchors.fill: parent
            hoverEnabled: true
            cursorShape: Qt.PointingHandCursor
            onClicked: root.clicked()
            onDoubleClicked: root.doubleClicked()
        }

        // Layout for Card Grid Mode
        Item {
            anchors.fill: parent
            anchors.margins: 10
            visible: root.isGridMode

            // Icon with monogram and vector cube fallback
            Rectangle {
                id: iconRect
                width: 44
                height: 44
                radius: 8
                color: (theme && theme.badgeBackground) ? theme.badgeBackground : "#2A2A3C"
                clip: true

                Text {
                    anchors.centerIn: parent
                    visible: iconImg.status !== Image.Ready && text.length > 0
                    text: {
                        var t = (typeof model.title !== "undefined" && model.title !== "") ? model.title : ((typeof model.display !== "undefined") ? model.display : "");
                        return (t && t.length > 0) ? t.substring(0, 1).toUpperCase() : "";
                    }
                    font.bold: true
                    font.pixelSize: 18
                    color: (theme && theme.accentColor) ? theme.accentColor : "#3B82F6"
                }

                // Vector cube outline if no text is available
                Shape {
                    anchors.centerIn: parent
                    width: 20
                    height: 20
                    layer.enabled: true
                    layer.samples: 4
                    visible: iconImg.status !== Image.Ready && (!model.title || model.title.length === 0)

                    ShapePath {
                        strokeColor: (theme && theme.accentColor) ? theme.accentColor : "#3B82F6"
                        strokeWidth: 1.8
                        fillColor: "transparent"
                        capStyle: ShapePath.RoundCap
                        joinStyle: ShapePath.RoundJoin
                        startX: 3; startY: 6
                        PathLine { x: 10; y: 2 }
                        PathLine { x: 17; y: 6 }
                        PathLine { x: 17; y: 14 }
                        PathLine { x: 10; y: 18 }
                        PathLine { x: 3; y: 14 }
                        PathLine { x: 3; y: 6 }
                    }
                    ShapePath {
                        strokeColor: (theme && theme.accentColor) ? theme.accentColor : "#3B82F6"
                        strokeWidth: 1.8
                        fillColor: "transparent"
                        capStyle: ShapePath.RoundCap
                        startX: 10; startY: 2
                        PathLine { x: 10; y: 18 }
                    }
                }

                Image {
                    id: iconImg
                    anchors.fill: parent
                    source: (typeof model.iconUrl !== "undefined" && model.iconUrl !== "") ? model.iconUrl : ""
                    fillMode: Image.PreserveAspectFit
                    asynchronous: true
                    cache: true
                }
            }

            // Title & Author
            Column {
                anchors.left: iconRect.right
                anchors.right: progressRing.left
                anchors.top: iconRect.top
                anchors.leftMargin: 10
                anchors.rightMargin: 6
                spacing: 2

                Text {
                    width: parent.width
                    text: (typeof model.title !== "undefined" && model.title !== "") ? model.title : ((typeof model.display !== "undefined") ? model.display : "")
                    color: (theme && theme.textPrimary) ? theme.textPrimary : "#CAD3F5"
                    font.bold: true
                    font.pixelSize: 13
                    elide: Text.ElideRight
                }

                Text {
                    width: parent.width
                    text: (typeof model.author !== "undefined" && model.author !== "") ? ("by " + model.author) : ""
                    color: (theme && theme.textSecondary) ? theme.textSecondary : "#A6ADC8"
                    font.pixelSize: 11
                    elide: Text.ElideRight
                    visible: text.length > 0
                }
            }

            // Top-right progress / checkmark ring
            DownloadProgressRing {
                id: progressRing
                anchors.top: parent.top
                anchors.right: parent.right
                width: 22
                height: 22
                isInstalled: (typeof model.installed !== "undefined" && model.installed) || (typeof model.checkState !== "undefined" && model.checkState === Qt.Checked)
                ringColor: (theme && theme.accentColor) ? theme.accentColor : "#3B82F6"
                visible: isInstalled
            }

            // Description
            Text {
                id: descText
                anchors.left: parent.left
                anchors.right: parent.right
                anchors.top: iconRect.bottom
                anchors.bottom: metaRow.top
                anchors.topMargin: 8
                anchors.bottomMargin: 4
                text: (typeof model.description !== "undefined") ? model.description : ""
                color: (theme && theme.textSecondary) ? theme.textSecondary : "#A6ADC8"
                font.pixelSize: 11
                wrapMode: Text.WordWrap
                maximumLineCount: 2
                elide: Text.ElideRight
            }

            // Bottom Badges / Tags row
            Row {
                id: metaRow
                anchors.left: parent.left
                anchors.right: parent.right
                anchors.bottom: parent.bottom
                spacing: 6

                // Provider badge
                Rectangle {
                    radius: 4
                    height: 18
                    width: providerLabel.width + 10
                    color: (theme && theme.badgeBackground) ? theme.badgeBackground : "#2A2A3C"
                    visible: (typeof model.provider !== "undefined" && model.provider !== "")

                    Text {
                        id: providerLabel
                        anchors.centerIn: parent
                        text: (typeof model.provider !== "undefined") ? model.provider : ""
                        color: (theme && theme.badgeText) ? theme.badgeText : "#CAD3F5"
                        font.pixelSize: 10
                        font.bold: true
                    }
                }

                // Side badge
                Rectangle {
                    radius: 4
                    height: 18
                    width: sideLabel.width + 10
                    color: (theme && theme.badgeBackground) ? theme.badgeBackground : "#2A2A3C"
                    visible: (typeof model.side !== "undefined" && model.side !== "")

                    Text {
                        id: sideLabel
                        anchors.centerIn: parent
                        text: (typeof model.side !== "undefined") ? model.side : ""
                        color: (theme && theme.badgeText) ? theme.badgeText : "#CAD3F5"
                        font.pixelSize: 10
                    }
                }

                // Installed / Selected badge with vector checkmark
                Rectangle {
                    radius: 4
                    height: 18
                    width: badgeRow.width + 12
                    color: (typeof model.checkState !== "undefined" && model.checkState === Qt.Checked) ? ((theme && theme.accentColor) ? theme.accentColor : "#3B82F6") : ((theme && theme.badgeBackground) ? theme.badgeBackground : "#2A2A3C")
                    visible: (typeof model.installed !== "undefined" && model.installed) || (typeof model.checkState !== "undefined" && model.checkState === Qt.Checked)

                    Row {
                        id: badgeRow
                        anchors.centerIn: parent
                        spacing: 4

                        Shape {
                            width: 8
                            height: 8
                            anchors.verticalCenter: parent.verticalCenter
                            layer.enabled: true
                            layer.samples: 4
                            visible: (typeof model.checkState !== "undefined" && model.checkState === Qt.Checked)

                            ShapePath {
                                strokeColor: "#FFFFFF"
                                strokeWidth: 1.8
                                fillColor: "transparent"
                                capStyle: ShapePath.RoundCap
                                joinStyle: ShapePath.RoundJoin
                                startX: 1
                                startY: 4
                                PathLine { x: 3; y: 6.5 }
                                PathLine { x: 7; y: 1.5 }
                            }
                        }

                        Text {
                            id: statusLabel
                            anchors.verticalCenter: parent.verticalCenter
                            text: (typeof model.checkState !== "undefined" && model.checkState === Qt.Checked) ? "Selected" : "Installed"
                            color: (typeof model.checkState !== "undefined" && model.checkState === Qt.Checked) ? "#FFFFFF" : ((theme && theme.badgeText) ? theme.badgeText : "#CAD3F5")
                            font.pixelSize: 10
                            font.bold: true
                        }
                    }
                }
            }
        }

        // Layout for Compact List Mode
        Item {
            anchors.fill: parent
            anchors.margins: 6
            visible: !root.isGridMode

            Rectangle {
                id: listIconRect
                width: 36
                height: 36
                anchors.verticalCenter: parent.verticalCenter
                radius: 6
                color: (theme && theme.badgeBackground) ? theme.badgeBackground : "#2A2A3C"
                clip: true

                Text {
                    anchors.centerIn: parent
                    visible: listIconImg.status !== Image.Ready && text.length > 0
                    text: {
                        var t = (typeof model.title !== "undefined" && model.title !== "") ? model.title : ((typeof model.display !== "undefined") ? model.display : "");
                        return (t && t.length > 0) ? t.substring(0, 1).toUpperCase() : "";
                    }
                    font.bold: true
                    font.pixelSize: 14
                    color: (theme && theme.accentColor) ? theme.accentColor : "#3B82F6"
                }

                Shape {
                    anchors.centerIn: parent
                    width: 16
                    height: 16
                    layer.enabled: true
                    layer.samples: 4
                    visible: listIconImg.status !== Image.Ready && (!model.title || model.title.length === 0)

                    ShapePath {
                        strokeColor: (theme && theme.accentColor) ? theme.accentColor : "#3B82F6"
                        strokeWidth: 1.5
                        fillColor: "transparent"
                        capStyle: ShapePath.RoundCap
                        joinStyle: ShapePath.RoundJoin
                        startX: 2.5; startY: 5
                        PathLine { x: 8; y: 1.5 }
                        PathLine { x: 13.5; y: 5 }
                        PathLine { x: 13.5; y: 11 }
                        PathLine { x: 8; y: 14.5 }
                        PathLine { x: 2.5; y: 11 }
                        PathLine { x: 2.5; y: 5 }
                    }
                }

                Image {
                    id: listIconImg
                    anchors.fill: parent
                    source: (typeof model.iconUrl !== "undefined" && model.iconUrl !== "") ? model.iconUrl : ""
                    fillMode: Image.PreserveAspectFit
                    asynchronous: true
                }
            }

            Column {
                anchors.left: listIconRect.right
                anchors.right: listBadgeRow.left
                anchors.verticalCenter: parent.verticalCenter
                anchors.leftMargin: 10
                anchors.rightMargin: 10
                spacing: 2

                Text {
                    width: parent.width
                    text: (typeof model.title !== "undefined" && model.title !== "") ? model.title : ((typeof model.display !== "undefined") ? model.display : "")
                    color: (theme && theme.textPrimary) ? theme.textPrimary : "#CAD3F5"
                    font.bold: true
                    font.pixelSize: 13
                    elide: Text.ElideRight
                }

                Text {
                    width: parent.width
                    text: (typeof model.description !== "undefined") ? model.description : ""
                    color: (theme && theme.textSecondary) ? theme.textSecondary : "#A6ADC8"
                    font.pixelSize: 11
                    elide: Text.ElideRight
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
                    width: listProviderLabel.width + 10
                    color: (theme && theme.badgeBackground) ? theme.badgeBackground : "#2A2A3C"
                    visible: (typeof model.provider !== "undefined" && model.provider !== "")

                    Text {
                        id: listProviderLabel
                        anchors.centerIn: parent
                        text: (typeof model.provider !== "undefined") ? model.provider : ""
                        color: (theme && theme.badgeText) ? theme.badgeText : "#CAD3F5"
                        font.pixelSize: 10
                    }
                }

                DownloadProgressRing {
                    width: 20
                    height: 20
                    anchors.verticalCenter: parent.verticalCenter
                    isInstalled: (typeof model.installed !== "undefined" && model.installed) || (typeof model.checkState !== "undefined" && model.checkState === Qt.Checked)
                    ringColor: (theme && theme.accentColor) ? theme.accentColor : "#3B82F6"
                    visible: isInstalled
                }
            }
        }
    }
}
