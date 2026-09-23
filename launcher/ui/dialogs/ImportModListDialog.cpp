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
#include "ImportModListDialog.h"

#include <QApplication>
#include <QCheckBox>
#include <QClipboard>
#include <QComboBox>
#include <QDialogButtonBox>
#include <QEventLoop>
#include <QFileDialog>
#include <QFileInfo>
#include <QHBoxLayout>
#include <QHeaderView>
#include <QLabel>
#include <QMessageBox>
#include <QPlainTextEdit>
#include <QProgressBar>
#include <QPushButton>
#include <QSet>
#include <QSplitter>
#include <QTreeWidget>
#include <QVBoxLayout>

#include "Application.h"
#include "FileSystem.h"
#include "ResourceDownloadTask.h"
#include "minecraft/MinecraftInstance.h"
#include "minecraft/PackProfile.h"
#include "minecraft/mod/ModFolderModel.h"
#include "minecraft/mod/tasks/GetModDependenciesTask.h"
#include "modplatform/ResourceAPI.h"
#include "modplatform/flame/FlameAPI.h"
#include "modplatform/modrinth/ModrinthAPI.h"
#include "net/ApiRequest.h"
#include "net/NetJob.h"
#include "tasks/ConcurrentTask.h"
#include "ui/dialogs/CustomMessageBox.h"
#include "ui/dialogs/ProgressDialog.h"

ImportModListDialog::ImportModListDialog(MinecraftInstance* instance, ModFolderModel* model, QWidget* parent)
    : QDialog(parent), m_instance(instance), m_model(model)
{
    setWindowTitle(tr("Import Mod List & Resolve Download Sources"));
    resize(920, 680);
    setupUi();
}

