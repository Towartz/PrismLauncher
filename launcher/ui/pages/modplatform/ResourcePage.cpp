// SPDX-FileCopyrightText: 2023 flowln <flowlnlnln@gmail.com>
//
// SPDX-License-Identifier: GPL-3.0-only AND Apache-2.0
/*
 *  Prism Launcher - Minecraft Launcher
 *  Copyright (C) 2022 Sefa Eyeoglu <contact@scrumplex.net>
 *  Copyright (C) 2023 TheKodeToad <TheKodeToad@proton.me>
 *  Copyright (c) 2023 Trial97 <alexandru.tripon97@gmail.com>
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
 *
 * This file incorporates work covered by the following copyright and
 * permission notice:
 *
 *      Copyright 2013-2021 MultiMC Contributors
 *
 *      Licensed under the Apache License, Version 2.0 (the "License");
 *      you may not use this file except in compliance with the License.
 *      You may obtain a copy of the License at
 *
 *          http://www.apache.org/licenses/LICENSE-2.0
 *
 *      Unless required by applicable law or agreed to in writing, software
 *      distributed under the License is distributed on an "AS IS" BASIS,
 *      WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
 *      See the License for the specific language governing permissions and
 *      limitations under the License.
 */

#include "ResourcePage.h"
#include "modplatform/ModIndex.h"
#include "ui_ResourcePage.h"

#include <StringUtils.h>
#include <QDesktopServices>
#include <QKeyEvent>
#include <QMessageBox>
#include <algorithm>
#include <utility>

#include "Markdown.h"

#include "Application.h"
#include "Json.h"
#include "ui/dialogs/ResourceDownloadDialog.h"
#include "ui/pages/modplatform/ResourceModel.h"
#include "ui/qml/QmlThemeBridge.h"
#include "ui/widgets/ProjectItem.h"

#include <QQmlContext>
#include <QQmlEngine>
#include <QQuickItem>
#include <QQuickWidget>
#include <QStackedWidget>
#include <QToolButton>

