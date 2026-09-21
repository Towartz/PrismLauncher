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

#include <QDateTime>
#include <QDir>
#include <QList>
#include <QString>
#include <optional>

class ResourceFolderModel;

struct ModBackupEntry {
    QString id;                     //!< Unique backup id: timestamp_modId
    QString modId;                  //!< Mod slug or unique id
    QString modName;                //!< Human-readable mod name
    QDateTime timestamp;            //!< Time backup was taken
    QString archivedFile;           //!< File path relative to backupDir()
    QString originalFileName;       //!< Filename when installed in mods/
    QString archivedIndex;          //!< Index path relative to backupDir()
    QString originalIndexFileName;  //!< e.g. mod.pw.toml
    QString oldVersion;             //!< Replaced version string
    QString newVersion;             //!< Upgraded version string
    QString replacedByFileName;     //!< The newly installed filename
    qint64 fileSize = 0;            //!< File size in bytes

    [[nodiscard]] bool isValid() const { return !id.isEmpty() && !archivedFile.isEmpty(); }
};

class ModBackupManager {
   public:
    explicit ModBackupManager(const QString& instanceRoot);

    [[nodiscard]] QDir backupDir() const;
    [[nodiscard]] QString manifestPath() const;

    [[nodiscard]] static bool isBackupEnabled();
    [[nodiscard]] static int maxBackupsPerMod();

    /**
     * @brief Safely backup an existing mod file and its index metadata before replacement.
     */
    bool createBackup(const QString& oldFilePath,
                      const QString& oldIndexFilePath,
                      const QString& modId,
                      const QString& modName,
                      const QString& oldVersion,
                      const QString& newVersion,
                      const QString& replacedByFileName);

    /**
     * @brief Retrieve all backups for this instance, ordered newest first.
     */
    [[nodiscard]] QList<ModBackupEntry> getAllBackups() const;

    /**
     * @brief Retrieve backups for a specific modId or slug, ordered newest first.
     */
    [[nodiscard]] QList<ModBackupEntry> getBackupsForMod(const QString& modIdOrSlug) const;

    /**
     * @brief Retrieve the most recent backup for a specific modId or slug.
     */
    [[nodiscard]] std::optional<ModBackupEntry> getLatestBackupForMod(const QString& modIdOrSlug) const;

    /**
     * @brief Restore a backed-up version, safely replacing the currently installed version in the instance.
     */
    bool restoreBackup(const ModBackupEntry& entry, ResourceFolderModel* model, QString* errorMsg = nullptr);

    /**
     * @brief Delete a specific backup by its ID.
     */
    bool deleteBackup(const QString& backupId);

    /**
     * @brief Delete all backups for this instance.
     */
    bool clearAllBackups();

    /**
     * @brief Compute total bytes occupied by all backups.
     */
    [[nodiscard]] qint64 getTotalBackupSize() const;

   private:
    QString m_instanceRoot;

    void loadManifest(QList<ModBackupEntry>& backups) const;
    bool saveManifest(const QList<ModBackupEntry>& backups) const;
    void pruneOldBackups(const QString& modId, QList<ModBackupEntry>& backups);
};