void ImportModListDialog::setupUi()
{
    auto* mainLayout = new QVBoxLayout(this);

    // Top bar: Load File / Paste Clipboard / Resolve
    auto* topBtnLayout = new QHBoxLayout();
    auto* loadFileBtn = new QPushButton(tr("Load from File..."), this);
    auto* pasteBtn = new QPushButton(tr("Paste from Clipboard"), this);
    m_resolveButton = new QPushButton(tr("Parse && Resolve Download Sources"), this);
    m_resolveButton->setDefault(true);

    topBtnLayout->addWidget(loadFileBtn);
    topBtnLayout->addWidget(pasteBtn);
    topBtnLayout->addStretch();
    topBtnLayout->addWidget(m_resolveButton);
    mainLayout->addLayout(topBtnLayout);

    // Options row
    auto* optionsLayout = new QHBoxLayout();
    optionsLayout->addWidget(new QLabel(tr("Version Mode:"), this));
    m_modeCombo = new QComboBox(this);
    m_modeCombo->addItem(tr("Match Target Instance MC Version & Loader (Recommended)"));
    m_modeCombo->addItem(tr("Prefer Exact Exported Version / URL"));
    optionsLayout->addWidget(m_modeCombo);

    optionsLayout->addSpacing(12);
    optionsLayout->addWidget(new QLabel(tr("Preferred Source:"), this));
    m_providerCombo = new QComboBox(this);
    m_providerCombo->addItem(tr("Modrinth -> CurseForge (Recommended)"));
    m_providerCombo->addItem(tr("CurseForge -> Modrinth"));
    optionsLayout->addWidget(m_providerCombo);
    optionsLayout->addStretch();
    mainLayout->addLayout(optionsLayout);

    // Checkboxes row
    auto* checkLayout = new QHBoxLayout();
    m_skipInstalledCheck = new QCheckBox(tr("Skip already installed mods"), this);
    m_skipInstalledCheck->setChecked(true);
    m_resolveDepsCheck = new QCheckBox(tr("Automatically install required dependencies"), this);
    m_resolveDepsCheck->setChecked(true);
    m_preserveEnabledCheck = new QCheckBox(tr("Preserve enabled/disabled state from export"), this);
    m_preserveEnabledCheck->setChecked(true);

    checkLayout->addWidget(m_skipInstalledCheck);
    checkLayout->addWidget(m_resolveDepsCheck);
    checkLayout->addWidget(m_preserveEnabledCheck);
    checkLayout->addStretch();
    mainLayout->addLayout(checkLayout);

    // Splitter with input text area (top) and resolved mods preview table (bottom)
    auto* splitter = new QSplitter(Qt::Vertical, this);

    m_inputEdit = new QPlainTextEdit(splitter);
    m_inputEdit->setPlaceholderText(
        tr("Paste or load any mod list here:\n"
           "• Prism Launcher exported JSON, Markdown, HTML, CSV, or Plain Text\n"
           "• Modrinth / CurseForge / GitHub / Direct .jar URLs\n"
           "• CurseForge manifest.json or Modrinth modrinth.index.json\n"
           "• Plain mod names, slugs, filenames, or Minecraft crash report mod tables"));
    splitter->addWidget(m_inputEdit);

    m_treeWidget = new QTreeWidget(splitter);
    m_treeWidget->setColumnCount(4);
    m_treeWidget->setHeaderLabels({ tr("Mod / Entry"), tr("Status"), tr("Source"), tr("Resolved Version / File") });
    m_treeWidget->setRootIsDecorated(false);
    m_treeWidget->setAlternatingRowColors(true);
    m_treeWidget->header()->setSectionResizeMode(0, QHeaderView::Stretch);
    m_treeWidget->header()->setSectionResizeMode(1, QHeaderView::ResizeToContents);
    m_treeWidget->header()->setSectionResizeMode(2, QHeaderView::ResizeToContents);
    m_treeWidget->header()->setSectionResizeMode(3, QHeaderView::Stretch);
    splitter->addWidget(m_treeWidget);

    splitter->setStretchFactor(0, 2);
    splitter->setStretchFactor(1, 3);
    mainLayout->addWidget(splitter, 1);

    // Progress bar & summary row
    m_progressBar = new QProgressBar(this);
    m_progressBar->setVisible(false);
    mainLayout->addWidget(m_progressBar);

    auto* bottomLayout = new QHBoxLayout();
    m_summaryLabel = new QLabel(tr("Load a file or paste a mod list, then click 'Parse & Resolve Download Sources'."), this);
    m_copyUnresolvedButton = new QPushButton(tr("Copy Unresolved List"), this);
    m_copyUnresolvedButton->setEnabled(false);

    auto* buttonBox = new QDialogButtonBox(QDialogButtonBox::Cancel, this);
    m_downloadButton = buttonBox->addButton(tr("Download && Install Selected"), QDialogButtonBox::AcceptRole);
    m_downloadButton->setEnabled(false);

    bottomLayout->addWidget(m_summaryLabel, 1);
    bottomLayout->addWidget(m_copyUnresolvedButton);
    bottomLayout->addWidget(buttonBox);
    mainLayout->addLayout(bottomLayout);

    connect(loadFileBtn, &QPushButton::clicked, this, &ImportModListDialog::loadFromFile);
    connect(pasteBtn, &QPushButton::clicked, this, &ImportModListDialog::pasteFromClipboard);
    connect(m_resolveButton, &QPushButton::clicked, this, &ImportModListDialog::parseAndResolve);
    connect(m_copyUnresolvedButton, &QPushButton::clicked, this, &ImportModListDialog::copyUnresolvedToClipboard);
    connect(m_treeWidget, &QTreeWidget::itemChanged, this, &ImportModListDialog::onTreeItemChanged);
    connect(buttonBox, &QDialogButtonBox::rejected, this, &QDialog::reject);
    connect(m_downloadButton, &QPushButton::clicked, this, &ImportModListDialog::downloadSelectedMods);
}

void ImportModListDialog::loadFromFile()
{
    const QString filePath = QFileDialog::getOpenFileName(this, tr("Open Mod List"), QDir::homePath(),
                                                          tr("Mod List Files (*.json *.md *.txt *.html *.csv *.toml);;All Files (*)"));
    if (filePath.isEmpty()) {
        return;
    }

    QFile file(filePath);
    if (!file.open(QIODevice::ReadOnly | QIODevice::Text)) {
        QMessageBox::warning(this, tr("Error"), tr("Could not open file: %1").arg(filePath));
        return;
    }

    m_inputEdit->setPlainText(QString::fromUtf8(file.readAll()));
    parseAndResolve();
}

void ImportModListDialog::pasteFromClipboard()
{
    if (auto* clip = QApplication::clipboard()) {
        const QString text = clip->text();
        if (!text.trimmed().isEmpty()) {
            m_inputEdit->setPlainText(text);
            parseAndResolve();
        }
    }
}

