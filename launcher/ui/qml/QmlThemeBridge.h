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

#pragma once

#include <QColor>
#include <QObject>
#include <QPalette>

class QmlThemeBridge : public QObject {
    Q_OBJECT

    Q_PROPERTY(QColor windowBackground READ windowBackground NOTIFY themeChanged)
    Q_PROPERTY(QColor cardBackground READ cardBackground NOTIFY themeChanged)
    Q_PROPERTY(QColor cardHover READ cardHover NOTIFY themeChanged)
    Q_PROPERTY(QColor cardSelected READ cardSelected NOTIFY themeChanged)
    Q_PROPERTY(QColor textPrimary READ textPrimary NOTIFY themeChanged)
    Q_PROPERTY(QColor textSecondary READ textSecondary NOTIFY themeChanged)
    Q_PROPERTY(QColor accentColor READ accentColor NOTIFY themeChanged)
    Q_PROPERTY(QColor borderColor READ borderColor NOTIFY themeChanged)
    Q_PROPERTY(QColor badgeBackground READ badgeBackground NOTIFY themeChanged)
    Q_PROPERTY(QColor badgeText READ badgeText NOTIFY themeChanged)
    Q_PROPERTY(bool isDark READ isDark NOTIFY themeChanged)

   public:
    explicit QmlThemeBridge(QObject* parent = nullptr);

    QColor windowBackground() const;
    QColor cardBackground() const;
    QColor cardHover() const;
    QColor cardSelected() const;
    QColor textPrimary() const;
    QColor textSecondary() const;
    QColor accentColor() const;
    QColor borderColor() const;
    QColor badgeBackground() const;
    QColor badgeText() const;
    bool isDark() const;

   public slots:
    void updateTheme();

   signals:
    void themeChanged();
};
