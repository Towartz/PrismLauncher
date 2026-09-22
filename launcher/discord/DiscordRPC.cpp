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

#include "DiscordRPC.h"

#include <QCoreApplication>
#include <QDataStream>
#include <QDebug>
#include <QFile>
#include <QJsonDocument>
#include <QJsonObject>
#include <QJsonValue>

#include "Application.h"
#include "minecraft/MinecraftInstance.h"
#include "minecraft/PackProfile.h"
#include "settings/SettingsObject.h"

DiscordRPC::DiscordRPC(QObject* parent) : QObject(parent)
{
    m_socket = new QLocalSocket(this);
    m_reconnectTimer = new QTimer(this);
    m_reconnectTimer->setSingleShot(false);

    connect(m_socket, &QLocalSocket::connected, this, &DiscordRPC::onConnected);
    connect(m_socket, &QLocalSocket::disconnected, this, &DiscordRPC::onDisconnected);
    connect(m_socket, &QLocalSocket::readyRead, this, &DiscordRPC::onReadyRead);
    connect(m_socket, &QLocalSocket::errorOccurred, this, &DiscordRPC::onErrorOccurred);
    connect(m_reconnectTimer, &QTimer::timeout, this, &DiscordRPC::onReconnectTimeout);
}

DiscordRPC::~DiscordRPC()
{
    clearActivity();
}

QString DiscordRPC::getEffectiveClientId() const
{
    QString id = APPLICATION->settings()->get("DiscordRPCClientID").toString().trimmed();
    if (id.isEmpty()) {
        return QString::fromLatin1(DEFAULT_APPLICATION_ID);
    }
    return id;
}

QString DiscordRPC::resolvePipePath(int index) const
{
#if defined(Q_OS_WIN)
    return QString("discord-ipc-%1").arg(index);
#else
    QStringList candidateDirs = {
        qEnvironmentVariable("XDG_RUNTIME_DIR"),
        qEnvironmentVariable("XDG_RUNTIME_DIR") + "/app/com.discordapp.Discord",
        qEnvironmentVariable("TMPDIR"),
        qEnvironmentVariable("TMP"),
        qEnvironmentVariable("TEMP"),
        "/tmp"
    };
    for (const auto& dir : candidateDirs) {
        if (dir.isEmpty()) {
            continue;
        }
        QString path = QString("%1/discord-ipc-%2").arg(dir).arg(index);
        if (QFile::exists(path)) {
            return path;
        }
    }
    return QString("/tmp/discord-ipc-%1").arg(index);
#endif
}

void DiscordRPC::attemptConnection()
{
    if (!m_socket) {
        return;
    }
    if (m_socket->state() == QLocalSocket::ConnectedState || m_socket->state() == QLocalSocket::ConnectingState) {
        return;
    }
    m_receiveBuffer.clear();
    m_ready = false;

    QString pipe = resolvePipePath(m_pipeIndex);
    qDebug() << "Connecting to Discord IPC pipe:" << pipe;
    m_socket->connectToServer(pipe);
}

void DiscordRPC::onConnected()
{
    qDebug() << "Connected to Discord IPC pipe index" << m_pipeIndex;
    m_reconnectTimer->stop();
    sendHandshake();
}

void DiscordRPC::sendHandshake()
{
    QJsonObject payload;
    payload["v"] = 1;
    payload["client_id"] = getEffectiveClientId();
    sendFrame(Opcode::Handshake, payload);
}

void DiscordRPC::sendFrame(Opcode op, const QJsonObject& payload)
{
    if (!m_socket || m_socket->state() != QLocalSocket::ConnectedState) {
        return;
    }
    QByteArray jsonBytes = QJsonDocument(payload).toJson(QJsonDocument::Compact);
    QByteArray packet;
    QDataStream stream(&packet, QIODevice::WriteOnly);
    stream.setByteOrder(QDataStream::LittleEndian);
    stream << static_cast<quint32>(op);
    stream << static_cast<quint32>(jsonBytes.size());
    packet.append(jsonBytes);
    m_socket->write(packet);
    m_socket->flush();
}

