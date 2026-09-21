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

#include "ModBackupDialog.h"

#include <QDialogButtonBox>
#include <QHBoxLayout>
#include <QHeaderView>
#include <QMessageBox>
#include <QVBoxLayout>

#include "DesktopServices.h"
#include "ui/dialogs/CustomMessageBox.h"

ModBackupDialog::ModBackupDialog(QWidget* parent, MinecraftInstance* instance, ResourceFolderModel* model, const QString& initialFilter)
    : QDialog(parent), m_instance(instance), m_model(model)
{
    m_manager = std::make_unique<ModBackupManager>(instance->instanceRoot());
    setupUi();

    if (!initialFilter.isEmpty()) {
        m_searchEdit->setText(initialFilter);
    }
    refreshList();
}

void ModBackupDialog::setupUi()
{
    setWindowTitle(tr("Mod Backups - %1").arg(m_instance->name()));
    resize(760, 480);

    auto* mainLayout = new QVBoxLayout(this);

    auto* descLabel =
        new QLabel(tr("Mod update backups are created automatically when updating mods.\n"
                      "If Minecraft crashes or has bugs after an update, select an older version below and click Rollback / Restore."),
                   this);
    descLabel->setWordWrap(true);
    mainLayout->addWidget(descLabel);

    // Search bar
    auto* searchLayout = new QHBoxLayout();
    auto* searchLabel = new QLabel(tr("Filter:"), this);
    m_searchEdit = new QLineEdit(this);
    m_searchEdit->setPlaceholderText(tr("Search by mod name, version, or file..."));
    m_searchEdit->setClearButtonEnabled(true);
    connect(m_searchEdit, &QLineEdit::textChanged, this, &ModBackupDialog::filterChanged);
    searchLayout->addWidget(searchLabel);
    searchLayout->addWidget(m_searchEdit);
    mainLayout->addLayout(searchLayout);

    // Table
    m_table = new QTableWidget(this);
    m_table->setColumnCount(5);
    m_table->setHorizontalHeaderLabels({ tr("Mod Name"), tr("Restorable Version"), tr("Replaced By"), tr("Backup Date"), tr("Size") });
    m_table->setSelectionBehavior(QAbstractItemView::SelectRows);
    m_table->setSelectionMode(QAbstractItemView::SingleSelection);
    m_table->setEditTriggers(QAbstractItemView::NoEditTriggers);
    m_table->setAlternatingRowColors(true);
    m_table->horizontalHeader()->setSectionResizeMode(0, QHeaderView::Stretch);
    m_table->horizontalHeader()->setSectionResizeMode(1, QHeaderView::ResizeToContents);
    m_table->horizontalHeader()->setSectionResizeMode(2, QHeaderView::ResizeToContents);
    m_table->horizontalHeader()->setSectionResizeMode(3, QHeaderView::ResizeToContents);
    m_table->horizontalHeader()->setSectionResizeMode(4, QHeaderView::ResizeToContents);
    connect(m_table, &QTableWidget::itemSelectionChanged, this, &ModBackupDialog::onSelectionChanged);
    connect(m_table, &QTableWidget::cellDoubleClicked, this,
            [this]([[maybe_unused]] int row, [[maybe_unused]] int column) { onRestoreClicked(); });
    mainLayout->addWidget(m_table);

    // Status label
    m_statusLabel = new QLabel(this);
    mainLayout->addWidget(m_statusLabel);

    // Action buttons
    auto* btnLayout = new QHBoxLayout();

    m_restoreBtn = new QPushButton(tr("Rollback / Restore Version"), this);
    m_restoreBtn->setIcon(QIcon::fromTheme("edit-undo"));
    m_restoreBtn->setEnabled(false);
    connect(m_restoreBtn, &QPushButton::clicked, this, &ModBackupDialog::onRestoreClicked);
    btnLayout->addWidget(m_restoreBtn);

    m_deleteBtn = new QPushButton(tr("Delete Backup"), this);
    m_deleteBtn->setIcon(QIcon::fromTheme("edit-delete"));
    m_deleteBtn->setEnabled(false);
    connect(m_deleteBtn, &QPushButton::clicked, this, &ModBackupDialog::onDeleteClicked);
    btnLayout->addWidget(m_deleteBtn);

    m_clearAllBtn = new QPushButton(tr("Clear All Backups"), this);
    connect(m_clearAllBtn, &QPushButton::clicked, this, &ModBackupDialog::onClearAllClicked);
    btnLayout->addWidget(m_clearAllBtn);

    btnLayout->addStretch();

    m_openFolderBtn = new QPushButton(tr("Open Backups Folder"), this);
    m_openFolderBtn->setIcon(QIcon::fromTheme("document-open-folder"));
    connect(m_openFolderBtn, &QPushButton::clicked, this, &ModBackupDialog::onOpenFolderClicked);
    btnLayout->addWidget(m_openFolderBtn);

    auto* closeBtn = new QPushButton(tr("Close"), this);
    connect(closeBtn, &QPushButton::clicked, this, &QDialog::accept);
    btnLayout->addWidget(closeBtn);

    mainLayout->addLayout(btnLayout);
}

void ModBackupDialog::refreshList()
{
    m_entries = m_manager->getAllBackups();
    filterChanged(m_searchEdit->text());
}

