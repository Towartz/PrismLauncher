// SPDX-License-Identifier: GPL-3.0-only
/*
 *  Prism Launcher - Minecraft Launcher
 *  Copyright (c) 2026 Trial97 <alexandru.tripon97@gmail.com>
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

#include <QList>
#include <QString>
#include <QStringList>
#include <QVariant>
#include <optional>

#include "modplatform/ModIndex.h"

class MinecraftInstance;
class ModFolderModel;

namespace ImportFromModList {

enum class ResolutionMode : std::uint8_t {
    AdaptiveToInstance = 0,  // Use exact version if compatible with target instance; otherwise resolve best match for target MC + loader
    ExactPreferExported = 1  // Use exact exported download_url / file_id whenever available
};

enum class ProviderPriority : std::uint8_t { ModrinthFirst = 0, CurseForgeFirst = 1 };

enum class EntryStatus : std::uint8_t {
    Pending = 0,
    Resolving,
    ReadyExact,
    ReadyAdapted,
    ReadyDirectUrl,
    AlreadyInstalled,
    NotFound,
    Error
};

struct ModEntry {
    QString rawLine;
    QString name;
    QString slug;
    QString modId;
    QString version;
    QString filename;
    QString homepageUrl;
    QString downloadUrl;
    std::optional<ModPlatform::ResourceProvider> provider;
    QVariant projectId;
    QVariant fileId;
    QString hash;
    QString hashFormat;
    QStringList mcVersions;
    ModPlatform::ModLoaderTypes loaders = ModPlatform::ModLoaderType::None;
    bool enabled = true;

    // Resolution result
    bool selected = true;
    EntryStatus status = EntryStatus::Pending;
    QString statusText;
    ModPlatform::IndexedPack::Ptr resolvedPack;
    ModPlatform::IndexedVersion resolvedVersion;

    QString displayName() const
    {
        if (!name.isEmpty())
            return name;
        if (!slug.isEmpty())
            return slug;
        if (!modId.isEmpty())
            return modId;
        if (!filename.isEmpty())
            return filename;
        return rawLine;
    }

    QString searchQuery() const;
    bool isExactCompatibleWith(const QString& targetMcVersion, ModPlatform::ModLoaderTypes targetLoaders) const;
};

QList<ModEntry> parse(const QString& content);
void markAlreadyInstalled(QList<ModEntry>& entries, ModFolderModel* model, bool uncheckInstalled = true);
QString cleanQueryFromFilename(const QString& filename);
QString normalizeKey(const QString& str);

}  // namespace ImportFromModList
