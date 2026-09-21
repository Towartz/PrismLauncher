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

#include "ModBackupManager.h"

#include <QDebug>
#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <algorithm>

#include "Application.h"
#include "FileSystem.h"
#include "Json.h"
#include "minecraft/MinecraftInstance.h"
#include "minecraft/mod/Resource.h"
#include "minecraft/mod/ResourceFolderModel.h"

ModBackupManager::ModBackupManager(const QString& instanceRoot) : m_instanceRoot(instanceRoot) {}

QDir ModBackupManager::backupDir() const
{
    return QDir(FS::PathCombine(m_instanceRoot, "mod_backups"));
}

QString ModBackupManager::manifestPath() const
{
    return FS::PathCombine(backupDir().absolutePath(), "manifest.json");
}

bool ModBackupManager::isBackupEnabled()
{
    if (APPLICATION && APPLICATION->settings()) {
        auto val = APPLICATION->settings()->get("KeepModBackups");
        if (!val.isNull() && val.isValid()) {
            return val.toBool();
        }
    }
    return true;
}

int ModBackupManager::maxBackupsPerMod()
{
    if (APPLICATION && APPLICATION->settings()) {
        auto val = APPLICATION->settings()->get("MaxModBackupsPerMod");
        if (!val.isNull() && val.isValid()) {
            return std::clamp(val.toInt(), 1, 20);
        }
    }
    return 3;
}

void ModBackupManager::loadManifest(QList<ModBackupEntry>& backups) const
{
    backups.clear();
    QFile file(manifestPath());
    if (!file.exists() || !file.open(QIODevice::ReadOnly)) {
        return;
    }

    auto doc = QJsonDocument::fromJson(file.readAll());
    if (!doc.isObject()) {
        return;
    }

    auto root = doc.object();
    auto array = root.value("backups").toArray();
    for (const auto& item : array) {
        if (!item.isObject()) {
            continue;
        }
        auto obj = item.toObject();
        ModBackupEntry entry;
        entry.id = obj.value("id").toString();
        entry.modId = obj.value("modId").toString();
        entry.modName = obj.value("modName").toString();
        entry.timestamp = QDateTime::fromString(obj.value("timestamp").toString(), Qt::ISODate);
        entry.archivedFile = obj.value("archivedFile").toString();
        entry.originalFileName = obj.value("originalFileName").toString();
        entry.archivedIndex = obj.value("archivedIndex").toString();
        entry.originalIndexFileName = obj.value("originalIndexFileName").toString();
        entry.oldVersion = obj.value("oldVersion").toString();
        entry.newVersion = obj.value("newVersion").toString();
        entry.replacedByFileName = obj.value("replacedByFileName").toString();
        entry.fileSize = obj.value("fileSize").toVariant().toLongLong();

        if (entry.isValid()) {
            backups.append(entry);
        }
    }
}

bool ModBackupManager::saveManifest(const QList<ModBackupEntry>& backups) const
{
    if (!FS::ensureFolderPathExists(backupDir().absolutePath())) {
        return false;
    }

    QJsonArray array;
    for (const auto& entry : backups) {
        QJsonObject obj;
        obj.insert("id", entry.id);
        obj.insert("modId", entry.modId);
        obj.insert("modName", entry.modName);
        obj.insert("timestamp", entry.timestamp.toString(Qt::ISODate));
        obj.insert("archivedFile", entry.archivedFile);
        obj.insert("originalFileName", entry.originalFileName);
        obj.insert("archivedIndex", entry.archivedIndex);
        obj.insert("originalIndexFileName", entry.originalIndexFileName);
        obj.insert("oldVersion", entry.oldVersion);
        obj.insert("newVersion", entry.newVersion);
        obj.insert("replacedByFileName", entry.replacedByFileName);
        obj.insert("fileSize", entry.fileSize);
        array.append(obj);
    }

    QJsonObject root;
    root.insert("version", 1);
    root.insert("backups", array);

    QFile file(manifestPath());
    if (!file.open(QIODevice::WriteOnly | QIODevice::Truncate)) {
        qWarning() << "Failed to open mod backup manifest for writing:" << file.errorString();
        return false;
    }

    file.write(QJsonDocument(root).toJson(QJsonDocument::Indented));
    file.close();
    return true;
}

