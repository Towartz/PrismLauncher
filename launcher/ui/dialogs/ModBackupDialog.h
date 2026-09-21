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

#include <QDialog>
#include <QLabel>
#include <QLineEdit>
#include <QPushButton>
#include <QTableWidget>
#include <memory>

#include "minecraft/MinecraftInstance.h"
#include "minecraft/mod/ModBackupManager.h"
#include "minecraft/mod/ResourceFolderModel.h"

class ModBackupDialog : public QDialog {
    Q_OBJECT

   public:
    explicit ModBackupDialog(QWidget* parent,
                             MinecraftInstance* instance,
                             ResourceFolderModel* model,
                             const QString& initialFilter = QString());
    ~ModBackupDialog() override = default;

   private slots:
    void refreshList();
    void onSelectionChanged();
    void onRestoreClicked();
    void onDeleteClicked();
    void onClearAllClicked();
    void onOpenFolderClicked();
    void filterChanged(const QString& text);

   private:
    MinecraftInstance* m_instance = nullptr;
    ResourceFolderModel* m_model = nullptr;
    std::unique_ptr<ModBackupManager> m_manager;

    QLineEdit* m_searchEdit = nullptr;
    QTableWidget* m_table = nullptr;
    QLabel* m_statusLabel = nullptr;

    QPushButton* m_restoreBtn = nullptr;
    QPushButton* m_deleteBtn = nullptr;
    QPushButton* m_clearAllBtn = nullptr;
    QPushButton* m_openFolderBtn = nullptr;

    QList<ModBackupEntry> m_entries;
    QList<int> m_visibleEntryIndices;

    void setupUi();
    static QString formatSize(qint64 bytes);
};