void ModBackupDialog::filterChanged(const QString& filterText)
{
    m_table->setRowCount(0);
    m_visibleEntryIndices.clear();

    qint64 totalBytes = 0;
    for (int i = 0; i < m_entries.size(); ++i) {
        const auto& entry = m_entries[i];
        totalBytes += entry.fileSize;

        if (!filterText.isEmpty()) {
            bool matches = entry.modName.contains(filterText, Qt::CaseInsensitive) ||
                           entry.modId.contains(filterText, Qt::CaseInsensitive) ||
                           entry.originalFileName.contains(filterText, Qt::CaseInsensitive) ||
                           entry.oldVersion.contains(filterText, Qt::CaseInsensitive);
            if (!matches) {
                continue;
            }
        }

        m_visibleEntryIndices.append(i);
        int row = m_table->rowCount();
        m_table->insertRow(row);

        auto* nameItem = new QTableWidgetItem(entry.modName);
        nameItem->setToolTip(entry.originalFileName);
        m_table->setItem(row, 0, nameItem);

        auto* oldVerItem = new QTableWidgetItem(entry.oldVersion.isEmpty() ? tr("(unknown)") : entry.oldVersion);
        m_table->setItem(row, 1, oldVerItem);

        auto* newVerItem = new QTableWidgetItem(entry.newVersion.isEmpty() ? tr("-") : entry.newVersion);
        m_table->setItem(row, 2, newVerItem);

        auto* dateItem = new QTableWidgetItem(entry.timestamp.toLocalTime().toString("yyyy-MM-dd HH:mm"));
        m_table->setItem(row, 3, dateItem);

        auto* sizeItem = new QTableWidgetItem(formatSize(entry.fileSize));
        sizeItem->setTextAlignment(Qt::AlignRight | Qt::AlignVCenter);
        m_table->setItem(row, 4, sizeItem);
    }

    m_statusLabel->setText(tr("Total backups: %1 (%2)").arg(m_entries.size()).arg(formatSize(totalBytes)));
    m_clearAllBtn->setEnabled(!m_entries.isEmpty());
    onSelectionChanged();
}

void ModBackupDialog::onSelectionChanged()
{
    int row = m_table->currentRow();
    bool hasSelection = (row >= 0 && row < m_visibleEntryIndices.size());
    m_restoreBtn->setEnabled(hasSelection);
    m_deleteBtn->setEnabled(hasSelection);
}

void ModBackupDialog::onRestoreClicked()
{
    int row = m_table->currentRow();
    if (row < 0 || row >= m_visibleEntryIndices.size()) {
        return;
    }

    const auto& entry = m_entries[m_visibleEntryIndices[row]];

    if (m_instance->isRunning()) {
        CustomMessageBox::selectable(this, tr("Game Running"),
                                     tr("Cannot rollback mods while Minecraft is running. Please close the game first."),
                                     QMessageBox::Warning)
            ->exec();
        return;
    }

    auto confirm =
        CustomMessageBox::selectable(
            this, tr("Confirm Rollback"),
            tr("Are you sure you want to rollback '%1' to version '%2'?\n\n"
               "This will restore '%3' and replace any current version of this mod.")
                .arg(entry.modName, entry.oldVersion.isEmpty() ? entry.originalFileName : entry.oldVersion, entry.originalFileName),
            QMessageBox::Question, QMessageBox::Yes | QMessageBox::No, QMessageBox::Yes)
            ->exec();

    if (confirm != QMessageBox::Yes) {
        return;
    }

    QString error;
    if (!m_manager->restoreBackup(entry, m_model, &error)) {
        CustomMessageBox::selectable(this, tr("Rollback Failed"), tr("Failed to rollback mod: %1").arg(error), QMessageBox::Critical)
            ->exec();
        return;
    }

    CustomMessageBox::selectable(this, tr("Rollback Successful"),
                                 tr("'%1' has been successfully rolled back to version '%2'.")
                                     .arg(entry.modName, entry.oldVersion.isEmpty() ? entry.originalFileName : entry.oldVersion),
                                 QMessageBox::Information)
        ->exec();

    accept();
}

void ModBackupDialog::onDeleteClicked()
{
    int row = m_table->currentRow();
    if (row < 0 || row >= m_visibleEntryIndices.size()) {
        return;
    }

    const auto& entry = m_entries[m_visibleEntryIndices[row]];

    auto confirm = CustomMessageBox::selectable(
                       this, tr("Confirm Delete"),
                       tr("Are you sure you want to delete the backup for '%1' (%2)?").arg(entry.modName, entry.originalFileName),
                       QMessageBox::Warning, QMessageBox::Yes | QMessageBox::No, QMessageBox::No)
                       ->exec();

    if (confirm == QMessageBox::Yes) {
        m_manager->deleteBackup(entry.id);
        refreshList();
    }
}

void ModBackupDialog::onClearAllClicked()
{
    auto confirm = CustomMessageBox::selectable(this, tr("Confirm Clear All"),
                                                tr("Are you sure you want to delete ALL mod backups for this instance?\n"
                                                   "This will permanently delete all backup files and cannot be undone."),
                                                QMessageBox::Warning, QMessageBox::Yes | QMessageBox::No, QMessageBox::No)
                       ->exec();

    if (confirm == QMessageBox::Yes) {
        m_manager->clearAllBackups();
        refreshList();
    }
}

void ModBackupDialog::onOpenFolderClicked()
{
    DesktopServices::openPath(m_manager->backupDir().absolutePath(), true);
}

QString ModBackupDialog::formatSize(qint64 bytes)
{
    if (bytes < 1024) {
        return tr("%1 B").arg(bytes);
    }
    if (bytes < 1024 * 1024) {
        return tr("%1 KB").arg(QString::number(bytes / 1024.0, 'f', 1));
    }
    return tr("%1 MB").arg(QString::number(bytes / (1024.0 * 1024.0), 'f', 1));
}
