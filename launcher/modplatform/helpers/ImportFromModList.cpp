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
#include "ImportFromModList.h"

#include <QFileInfo>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QRegularExpression>
#include <QSet>
#include <QUrl>

#include "minecraft/mod/Mod.h"
#include "minecraft/mod/ModFolderModel.h"

namespace ImportFromModList {

QString normalizeKey(const QString& str)
{
    QString out = str.trimmed().toLower();
    static const QRegularExpression reNonAlphaNum("[^a-z0-9]+");
    out.replace(reNonAlphaNum, "");
    return out;
}

QString cleanQueryFromFilename(const QString& filename)
{
    QString base = filename.trimmed();
    if (base.endsWith(".disabled", Qt::CaseInsensitive)) {
        base.chop(9);
    }
    if (base.endsWith(".jar", Qt::CaseInsensitive) || base.endsWith(".zip", Qt::CaseInsensitive)) {
        base.chop(4);
    }

    // Strip common loader and Minecraft version suffixes: e.g. "sodium-fabric-0.5.8+mc1.20.6" -> "sodium"
    static const QRegularExpression reLoaderVer(R"(([-_+ ](fabric|forge|neoforge|quilt|mc\d+\.\d+(\.\d+)?|1\.\d+(\.\d+)?|v?\d+\.\d+.*)$))",
                                                QRegularExpression::CaseInsensitiveOption);
    base.replace(reLoaderVer, "");
    base.replace('_', ' ');
    base.replace('-', ' ');
    return base.trimmed();
}

QString ModEntry::searchQuery() const
{
    if (!slug.isEmpty()) {
        QString s = slug;
        s.replace('-', ' ');
        return s.trimmed();
    }
    if (!name.isEmpty()) {
        return name.trimmed();
    }
    if (!modId.isEmpty()) {
        return modId.trimmed();
    }
    if (!filename.isEmpty()) {
        return cleanQueryFromFilename(filename);
    }
    return rawLine.trimmed();
}

bool ModEntry::isExactCompatibleWith(const QString& targetMcVersion, ModPlatform::ModLoaderTypes targetLoaders) const
{
    if (downloadUrl.isEmpty()) {
        return false;
    }
    if (!targetMcVersion.isEmpty() && !mcVersions.isEmpty()) {
        if (!mcVersions.contains(targetMcVersion)) {
            return false;
        }
    }
    if (targetLoaders != ModPlatform::ModLoaderType::None && loaders != ModPlatform::ModLoaderType::None) {
        if ((loaders & targetLoaders) == 0) {
            return false;
        }
    }
    return true;
}

namespace {

void parseUrlIntoEntry(const QString& urlStr, ModEntry& entry)
{
    QUrl url(urlStr.trimmed());
    if (!url.isValid() || url.scheme().isEmpty()) {
        return;
    }

    const QString host = url.host().toLower();
    const QString path = url.path();

    // Modrinth CDN direct download: https://cdn.modrinth.com/data/<project_id>/versions/<file_id>/<filename>.jar
    static const QRegularExpression reModrinthCdn(R"(^/data/([^/]+)/versions/([^/]+)/([^/?#]+))");
    if (host.contains("cdn.modrinth.com")) {
        auto m = reModrinthCdn.match(path);
        if (m.hasMatch()) {
            entry.provider = ModPlatform::ResourceProvider::MODRINTH;
            entry.projectId = m.captured(1);
            entry.fileId = m.captured(2);
            if (entry.filename.isEmpty()) {
                entry.filename = QUrl::fromPercentEncoding(m.captured(3).toUtf8());
            }
            entry.downloadUrl = urlStr.trimmed();
            return;
        }
    }

    // CurseForge CDN direct download: https://edge.forgecdn.net/files/1234/567/<filename>.jar or mediafilez.forgecdn.net
    if (host.contains("forgecdn.net")) {
        entry.provider = ModPlatform::ResourceProvider::FLAME;
        entry.downloadUrl = urlStr.trimmed();
        if (entry.filename.isEmpty()) {
            entry.filename = QFileInfo(path).fileName();
        }
        return;
    }

    // Modrinth project URL: https://modrinth.com/mod/<slug> or /plugin/<slug> or /datapack/<slug>
    static const QRegularExpression reModrinthProj(R"(^/(?:mod|plugin|datapack|resourcepack|shader)/([^/?#]+)(?:/version/([^/?#]+))?)");
    if (host.contains("modrinth.com")) {
        auto m = reModrinthProj.match(path);
        if (m.hasMatch()) {
            entry.provider = ModPlatform::ResourceProvider::MODRINTH;
            if (entry.slug.isEmpty()) {
                entry.slug = m.captured(1);
            }
            if (!m.captured(2).isEmpty() && entry.fileId.isNull()) {
                entry.fileId = m.captured(2);
            }
            if (entry.homepageUrl.isEmpty()) {
                entry.homepageUrl = urlStr.trimmed();
            }
            return;
        }
    }

    // CurseForge project URL: https://www.curseforge.com/minecraft/mc-mods/<slug>(/files/<file_id>)
    static const QRegularExpression reCurseForgeProj(R"(^/minecraft/mc-mods/([^/?#]+)(?:/(?:files|download)/(\d+))?)");
    if (host.contains("curseforge.com")) {
        auto m = reCurseForgeProj.match(path);
        if (m.hasMatch()) {
            entry.provider = ModPlatform::ResourceProvider::FLAME;
            if (entry.slug.isEmpty()) {
                entry.slug = m.captured(1);
            }
            if (!m.captured(2).isEmpty() && entry.fileId.isNull()) {
                entry.fileId = m.captured(2);
            }
            if (entry.homepageUrl.isEmpty()) {
                entry.homepageUrl = urlStr.trimmed();
            }
            return;
        }
    }

    // Direct .jar URL (e.g. GitHub Releases or custom Maven/CDN)
    if (path.endsWith(".jar", Qt::CaseInsensitive)) {
        entry.downloadUrl = urlStr.trimmed();
        if (entry.filename.isEmpty()) {
            entry.filename = QFileInfo(path).fileName();
        }
        return;
    }

    if (entry.homepageUrl.isEmpty()) {
        entry.homepageUrl = urlStr.trimmed();
    }
}

ModEntry parseJsonObject(const QJsonObject& obj)
{
    ModEntry entry;
    entry.name = obj.value("name").toString().trimmed();
    entry.slug = obj.value("slug").toString().trimmed();
    entry.modId = obj.value("mod_id").toString(obj.value("modId").toString()).trimmed();
    entry.version = obj.value("version").toString().trimmed();
    entry.filename = obj.value("filename").toString(obj.value("fileName").toString()).trimmed();
    entry.enabled = obj.value("enabled").toBool(true);

    if (obj.contains("provider")) {
        const QString prov = obj.value("provider").toString().trimmed().toLower();
        if (prov.contains("modrinth")) {
            entry.provider = ModPlatform::ResourceProvider::MODRINTH;
        } else if (prov.contains("curseforge") || prov.contains("flame")) {
            entry.provider = ModPlatform::ResourceProvider::FLAME;
        }
    }

    if (obj.contains("project_id") || obj.contains("projectID") || obj.contains("addonId")) {
        QVariant pid = obj.value("project_id").toVariant();
        if (pid.isNull() || !pid.isValid())
            pid = obj.value("projectID").toVariant();
        if (pid.isNull() || !pid.isValid())
            pid = obj.value("addonId").toVariant();
        entry.projectId = pid;
        if (!entry.provider.has_value() && obj.contains("projectID")) {
            entry.provider = ModPlatform::ResourceProvider::FLAME;
        }
    }

    if (obj.contains("file_id") || obj.contains("fileID")) {
        QVariant fid = obj.value("file_id").toVariant();
        if (fid.isNull() || !fid.isValid())
            fid = obj.value("fileID").toVariant();
        entry.fileId = fid;
    }

    if (obj.contains("download_url") || obj.contains("downloadUrl")) {
        parseUrlIntoEntry(obj.value("download_url").toString(obj.value("downloadUrl").toString()), entry);
    }

    if (obj.contains("url")) {
        parseUrlIntoEntry(obj.value("url").toString(), entry);
    }

    if (obj.contains("hash")) {
        entry.hash = obj.value("hash").toString();
        entry.hashFormat = obj.value("hash_format").toString("sha512");
    } else if (obj.contains("hashes") && obj.value("hashes").isObject()) {
        const QJsonObject hashes = obj.value("hashes").toObject();
        if (hashes.contains("sha512")) {
            entry.hash = hashes.value("sha512").toString();
            entry.hashFormat = "sha512";
        } else if (hashes.contains("sha1")) {
            entry.hash = hashes.value("sha1").toString();
            entry.hashFormat = "sha1";
        }
    }

    // Modrinth modrinth.index.json entry: { "path": "mods/foo.jar", "downloads": ["https://..."] }
    if (obj.contains("path") && obj.value("path").toString().startsWith("mods/")) {
        if (entry.filename.isEmpty()) {
            entry.filename = QFileInfo(obj.value("path").toString()).fileName();
        }
        if (obj.contains("downloads") && obj.value("downloads").isArray()) {
            const QJsonArray downloads = obj.value("downloads").toArray();
            if (!downloads.isEmpty()) {
                parseUrlIntoEntry(downloads.first().toString(), entry);
            }
        }
    }

    if (obj.contains("mc_versions") && obj.value("mc_versions").isArray()) {
        for (const auto& v : obj.value("mc_versions").toArray()) {
            entry.mcVersions << v.toString();
        }
    }

    if (obj.contains("loaders") && obj.value("loaders").isArray()) {
        for (const auto& l : obj.value("loaders").toArray()) {
            entry.loaders |= ModPlatform::getModLoaderFromString(l.toString());
        }
    }

    if (entry.name.isEmpty() && !entry.filename.isEmpty()) {
        entry.name = cleanQueryFromFilename(entry.filename);
    }
    entry.rawLine = entry.displayName();
    return entry;
}

std::optional<ModEntry> parseTextLine(const QString& rawLine)
{
    QString line = rawLine.trimmed();
    if (line.isEmpty() || line.startsWith('#') || line.startsWith("//") || line.startsWith("<html>", Qt::CaseInsensitive) ||
        line.startsWith("</html>", Qt::CaseInsensitive) || line.startsWith("<ul>", Qt::CaseInsensitive) ||
        line.startsWith("</ul>", Qt::CaseInsensitive)) {
        return std::nullopt;
    }

    ModEntry entry;
    entry.rawLine = line;

    // 1. Check for Forge / Fabric crash report table format: | LCH | modid | version | filename.jar |
    static const QRegularExpression reCrashTable(R"(^\|\s*[A-Z]+\s*\|\s*([^|]+)\|\s*([^|]+)\|\s*([^|]+\.jar)\s*\|)",
                                                 QRegularExpression::CaseInsensitiveOption);
    auto crashMatch = reCrashTable.match(line);
    if (crashMatch.hasMatch()) {
        entry.modId = crashMatch.captured(1).trimmed();
        entry.version = crashMatch.captured(2).trimmed();
        entry.filename = crashMatch.captured(3).trimmed();
        entry.name = entry.modId;
        if (entry.modId == "minecraft" || entry.modId == "forge" || entry.modId == "neoforge" || entry.modId == "mcp" ||
            entry.modId == "FML") {
            return std::nullopt;
        }
        return entry;
    }

    // 2. Strip HTML <li>...</li> and <a href="...">Name</a>
    static const QRegularExpression reHtmlLi(R"(^<li>(.*)</li>$)", QRegularExpression::CaseInsensitiveOption);
    auto liMatch = reHtmlLi.match(line);
    if (liMatch.hasMatch()) {
        line = liMatch.captured(1).trimmed();
    }

    static const QRegularExpression reHtmlLink(R"(<a\s+href=["']([^"']+)["'][^>]*>([^<]+)</a>)", QRegularExpression::CaseInsensitiveOption);
    auto htmlLinkMatch = reHtmlLink.match(line);
    if (htmlLinkMatch.hasMatch()) {
        parseUrlIntoEntry(htmlLinkMatch.captured(1), entry);
        entry.name = htmlLinkMatch.captured(2).trimmed();
        line.remove(htmlLinkMatch.capturedStart(), htmlLinkMatch.capturedLength());
    }

