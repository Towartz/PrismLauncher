// SPDX-FileCopyrightText: 2023 flowln <flowlnlnln@gmail.com>
//
// SPDX-License-Identifier: GPL-3.0-only

#pragma once

#include <QAbstractListModel>
#include <QString>
#include <QVariant>
#include <memory>
#include <utility>

#include "BaseInstance.h"

#include "modplatform/ModIndex.h"
#include "modplatform/ResourceAPI.h"

#include "ui/pages/modplatform/ResourceModel.h"
#include "ui/widgets/ModFilterWidget.h"

#include "Version.h"

namespace ResourceDownload {

class ModPage;

class ModModel : public ResourceModel {
    Q_OBJECT

   public:
    ModModel(BaseInstance&, ResourceFolderModel*, const ResourceAPI* api, const QString& debugName, QString metaEntryBase);

    /* Ask the API for more information */
    void searchWithTerm(const QString& term, unsigned int sort, bool filterChanged);

    void setFilter(std::shared_ptr<ModFilterWidget::Filter> filter) { m_filter = std::move(filter); }
    [[nodiscard]] QString debugName() const override { return m_debugName; }
    [[nodiscard]] QString metaEntryBase() const override { return m_metaEntryBase; }

    [[nodiscard]] std::optional<Version> queryVersion() const { return m_queryVersion; }
    [[nodiscard]] QString cleanedSearchTerm() const { return m_cleanedSearchTerm; }

   public slots:
    ResourceAPI::SearchArgs createSearchArguments() override;
    ResourceAPI::VersionSearchArgs createVersionsArguments(const QModelIndex& index) override;

   protected:
    bool checkFilters(ModPlatform::IndexedPack::Ptr pack) override;
    bool checkVersionFilters(const ModPlatform::IndexedVersion& version) override;

   protected:
    BaseInstance& m_baseInstance;

    std::shared_ptr<ModFilterWidget::Filter> m_filter = nullptr;

   private:
    void parseSearchQuery(const QString& query);

    std::optional<Version> m_queryVersion;
    QString m_cleanedSearchTerm;
    QString m_lastParsedTerm;

    QString m_debugName;
    QString m_metaEntryBase;
};

}  // namespace ResourceDownload
