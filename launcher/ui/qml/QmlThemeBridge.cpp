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

#include "QmlThemeBridge.h"
#include <QEvent>
#include <QGuiApplication>
#include "Application.h"
#include "settings/Setting.h"
#include "settings/SettingsObject.h"

QmlThemeBridge::QmlThemeBridge(QObject* parent) : QObject(parent)
{
    if (qApp) {
        qApp->installEventFilter(this);
    }

    if (APPLICATION_DYN && APPLICATION->settings()) {
        if (auto setting = APPLICATION->settings()->getSetting("ApplicationTheme")) {
            connect(setting.get(), &Setting::SettingChanged, this, &QmlThemeBridge::updateTheme);
        }
    }
}

auto QmlThemeBridge::eventFilter(QObject* watched, QEvent* event) -> bool
{
    if (event && (event->type() == QEvent::ApplicationPaletteChange || event->type() == QEvent::PaletteChange)) {
        updateTheme();
    }
    return QObject::eventFilter(watched, event);
}

void QmlThemeBridge::updateTheme()
{
    emit themeChanged();
}

QColor QmlThemeBridge::windowBackground() const
{
    return QGuiApplication::palette().color(QPalette::Window);
}

QColor QmlThemeBridge::cardBackground() const
{
    QColor base = QGuiApplication::palette().color(QPalette::Base);
    if (isDark()) {
        return base.lighter(108);
    }
    return base;
}

QColor QmlThemeBridge::cardHover() const
{
    QColor base = cardBackground();
    if (isDark()) {
        return base.lighter(120);
    }
    return base.darker(104);
}

QColor QmlThemeBridge::cardSelected() const
{
    QColor accent = accentColor();
    accent.setAlpha(isDark() ? 45 : 35);
    return accent;
}

QColor QmlThemeBridge::textPrimary() const
{
    return QGuiApplication::palette().color(QPalette::WindowText);
}

QColor QmlThemeBridge::textSecondary() const
{
    QColor text = textPrimary();
    text.setAlpha(165);
    return text;
}

QColor QmlThemeBridge::accentColor() const
{
    QColor hl = QGuiApplication::palette().color(QPalette::Highlight);
    if (!hl.isValid() || hl.lightness() == 0) {
        return QColor(0x3B, 0x82, 0xF6);  // Modern Blue fallback
    }
    return hl;
}

QColor QmlThemeBridge::borderColor() const
{
    QColor c = textPrimary();
    c.setAlpha(isDark() ? 28 : 22);
    return c;
}

QColor QmlThemeBridge::badgeBackground() const
{
    QColor c = textPrimary();
    c.setAlpha(isDark() ? 25 : 18);
    return c;
}

QColor QmlThemeBridge::badgeText() const
{
    return textPrimary();
}

bool QmlThemeBridge::isDark() const
{
    return windowBackground().lightness() < 128;
}
