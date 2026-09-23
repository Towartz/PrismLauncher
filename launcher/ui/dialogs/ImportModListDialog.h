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

#include <QDialog>
#include <QList>

#include "modplatform/helpers/ImportFromModList.h"

class MinecraftInstance;
class ModFolderModel;
class QCheckBox;
class QComboBox;
class QLabel;
class QPlainTextEdit;
class QProgressBar;
class QPushButton;
class QTreeWidget;
class QTreeWidgetItem;
class ResourceAPI;

class ImportModListDialog : public QDialog {
    Q_OBJECT

   public:
    explicit ImportModListDialog(MinecraftInstance* instance, ModFolderModel* model, QWidget* parent = nullptr);
    ~ImportModListDialog() override = default;

   private slots:
    void loadFromFile();
    void pasteFromClipboard();
    void parseAndResolve();
    void copyUnresolvedToClipboard();
    void onTreeItemChanged(QTreeWidgetItem* item, int column);
    void downloadSelectedMods();

   private:
    void setupUi();
    void refreshTable();
    void updateSummaryLabel();
    bool resolveSingleEntry(ImportFromModList::ModEntry& entry,
                            ImportFromModList::ResolutionMode mode,
                            ImportFromModList::ProviderPriority priority);
    bool tryResolveWithProvider(ImportFromModList::ModEntry& entry, ModPlatform::ResourceProvider provider, const ResourceAPI* api);

    MinecraftInstance* m_instance = nullptr;
    ModFolderModel* m_model = nullptr;

    QPlainTextEdit* m_inputEdit = nullptr;
    QComboBox* m_modeCombo = nullptr;
    QComboBox* m_providerCombo = nullptr;
    QCheckBox* m_skipInstalledCheck = nullptr;
    QCheckBox* m_resolveDepsCheck = nullptr;
    QCheckBox* m_preserveEnabledCheck = nullptr;
    QPushButton* m_resolveButton = nullptr;
    QPushButton* m_copyUnresolvedButton = nullptr;
    QPushButton* m_downloadButton = nullptr;
    QTreeWidget* m_treeWidget = nullptr;
    QProgressBar* m_progressBar = nullptr;
    QLabel* m_summaryLabel = nullptr;

    QList<ImportFromModList::ModEntry> m_entries;
    bool m_isResolving = false;
    bool m_updatingTable = false;
};