void ImportModListDialog::parseAndResolve()
{
    if (m_isResolving) {
        return;
    }

    m_entries = ImportFromModList::parse(m_inputEdit->toPlainText());
    if (m_entries.isEmpty()) {
        refreshTable();
        m_summaryLabel->setText(tr("No valid mod entries found in input."));
        return;
    }

    ImportFromModList::markAlreadyInstalled(m_entries, m_model, m_skipInstalledCheck->isChecked());
    refreshTable();

    m_isResolving = true;
    m_resolveButton->setEnabled(false);
    m_downloadButton->setEnabled(false);
    m_progressBar->setVisible(true);
    m_progressBar->setRange(0, m_entries.size());
    m_progressBar->setValue(0);

    const auto mode = m_modeCombo->currentIndex() == 0 ? ImportFromModList::ResolutionMode::AdaptiveToInstance
                                                       : ImportFromModList::ResolutionMode::ExactPreferExported;
    const auto priority = m_providerCombo->currentIndex() == 0 ? ImportFromModList::ProviderPriority::ModrinthFirst
                                                               : ImportFromModList::ProviderPriority::CurseForgeFirst;

    for (int i = 0; i < m_entries.size(); ++i) {
        auto& entry = m_entries[i];
        if (entry.status != ImportFromModList::EntryStatus::AlreadyInstalled || !m_skipInstalledCheck->isChecked()) {
            entry.status = ImportFromModList::EntryStatus::Resolving;
            entry.statusText = tr("Resolving...");
            refreshTable();
            QApplication::processEvents();

            resolveSingleEntry(entry, mode, priority);
        }
        m_progressBar->setValue(i + 1);
        refreshTable();
        QApplication::processEvents();
    }

    m_progressBar->setVisible(false);
    m_resolveButton->setEnabled(true);
    m_isResolving = false;
    refreshTable();
}

