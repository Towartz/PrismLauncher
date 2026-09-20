// SPDX-License-Identifier: GPL-3.0-only
/*
 *  Prism Launcher - Minecraft Launcher
 *  Copyright (c) 2026 Towartz
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

#include "ApplyOfflineSkin.h"

#include <QClipboard>
#include <QDebug>
#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QGuiApplication>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QTextStream>

#include "Application.h"
#include "FileSystem.h"
#include "Json.h"
#include "minecraft/MinecraftInstance.h"

ApplyOfflineSkin::ApplyOfflineSkin(LaunchTask* parent, AuthSessionPtr session, MinecraftInstance* instance)
    : LaunchStep(parent), m_session(session), m_instance(instance)
{}

void ApplyOfflineSkin::executeTask()
{
    if (!m_session || m_session->user_type != "Offline" || m_session->skin_data.isEmpty()) {
        emitSucceeded();
        return;
    }

    const QString gameDir = m_instance->gameRoot();
    const QString playerName = m_session->player_name;
    const QString model = m_session->skin_model.isEmpty() ? "classic" : m_session->skin_model;

    if (APPLICATION->settings()->get("OfflineSkinCSLEnabled").toBool()) {
        applyCustomSkinLoader(gameDir, playerName, model);
    }

    if (APPLICATION->settings()->get("OfflineSkinResourcePackEnabled").toBool()) {
        applyResourcePack(gameDir, playerName, model);
        ensureResourcePackEnabled(gameDir);
    }

    copyServerCommand(playerName, model);
    emitSucceeded();
}

void ApplyOfflineSkin::applyCustomSkinLoader(const QString& gameDir, const QString& playerName, const QString& model)
{
    const QString cslSkinsDir = FS::PathCombine(gameDir, "CustomSkinLoader/LocalSkin/skins");
    FS::ensureFolderPathExists(cslSkinsDir);

    // Standard CustomSkinLoader skin file
    const QString skinPath = FS::PathCombine(cslSkinsDir, playerName + ".png");
    (void)FS::write(skinPath, m_session->skin_data);

    // CustomSkinLoader json model descriptor
    QJsonObject json;
    json["model"] = (model == "slim" ? "slim" : "default");
    const QString jsonPath = FS::PathCombine(cslSkinsDir, playerName + ".json");
    (void)FS::write(jsonPath, Json::toText(json));

    // Fallback file suffix for older CSL builds
    const QString suffixPath = FS::PathCombine(cslSkinsDir, playerName + (model == "slim" ? ".slim.png" : ".classic.png"));
    (void)FS::write(suffixPath, m_session->skin_data);
}

void ApplyOfflineSkin::applyResourcePack(const QString& gameDir, const QString& playerName, const QString& /*model*/)
{
    const QString packDir = FS::PathCombine(gameDir, "resourcepacks/PrismOfflineSkin");
    FS::ensureFolderPathExists(packDir);

    // Generate pack.mcmeta
    QJsonObject packObj;
    packObj["pack_format"] = 15;

    QJsonObject supportedFormats;
    supportedFormats["min_inclusive"] = 1;
    supportedFormats["max_inclusive"] = 99;
    packObj["supported_formats"] = supportedFormats;
    packObj["description"] = QString("Prism Launcher Offline Skin for %1").arg(playerName);

    QJsonObject rootObj;
    rootObj["pack"] = packObj;

    const QString metaPath = FS::PathCombine(packDir, "pack.mcmeta");
    (void)FS::write(metaPath, Json::toText(rootObj));

    // Create texture directories
    const QString wideDir = FS::PathCombine(packDir, "assets/minecraft/textures/entity/player/wide");
    const QString slimDir = FS::PathCombine(packDir, "assets/minecraft/textures/entity/player/slim");
    const QString entityDir = FS::PathCombine(packDir, "assets/minecraft/textures/entity");
    const QString mobDir = FS::PathCombine(packDir, "mob");

    FS::ensureFolderPathExists(wideDir);
    FS::ensureFolderPathExists(slimDir);
    FS::ensureFolderPathExists(entityDir);
    FS::ensureFolderPathExists(mobDir);

    // Modern wide (classic) player models
    const QStringList wideModels = { "steve.png", "kai.png", "sunny.png", "zuri.png" };
    for (const auto& f : wideModels) {
        (void)FS::write(FS::PathCombine(wideDir, f), m_session->skin_data);
    }

    // Modern slim player models
    const QStringList slimModels = { "alex.png", "ari.png", "efe.png", "makena.png", "noor.png" };
    for (const auto& f : slimModels) {
        (void)FS::write(FS::PathCombine(slimDir, f), m_session->skin_data);
    }

    // Legacy / 1.8-1.19.2 paths
    (void)FS::write(FS::PathCombine(entityDir, "steve.png"), m_session->skin_data);
    (void)FS::write(FS::PathCombine(entityDir, "alex.png"), m_session->skin_data);

    // Pre-1.6 / alpha / beta path
    (void)FS::write(FS::PathCombine(mobDir, "char.png"), m_session->skin_data);
}