namespace ResourceDownload {

namespace {
QString versionText(const ModPlatform::IndexedVersion& version, const QVariant& installedVersion)
{
    auto text = version.version;
    if (version.versionType.isValid()) {
        text += QString(" [%1]").arg(version.versionType.toString());
    }
    if (version.fileId == installedVersion) {
        text += ResourcePage::tr(" [installed]", "Mod version select");
    }
    if (version.isCurrentlySelected) {
        text += ResourcePage::tr(" [selected]", "Mod version select");
    }
    return text;
}
}  // namespace

ResourcePage::ResourcePage(ResourceDownloadDialog* parent,
                           BaseInstance& baseInstance,
                           ResourceDescriptor desc,
                           ResourceProviderData provider)
    : QWidget(parent)
    , m_baseInstance(baseInstance)
    , m_ui(new Ui::ResourcePage)
    , m_parentDialog(parent)
    , m_fetchProgress(this, false)
    , m_desc(std::move(desc))
    , m_provider(std::move(provider))
{
    m_ui->setupUi(this);

    m_ui->resourceFilterButton->setSizePolicy(QSizePolicy::Fixed, QSizePolicy::Fixed);
    m_ui->horizontalLayout->setStretchFactor(m_ui->resourceFilterButton, 0);
    m_ui->horizontalLayout->setStretchFactor(m_ui->searchEdit, 1);

    if (!supportsFiltering()) {
        m_ui->resourceFilterButton->hide();
        m_ui->resourceFilterButton->setEnabled(false);
        m_ui->filterWidget->hide();
    } else {
        m_ui->resourceFilterButton->show();
        m_ui->resourceFilterButton->setEnabled(true);
        m_ui->resourceFilterButton->setText(tr("Filter options"));
    }

    m_viewStack = new QStackedWidget(m_ui->splitter);
    int packViewIndex = m_ui->splitter->indexOf(m_ui->packView);
    m_ui->splitter->insertWidget(packViewIndex, m_viewStack);
    m_viewStack->addWidget(m_ui->packView);

    m_ui->splitter->setStretchFactor(0, 0);
    m_ui->splitter->setStretchFactor(packViewIndex, 4);
    m_ui->splitter->setStretchFactor(2, 5);

    m_viewModeButton = new QToolButton(this);
    m_viewModeButton->setSizePolicy(QSizePolicy::Fixed, QSizePolicy::Fixed);
    m_viewModeButton->setToolTip(tr("Switch between Grid, Compact List, and Classic view"));
    m_viewModeButton->setFocusPolicy(Qt::NoFocus);
    m_ui->horizontalLayout->addWidget(m_viewModeButton);
    m_ui->horizontalLayout->setStretchFactor(m_viewModeButton, 0);
    connect(m_viewModeButton, &QToolButton::clicked, this, &ResourcePage::cycleViewMode);
    updateViewModeButton();

    m_ui->versionSelectionBox->view()->setVerticalScrollBarPolicy(Qt::ScrollBarAsNeeded);
    m_ui->versionSelectionBox->view()->setVerticalScrollMode(QAbstractItemView::ScrollPerPixel);

    m_searchTimer.setTimerType(Qt::TimerType::CoarseTimer);
    m_searchTimer.setSingleShot(true);

    connect(&m_searchTimer, &QTimer::timeout, this, &ResourcePage::triggerSearch);

    connect(m_ui->searchEdit, &QLineEdit::textEdited, this, [this] {
        if (m_searchTimer.isActive()) {
            m_searchTimer.stop();
        }
        m_searchTimer.start(350);
    });

    // hide progress bar to prevent weird artifact
    m_fetchProgress.hide();
    m_fetchProgress.hideIfInactive(true);
    m_fetchProgress.setFixedHeight(24);
    m_fetchProgress.progressFormat("");

    m_ui->verticalLayout->insertWidget(1, &m_fetchProgress);

    auto* delegate = new ProjectItemDelegate(this);
    m_ui->packView->setItemDelegate(delegate);
    m_ui->packView->installEventFilter(this);
    m_ui->packView->viewport()->installEventFilter(this);

    connect(m_ui->packDescription, &QTextBrowser::anchorClicked, this, &ResourcePage::openUrl);

    connect(m_ui->packView, &QAbstractItemView::doubleClicked, this, &ResourcePage::onResourceToggle);
    connect(delegate, &ProjectItemDelegate::checkboxClicked, this, &ResourcePage::onResourceToggle);
}

ResourcePage::~ResourcePage()
{
    delete m_ui;
    delete m_model;
}

void ResourcePage::retranslate()
{
    m_ui->retranslateUi(this);
    updateViewModeButton();
}

void ResourcePage::openedImpl()
{
    if (!m_projectMode) {
        initQuickWidget();
    } else {
        if (m_viewStack) {
            m_viewStack->hide();
        }
        if (m_viewModeButton) {
            m_viewModeButton->hide();
        }
    }

    m_ui->horizontalLayout->setStretchFactor(m_ui->resourceFilterButton, 0);
    m_ui->horizontalLayout->setStretchFactor(m_ui->searchEdit, 1);
    if (m_viewModeButton) {
        m_ui->horizontalLayout->setStretchFactor(m_viewModeButton, 0);
    }

    if (!supportsFiltering()) {
        m_ui->resourceFilterButton->hide();
        m_ui->resourceFilterButton->setEnabled(false);
        m_ui->filterWidget->hide();
    } else {
        m_ui->resourceFilterButton->show();
        m_ui->resourceFilterButton->setEnabled(true);
        m_ui->resourceFilterButton->setText(tr("Filter options"));
    }

    //: String in the search bar of the mod downloading dialog
    m_ui->searchEdit->setPlaceholderText(tr("Search for %1...").arg(resourcesString()));
    m_ui->resourceSelectionButton->setText(tr("Select %1 for download").arg(resourceString()));

    if (m_model) {
        connect(m_model, &QAbstractItemModel::rowsInserted, this, &ResourcePage::onRowsInserted, Qt::UniqueConnection);
    }

    auto currentPack = getCurrentPack();
    bool hasSelectedPack = currentPack && currentPack->versionsLoaded;

    if (m_ui->packView->currentIndex().isValid()) {
        onSelectionChanged(m_ui->packView->currentIndex(), {});
    } else if (m_model && m_model->rowCount({}) > 0) {
        selectFirstRow();
    } else {
        updateUi(QModelIndex());
    }

    if (m_quickWidget && m_quickWidget->rootObject()) {
        int row = m_ui->packView->currentIndex().isValid() ? m_ui->packView->currentIndex().row() : 0;
        QMetaObject::invokeMethod(m_quickWidget->rootObject(), "selectRow", Q_ARG(QVariant, row));
    }

    if (!m_suppressInitialSearch && !hasSelectedPack) {
        triggerSearch();
    } else {
        m_suppressInitialSearch = false;
    }
    updateSelectionButton();
    m_ui->searchEdit->setCursorPosition(0);
    m_ui->searchEdit->setFocus();
}

void ResourcePage::setSuppressInitialSearch(bool suppress)
{
    m_suppressInitialSearch = suppress;
}

auto ResourcePage::eventFilter(QObject* watched, QEvent* event) -> bool
{
    if (event->type() == QEvent::KeyPress) {
        auto* keyEvent = static_cast<QKeyEvent*>(event);
        if (watched == m_ui->packView && keyEvent->key() == Qt::Key_Return) {  // stop the event from going to the confirm button
            onResourceToggle(m_ui->packView->currentIndex());
            keyEvent->accept();
            return true;
        }
    } else if (watched == m_ui->packView->viewport() && event->type() == QEvent::MouseButtonPress) {
        auto* mouseEvent = static_cast<QMouseEvent*>(event);

        if (mouseEvent->button() == Qt::MiddleButton) {
            onResourceToggle(m_ui->packView->indexAt(mouseEvent->pos()));
            return true;
        }
    }

    return QWidget::eventFilter(watched, event);
}

QString ResourcePage::getSearchTerm() const
{
    return m_ui->searchEdit->text();
}

void ResourcePage::setSearchTerm(const QString& term)
{
    m_ui->searchEdit->setText(term);
}

void ResourcePage::addSortings()
{
    Q_ASSERT(m_model);

    auto sorts = m_model->getSortingMethods();
    std::ranges::sort(sorts, [](const auto& l, const auto& r) { return l.index < r.index; });

    for (auto&& sorting : sorts) {
        m_ui->sortByBox->addItem(sorting.readableName, QVariant(sorting.index));
    }
}

bool ResourcePage::setCurrentPack(ModPlatform::IndexedPack::Ptr pack)
{
    QVariant v;
    v.setValue(pack);
    return m_model->setData(m_ui->packView->currentIndex(), v, Qt::UserRole);
}

ModPlatform::IndexedPack::Ptr ResourcePage::getCurrentPack() const
{
    return m_model->data(m_ui->packView->currentIndex(), Qt::UserRole).value<ModPlatform::IndexedPack::Ptr>();
}

void ResourcePage::updateUi(const QModelIndex& index)
{
    if (index.isValid() && index != m_ui->packView->currentIndex()) {
        return;
    }

    auto currentPack = getCurrentPack();
    if (!currentPack) {
        QString message = (m_model && m_model->hasActiveSearchJob())
                              ? tr("Searching for %1...").arg(resourcesString())
                              : tr("No %1 selected.").arg(resourceString());
        QString emptyHtml = QString(
            "<div style='margin-top: 60px; text-align: center; color: %1; font-size: 13px;'>"
            "%2</div>")
            .arg(palette().color(QPalette::PlaceholderText).name(), message);
        m_ui->packDescription->setHtml(emptyHtml);
        m_ui->packDescription->flush();
        m_ui->versionSelectionBox->clear();
        m_ui->resourceSelectionButton->setEnabled(false);
        return;
    }
    QString text = "";
    QString name = currentPack->name;

    if (currentPack->websiteUrl.isEmpty()) {
        text = name;
    } else {
        text = "<a href=\"" + currentPack->websiteUrl + "\">" + name + "</a>";
    }

    if (!currentPack->authors.empty()) {
        auto authorToStr = [](ModPlatform::ModpackAuthor& author) -> QString {
            if (author.url.isEmpty()) {
                return author.name;
            }
            return QString("<a href=\"%1\">%2</a>").arg(author.url, author.name);
        };
        QStringList authorStrs;
        for (auto& author : currentPack->authors) {
            authorStrs.push_back(authorToStr(author));
        }
        text += "<br>" + tr(" by ") + authorStrs.join(", ");
    }

    if (currentPack->extraDataLoaded) {
        if (currentPack->extraData.status == "archived") {
            text += "<br><br>" + tr("<b>This project has been archived. It will not receive any further updates unless the author decides "
                                    "to unarchive the project.</b>");
        }

        if (!currentPack->extraData.donate.isEmpty()) {
            text += "<br><br>" + tr("Donate information: ");
            auto donateToStr = [](ModPlatform::DonationData& donate) -> QString {
                return QString("<a href=\"%1\">%2</a>").arg(donate.url, donate.platform);
            };
            QStringList donates;
            for (auto& donate : currentPack->extraData.donate) {
                donates.append(donateToStr(donate));
            }
            text += donates.join(", ");
        }

        if (!currentPack->extraData.issuesUrl.isEmpty() || !currentPack->extraData.sourceUrl.isEmpty() ||
            !currentPack->extraData.wikiUrl.isEmpty() || !currentPack->extraData.discordUrl.isEmpty()) {
            text += "<br><br>" + tr("External links:") + "<br>";
        }

        if (!currentPack->extraData.issuesUrl.isEmpty()) {
            text += "- " + tr("Issues: <a href=%1>%1</a>").arg(currentPack->extraData.issuesUrl) + "<br>";
        }
        if (!currentPack->extraData.wikiUrl.isEmpty()) {
            text += "- " + tr("Wiki: <a href=%1>%1</a>").arg(currentPack->extraData.wikiUrl) + "<br>";
        }
        if (!currentPack->extraData.sourceUrl.isEmpty()) {
            text += "- " + tr("Source code: <a href=%1>%1</a>").arg(currentPack->extraData.sourceUrl) + "<br>";
        }
        if (!currentPack->extraData.discordUrl.isEmpty()) {
            text += "- " + tr("Discord: <a href=%1>%1</a>").arg(currentPack->extraData.discordUrl) + "<br>";
        }
    }

    text += "<hr>";

    m_ui->packDescription->setHtml(StringUtils::htmlListPatch(
        text + (currentPack->extraData.body.isEmpty() ? currentPack->description : markdownToHTML(currentPack->extraData.body))));
    m_ui->packDescription->flush();

    refreshVersionComboBox();
    if (currentPack) {
        restoreSelectedVersion(currentPack);
    }
}

void ResourcePage::updateSelectionButton()
{
    if (!isOpened) {
        m_ui->resourceSelectionButton->setEnabled(false);
        return;
    }

    auto currentPack = getCurrentPack();
    if (!currentPack) {
        m_ui->resourceSelectionButton->setEnabled(false);
        m_ui->resourceSelectionButton->setText(tr("Select a %1").arg(resourceString()));
        return;
    }

    if (!currentPack->versionsLoaded) {
        m_ui->resourceSelectionButton->setEnabled(false);
        m_ui->resourceSelectionButton->setText(tr("Loading versions..."));
        return;
    }

    if (currentPack->versions.empty() || m_selectedVersionIndex < 0) {
        m_ui->resourceSelectionButton->setEnabled(false);
        m_ui->resourceSelectionButton->setText(tr("No compatible version"));
        return;
    }

    m_ui->resourceSelectionButton->setEnabled(true);
    if (!currentPack->isVersionSelected(m_selectedVersionIndex)) {
        m_ui->resourceSelectionButton->setText(tr("Select %1 for download").arg(resourceString()));
    } else {
        m_ui->resourceSelectionButton->setText(tr("Deselect %1 for download").arg(resourceString()));
    }
}

void ResourcePage::refreshVersionComboBox()
{
    if (!isOpened) {
        return;
    }

    auto currentPack = getCurrentPack();
    if (!currentPack || !currentPack->versionsLoaded) {
        return;
    }

    auto installedVersion = m_model->getInstalledPackVersion(currentPack);

    for (int i = 0; i < m_ui->versionSelectionBox->count(); i++) {
        int versionIndex = m_ui->versionSelectionBox->itemData(i).toInt();
        if (versionIndex < 0 || versionIndex >= currentPack->versions.size()) {
            continue;
        }

        auto& version = currentPack->versions[versionIndex];

        m_ui->versionSelectionBox->setItemText(i, versionText(version, installedVersion));
    }
}

void ResourcePage::restoreSelectedVersion(const ModPlatform::IndexedPack::Ptr& currentPack)
{
    int selectedVersionIndex = -1;
    for (int i = 0; i < currentPack->versions.size(); i++) {
        if (currentPack->versions[i].isCurrentlySelected) {
            selectedVersionIndex = i;
            break;
        }
    }

    if (selectedVersionIndex >= 0) {
        for (int i = 0; i < m_ui->versionSelectionBox->count(); i++) {
            if (m_ui->versionSelectionBox->itemData(i).toInt() == selectedVersionIndex) {
                m_ui->versionSelectionBox->blockSignals(true);
                m_ui->versionSelectionBox->setCurrentIndex(i);
                m_selectedVersionIndex = selectedVersionIndex;
                m_ui->versionSelectionBox->blockSignals(false);
                break;
            }
        }
    }
}

void ResourcePage::versionListUpdated(const QModelIndex& index)
{
    if (index == m_ui->packView->currentIndex()) {
        auto currentPack = getCurrentPack();

        m_ui->versionSelectionBox->blockSignals(true);
        m_ui->versionSelectionBox->clear();
        m_ui->versionSelectionBox->blockSignals(false);

        if (currentPack) {
            bool versionChosen = false;
            const auto releaseTypesSetting = APPLICATION->settings()->get("ModUpdateReleaseTypes");
            const auto releaseTypes = ModPlatform::IndexedVersionType::fromStringList(Json::toStringList(releaseTypesSetting.toString()));

            auto installedVersion = m_model->getInstalledPackVersion(currentPack);

            for (int i = 0; i < currentPack->versions.size(); i++) {
                auto& version = currentPack->versions[i];
                if (!m_model->checkVersionFilters(version)) {
                    continue;
                }

                m_ui->versionSelectionBox->addItem(versionText(version, installedVersion), QVariant(i));

                if (versionChosen) {
                    continue;
                }

                const bool preferred =
                    releaseTypes.empty() || std::ranges::any_of(releaseTypes, [&version](ModPlatform::IndexedVersionType type) {
                        return version.versionType == type;
                    });
                if (!preferred) {
                    continue;
                }

                versionChosen = true;
                m_ui->versionSelectionBox->setCurrentIndex(m_ui->versionSelectionBox->count() - 1);
            }

            restoreSelectedVersion(currentPack);
        }
        if (m_ui->versionSelectionBox->count() == 0) {
            m_ui->versionSelectionBox->addItem(tr("No valid version found."), QVariant(-1));
            m_ui->resourceSelectionButton->setText(tr("Cannot select invalid version :("));
        }

        m_selectedVersionIndex = m_ui->versionSelectionBox->currentData().toInt();

        if (m_enableQueue.contains(index.row())) {
            m_enableQueue.remove(index.row());
            onResourceToggle(index);
        } else {
            updateSelectionButton();
        }
    } else if (m_enableQueue.contains(index.row())) {
        m_enableQueue.remove(index.row());
        onResourceToggle(index);
    }
}

void ResourcePage::onSelectionChanged(QModelIndex curr, [[maybe_unused]] QModelIndex prev)
{
    if (!curr.isValid()) {
        return;
    }

    auto currentPack = getCurrentPack();

    bool requestLoad = false;
    if (!currentPack || !currentPack->versionsLoaded) {
        m_ui->resourceSelectionButton->setText(tr("Loading versions..."));
        m_ui->resourceSelectionButton->setEnabled(false);

        requestLoad = true;
    } else {
        versionListUpdated(curr);
    }

    if (currentPack && !currentPack->extraDataLoaded) {
        requestLoad = true;
    }

    // we are already requesting this
    if (m_enableQueue.contains(curr.row())) {
        requestLoad = false;
    }

    if (requestLoad) {
        m_model->loadEntry(curr);
    }

    updateUi(curr);

    if (m_quickWidget && m_quickWidget->rootObject()) {
        QMetaObject::invokeMethod(m_quickWidget->rootObject(), "selectRow", Q_ARG(QVariant, curr.row()));
    }
}

void ResourcePage::onVersionSelectionChanged(int index)
{
    m_selectedVersionIndex = m_ui->versionSelectionBox->itemData(index).toInt();

    if (auto currentPack = getCurrentPack(); currentPack && currentPack->isAnyVersionSelected()) {
        if (m_selectedVersionIndex >= 0 && m_selectedVersionIndex < currentPack->versions.size()) {
            auto& newVersion = currentPack->versions[m_selectedVersionIndex];
            removeResourceFromDialog(currentPack->name);
            addResourceToDialog(currentPack, newVersion);
        }
    }

    updateSelectionButton();
    refreshVersionComboBox();
}

void ResourcePage::addResourceToDialog(ModPlatform::IndexedPack::Ptr pack, ModPlatform::IndexedVersion& version)
{
    m_parentDialog->addResource(pack, version);
}

void ResourcePage::removeResourceFromDialog(const QString& packName)
{
    m_parentDialog->removeResource(packName);
}

void ResourcePage::addResourceToPage(ModPlatform::IndexedPack::Ptr pack,
                                     ModPlatform::IndexedVersion& ver,
                                     ResourceFolderModel* baseModel,
                                     QString downloadReason,
                                     QString dependentOn)
{
    bool isIndexed = m_desc.isIndexed && !APPLICATION->settings()->get("ModMetadataDisabled").toBool();
    m_model->addPack(std::move(pack), ver, baseModel, isIndexed, std::move(downloadReason), std::move(dependentOn));
}

void ResourcePage::modelReset()
{
    m_enableQueue.clear();
}

void ResourcePage::removeResourceFromPage(const QString& name)
{
    m_model->removePack(name);
}

void ResourcePage::onResourceSelected()
{
    if (m_selectedVersionIndex < 0) {
        return;
    }

    auto currentPack = getCurrentPack();
    if (!currentPack || !currentPack->versionsLoaded || currentPack->versions.size() <= m_selectedVersionIndex) {
        return;
    }

    auto& version = currentPack->versions[m_selectedVersionIndex];
    Q_ASSERT(!version.downloadUrl.isNull());
    if (version.isCurrentlySelected) {
        removeResourceFromDialog(currentPack->name);
    } else {
        addResourceToDialog(currentPack, version);
    }

    // Save the modified pack (and prevent warning in release build)
    [[maybe_unused]] bool set = setCurrentPack(currentPack);
    Q_ASSERT(set);

    updateSelectionButton();
    refreshVersionComboBox();

    /* Force redraw on the resource list when the selection changes */
    m_ui->packView->repaint();
}

void ResourcePage::onResourceToggle(const QModelIndex& index)
{
    if (!index.isValid()) {
        return;
    }
    const bool isSelected = index == m_ui->packView->currentIndex();
    auto pack = m_model->data(index, Qt::UserRole).value<ModPlatform::IndexedPack::Ptr>();

    if (pack->versionsLoaded) {
        if (pack->isAnyVersionSelected()) {
            removeResourceFromDialog(pack->name);
        } else {
            auto version = std::find_if(pack->versions.begin(), pack->versions.end(), [this](const ModPlatform::IndexedVersion& version) {
                return m_model->checkVersionFilters(version);
            });

            if (version == pack->versions.end()) {
                auto* errorMessage = new QMessageBox(
                    QMessageBox::Warning, tr("No versions available"),
                    tr("No versions for '%1' are available.\nThe author likely blocked third-party launchers.").arg(pack->name),
                    QMessageBox::Ok, this);

                errorMessage->open();
            } else {
                addResourceToDialog(pack, *version);
            }
        }

        if (isSelected) {
            updateSelectionButton();
            refreshVersionComboBox();
        }

        // force update
        QVariant variant;
        variant.setValue(pack);
        m_model->setData(index, variant, Qt::UserRole);
    } else {
        // the model is just 1 dimensional so this is fine
        m_enableQueue.insert(index.row());

        // we can't be sure that this hasn't already been requested...
        // but this does the job well enough and there's not much point preventing edgecases
        if (!isSelected) {
            m_model->loadEntry(index);
        }
    }
}

void ResourcePage::openUrl(QUrl url)
{
    if (url.scheme().isEmpty()) {
        QString query = url.query(QUrl::FullyDecoded);

        if (query.startsWith("remoteUrl=")) {
            // attempt to resolve url from warning page
            query.remove(0, 10);
            url = QUrl::fromPercentEncoding(query.toUtf8());  // double decoding is necessary
        }
    }
    // do not allow other url schemes for security reasons
    if (!(url.scheme() == "http" || url.scheme() == "https")) {
        qWarning() << "Unsupported scheme" << url.scheme();
        return;
    }

    // detect URLs and search instead

    const QString address = url.host() + url.path();
    QRegularExpressionMatch match;
    QString page;

    auto handlers = urlHandlers();
    for (auto it = handlers.constKeyValueBegin(); it != handlers.constKeyValueEnd(); it++) {
        auto&& [regex, candidate] = *it;
        if (match = QRegularExpression(regex).match(address); match.hasMatch()) {
            page = candidate;
            break;
        }
    }

    if (!page.isNull() && !m_doNotJumpToMod) {
        const QString slug = match.captured(1);

        // ensure the user isn't opening the same mod
        if (auto currentPack = getCurrentPack(); currentPack && slug != currentPack->slug) {
            m_parentDialog->selectPage(page);

            auto* newPage = m_parentDialog->selectedPage();

            QLineEdit* searchEdit = newPage->m_ui->searchEdit;
            auto* model = newPage->m_model;
            QListView* view = newPage->m_ui->packView;

            auto jump = [url, slug, model, view] {
                for (int row = 0; row < model->rowCount({}); row++) {
                    const QModelIndex index = model->index(row);
                    const auto pack = model->data(index, Qt::UserRole).value<ModPlatform::IndexedPack::Ptr>();

                    if (pack->slug == slug) {
                        view->setCurrentIndex(index);
                        return;
                    }
                }

                // The final fallback.
                QDesktopServices::openUrl(url);
            };

            searchEdit->setText(slug);
            newPage->triggerSearch();

            if (model->hasActiveSearchJob()) {
                connect(model->activeSearchJob().get(), &Task::finished, newPage, jump);
            } else {
                jump();
            }

            return;
        }
    }

    // open in the user's web browser
    QDesktopServices::openUrl(url);
}

void ResourcePage::openProject(const QVariant& projectID)
{
    m_projectMode = true;

    m_ui->sortByBox->hide();
    m_ui->searchEdit->hide();
    if (!supportsFiltering()) {
        m_ui->resourceFilterButton->hide();
    }
    m_ui->packView->hide();
    if (m_viewStack) {
        m_viewStack->hide();
    }
    if (m_viewModeButton) {
        m_viewModeButton->hide();
    }
    m_ui->resourceSelectionButton->hide();
    m_doNotJumpToMod = true;

    auto* buttonBox = new QDialogButtonBox(QDialogButtonBox::Ok | QDialogButtonBox::Cancel, this);

    auto* okBtn = buttonBox->button(QDialogButtonBox::Ok);
    okBtn->setDefault(true);
    okBtn->setAutoDefault(true);
    okBtn->setText(tr("Reinstall"));
    okBtn->setShortcut(tr("Ctrl+Return"));
    okBtn->setEnabled(false);

    auto* cancelBtn = buttonBox->button(QDialogButtonBox::Cancel);
    cancelBtn->setDefault(false);
    cancelBtn->setAutoDefault(false);
    cancelBtn->setText(tr("Cancel"));

    connect(okBtn, &QPushButton::clicked, this, [this] {
        onResourceSelected();
        m_parentDialog->accept();
    });

    connect(cancelBtn, &QPushButton::clicked, m_parentDialog, &ResourceDownloadDialog::reject);
    m_ui->gridLayout_4->addWidget(buttonBox, 0, 3);

    connect(m_ui->versionSelectionBox, &QComboBox::currentIndexChanged, this,
            [this, okBtn](int index) { okBtn->setEnabled(m_ui->versionSelectionBox->itemData(index).toInt() >= 0); });

    auto jump = [this] {
        if (m_model->rowCount({}) > 0) {
            m_ui->packView->setCurrentIndex(m_model->index(0));
            return;
        }
        m_ui->packDescription->setText(tr("The resource was not found"));
    };

    m_ui->searchEdit->setText("#" + projectID.toString());
    triggerSearch();

    if (m_model->hasActiveSearchJob()) {
        connect(m_model->activeSearchJob().get(), &Task::finished, this, jump);
    } else {
        jump();
    }
}

void ResourcePage::initQuickWidget()
{
    if (m_quickWidget || !m_model) {
        return;
    }

    if (!m_themeBridge) {
        m_themeBridge = new QmlThemeBridge(this);
    }

    m_quickWidget = new QQuickWidget(m_viewStack);
    m_quickWidget->hide();
    m_quickWidget->setSizePolicy(QSizePolicy::Expanding, QSizePolicy::Expanding);
    m_quickWidget->setMinimumWidth(260);
    m_quickWidget->setResizeMode(QQuickWidget::SizeRootObjectToView);
    m_quickWidget->setClearColor(palette().color(QPalette::Window));

    if (auto* engine = m_quickWidget->engine()) {
        engine->addImportPath(QCoreApplication::applicationDirPath() + "/qml");
        engine->addImportPath(QCoreApplication::applicationDirPath() + "/../qml");
        engine->addImportPath(QCoreApplication::applicationDirPath() + "/../Resources/qml");
    }

    m_quickWidget->rootContext()->setContextProperty("resourceModel", m_model);
    m_quickWidget->rootContext()->setContextProperty("theme", m_themeBridge);

    m_quickWidget->setSource(QUrl("qrc:/qml/ResourceBrowser.qml"));

    if (m_quickWidget->status() == QQuickWidget::Error || !m_quickWidget->rootObject()) {
        qWarning() << "ResourceBrowser QML failed to load:" << m_quickWidget->errors();
        m_quickWidget->hide();
        m_quickWidget->deleteLater();
        m_quickWidget = nullptr;
        m_viewStack->setCurrentWidget(m_ui->packView);
        m_currentViewMode = ViewMode::Classic;
        if (m_viewModeButton) {
            updateViewModeButton();
            m_viewModeButton->setEnabled(false);
            m_viewModeButton->setToolTip(tr("Modern QML view unavailable."));
        }
        return;
    }

    auto* rootObj = m_quickWidget->rootObject();
    if (rootObj) {
        connect(rootObj, SIGNAL(itemActivated(int)), this, SLOT(onQmlItemActivated(int)));
        connect(rootObj, SIGNAL(itemToggled(int)), this, SLOT(onQmlItemToggled(int)));
    }

    m_viewStack->insertWidget(0, m_quickWidget);

    QString savedMode = "Grid";
    if (APPLICATION_DYN && APPLICATION->settings()) {
        savedMode = APPLICATION->settings()->get("ResourceBrowserViewMode").toString();
    }
    if (savedMode == "List") {
        m_currentViewMode = ViewMode::List;
        m_viewStack->setCurrentWidget(m_quickWidget);
        if (rootObj) {
            rootObj->setProperty("isGridMode", false);
        }
    } else if (savedMode == "Classic") {
        m_currentViewMode = ViewMode::Classic;
        m_viewStack->setCurrentWidget(m_ui->packView);
    } else {
        m_currentViewMode = ViewMode::Grid;
        m_viewStack->setCurrentWidget(m_quickWidget);
        if (rootObj) {
            rootObj->setProperty("isGridMode", true);
        }
    }
    updateViewModeButton();
}

void ResourcePage::onRowsInserted([[maybe_unused]] const QModelIndex& parent, [[maybe_unused]] int first, [[maybe_unused]] int last)
{
    if (!m_ui->packView->currentIndex().isValid() && m_model && m_model->rowCount({}) > 0) {
        selectFirstRow();
    }
}

void ResourcePage::selectFirstRow()
{
    if (!m_model || m_model->rowCount({}) == 0) {
        return;
    }
    QModelIndex firstIndex = m_model->index(0, 0);
    m_ui->packView->setCurrentIndex(firstIndex);
    if (m_ui->packView->selectionModel()) {
        m_ui->packView->selectionModel()->select(firstIndex, QItemSelectionModel::ClearAndSelect | QItemSelectionModel::Rows);
    }
    onSelectionChanged(firstIndex, {});
}

void ResourcePage::onQmlItemActivated(int row)
{
    if (!m_model || row < 0 || row >= m_model->rowCount({})) {
        return;
    }
    QModelIndex index = m_model->index(row, 0);
    m_ui->packView->setCurrentIndex(index);
    if (m_ui->packView->selectionModel()) {
        m_ui->packView->selectionModel()->select(index, QItemSelectionModel::ClearAndSelect | QItemSelectionModel::Rows);
    }
    onSelectionChanged(index, {});
}

void ResourcePage::onQmlItemToggled(int row)
{
    if (!m_model || row < 0 || row >= m_model->rowCount({})) {
        return;
    }
    QModelIndex index = m_model->index(row, 0);
    onResourceToggle(index);
}

void ResourcePage::updateViewModeButton()
{
    if (!m_viewModeButton) {
        return;
    }
    switch (m_currentViewMode) {
        case ViewMode::Grid:
            m_viewModeButton->setText(tr("View: Grid"));
            m_viewModeButton->setToolTip(tr("Current view: Grid. Click to switch to Compact List."));
            break;
        case ViewMode::List:
            m_viewModeButton->setText(tr("View: List"));
            m_viewModeButton->setToolTip(tr("Current view: Compact List. Click to switch to Classic View."));
            break;
        case ViewMode::Classic:
            m_viewModeButton->setText(tr("View: Classic"));
            m_viewModeButton->setToolTip(tr("Current view: Classic. Click to switch to Modern Grid."));
            break;
    }
}

void ResourcePage::cycleViewMode()
{
    if (!m_quickWidget || m_quickWidget->status() != QQuickWidget::Ready) {
        m_viewStack->setCurrentWidget(m_ui->packView);
        m_currentViewMode = ViewMode::Classic;
        updateViewModeButton();
        return;
    }

    auto* rootObj = m_quickWidget->rootObject();
    if (!rootObj) {
        return;
    }

    switch (m_currentViewMode) {
        case ViewMode::Grid:
            m_currentViewMode = ViewMode::List;
            m_viewStack->setCurrentWidget(m_quickWidget);
            rootObj->setProperty("isGridMode", false);
            break;
        case ViewMode::List:
            m_currentViewMode = ViewMode::Classic;
            m_viewStack->setCurrentWidget(m_ui->packView);
            break;
        case ViewMode::Classic:
            m_currentViewMode = ViewMode::Grid;
            m_viewStack->setCurrentWidget(m_quickWidget);
            rootObj->setProperty("isGridMode", true);
            break;
    }

    updateViewModeButton();

    if (APPLICATION_DYN && APPLICATION->settings()) {
        QString modeStr = (m_currentViewMode == ViewMode::Grid) ? "Grid" : ((m_currentViewMode == ViewMode::List) ? "List" : "Classic");
        APPLICATION->settings()->set("ResourceBrowserViewMode", modeStr);
    }
}

void ResourcePage::reloadCurrentVersions()
{
    auto index = m_ui->packView->currentIndex();
    auto pack = getCurrentPack();
    if (!index.isValid() || !pack) {
        return;
    }

    pack->versionsLoaded = false;
    pack->versions.clear();
    m_model->loadEntry(index);
}
}  // namespace ResourceDownload