bool ImportModListDialog::resolveSingleEntry(ImportFromModList::ModEntry& entry,
                                             ImportFromModList::ResolutionMode mode,
                                             ImportFromModList::ProviderPriority priority)
{
    auto* profile = m_instance ? m_instance->getPackProfile() : nullptr;
    const QString targetMcVer = profile ? profile->getComponentVersion("net.minecraft") : QString();
    const ModPlatform::ModLoaderTypes targetLoaders =
        (profile && profile->getModLoaders().has_value()) ? profile->getModLoaders().value() : ModPlatform::ModLoaderType::None;

    // Tier 1: Check if exact download URL + metadata is already present and compatible (or ExactPreferExported is chosen)
    const bool useExact = !entry.downloadUrl.isEmpty() && (mode == ImportFromModList::ResolutionMode::ExactPreferExported ||
                                                           entry.isExactCompatibleWith(targetMcVer, targetLoaders));

    if (useExact) {
        if (entry.provider.has_value() && !entry.projectId.isNull() && !entry.fileId.isNull()) {
            auto pack = std::make_shared<ModPlatform::IndexedPack>();
            pack->addonId = entry.projectId;
            pack->provider = *entry.provider;
            pack->name = entry.displayName();
            pack->slug = !entry.slug.isEmpty() ? entry.slug : ImportFromModList::normalizeKey(entry.displayName());
            pack->websiteUrl = entry.homepageUrl;

            ModPlatform::IndexedVersion ver;
            ver.addonId = entry.projectId;
            ver.fileId = entry.fileId;
            ver.version = !entry.version.isEmpty() ? entry.version : entry.fileId.toString();
            ver.versionNumber = ver.version;
            ver.downloadUrl = entry.downloadUrl;
            ver.fileName = !entry.filename.isEmpty() ? entry.filename : QFileInfo(QUrl(entry.downloadUrl).path()).fileName();
            ver.hash = entry.hash;
            ver.hashType = entry.hashFormat;
            ver.mcVersion = !entry.mcVersions.isEmpty() ? entry.mcVersions : QStringList{ targetMcVer };
            ver.loaders = entry.loaders != ModPlatform::ModLoaderType::None ? entry.loaders : targetLoaders;

            entry.resolvedPack = pack;
            entry.resolvedVersion = ver;
            entry.status = ImportFromModList::EntryStatus::ReadyExact;
            entry.statusText = tr("Ready (Exact Match)");
            entry.selected = true;
            return true;
        }

        // Direct .jar download URL without provider metadata (e.g. GitHub release .jar)
        ModPlatform::IndexedVersion ver;
        ver.downloadUrl = entry.downloadUrl;
        ver.fileName = !entry.filename.isEmpty() ? entry.filename : QFileInfo(QUrl(entry.downloadUrl).path()).fileName();
        ver.version = entry.version;
        ver.hash = entry.hash;
        ver.hashType = entry.hashFormat;
        entry.resolvedVersion = ver;
        entry.status = ImportFromModList::EntryStatus::ReadyDirectUrl;
        entry.statusText = tr("Ready (Direct URL)");
        entry.selected = true;
        return true;
    }

    // Tier 2 & 3: Smart lookup across Modrinth & CurseForge for target instance MC version + Loader
    QList<std::pair<ModPlatform::ResourceProvider, const ResourceAPI*>> providers;
    if (priority == ImportFromModList::ProviderPriority::ModrinthFirst) {
        providers.append({ ModPlatform::ResourceProvider::MODRINTH, &ModrinthAPI::get() });
        providers.append({ ModPlatform::ResourceProvider::FLAME, &FlameAPI::get() });
    } else {
        providers.append({ ModPlatform::ResourceProvider::FLAME, &FlameAPI::get() });
        providers.append({ ModPlatform::ResourceProvider::MODRINTH, &ModrinthAPI::get() });
    }

    // If the entry explicitly came from a specific provider, try that provider first when its projectId/slug is known
    if (entry.provider.has_value() && (!entry.projectId.isNull() || !entry.slug.isEmpty())) {
        const auto explicitProv = *entry.provider;
        if (providers.first().first != explicitProv) {
            std::swap(providers[0], providers[1]);
        }
    }

    for (const auto& [prov, api] : providers) {
        if (tryResolveWithProvider(entry, prov, api)) {
            return true;
        }
    }

    // Fallback: if exact downloadUrl existed (even for another MC version) and user wants a fallback or direct URL
    if (!entry.downloadUrl.isEmpty() && mode == ImportFromModList::ResolutionMode::ExactPreferExported) {
        ModPlatform::IndexedVersion ver;
        ver.downloadUrl = entry.downloadUrl;
        ver.fileName = !entry.filename.isEmpty() ? entry.filename : QFileInfo(QUrl(entry.downloadUrl).path()).fileName();
        ver.version = entry.version;
        entry.resolvedVersion = ver;
        entry.status = ImportFromModList::EntryStatus::ReadyDirectUrl;
        entry.statusText = tr("Ready (Direct URL)");
        entry.selected = true;
        return true;
    }

    entry.status = ImportFromModList::EntryStatus::NotFound;
    entry.statusText = tr("Not found for MC %1").arg(targetMcVer.isEmpty() ? tr("current") : targetMcVer);
    entry.selected = false;
    return false;
}