bool ModBackupManager::createBackup(const QString& oldFilePath,
                                    const QString& oldIndexFilePath,
                                    const QString& modId,
                                    const QString& modName,
                                    const QString& oldVersion,
                                    const QString& newVersion,
                                    const QString& replacedByFileName)
{
    if (!isBackupEnabled()) {
        qDebug() << "Mod backup skipped: feature is disabled in settings.";
        return true;
    }

    QFileInfo oldFileInfo(oldFilePath);
    if (!oldFileInfo.exists() || !oldFileInfo.isFile()) {
        qWarning() << "Mod backup failed: old file does not exist:" << oldFilePath;
        return false;
    }

    QString safeModId = modId.isEmpty() ? oldFileInfo.baseName() : modId;
    safeModId.replace(QRegularExpression("[^a-zA-Z0-9._-]"), "_");

    QDir modTargetDir(FS::PathCombine(backupDir().absolutePath(), safeModId));
    if (!FS::ensureFolderPathExists(modTargetDir.absolutePath())) {
        qWarning() << "Failed to create mod backup folder:" << modTargetDir.absolutePath();
        return false;
    }

    QString timestampStr = QDateTime::currentDateTimeUtc().toString("yyyyMMdd_HHmmss");
    QString backupFileRel = FS::PathCombine(safeModId, QString("%1_%2").arg(timestampStr, oldFileInfo.fileName()));
    QString backupFileAbs = FS::PathCombine(backupDir().absolutePath(), backupFileRel);

    if (!FS::copy(oldFilePath, backupFileAbs).overwrite(true)()) {
        qWarning() << "Failed to copy old mod file to backup location:" << backupFileAbs;
        return false;
    }

    QString backupIndexRel;
    QString originalIndexName;
    if (!oldIndexFilePath.isEmpty()) {
        QFileInfo oldIndexInfo(oldIndexFilePath);
        if (oldIndexInfo.exists() && oldIndexInfo.isFile()) {
            originalIndexName = oldIndexInfo.fileName();
            backupIndexRel = FS::PathCombine(safeModId, QString("%1_%2").arg(timestampStr, originalIndexName));
            QString backupIndexAbs = FS::PathCombine(backupDir().absolutePath(), backupIndexRel);
            FS::copy(oldIndexFilePath, backupIndexAbs).overwrite(true)();
        }
    }

    ModBackupEntry entry;
    entry.id = QString("%1_%2").arg(timestampStr, safeModId);
    entry.modId = safeModId;
    entry.modName = modName.isEmpty() ? oldFileInfo.baseName() : modName;
    entry.timestamp = QDateTime::currentDateTimeUtc();
    entry.archivedFile = backupFileRel;
    entry.originalFileName = oldFileInfo.fileName();
    entry.archivedIndex = backupIndexRel;
    entry.originalIndexFileName = originalIndexName;
    entry.oldVersion = oldVersion;
    entry.newVersion = newVersion;
    entry.replacedByFileName = replacedByFileName;
    entry.fileSize = oldFileInfo.size();

    QList<ModBackupEntry> backups;
    loadManifest(backups);

    // Prepend new backup (newest first)
    backups.prepend(entry);

    // Prune excess backups for this mod
    pruneOldBackups(safeModId, backups);

    saveManifest(backups);
    qDebug() << "Successfully created mod backup for" << entry.modName << "version" << oldVersion << "at" << backupFileAbs;
    return true;
}

void ModBackupManager::pruneOldBackups(const QString& modId, QList<ModBackupEntry>& backups)
{
    int limit = maxBackupsPerMod();
    int count = 0;
    auto it = backups.begin();
    while (it != backups.end()) {
        if (it->modId == modId) {
            count++;
            if (count > limit) {
                // Delete old archived files
                QString fileAbs = FS::PathCombine(backupDir().absolutePath(), it->archivedFile);
                FS::deletePath(fileAbs);
                if (!it->archivedIndex.isEmpty()) {
                    QString indexAbs = FS::PathCombine(backupDir().absolutePath(), it->archivedIndex);
                    FS::deletePath(indexAbs);
                }
                it = backups.erase(it);
                continue;
            }
        }
        ++it;
    }
}

QList<ModBackupEntry> ModBackupManager::getAllBackups() const
{
    QList<ModBackupEntry> backups;
    loadManifest(backups);
    return backups;
}