void DiscordRPC::onReadyRead()
{
    m_receiveBuffer.append(m_socket->readAll());
    while (m_receiveBuffer.size() >= 8) {
        quint32 opVal = 0;
        quint32 length = 0;
        QDataStream stream(m_receiveBuffer);
        stream.setByteOrder(QDataStream::LittleEndian);
        stream >> opVal;
        stream >> length;

        if (static_cast<quint32>(m_receiveBuffer.size()) < 8 + length) {
            break;
        }

        QByteArray payloadBytes = m_receiveBuffer.mid(8, length);
        m_receiveBuffer.remove(0, 8 + length);
        handleMessage(static_cast<Opcode>(opVal), payloadBytes);
    }
}

void DiscordRPC::handleMessage(Opcode op, const QByteArray& data)
{
    if (op == Opcode::Frame) {
        QJsonParseError err;
        auto doc = QJsonDocument::fromJson(data, &err);
        if (err.error == QJsonParseError::NoError && doc.isObject()) {
            auto obj = doc.object();
            QString evt = obj.value("evt").toString();
            if (evt == "READY") {
                qDebug() << "Discord RPC handshake completed, client ready";
                m_ready = true;
                if (m_hasActiveActivity) {
                    sendActivityPayload();
                }
            }
        }
    } else if (op == Opcode::Close) {
        m_socket->disconnectFromServer();
    } else if (op == Opcode::Ping) {
        sendFrame(Opcode::Pong, QJsonObject());
    }
}

void DiscordRPC::onErrorOccurred(QLocalSocket::LocalSocketError)
{
    m_ready = false;
    if (m_hasActiveActivity && m_pipeIndex < 9) {
        m_pipeIndex++;
        QTimer::singleShot(50, this, &DiscordRPC::attemptConnection);
    } else if (m_hasActiveActivity) {
        m_pipeIndex = 0;
        if (!m_reconnectTimer->isActive()) {
            m_reconnectTimer->start(5000);
        }
    }
}

void DiscordRPC::onDisconnected()
{
    m_ready = false;
    if (m_hasActiveActivity) {
        m_pipeIndex = 0;
        if (!m_reconnectTimer->isActive()) {
            m_reconnectTimer->start(5000);
        }
    }
}

void DiscordRPC::onReconnectTimeout()
{
    if (m_hasActiveActivity && !m_ready) {
        attemptConnection();
    }
}

void DiscordRPC::setActivity(const DiscordActivity& activity)
{
    m_currentActivity = activity;
    m_hasActiveActivity = true;

    if (!m_ready || !m_socket || m_socket->state() != QLocalSocket::ConnectedState) {
        m_pipeIndex = 0;
        attemptConnection();
    } else {
        sendActivityPayload();
    }
}

void DiscordRPC::sendActivityPayload()
{
    if (!m_ready) {
        return;
    }

    QJsonObject activityObj;
    if (!m_currentActivity.details.isEmpty()) {
        activityObj["details"] = m_currentActivity.details;
    }
    if (!m_currentActivity.state.isEmpty()) {
        activityObj["state"] = m_currentActivity.state;
    }
    if (m_currentActivity.startTimestamp > 0) {
        QJsonObject timestampsObj;
        timestampsObj["start"] = m_currentActivity.startTimestamp;
        activityObj["timestamps"] = timestampsObj;
    }
    QJsonObject assetsObj;
    if (!m_currentActivity.largeImage.isEmpty()) {
        assetsObj["large_image"] = m_currentActivity.largeImage;
    }
    if (!m_currentActivity.largeText.isEmpty()) {
        assetsObj["large_text"] = m_currentActivity.largeText;
    }
    if (!m_currentActivity.smallImage.isEmpty()) {
        assetsObj["small_image"] = m_currentActivity.smallImage;
    }
    if (!m_currentActivity.smallText.isEmpty()) {
        assetsObj["small_text"] = m_currentActivity.smallText;
    }
    if (!assetsObj.isEmpty()) {
        activityObj["assets"] = assetsObj;
    }

    QJsonObject argsObj;
    argsObj["pid"] = m_currentActivity.processId > 0 ? m_currentActivity.processId : static_cast<qint64>(QCoreApplication::applicationPid());
    argsObj["activity"] = activityObj;

    QJsonObject packet;
    packet["cmd"] = "SET_ACTIVITY";
    packet["args"] = argsObj;
    packet["nonce"] = QString::number(++m_nonce);

    sendFrame(Opcode::Frame, packet);
}