bool ImportModListDialog::tryResolveWithProvider(ImportFromModList::ModEntry& entry,
                                                 ModPlatform::ResourceProvider provider,
                                                 const ResourceAPI* api)
{
    auto* profile = m_instance ? m_instance->getPackProfile() : nullptr;
    const QString targetMcVer = profile ? profile->getComponentVersion("net.minecraft") : QString();
    const ModPlatform::ModLoaderTypes targetLoaders =
        (profile && profile->getModLoaders().has_value()) ? profile->getModLoaders().value() : ModPlatform::ModLoaderType::None;

    const QString query = entry.searchQuery();
    if (query.isEmpty() && entry.projectId.isNull()) {
        return false;
    }

    ResourceAPI::SearchArgs searchArgs;
    searchArgs.type = ModPlatform::ResourceType::Mod;
    searchArgs.search = query;
    if (!targetMcVer.isEmpty()) {
        searchArgs.versions = std::vector<Version>{ Version(targetMcVer) };
    }
    if (targetLoaders != ModPlatform::ModLoaderType::None) {
        searchArgs.loaders = targetLoaders;
    }

    QList<ModPlatform::IndexedPack::Ptr> foundPacks;
    {
        QEventLoop loop;
        ResourceAPI::Callback<QList<ModPlatform::IndexedPack::Ptr>> cb;
        cb.onSucceed = [&foundPacks](QList<ModPlatform::IndexedPack::Ptr>& packs) { foundPacks = packs; };
        cb.onFail = [](const QString&, int) {};
        cb.onAbort = []() {};

        auto task = api->searchProjects(searchArgs, cb);
        if (!task) {
            return false;
        }
        connect(task.get(), &Task::finished, &loop, &QEventLoop::quit);
        task->start();
        loop.exec();
    }

    if (foundPacks.isEmpty()) {
        return false;
    }

    // Score candidate packs to find the best match
    const QString wantSlug = ImportFromModList::normalizeKey(entry.slug);
    const QString wantName = ImportFromModList::normalizeKey(entry.name);
    const QString wantModId = ImportFromModList::normalizeKey(entry.modId);
    const QString wantQuery = ImportFromModList::normalizeKey(query);

    ModPlatform::IndexedPack::Ptr bestPack = nullptr;
    int bestScore = -1;

    for (const auto& pack : foundPacks) {
        if (!pack) {
            continue;
        }
        int score = 0;
        const QString packSlug = ImportFromModList::normalizeKey(pack->slug);
        const QString packName = ImportFromModList::normalizeKey(pack->name);

        if (entry.provider == provider && !entry.projectId.isNull() &&
            pack->addonId.toString().compare(entry.projectId.toString(), Qt::CaseInsensitive) == 0) {
            score += 1000;
        }
        if (!wantSlug.isEmpty() && packSlug == wantSlug) {
            score += 500;
        }
        if (!wantName.isEmpty() && packName == wantName) {
            score += 400;
        }
        if (!wantModId.isEmpty() && (packSlug == wantModId || packName == wantModId)) {
            score += 350;
        }
        if (!wantQuery.isEmpty() && (packSlug == wantQuery || packName == wantQuery)) {
            score += 300;
        } else if (!wantQuery.isEmpty() && (packSlug.startsWith(wantQuery) || packName.startsWith(wantQuery))) {
            score += 120;
        }

        if (score > bestScore) {
            bestScore = score;
            bestPack = pack;
        }
    }

    if (!bestPack) {
        bestPack = foundPacks.first();
    }

    // Fetch compatible versions for bestPack
    ResourceAPI::VersionSearchArgs verArgs;
    verArgs.pack = bestPack;
    verArgs.resourceType = ModPlatform::ResourceType::Mod;
    if (!targetMcVer.isEmpty()) {
        verArgs.mcVersions = std::vector<Version>{ Version(targetMcVer) };
    }
    if (targetLoaders != ModPlatform::ModLoaderType::None) {
        verArgs.loaders = targetLoaders;
    }

    QVector<ModPlatform::IndexedVersion> versions;
    {
        QEventLoop loop;
        ResourceAPI::Callback<QVector<ModPlatform::IndexedVersion>> cb;
        cb.onSucceed = [&versions](QVector<ModPlatform::IndexedVersion>& vers) { versions = vers; };
        cb.onFail = [](const QString&, int) {};
        cb.onAbort = []() {};

        auto task = api->getProjectVersions(verArgs, cb);
        if (!task) {
            return false;
        }
        connect(task.get(), &Task::finished, &loop, &QEventLoop::quit);
        task->start();
        loop.exec();
    }

    if (versions.isEmpty()) {
        return false;
    }

    // Prefer exact fileId/version match if compatible, otherwise latest compatible release
    ModPlatform::IndexedVersion chosenVer = versions.first();
    for (const auto& v : versions) {
        if (v.downloadUrl.isEmpty()) {
            continue;
        }
        if (!entry.fileId.isNull() && v.fileId.toString() == entry.fileId.toString()) {
            chosenVer = v;
            break;
        }
        if (!entry.version.isEmpty() && (v.version == entry.version || v.versionNumber == entry.version)) {
            chosenVer = v;
            break;
        }
    }

    if (chosenVer.downloadUrl.isEmpty()) {
        return false;
    }

    chosenVer.addonId = bestPack->addonId;
    entry.resolvedPack = bestPack;
    entry.resolvedVersion = chosenVer;
    entry.status = ImportFromModList::EntryStatus::ReadyAdapted;
    entry.statusText = tr("Ready (%1)").arg(ModPlatform::ProviderCapabilities::readableName(provider));
    entry.selected = true;
    return true;
}

