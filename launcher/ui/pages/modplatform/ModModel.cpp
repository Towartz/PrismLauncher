// SPDX-FileCopyrightText: 2023 flowln <flowlnlnln@gmail.com>
//
// SPDX-License-Identifier: GPL-3.0-only

#include "ModModel.h"

#include "minecraft/MinecraftInstance.h"
#include "minecraft/PackProfile.h"
#include "minecraft/mod/ModFolderModel.h"
#include "modplatform/ModIndex.h"
#include "modplatform/ResourceAPI.h"
#include "modplatform/ResourceType.h"
#include "ui/pages/modplatform/ResourceModel.h"

#include <QMessageBox>
#include <QModelIndex>
#include <QRegularExpression>
#include <QString>
#include <algorithm>
#include <utility>

namespace ResourceDownload {

ModModel::ModModel(BaseInstance& baseInst,
                   ResourceFolderModel* resourceList,
                   const ResourceAPI* api,
                   const QString& debugName,
                   QString metaEntryBase)
    : ResourceModel(resourceList, api)
    , m_baseInstance(baseInst)
    , m_debugName(debugName + " (Model)")
    , m_metaEntryBase(std::move(metaEntryBase))
{}

/******** Make data requests ********/

void ModModel::parseSearchQuery(const QString& query)
{
    m_queryVersion.reset();
    m_cleanedSearchTerm = query.trimmed();
    m_lastParsedTerm = query;

    if (m_cleanedSearchTerm.isEmpty()) {
        return;
    }

    // Matches Minecraft versions:
    // - Major/Minor: 1.20, 1.20.1, 1.7.10
    // - Pre-release / RC: 1.20-pre1, 1.20.1-rc1, 1.20-pre-1, 1.20 Pre-Release 1
    // - Snapshots: 24w14a, 23w13a_or_b
    static const QRegularExpression versionRegex(
        R"((?:^|\s)(?:(?:for|mc|v|version)\s+)?(1\.\d+(?:\.\d+)?(?:(?:-|\s+)(?:pre(?:-release)?|rc|release\s+candidate)(?:-|\s*)?\d+)?|\d{2}w\d{2}[a-z](?:_[a-z0-9]+)?)(?:\s|$))",
        QRegularExpression::CaseInsensitiveOption);

    auto match = versionRegex.match(m_cleanedSearchTerm);
    if (match.hasMatch()) {
        QString verStr = match.captured(1).trimmed();
        m_queryVersion = Version(verStr);

        QString cleaned = m_cleanedSearchTerm;
        cleaned.remove(match.capturedStart(), match.capturedLength());

        static const QRegularExpression trailingFiller(R"(\s+\b(?:for|mc|v|version)\b\s*$)", QRegularExpression::CaseInsensitiveOption);
        static const QRegularExpression leadingFiller(R"(^\s*\b(?:for|mc|v|version)\b\s+)", QRegularExpression::CaseInsensitiveOption);
        cleaned.remove(trailingFiller);
        cleaned.remove(leadingFiller);
        m_cleanedSearchTerm = cleaned.simplified().trimmed();
    }
}

ResourceAPI::SearchArgs ModModel::createSearchArguments()
{
    auto* profile = static_cast<const MinecraftInstance&>(m_baseInstance).getPackProfile();

    Q_ASSERT(profile);
    Q_ASSERT(m_filter);

    if (m_searchTerm != m_lastParsedTerm) {
        parseSearchQuery(m_searchTerm);
    }

    std::optional<std::vector<Version>> versions{};
    std::optional<QStringList> categories{};
    auto loaders = profile->getSupportedModLoaders();

    // Version filter: query-specified version takes precedence, then filter widget
    if (m_queryVersion.has_value()) {
        versions = std::vector<Version>{ m_queryVersion.value() };
    } else if (!m_filter->versions.empty()) {
        versions = m_filter->versions;
    }
    if (m_filter->loaders != 0U) {
        loaders = m_filter->loaders;
    }
    if (!m_filter->categoryIds.empty()) {
        categories = m_filter->categoryIds;
    }
    auto side = m_filter->side;

    auto sort = getCurrentSortingMethodByIndex();

    std::optional<QString> search;
    if (!m_cleanedSearchTerm.isEmpty()) {
        search = m_cleanedSearchTerm;
    }

    return {
        .type = ModPlatform::ResourceType::Mod,
        .offset = m_nextSearchOffset,
        .search = search,
        .sorting = sort,
        .loaders = loaders,
        .versions = versions,
        .side = side,
        .categoryIds = categories,
        .openSource = m_filter->openSource,
        .excludeDisclosureTypes = m_filter->excludeDisclosureTypes,
    };
}

ResourceAPI::VersionSearchArgs ModModel::createVersionsArguments(const QModelIndex& index)
{
    auto pack = m_packs[index.row()];
    auto* profile = static_cast<const MinecraftInstance&>(m_baseInstance).getPackProfile();

    Q_ASSERT(profile);
    Q_ASSERT(m_filter);

    if (m_searchTerm != m_lastParsedTerm) {
        parseSearchQuery(m_searchTerm);
    }

    std::optional<std::vector<Version>> versions{};
    auto loaders = profile->getSupportedModLoaders();
    if (m_queryVersion.has_value()) {
        versions = std::vector<Version>{ m_queryVersion.value() };
    } else if (!m_filter->versions.empty()) {
        versions = m_filter->versions;
    }
    if (m_filter->loaders != 0U) {
        loaders = m_filter->loaders;
    }

    return { .pack = pack, .mcVersions = versions, .loaders = loaders, .resourceType = ModPlatform::ResourceType::Mod };
}

void ModModel::searchWithTerm(const QString& term, unsigned int sort, bool filterChanged)
{
    if (m_searchTerm == term && m_searchTerm.isNull() == term.isNull() && m_currentSortIndex == sort && !filterChanged && !m_packs.isEmpty()) {
        return;
    }

    setSearchTerm(term);
    parseSearchQuery(term);
    m_currentSortIndex = sort;

    refresh();
}

namespace {

bool checkSide(ModPlatform::SideType filter, ModPlatform::SideType value)
{
    return (filter != ModPlatform::SideType::ClientSide && filter != ModPlatform::SideType::ServerSide) ||
           (value != ModPlatform::SideType::ClientSide && value != ModPlatform::SideType::ServerSide) || filter == value;
}
}  // namespace

bool ModModel::checkFilters(ModPlatform::IndexedPack::Ptr pack)
{
    if (!m_filter) {
        return true;
    }
    return !(m_filter->hideInstalled && isPackInstalled(pack)) && checkSide(m_filter->side, pack->side);
}

bool ModModel::checkVersionFilters(const ModPlatform::IndexedVersion& v)
{
    if (!m_filter) {
        return true;
    }
    auto loaders = static_cast<MinecraftInstance&>(m_baseInstance).getPackProfile()->getSupportedModLoaders();
    if (m_filter->loaders != 0U) {
        loaders = m_filter->loaders;
    }

    bool versionsOk = false;
    if (m_queryVersion.has_value()) {
        versionsOk = ModFilterWidget::Filter::checkSingleMcVersion(v.mcVersion, m_queryVersion.value());
    } else {
        versionsOk = m_filter->checkMcVersions(v.mcVersion);
    }

    return (!optedOut(v) &&                                                                   // is opted out(aka curseforge download link)
            (!loaders.has_value() || !v.loaders || ((loaders.value() & v.loaders) != 0U)) &&  // loaders
            checkSide(m_filter->side, v.side) &&                                              // side
            (m_filter->releases.empty() ||                                                    // releases
             std::find(m_filter->releases.cbegin(), m_filter->releases.cend(), v.versionType) != m_filter->releases.cend()) &&
            versionsOk);  // mcVersions
}

}  // namespace ResourceDownload