void DiscordRPC::clearActivity()
{
    m_hasActiveActivity = false;
    if (m_reconnectTimer) {
        m_reconnectTimer->stop();
    }

    if (m_ready && m_socket && m_socket->state() == QLocalSocket::ConnectedState) {
        QJsonObject argsObj;
        argsObj["pid"] = m_currentActivity.processId > 0 ? m_currentActivity.processId : static_cast<qint64>(QCoreApplication::applicationPid());
        argsObj["activity"] = QJsonValue::Null;

        QJsonObject packet;
        packet["cmd"] = "SET_ACTIVITY";
        packet["args"] = argsObj;
        packet["nonce"] = QString::number(++m_nonce);

        sendFrame(Opcode::Frame, packet);
        m_socket->flush();
    }
    m_ready = false;
    if (m_socket && m_socket->state() != QLocalSocket::UnconnectedState) {
        m_socket->disconnectFromServer();
    }
}

void DiscordRPC::setActivityForInstance(BaseInstance* instance, qint64 pid)
{
    if (!APPLICATION->settings()->get("DiscordRPCEnabled").toBool()) {
        return;
    }

    auto* mcInstance = qobject_cast<MinecraftInstance*>(instance);
    if (!mcInstance) {
        return;
    }

    QString mcVersion;
    if (auto* profile = mcInstance->getPackProfile()) {
        mcVersion = profile->getComponentVersion("net.minecraft");
    }

    QString loaderStr;
    if (auto* profile = mcInstance->getPackProfile()) {
        auto fabricVer = profile->getComponentVersion("net.fabricmc.fabric-loader");
        auto neoforgeVer = profile->getComponentVersion("net.neoforged.neoforge");
        auto forgeVer = profile->getComponentVersion("net.minecraftforge");
        auto quiltVer = profile->getComponentVersion("org.quiltmc.quilt-loader");

        if (!fabricVer.isEmpty()) {
            loaderStr = QString("Fabric %1").arg(fabricVer);
        } else if (!neoforgeVer.isEmpty()) {
            loaderStr = QString("NeoForge %1").arg(neoforgeVer);
        } else if (!forgeVer.isEmpty()) {
            loaderStr = QString("Forge %1").arg(forgeVer);
        } else if (!quiltVer.isEmpty()) {
            loaderStr = QString("Quilt %1").arg(quiltVer);
        }
    }

    bool showInstanceName = APPLICATION->settings()->get("DiscordRPCShowInstanceName").toBool();
    bool showVersion = APPLICATION->settings()->get("DiscordRPCShowVersion").toBool();
    bool showLoader = APPLICATION->settings()->get("DiscordRPCShowModLoader").toBool();

    DiscordActivity activity;
    activity.processId = pid;
    activity.startTimestamp = QDateTime::currentSecsSinceEpoch();

    QString versionText;
    if (showVersion && !mcVersion.isEmpty()) {
        versionText = QString("Minecraft %1").arg(mcVersion);
    } else {
        versionText = "Minecraft";
    }

    if (showInstanceName && !instance->name().isEmpty()) {
        activity.details = instance->name();
        if (showLoader && !loaderStr.isEmpty()) {
            activity.state = QString("%1 (%2)").arg(versionText, loaderStr);
        } else {
            activity.state = versionText;
        }
    } else {
        activity.details = versionText;
        if (showLoader && !loaderStr.isEmpty()) {
            activity.state = QString("Playing %1").arg(loaderStr);
        } else {
            activity.state = "Playing Java Edition";
        }
    }

    if (!mcVersion.isEmpty()) {
        activity.largeText = QString("Minecraft %1").arg(mcVersion);
    } else {
        activity.largeText = "Minecraft: Java Edition";
    }

    setActivity(activity);
}