void ApplyOfflineSkin::ensureResourcePackEnabled(const QString& gameDir)
{
    const QString optionsPath = FS::PathCombine(gameDir, "options.txt");
    const QString packEntry = "\"file/PrismOfflineSkin\"";

    if (!QFile::exists(optionsPath)) {
        // Create initial options.txt with the pack enabled
        QString content = QString("resourcePacks:[\"vanilla\",%1]\n").arg(packEntry);
        (void)FS::write(optionsPath, content.toUtf8());
        return;
    }

    QFile file(optionsPath);
    if (!file.open(QIODevice::ReadOnly | QIODevice::Text)) {
        return;
    }

    QStringList lines;
    bool foundRP = false;
    bool modified = false;

    QTextStream in(&file);
    while (!in.atEnd()) {
        QString line = in.readLine();
        if (line.startsWith("resourcePacks:")) {
            foundRP = true;
            if (!line.contains(packEntry)) {
                int closeBracket = line.lastIndexOf(']');
                if (closeBracket != -1) {
                    QString beforeClose = line.left(closeBracket);
                    if (beforeClose.endsWith('[') || beforeClose.endsWith("resourcePacks:[")) {
                        line = beforeClose + packEntry + line.mid(closeBracket);
                    } else {
                        line = beforeClose + "," + packEntry + line.mid(closeBracket);
                    }
                    modified = true;
                }
            }
        }
        lines.append(line);
    }
    file.close();

    if (!foundRP) {
        lines.append(QString("resourcePacks:[\"vanilla\",%1]").arg(packEntry));
        modified = true;
    }

    if (modified) {
        if (file.open(QIODevice::WriteOnly | QIODevice::Text | QIODevice::Truncate)) {
            QTextStream out(&file);
            for (const auto& l : lines) {
                out << l << "\n";
            }
            file.close();
        }
    }
}

void ApplyOfflineSkin::copyServerCommand(const QString& playerName, const QString& model)
{
    QString url = m_session->skin_url.trimmed();
    QString format = APPLICATION->settings()->get("OfflineSkinCopyFormat").toString();
    if (format.isEmpty()) {
        format = "url";
    }

    QString textToCopy;
    if (!url.isEmpty()) {
        if (format == "skin_url") {
            textToCopy = QString("/skin %1").arg(url);
        } else if (format == "skin_url_model") {
            textToCopy = QString("/skin url %1 %2").arg(url, model == "slim" ? "slim" : "classic");
        } else if (format == "skin_set") {
            textToCopy = QString("/skin set %1").arg(url);
        } else {
            // "url" (default: direct URL link)
            textToCopy = url;
        }
    }

    if (APPLICATION->settings()->get("AutoCopyOfflineSkinCommand").toBool() && !textToCopy.isEmpty()) {
        auto* clipboard = QGuiApplication::clipboard();
        if (clipboard) {
            clipboard->setText(textToCopy);
        }
        emit logLine(QString("[Offline Skin] Skin URL copied to clipboard: %1 (for server sharing)").arg(textToCopy),
                     MessageLevel::Launcher);
    } else {
        emit logLine(QString("[Offline Skin] Applied offline skin for player %1 (Singleplayer & LocalSkin)").arg(playerName),
                     MessageLevel::Launcher);
    }
}
