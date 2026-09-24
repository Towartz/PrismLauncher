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

Item {
    id: root
    width: isGridMode ? 260 : parent.width
    height: isGridMode ? 144 : 58

    property bool isGridMode: true
    property bool isSelected: false
    property var theme

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
        color: isSelected ? theme.cardSelected : (cardArea.containsMouse ? theme.cardHover : theme.cardBackground)
        border.color: isSelected ? theme.accentColor : (cardArea.containsMouse ? theme.accentColor : theme.borderColor)
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

            // Icon with monogram fallback
            Rectangle {
                id: iconRect
                width: 44
                height: 44
                radius: 8
                color: theme ? theme.badgeBackground : "#2A2A3C"
                clip: true

                Text {
                    anchors.centerIn: parent
                    visible: iconImg.status !== Image.Ready
                    text: {
                        var t = (typeof model.title !== "undefined" && model.title !== "") ? model.title : ((typeof model.display !== "undefined") ? model.display : "");
                        return (t && t.length > 0) ? t.substring(0, 1).toUpperCase() : "📦";
                    }
                    font.bold: true
                    font.pixelSize: 18
                    color: theme ? theme.accentColor : "#3B82F6"
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
                    color: theme ? theme.textPrimary : "#CAD3F5"
                    font.bold: true
                    font.pixelSize: 13
                    elide: Text.ElideRight
                }

                Text {
                    width: parent.width
                    text: (typeof model.author !== "undefined" && model.author !== "") ? ("by " + model.author) : ""
                    color: theme ? theme.textSecondary : "#A6ADC8"
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
                ringColor: theme ? theme.accentColor : "#3B82F6"
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
                color: theme ? theme.textSecondary : "#A6ADC8"
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
                    color: theme ? theme.badgeBackground : "#2A2A3C"
                    visible: (typeof model.provider !== "undefined" && model.provider !== "")

                    Text {
                        id: providerLabel
                        anchors.centerIn: parent
                        text: (typeof model.provider !== "undefined") ? model.provider : ""
                        color: theme ? theme.badgeText : "#CAD3F5"
                        font.pixelSize: 10
                        font.bold: true
                    }
                }

                // Side badge
                Rectangle {
                    radius: 4
                    height: 18
                    width: sideLabel.width + 10
                    color: theme ? theme.badgeBackground : "#2A2A3C"
                    visible: (typeof model.side !== "undefined" && model.side !== "")

                    Text {
                        id: sideLabel
                        anchors.centerIn: parent
                        text: (typeof model.side !== "undefined") ? model.side : ""
                        color: theme ? theme.badgeText : "#CAD3F5"
                        font.pixelSize: 10
                    }
                }

                // Installed / Selected badge
                Rectangle {
                    radius: 4
                    height: 18
                    width: statusLabel.width + 10
                    color: (typeof model.checkState !== "undefined" && model.checkState === Qt.Checked) ? (theme ? theme.accentColor : "#3B82F6") : (theme ? theme.badgeBackground : "#2A2A3C")
                    visible: (typeof model.installed !== "undefined" && model.installed) || (typeof model.checkState !== "undefined" && model.checkState === Qt.Checked)

                    Text {
                        id: statusLabel
                        anchors.centerIn: parent
                        text: (typeof model.checkState !== "undefined" && model.checkState === Qt.Checked) ? "✓ Selected" : "Installed"
                        color: (typeof model.checkState !== "undefined" && model.checkState === Qt.Checked) ? "#FFFFFF" : (theme ? theme.badgeText : "#CAD3F5")
                        font.pixelSize: 10
                        font.bold: true
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
                color: theme ? theme.badgeBackground : "#2A2A3C"
                clip: true

                Text {
                    anchors.centerIn: parent
                    visible: listIconImg.status !== Image.Ready
                    text: {
                        var t = (typeof model.title !== "undefined" && model.title !== "") ? model.title : ((typeof model.display !== "undefined") ? model.display : "");
                        return (t && t.length > 0) ? t.substring(0, 1).toUpperCase() : "📦";
                    }
                    font.bold: true
                    font.pixelSize: 14
                    color: theme ? theme.accentColor : "#3B82F6"
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
                    color: theme ? theme.textPrimary : "#CAD3F5"
                    font.bold: true
                    font.pixelSize: 13
                    elide: Text.ElideRight
                }

                Text {
                    width: parent.width
                    text: (typeof model.description !== "undefined") ? model.description : ""
                    color: theme ? theme.textSecondary : "#A6ADC8"
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
                    color: theme ? theme.badgeBackground : "#2A2A3C"
                    visible: (typeof model.provider !== "undefined" && model.provider !== "")

                    Text {
                        id: listProviderLabel
                        anchors.centerIn: parent
                        text: (typeof model.provider !== "undefined") ? model.provider : ""
                        color: theme ? theme.badgeText : "#CAD3F5"
                        font.pixelSize: 10
                    }
                }

                DownloadProgressRing {
                    width: 20
                    height: 20
                    anchors.verticalCenter: parent.verticalCenter
                    isInstalled: (typeof model.installed !== "undefined" && model.installed) || (typeof model.checkState !== "undefined" && model.checkState === Qt.Checked)
                    ringColor: theme ? theme.accentColor : "#3B82F6"
                    visible: isInstalled
                }
            }
        }
    }
}