void ImportModListDialog::refreshTable()
{
    m_updatingTable = true;
    m_treeWidget->clear();

    for (int i = 0; i < m_entries.size(); ++i) {
        const auto& entry = m_entries[i];
        auto* item = new QTreeWidgetItem(m_treeWidget);
        item->setData(0, Qt::UserRole, i);
        item->setFlags(item->flags() | Qt::ItemIsUserCheckable);
        item->setCheckState(0, entry.selected ? Qt::Checked : Qt::Unchecked);

        QString title = entry.displayName();
        if (!entry.enabled) {
            title += tr(" [Disabled]");
        }
        item->setText(0, title);
        item->setText(1, entry.statusText.isEmpty() ? tr("Pending") : entry.statusText);

        QString sourceStr = "-";
        if (entry.resolvedPack) {
            sourceStr = ModPlatform::ProviderCapabilities::readableName(entry.resolvedPack->provider);
        } else if (entry.status == ImportFromModList::EntryStatus::ReadyDirectUrl) {
            sourceStr = tr("Direct URL");
        } else if (entry.provider.has_value()) {
            sourceStr = ModPlatform::ProviderCapabilities::readableName(*entry.provider);
        }
        item->setText(2, sourceStr);

        QString verFileStr = "-";
        if (!entry.resolvedVersion.fileName.isEmpty()) {
            verFileStr = QString("%1 (%2)").arg(
                !entry.resolvedVersion.version.isEmpty() ? entry.resolvedVersion.version : entry.resolvedVersion.versionNumber,
                entry.resolvedVersion.fileName);
        } else if (!entry.version.isEmpty() || !entry.filename.isEmpty()) {
            verFileStr = QString("%1 %2").arg(entry.version, entry.filename).trimmed();
        }
        item->setText(3, verFileStr);
    }

    m_updatingTable = false;
    updateSummaryLabel();
}

void ImportModListDialog::onTreeItemChanged(QTreeWidgetItem* item, int column)
{
    if (m_updatingTable || column != 0 || !item) {
        return;
    }
    const int idx = item->data(0, Qt::UserRole).toInt();
    if (idx >= 0 && idx < m_entries.size()) {
        m_entries[idx].selected = (item->checkState(0) == Qt::Checked);
        updateSummaryLabel();
    }
}

void ImportModListDialog::updateSummaryLabel()
{
    int readyCount = 0;
    int selectedCount = 0;
    int installedCount = 0;
    int unresolvedCount = 0;

    for (const auto& e : m_entries) {
        if (e.status == ImportFromModList::EntryStatus::ReadyExact || e.status == ImportFromModList::EntryStatus::ReadyAdapted ||
            e.status == ImportFromModList::EntryStatus::ReadyDirectUrl) {
            ++readyCount;
            if (e.selected) {
                ++selectedCount;
            }
        } else if (e.status == ImportFromModList::EntryStatus::AlreadyInstalled) {
            ++installedCount;
        } else if (e.status == ImportFromModList::EntryStatus::NotFound || e.status == ImportFromModList::EntryStatus::Error) {
            ++unresolvedCount;
        }
    }

    m_summaryLabel->setText(tr("Total: %1 | Selected to Download: %2 (of %3 Ready) | Already Installed: %4 | Unresolved: %5")
                                .arg(m_entries.size())
                                .arg(selectedCount)
                                .arg(readyCount)
                                .arg(installedCount)
                                .arg(unresolvedCount));
    m_copyUnresolvedButton->setEnabled(unresolvedCount > 0);
    m_downloadButton->setEnabled(!m_isResolving && selectedCount > 0);
}

void ImportModListDialog::copyUnresolvedToClipboard()
{
    QStringList lines;
    for (const auto& e : m_entries) {
        if (e.status == ImportFromModList::EntryStatus::NotFound || e.status == ImportFromModList::EntryStatus::Error) {
            if (!e.homepageUrl.isEmpty()) {
                lines << QString("%1 (%2)").arg(e.displayName(), e.homepageUrl);
            } else {
                lines << e.displayName();
            }
        }
    }
    if (!lines.isEmpty() && QApplication::clipboard()) {
        QApplication::clipboard()->setText(lines.join("\n"));
        CustomMessageBox::selectable(this, tr("Copied Unresolved Mods"),
                                     tr("Copied %1 unresolved mod(s) to the clipboard.").arg(lines.size()), QMessageBox::Information)
            ->exec();
    }
}