QList<ModBackupEntry> ModBackupManager::getBackupsForMod(const QString& modIdOrSlug) const
{
    QList<ModBackupEntry> all = getAllBackups();
    QList<ModBackupEntry> filtered;
    for (const auto& item : all) {
        if (item.modId.compare(modIdOrSlug, Qt::CaseInsensitive) == 0 || item.modName.compare(modIdOrSlug, Qt::CaseInsensitive) == 0) {
            filtered.append(item);
        }
    }
    return filtered;
}

std::optional<ModBackupEntry> ModBackupManager::getLatestBackupForMod(const QString& modIdOrSlug) const
{
    auto list = getBackupsForMod(modIdOrSlug);
    if (!list.isEmpty()) {
        return list.first();
    }
    return std::nullopt;
}

bool ModBackupManager::restoreBackup(const ModBackupEntry& entry, ResourceFolderModel* model, QString* errorMsg)
{
    if (!entry.isValid()) {
        if (errorMsg)
            *errorMsg = QObject::tr("Invalid backup entry.");
        return false;
    }

    if (model && model->instance() && model->instance()->isRunning()) {
        if (errorMsg)
            *errorMsg = QObject::tr("Cannot rollback mods while Minecraft is running. Please stop the game first.");
        return false;
    }

    QString archivedPath = FS::PathCombine(backupDir().absolutePath(), entry.archivedFile);
    if (!QFileInfo::exists(archivedPath)) {
        if (errorMsg)
            *errorMsg = QObject::tr("Backup file not found at: %1").arg(archivedPath);
        return false;
    }

    QDir modsDir = model ? model->dir() : QDir(FS::PathCombine(m_instanceRoot, "minecraft/mods"));
    QDir indexDir = model ? model->indexDir() : QDir(FS::PathCombine(modsDir.absolutePath(), ".index"));

    // Find and uninstall currently installed file for this mod (if present)
    if (model) {
        QString fileToRemove;
        for (const auto* res : model->allResources()) {
            if (!res)
                continue;
            auto meta = res->metadata();
            if ((meta && meta->slug.compare(entry.modId, Qt::CaseInsensitive) == 0) ||
                res->fileinfo().fileName().compare(entry.replacedByFileName, Qt::CaseInsensitive) == 0 ||
                res->fileinfo().fileName().compare(entry.originalFileName, Qt::CaseInsensitive) == 0) {
                fileToRemove = res->fileinfo().fileName();
                break;
            }
        }
        if (!fileToRemove.isEmpty()) {
            model->uninstallResource(fileToRemove, true);
        }
    }

    // Restore the archived mod file
    QString destFilePath = modsDir.filePath(entry.originalFileName);
    if (!FS::copy(archivedPath, destFilePath).overwrite(true)()) {
        if (errorMsg)
            *errorMsg = QObject::tr("Failed to copy backup file to: %1").arg(destFilePath);
        return false;
    }

    // Restore the archived index metadata file if available
    if (!entry.archivedIndex.isEmpty() && !entry.originalIndexFileName.isEmpty()) {
        QString archivedIndexAbs = FS::PathCombine(backupDir().absolutePath(), entry.archivedIndex);
        if (QFileInfo::exists(archivedIndexAbs)) {
            FS::ensureFolderPathExists(indexDir.absolutePath());
            QString destIndexPath = indexDir.filePath(entry.originalIndexFileName);
            FS::copy(archivedIndexAbs, destIndexPath).overwrite(true)();
        }
    }

    if (model) {
        model->update();
    }

    qDebug() << "Successfully restored mod backup" << entry.originalFileName << "version" << entry.oldVersion;
    return true;
}

bool ModBackupManager::deleteBackup(const QString& backupId)
{
    QList<ModBackupEntry> backups;
    loadManifest(backups);

    auto it = std::find_if(backups.begin(), backups.end(), [&](const ModBackupEntry& e) { return e.id == backupId; });
    if (it == backups.end()) {
        return false;
    }

    FS::deletePath(FS::PathCombine(backupDir().absolutePath(), it->archivedFile));
    if (!it->archivedIndex.isEmpty()) {
        FS::deletePath(FS::PathCombine(backupDir().absolutePath(), it->archivedIndex));
    }
    backups.erase(it);

    return saveManifest(backups);
}

bool ModBackupManager::clearAllBackups()
{
    bool ok = FS::deletePath(backupDir().absolutePath());
    return ok;
}

qint64 ModBackupManager::getTotalBackupSize() const
{
    qint64 total = 0;
    for (const auto& item : getAllBackups()) {
        total += item.fileSize;
    }
    return total;
}