    // 3. Strip Markdown bullet prefix ("- ", "* ", "1. ") and unescape Markdown backslashes
    static const QRegularExpression reBullet(R"(^(?:[-*+]|\d+\.)\s+)");
    line.remove(reBullet);
    line.replace("\\[", "[")
        .replace("\\]", "]")
        .replace("\\(", "(")
        .replace("\\)", ")")
        .replace("\\_", "_")
        .replace("\\*", "*")
        .replace("\\+", "+")
        .replace("\\-", "-")
        .replace("\\.", ".");

    // 4. Check Markdown link: [Mod Name](https://...)
    static const QRegularExpression reMdLink(R"(\[([^\]]+)\]\((https?://[^)\s]+)\))");
    auto mdMatch = reMdLink.match(line);
    if (mdMatch.hasMatch()) {
        if (entry.name.isEmpty()) {
            entry.name = mdMatch.captured(1).trimmed();
        }
        parseUrlIntoEntry(mdMatch.captured(2), entry);
        line.remove(mdMatch.capturedStart(), mdMatch.capturedLength());
    }

    // 5. Extract any remaining http/https URL in the line
    static const QRegularExpression reAnyUrl(R"re(\(?(https?://[^\s)"'>\]]+)\)?)re");
    auto urlMatch = reAnyUrl.match(line);
    if (urlMatch.hasMatch()) {
        parseUrlIntoEntry(urlMatch.captured(1), entry);
        line.remove(urlMatch.capturedStart(), urlMatch.capturedLength());
    }

    // 6. Extract filename in parentheses: (modfile-1.20.1.jar) or (modfile.jar.disabled)
    static const QRegularExpression reFileParen(R"(\(([^()]+\.jar(?:\.disabled)?)\))", QRegularExpression::CaseInsensitiveOption);
    auto fileMatch = reFileParen.match(line);
    if (fileMatch.hasMatch()) {
        entry.filename = fileMatch.captured(1).trimmed();
        if (entry.filename.endsWith(".disabled", Qt::CaseInsensitive)) {
            entry.enabled = false;
        }
        line.remove(fileMatch.capturedStart(), fileMatch.capturedLength());
    }

    // 7. Extract version in square brackets: [1.2.3]
    static const QRegularExpression reVerBracket(R"(\[([^\[\]]+)\])");
    auto verMatch = reVerBracket.match(line);
    if (verMatch.hasMatch()) {
        entry.version = verMatch.captured(1).trimmed();
        line.remove(verMatch.capturedStart(), verMatch.capturedLength());
    }

    // 8. Strip " by Author1, Author2" suffix
    static const QRegularExpression reByAuthors(R"(\s+by\s+.+$)");
    line.remove(reByAuthors);

    // 9. Handle CSV line if commas are present and name is not yet extracted
    if (entry.name.isEmpty() && line.contains(',')) {
        const QStringList parts = line.split(',');
        if (!parts.isEmpty()) {
            QString firstCol = parts.first().trimmed();
            if (firstCol.startsWith('"') && firstCol.endsWith('"') && firstCol.size() >= 2) {
                firstCol = firstCol.mid(1, firstCol.size() - 2);
            }
            if (firstCol.compare("name", Qt::CaseInsensitive) == 0 || firstCol.compare("mod name", Qt::CaseInsensitive) == 0) {
                return std::nullopt;  // CSV header row
            }
            line = firstCol;
            for (qsizetype i = 1; i < parts.size(); ++i) {
                const QString col = parts[i].trimmed();
                if (col.startsWith("http://") || col.startsWith("https://")) {
                    parseUrlIntoEntry(col, entry);
                } else if (col.endsWith(".jar", Qt::CaseInsensitive) || col.endsWith(".jar.disabled", Qt::CaseInsensitive)) {
                    entry.filename = col;
                }
            }
        }
    }

    line = line.trimmed();
    if (entry.name.isEmpty() && !line.isEmpty()) {
        if (line.endsWith(".jar", Qt::CaseInsensitive) || line.endsWith(".jar.disabled", Qt::CaseInsensitive)) {
            entry.filename = line;
            if (line.endsWith(".disabled", Qt::CaseInsensitive)) {
                entry.enabled = false;
            }
            entry.name = cleanQueryFromFilename(line);
        } else {
            entry.name = line;
        }
    }

    if (entry.name.isEmpty() && entry.slug.isEmpty() && entry.projectId.isNull() && entry.downloadUrl.isEmpty() &&
        entry.filename.isEmpty()) {
        return std::nullopt;
    }

    return entry;
}

}  // namespace

QList<ModEntry> parse(const QString& content)
{
    QList<ModEntry> results;
    const QString trimmed = content.trimmed();
    if (trimmed.isEmpty()) {
        return results;
    }

    // Try parsing as JSON document first (Prism JSON array, Modrinth index.json, or CurseForge manifest.json)
    QJsonParseError jsonErr{};
    QJsonDocument doc = QJsonDocument::fromJson(trimmed.toUtf8(), &jsonErr);
    if (jsonErr.error == QJsonParseError::NoError) {
        if (doc.isArray()) {
            for (const auto& item : doc.array()) {
                if (item.isObject()) {
                    ModEntry e = parseJsonObject(item.toObject());
                    if (!e.displayName().isEmpty() || !e.projectId.isNull() || !e.downloadUrl.isEmpty()) {
                        results.append(std::move(e));
                    }
                } else if (item.isString()) {
                    auto e = parseTextLine(item.toString());
                    if (e.has_value()) {
                        results.append(std::move(*e));
                    }
                }
            }
            return results;
        }
        if (doc.isObject()) {
            const QJsonObject root = doc.object();
            if (root.contains("files") && root.value("files").isArray()) {
                for (const auto& item : root.value("files").toArray()) {
                    if (item.isObject()) {
                        ModEntry e = parseJsonObject(item.toObject());
                        if (!e.displayName().isEmpty() || !e.projectId.isNull() || !e.downloadUrl.isEmpty()) {
                            results.append(std::move(e));
                        }
                    }
                }
                return results;
            }
            if (root.contains("mods") && root.value("mods").isArray()) {
                for (const auto& item : root.value("mods").toArray()) {
                    if (item.isObject()) {
                        results.append(parseJsonObject(item.toObject()));
                    }
                }
                return results;
            }
        }
    }

    // Fallback: line-by-line universal parser (HTML, Markdown, CSV, Plain Text, URLs, Crash Logs)
    QSet<QString> seenKeys;
    const QStringList lines = trimmed.split(QRegularExpression("[\r\n]+"), Qt::SkipEmptyParts);
    for (const QString& rawLine : lines) {
        // Also support inline JSON objects from custom templates like `{"name":"...","url":"..."},`
        QString candidateJson = rawLine.trimmed();
        if (candidateJson.endsWith(',')) {
            candidateJson.chop(1);
        }
        if (candidateJson.startsWith('{') && candidateJson.endsWith('}')) {
            QJsonParseError lineErr{};
            QJsonDocument lineDoc = QJsonDocument::fromJson(candidateJson.toUtf8(), &lineErr);
            if (lineErr.error == QJsonParseError::NoError && lineDoc.isObject()) {
                ModEntry e = parseJsonObject(lineDoc.object());
                const QString dedupKey = normalizeKey(e.slug.isEmpty() ? e.displayName() : e.slug);
                if (!dedupKey.isEmpty() && !seenKeys.contains(dedupKey)) {
                    seenKeys.insert(dedupKey);
                    results.append(std::move(e));
                }
                continue;
            }
        }

        auto parsed = parseTextLine(rawLine);
        if (parsed.has_value()) {
            const QString dedupKey = !parsed->projectId.isNull()
                                         ? parsed->projectId.toString()
                                         : normalizeKey(!parsed->slug.isEmpty() ? parsed->slug : parsed->displayName());
            if (!dedupKey.isEmpty()) {
                if (seenKeys.contains(dedupKey)) {
                    continue;
                }
                seenKeys.insert(dedupKey);
            }
            results.append(std::move(*parsed));
        }
    }

    return results;
}

void markAlreadyInstalled(QList<ModEntry>& entries, ModFolderModel* model, bool uncheckInstalled)
{
    if (!model) {
        return;
    }

    QSet<QString> installedProjectIds;
    QSet<QString> installedSlugs;
    QSet<QString> installedModIds;
    QSet<QString> installedNames;
    QSet<QString> installedFiles;

    for (auto* mod : model->allMods()) {
        if (!mod) {
            continue;
        }
        if (!mod->modId().isEmpty()) {
            installedModIds.insert(normalizeKey(mod->modId()));
        }
        if (!mod->name().isEmpty()) {
            installedNames.insert(normalizeKey(mod->name()));
        }
        QString fname = mod->fileinfo().fileName();
        if (fname.endsWith(".disabled", Qt::CaseInsensitive)) {
            fname.chop(9);
        }
        if (!fname.isEmpty()) {
            installedFiles.insert(fname.toLower());
        }
        if (auto meta = mod->metadata()) {
            if (!meta->projectId.isNull()) {
                installedProjectIds.insert(meta->projectId.toString().toLower());
            }
            if (!meta->slug.isEmpty()) {
                installedSlugs.insert(normalizeKey(meta->slug));
            }
        }
    }

    for (auto& entry : entries) {
        bool isInstalled = false;
        if (!entry.projectId.isNull() && installedProjectIds.contains(entry.projectId.toString().toLower())) {
            isInstalled = true;
        } else if (!entry.slug.isEmpty() && installedSlugs.contains(normalizeKey(entry.slug))) {
            isInstalled = true;
        } else if (!entry.modId.isEmpty() && installedModIds.contains(normalizeKey(entry.modId))) {
            isInstalled = true;
        } else if (!entry.name.isEmpty() && installedNames.contains(normalizeKey(entry.name))) {
            isInstalled = true;
        } else if (!entry.filename.isEmpty()) {
            QString cleanFname = entry.filename;
            if (cleanFname.endsWith(".disabled", Qt::CaseInsensitive)) {
                cleanFname.chop(9);
            }
            if (installedFiles.contains(cleanFname.toLower())) {
                isInstalled = true;
            }
        }

        if (isInstalled) {
            entry.status = EntryStatus::AlreadyInstalled;
            entry.statusText = QObject::tr("Already Installed");
            if (uncheckInstalled) {
                entry.selected = false;
            }
        }
    }
}

}  // namespace ImportFromModList