void ImportModListDialog::downloadSelectedMods()
{
    QList<ImportFromModList::ModEntry> toDownload;
    QList<std::shared_ptr<GetModDependenciesTask::PackDependency>> depCandidates;

    for (const auto& e : m_entries) {
        if (!e.selected) {
            continue;
        }
        if (e.status == ImportFromModList::EntryStatus::ReadyExact || e.status == ImportFromModList::EntryStatus::ReadyAdapted ||
            e.status == ImportFromModList::EntryStatus::ReadyDirectUrl) {
            toDownload.append(e);
            if (e.resolvedPack) {
                depCandidates.append(std::make_shared<GetModDependenciesTask::PackDependency>(e.resolvedPack, e.resolvedVersion));
            }
        }
    }

    if (toDownload.isEmpty()) {
        return;
    }

    // Resolve required dependencies if enabled
    QList<std::shared_ptr<GetModDependenciesTask::PackDependency>> resolvedDeps;
    if (m_resolveDepsCheck->isChecked() && !depCandidates.isEmpty() && m_instance && m_model) {
        GetModDependenciesTask depTask(m_instance, m_model, depCandidates);
        ProgressDialog depDialog(this);
        depDialog.setSkipButton(true, tr("Skip Dependencies"));
        if (depDialog.execWithTask(&depTask) == QDialog::Accepted) {
            resolvedDeps = depTask.getDependecies();
        }
    }

    ConcurrentTask downloadTasks(tr("Download Imported Mods"), APPLICATION->settings()->get("NumberOfConcurrentDownloads").toInt());
    QStringList disableAfterDownload;

    for (const auto& e : toDownload) {
        if (e.resolvedPack) {
            downloadTasks.addTask(makeShared<ResourceDownloadTask>(e.resolvedPack, e.resolvedVersion, m_model, true, "import_mod_list"));
        } else if (!e.resolvedVersion.downloadUrl.isEmpty()) {
            auto netJob = makeShared<NetJob>(tr("Download %1").arg(e.displayName()), APPLICATION->network());
            const QString targetPath = m_model->dir().absoluteFilePath(e.resolvedVersion.fileName);
            netJob->addNetAction(Net::ApiRequest::makeFile(e.resolvedVersion.downloadUrl, targetPath));
            downloadTasks.addTask(netJob);
        }

        if (m_preserveEnabledCheck->isChecked() && !e.enabled && !e.resolvedVersion.fileName.isEmpty()) {
            disableAfterDownload.append(e.resolvedVersion.fileName);
        }
    }

    QSet<QString> alreadyQueuedIds;
    for (const auto& e : toDownload) {
        if (e.resolvedPack && !e.resolvedPack->addonId.isNull()) {
            alreadyQueuedIds.insert(e.resolvedPack->addonId.toString());
        }
    }

    for (const auto& dep : resolvedDeps) {
        if (dep && dep->pack && !dep->version.downloadUrl.isEmpty()) {
            const QString idStr = dep->pack->addonId.toString();
            if (!alreadyQueuedIds.contains(idStr)) {
                alreadyQueuedIds.insert(idStr);
                downloadTasks.addTask(makeShared<ResourceDownloadTask>(dep->pack, dep->version, m_model, true, "dependency"));
            }
        }
    }

    ProgressDialog loadDialog(this);
    loadDialog.setSkipButton(true, tr("Abort"));
    loadDialog.execWithTask(&downloadTasks);

    // Apply .disabled state for mods that were exported as disabled
    for (const QString& fname : disableAfterDownload) {
        if (fname.endsWith(".disabled", Qt::CaseInsensitive)) {
            continue;
        }
        const QString fullPath = m_model->dir().absoluteFilePath(fname);
        if (QFileInfo::exists(fullPath)) {
            FS::move(fullPath, fullPath + ".disabled");
        }
    }

    m_model->update();
    accept();
}
