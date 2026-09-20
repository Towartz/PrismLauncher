// SPDX-License-Identifier: GPL-3.0-only
/*
 *  Prism Launcher - Minecraft Launcher
 *  Copyright (c) 2026 Towartz
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

#include "CloudSkinUpload.h"

#include <QDebug>
#include <QFile>
#include <QHttpMultiPart>
#include <QHttpPart>
#include <QJsonDocument>
#include <QJsonObject>
#include <QNetworkRequest>
#include <QUrl>
#include <expected>

#include "Json.h"
#include "net/RPCSink.h"
#include "net/RawHeaderProxy.h"

namespace CloudSkinUpload {

std::pair<Net::Request::Ptr, QString*> makeMineskinUpload(const QString& imagePath, const QString& variant)
{
    Net::Request::MultiPartFactory getPayload = [imagePath, variant]() -> std::expected<QHttpMultiPart*, QString> {
        auto* file = new QFile(imagePath);
        if (!file->open(QFile::ReadOnly)) {
            QString err = file->errorString();
            file->deleteLater();
            return std::unexpected(QObject::tr("Could not open skin file %1 for reading: %2").arg(imagePath, err));
        }

        auto* multiPart = new QHttpMultiPart(QHttpMultiPart::FormDataType);
        file->setParent(multiPart);

        QHttpPart filePart;
        filePart.setHeader(QNetworkRequest::ContentTypeHeader, "image/png");
        filePart.setHeader(QNetworkRequest::ContentDispositionHeader, R"(form-data; name="file"; filename="skin.png")");
        filePart.setBodyDevice(file);
        multiPart->append(filePart);

        QHttpPart variantPart;
        variantPart.setHeader(QNetworkRequest::ContentDispositionHeader, R"(form-data; name="variant")");
        variantPart.setBody(variant.toUtf8());
        multiPart->append(variantPart);

        QHttpPart visibilityPart;
        visibilityPart.setHeader(QNetworkRequest::ContentDispositionHeader, R"(form-data; name="visibility")");
        visibilityPart.setBody("0");
        multiPart->append(visibilityPart);

        return multiPart;
    };

    auto parseFunc = [](const QByteArray& response) -> Result<QString> {
        auto doc = Json::requireDocument(response);
        if (!doc) {
            return std::unexpected(doc.error());
        }
        auto obj = doc->object();
        QString url;
        if (obj.contains("data")) {
            auto dataObj = obj.value("data").toObject();
            if (dataObj.contains("texture")) {
                url = dataObj.value("texture").toObject().value("url").toString();
            }
        }
        if (url.isEmpty() && obj.contains("url")) {
            url = obj.value("url").toString();
        }
        if (url.isEmpty()) {
            return std::unexpected(obj.value("error").toString("Failed to parse Mineskin response"));
        }
        if (url.startsWith("http://")) {
            url.replace(0, 4, "https");
        }
        return url;
    };

    auto dl = Net::Request::makeCustomRequest({
        .method = Net::HttpMethod::Post,
        .url = QUrl("https://api.mineskin.org/generate/upload"),
        .data = getPayload,
    });
    auto sink = std::make_unique<Net::RPC::Sink<QString>>(std::move(parseFunc));
    auto* result = sink->result();
    dl->setSink(std::move(sink));
    dl->addHeaderProxy(std::make_unique<Net::RawHeaderProxy>(QList<Net::HeaderPair>{
        { .headerName = "Accept", .headerValue = "application/json" },
        { .headerName = "User-Agent", .headerValue = "PrismLauncher" },
    }));
    return { dl, result };
}

std::pair<Net::Request::Ptr, QString*> makeCatboxUpload(const QString& imagePath)
{
    Net::Request::MultiPartFactory getPayload = [imagePath]() -> std::expected<QHttpMultiPart*, QString> {
        auto* file = new QFile(imagePath);
        if (!file->open(QFile::ReadOnly)) {
            QString err = file->errorString();
            file->deleteLater();
            return std::unexpected(QObject::tr("Could not open skin file %1 for reading: %2").arg(imagePath, err));
        }

        auto* multiPart = new QHttpMultiPart(QHttpMultiPart::FormDataType);
        file->setParent(multiPart);

        QHttpPart reqTypePart;
        reqTypePart.setHeader(QNetworkRequest::ContentDispositionHeader, R"(form-data; name="reqtype")");
        reqTypePart.setBody("fileupload");
        multiPart->append(reqTypePart);

        QHttpPart filePart;
        filePart.setHeader(QNetworkRequest::ContentTypeHeader, "image/png");
        filePart.setHeader(QNetworkRequest::ContentDispositionHeader, R"(form-data; name="fileToUpload"; filename="skin.png")");
        filePart.setBodyDevice(file);
        multiPart->append(filePart);

        return multiPart;
    };

    auto parseFunc = [](const QByteArray& response) -> Result<QString> {
        QString url = QString::fromUtf8(response).trimmed();
        if (url.startsWith("http://") || url.startsWith("https://")) {
            return url;
        }
        return std::unexpected(QObject::tr("Invalid server response: %1").arg(url));
    };

    auto dl = Net::Request::makeCustomRequest({
        .method = Net::HttpMethod::Post,
        .url = QUrl("https://catbox.moe/user/api.php"),
        .data = getPayload,
    });
    auto sink = std::make_unique<Net::RPC::Sink<QString>>(std::move(parseFunc));
    auto* result = sink->result();
    dl->setSink(std::move(sink));
    dl->addHeaderProxy(std::make_unique<Net::RawHeaderProxy>(QList<Net::HeaderPair>{
        { .headerName = "User-Agent", .headerValue = "PrismLauncher" },
    }));
    return { dl, result };
}

}  // namespace CloudSkinUpload
