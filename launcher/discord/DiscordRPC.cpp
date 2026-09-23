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
#include <QDir>
#include <QFile>
#include <QJsonDocument>
#include <QJsonObject>
#include <QJsonValue>
#include <QRegularExpression>

#include "Application.h"
#include "minecraft/MinecraftInstance.h"
#include "minecraft/PackProfile.h"
#include "settings/SettingsObject.h"

#ifdef Q_OS_WIN
#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <windows.h>
#ifdef OPTIONAL
#undef OPTIONAL
#endif
#endif

namespace {
#ifdef Q_OS_WIN
struct WindowSearchContext {
    DWORD targetPid = 0;
    HWND foundHwnd = nullptr;
};

BOOL CALLBACK enumWindowsProc(HWND hwnd, LPARAM lParam)
{
    auto* ctx = reinterpret_cast<WindowSearchContext*>(lParam);
    if (!IsWindowVisible(hwnd)) {
        return TRUE;
    }
    DWORD pid = 0;
    GetWindowThreadProcessId(hwnd, &pid);
    if (pid != ctx->targetPid) {
        return TRUE;
    }
    wchar_t titleBuf[256] = { 0 };
    int len = GetWindowTextW(hwnd, titleBuf, 255);
    if (len > 0) {
        ctx->foundHwnd = hwnd;
        return FALSE;
    }
    return TRUE;
}

QString getProcessWindowTitleWin32(qint64 gamePid, quintptr& cachedHandle)
{
    if (gamePid <= 0) {
        return {};
    }
    const DWORD targetPid = static_cast<DWORD>(gamePid);
    HWND hwnd = reinterpret_cast<HWND>(cachedHandle);
    if (hwnd != nullptr) {
        DWORD pid = 0;
        if (IsWindow(hwnd) && IsWindowVisible(hwnd) && GetWindowThreadProcessId(hwnd, &pid) && pid == targetPid) {
            wchar_t titleBuf[256] = { 0 };
            int len = GetWindowTextW(hwnd, titleBuf, 255);
            if (len > 0) {
                return QString::fromWCharArray(titleBuf, len);
            }
        }
        cachedHandle = 0;
    }

    WindowSearchContext ctx{ targetPid, nullptr };
    EnumWindows(enumWindowsProc, reinterpret_cast<LPARAM>(&ctx));
    if (ctx.foundHwnd != nullptr) {
        cachedHandle = reinterpret_cast<quintptr>(ctx.foundHwnd);
        wchar_t titleBuf[256] = { 0 };
        int len = GetWindowTextW(ctx.foundHwnd, titleBuf, 255);
        if (len > 0) {
            return QString::fromWCharArray(titleBuf, len);
        }
    }
    return {};
}
#endif

bool hasRelevantLogKeyword(const QString& line)
{
    return line.contains(QLatin1String("Connecting to"), Qt::CaseInsensitive) ||
           line.contains(QLatin1String("Starting integrated"), Qt::CaseInsensitive) ||
           line.contains(QLatin1String("ServerLevel["), Qt::CaseInsensitive) ||
           line.contains(QLatin1String("Loading level"), Qt::CaseInsensitive) ||
           line.contains(QLatin1String("Changing to dimension"), Qt::CaseInsensitive) ||
           line.contains(QLatin1String("Stopping"), Qt::CaseInsensitive) ||
           line.contains(QLatin1String("Disconnected"), Qt::CaseInsensitive) ||
           line.contains(QLatin1String("Disconnecting"), Qt::CaseInsensitive) ||
           line.contains(QLatin1String("Lost connection"), Qt::CaseInsensitive) ||
           line.contains(QLatin1String("Failed to connect"), Qt::CaseInsensitive) ||
           line.contains(QLatin1String("Realms"), Qt::CaseInsensitive) ||
           line.contains(QLatin1String("Sound engine started"), Qt::CaseInsensitive) ||
           line.contains(QLatin1String("OpenAL initialized"), Qt::CaseInsensitive) ||
           line.contains(QLatin1String("Backend library"), Qt::CaseInsensitive) ||
           line.contains(QLatin1String("LWJGL"), Qt::CaseInsensitive);
}
}  // namespace

DiscordRPC::DiscordRPC(QObject* parent) : QObject(parent)
{
    m_socket = new QLocalSocket(this);
    m_reconnectTimer = new QTimer(this);
    m_reconnectTimer->setSingleShot(false);
    m_ipcDelayTimer = new QTimer(this);
    m_ipcDelayTimer->setSingleShot(true);
    m_windowPollTimer = new QTimer(this);
    m_windowPollTimer->setSingleShot(false);

    connect(m_socket, &QLocalSocket::connected, this, &DiscordRPC::onConnected);
    connect(m_socket, &QLocalSocket::disconnected, this, &DiscordRPC::onDisconnected);
    connect(m_socket, &QLocalSocket::readyRead, this, &DiscordRPC::onReadyRead);
    connect(m_socket, &QLocalSocket::errorOccurred, this, &DiscordRPC::onErrorOccurred);
    connect(m_reconnectTimer, &QTimer::timeout, this, &DiscordRPC::onReconnectTimeout);
    connect(m_windowPollTimer, &QTimer::timeout, this, &DiscordRPC::pollGameWindow);
    connect(m_ipcDelayTimer, &QTimer::timeout, this, [this]() {
        if (m_hasActiveActivity) {
            m_ipcDelayedUntilWindow = false;
            if (!m_ready) {
                m_pipeIndex = 0;
                attemptConnection();
            } else {
                sendActivityPayload();
            }
        }
    });
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

QString DiscordRPC::sanitizeServerAddress(const QString& rawAddress)
{
    QString host = rawAddress.trimmed();
    if (host.isEmpty()) {
        return {};
    }

    // Remove trailing dot or bracketed IPv6 port
    if (host.startsWith('[')) {
        int closeBracket = host.indexOf(']');
        if (closeBracket > 1) {
            host = host.mid(1, closeBracket - 1);
        }
    } else {
        // If single colon (host:port), strip the port
        int firstColon = host.indexOf(':');
        int lastColon = host.lastIndexOf(':');
        if (firstColon != -1 && firstColon == lastColon) {
            host = host.left(firstColon).trimmed();
        }
    }

    while (host.endsWith('.')) {
        host.chop(1);
    }

    if (host.isEmpty() || host.compare(QLatin1String("localhost"), Qt::CaseInsensitive) == 0 || host.startsWith(QLatin1String("127.")) ||
        host == QLatin1String("::1") || host == QLatin1String("0.0.0.0")) {
        return QStringLiteral("127.0.*.*");
    }

    // Check IPv4 pattern (A.B.C.D) and mask the last two octets (A.B.*.*)
    static const QRegularExpression reIpv4(QStringLiteral(R"(^(\d{1,3})\.(\d{1,3})\.(\d{1,3})\.(\d{1,3})$)"));
    auto matchV4 = reIpv4.match(host);
    if (matchV4.hasMatch()) {
        return QString("%1.%2.*.*").arg(matchV4.captured(1), matchV4.captured(2));
    }

    // Check IPv6 address (contains colons) and mask suffix
    if (host.contains(':')) {
        QStringList parts = host.split(':', Qt::SkipEmptyParts);
        if (parts.size() >= 2) {
            return QString("%1:%2:*:*").arg(parts[0], parts[1]);
        }
        return QStringLiteral("*:*:*:*");
    }

    return host;
}

QString DiscordRPC::resolvePipePath(int index) const
{
#if defined(Q_OS_WIN)
    return QString("discord-ipc-%1").arg(index);
#else
    QStringList candidateDirs = { qEnvironmentVariable("XDG_RUNTIME_DIR"),
                                  qEnvironmentVariable("XDG_RUNTIME_DIR") + "/app/com.discordapp.Discord",
                                  qEnvironmentVariable("TMPDIR"),
                                  qEnvironmentVariable("TMP"),
                                  qEnvironmentVariable("TEMP"),
                                  "/tmp" };
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
                m_hasSentActivity = false;
                if (m_hasActiveActivity && !m_ipcDelayedUntilWindow) {
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
    m_hasSentActivity = false;
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
    m_hasSentActivity = false;
    if (m_hasActiveActivity) {
        m_pipeIndex = 0;
        if (!m_reconnectTimer->isActive()) {
            m_reconnectTimer->start(5000);
        }
    }
}

void DiscordRPC::onReconnectTimeout()
{
    if (m_hasActiveActivity && !m_ready && !m_ipcDelayedUntilWindow) {
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
    if (!m_ready || m_ipcDelayedUntilWindow) {
        return;
    }

    if (m_hasSentActivity && m_currentActivity == m_lastSentActivity) {
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
    argsObj["pid"] =
        m_currentActivity.processId > 0 ? m_currentActivity.processId : static_cast<qint64>(QCoreApplication::applicationPid());
    argsObj["activity"] = activityObj;

    QJsonObject packet;
    packet["cmd"] = "SET_ACTIVITY";
    packet["args"] = argsObj;
    packet["nonce"] = QString::number(++m_nonce);

    sendFrame(Opcode::Frame, packet);
    m_lastSentActivity = m_currentActivity;
    m_hasSentActivity = true;
}

void DiscordRPC::clearActivity()
{
    m_hasActiveActivity = false;
    m_hasSentActivity = false;
    m_ipcDelayedUntilWindow = false;
    m_windowDetected = false;
    m_windowDetectedTimestamp = 0;
    m_cachedWindowHandle = 0;
    m_isLanServer = false;
    m_inGameState = InGameState::MainMenu;
    m_gameStateDetail.clear();
    m_worldName.clear();
    m_instanceName.clear();
    m_mcVersion.clear();
    m_loaderStr.clear();
    m_modCount = 0;
    m_sessionStartTimestamp = 0;
    m_gamePid = 0;

    if (m_reconnectTimer) {
        m_reconnectTimer->stop();
    }
    if (m_ipcDelayTimer) {
        m_ipcDelayTimer->stop();
    }
    if (m_windowPollTimer) {
        m_windowPollTimer->stop();
    }

    if (m_ready && m_socket && m_socket->state() == QLocalSocket::ConnectedState) {
        QJsonObject argsObj;
        argsObj["pid"] =
            m_currentActivity.processId > 0 ? m_currentActivity.processId : static_cast<qint64>(QCoreApplication::applicationPid());
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

    m_instanceName = instance->name();
    m_gamePid = pid;
    m_sessionStartTimestamp = QDateTime::currentSecsSinceEpoch();
    // Start directly at MainMenu so Rich Presence never gets stuck on "Starting up..."
    m_inGameState = InGameState::MainMenu;
    m_gameStateDetail.clear();
    m_worldName.clear();
    m_isLanServer = false;
    m_windowDetected = false;
    m_windowDetectedTimestamp = 0;
    m_cachedWindowHandle = 0;
    m_hasSentActivity = false;

    m_mcVersion.clear();
    m_loaderStr.clear();
    m_modCount = QDir(mcInstance->modsRoot()).entryList({ "*.jar" }, QDir::Files).size();

    if (auto* profile = mcInstance->getPackProfile()) {
        m_mcVersion = profile->getComponentVersion("net.minecraft");
        auto fabricVer = profile->getComponentVersion("net.fabricmc.fabric-loader");
        auto neoforgeVer = profile->getComponentVersion("net.neoforged.neoforge");
        auto forgeVer = profile->getComponentVersion("net.minecraftforge");
        auto quiltVer = profile->getComponentVersion("org.quiltmc.quilt-loader");

        if (!fabricVer.isEmpty()) {
            m_loaderStr = QStringLiteral("Fabric");
        } else if (!neoforgeVer.isEmpty()) {
            m_loaderStr = QStringLiteral("NeoForge");
        } else if (!forgeVer.isEmpty()) {
            m_loaderStr = QStringLiteral("Forge");
        } else if (!quiltVer.isEmpty()) {
            m_loaderStr = QStringLiteral("Quilt");
        }
    }

    m_hasActiveActivity = true;
    m_windowPollTimer->start(2500);

    // When native Discord process detection is enabled, let Discord's 5s RunningGameStore scanner
    // detect javaw.exe's GLFW window first so Official Play History (1402418491272986635) is permanently logged
    // before overlaying IPC Rich Presence.
    if (APPLICATION->settings()->get("DiscordRPCProcessDetection").toBool()) {
        m_ipcDelayedUntilWindow = true;
        rebuildActivity();
        m_ipcDelayTimer->start(12000);
    } else {
        m_ipcDelayedUntilWindow = false;
        rebuildActivity();
        if (!m_ready) {
            attemptConnection();
        }
    }
}

void DiscordRPC::pollGameWindow()
{
    if (!m_hasActiveActivity || m_gamePid <= 0) {
        return;
    }

#ifdef Q_OS_WIN
    QString title = getProcessWindowTitleWin32(m_gamePid, m_cachedWindowHandle);
    if (title.isEmpty()) {
        return;
    }

    if (!m_windowDetected) {
        m_windowDetected = true;
        m_windowDetectedTimestamp = QDateTime::currentSecsSinceEpoch();
        if (m_ipcDelayedUntilWindow) {
            // GLFW window is now visible; wait 5.5s so Discord's 5s RunningGameStore scanner
            // registers Official Play History (1402418491272986635), then attach Rich Presence.
            m_ipcDelayTimer->start(5500);
        }
    }

    if (!APPLICATION->settings()->get("DiscordRPCShowGameState").toBool()) {
        return;
    }

    // Parse Vanilla/Modded Minecraft window title transitions:
    // - "Minecraft 1.21.1 - Singleplayer"
    // - "Minecraft 1.21.1 - Multiplayer (3rd-party Server)"
    // - "Minecraft 1.21.1 - Multiplayer (LAN)"
    // - "Minecraft 1.21.1 - Multiplayer (Realms)"
    // - "Minecraft 1.21.1" (Main Menu)
    if (title.contains(QLatin1String(" - Singleplayer"), Qt::CaseInsensitive)) {
        m_isLanServer = false;
        if (m_inGameState != InGameState::Singleplayer) {
            QString dim = m_gameStateDetail.isEmpty() ? QStringLiteral("Overworld") : m_gameStateDetail;
            updateInGameState(InGameState::Singleplayer, dim);
        }
    } else if (title.contains(QLatin1String(" - Multiplayer (Realms)"), Qt::CaseInsensitive)) {
        m_isLanServer = false;
        updateInGameState(InGameState::Realms);
    } else if (title.contains(QLatin1String(" - Multiplayer (LAN)"), Qt::CaseInsensitive)) {
        if (!m_isLanServer || m_inGameState != InGameState::Multiplayer) {
            m_isLanServer = true;
            updateInGameState(InGameState::Multiplayer, m_gameStateDetail);
        }
    } else if (title.contains(QLatin1String(" - Multiplayer"), Qt::CaseInsensitive)) {
        if (m_isLanServer || m_inGameState != InGameState::Multiplayer) {
            m_isLanServer = false;
            updateInGameState(InGameState::Multiplayer, m_gameStateDetail);
        }
    } else if (title.startsWith(QLatin1String("Minecraft"), Qt::CaseInsensitive) &&
               !title.contains(QLatin1String(" - "), Qt::CaseInsensitive)) {
        if (m_inGameState != InGameState::MainMenu) {
            m_worldName.clear();
            m_isLanServer = false;
            updateInGameState(InGameState::MainMenu);
        }
    }
#endif
}

void DiscordRPC::rebuildActivity()
{
    if (!m_hasActiveActivity) {
        return;
    }

    const bool showInstanceName = APPLICATION->settings()->get("DiscordRPCShowInstanceName").toBool();
    const bool showVersion = APPLICATION->settings()->get("DiscordRPCShowVersion").toBool();
    const bool showLoader = APPLICATION->settings()->get("DiscordRPCShowModLoader").toBool();
    const bool showGameState = APPLICATION->settings()->get("DiscordRPCShowGameState").toBool();
    const bool showServerAddress = APPLICATION->settings()->get("DiscordRPCShowServerAddress").toBool();

    DiscordActivity activity;
    activity.processId = m_gamePid;
    activity.startTimestamp = m_sessionStartTimestamp;

    // Build concise Line 2 (state): "<InstanceName> • <Loader> <Version> (<N> mods)"
    QStringList metaParts;
    if (showLoader && !m_loaderStr.isEmpty()) {
        if (showVersion && !m_mcVersion.isEmpty()) {
            metaParts << QString("%1 %2").arg(m_loaderStr, m_mcVersion);
        } else {
            metaParts << m_loaderStr;
        }
    } else if (showVersion && !m_mcVersion.isEmpty()) {
        metaParts << QString("Minecraft %1").arg(m_mcVersion);
    }

    if (showLoader && m_modCount > 0) {
        metaParts << QString("%1 %2").arg(m_modCount).arg(m_modCount == 1 ? "mod" : "mods");
    }

    QString techSummary = metaParts.join(QStringLiteral(" • "));
    QString stateLine;
    if (showInstanceName && !m_instanceName.isEmpty()) {
        if (!techSummary.isEmpty() && m_instanceName.compare(techSummary, Qt::CaseInsensitive) != 0 &&
            m_instanceName.compare(m_mcVersion, Qt::CaseInsensitive) != 0) {
            stateLine = QString("%1 • %2").arg(m_instanceName, techSummary);
        } else if (!techSummary.isEmpty()) {
            stateLine = techSummary;
        } else {
            stateLine = m_instanceName;
        }
    } else {
        stateLine = !techSummary.isEmpty() ? techSummary : QStringLiteral("Minecraft: Java Edition");
    }

    if (showGameState) {
        switch (m_inGameState) {
            case InGameState::Starting:
            case InGameState::MainMenu:
                activity.details = QStringLiteral("In Main Menu");
                activity.state = stateLine;
                break;
            case InGameState::Singleplayer:
                if (!m_worldName.isEmpty() && !m_gameStateDetail.isEmpty()) {
                    activity.details = QString("Singleplayer: %1 (%2)").arg(m_worldName, m_gameStateDetail);
                } else if (!m_worldName.isEmpty()) {
                    activity.details = QString("Singleplayer: %1").arg(m_worldName);
                } else if (!m_gameStateDetail.isEmpty()) {
                    activity.details = QString("Playing Singleplayer (%1)").arg(m_gameStateDetail);
                } else {
                    activity.details = QStringLiteral("Playing Singleplayer");
                }
                activity.state = stateLine;
                break;
            case InGameState::Multiplayer: {
                if (m_isLanServer) {
                    activity.details = QStringLiteral("Playing Multiplayer (LAN)");
                } else if (showServerAddress && !m_gameStateDetail.isEmpty()) {
                    QString safeHost = sanitizeServerAddress(m_gameStateDetail);
                    if (!safeHost.isEmpty()) {
                        activity.details = QString("Playing Multiplayer on %1").arg(safeHost);
                    } else {
                        activity.details = QStringLiteral("Playing Multiplayer");
                    }
                } else {
                    activity.details = QStringLiteral("Playing Multiplayer");
                }
                activity.state = stateLine;
                break;
            }
            case InGameState::Realms:
                activity.details = QStringLiteral("Playing on Realms");
                activity.state = stateLine;
                break;
        }
    } else {
        if (showInstanceName && !m_instanceName.isEmpty()) {
            activity.details = m_instanceName;
            activity.state = !techSummary.isEmpty() ? techSummary : QStringLiteral("Playing Minecraft");
        } else {
            activity.details = !techSummary.isEmpty() ? techSummary : QStringLiteral("Minecraft");
            activity.state = QStringLiteral("Playing Java Edition");
        }
    }

    activity.largeText = !techSummary.isEmpty() ? techSummary : QStringLiteral("Minecraft: Java Edition");

    m_currentActivity = activity;
    if (m_ready && !m_ipcDelayedUntilWindow) {
        sendActivityPayload();
    }
}

void DiscordRPC::updateInGameState(InGameState state, const QString& detail)
{
    if (m_inGameState == state && m_gameStateDetail == detail) {
        return;
    }
    m_inGameState = state;
    m_gameStateDetail = detail;
    rebuildActivity();
}

void DiscordRPC::handleLogLines(const QStringList& lines)
{
    if (!m_hasActiveActivity) {
        return;
    }

    static const QRegularExpression reWindowInit(QStringLiteral(R"(Backend library|LWJGL Version|OpenAL initialized|Sound engine started)"),
                                                 QRegularExpression::CaseInsensitiveOption);
    static const QRegularExpression reConnect(QStringLiteral(R"((?:Connecting to|Connecting to server)\s+([^\s,]+)(?:,\s*(\d+))?)"),
                                              QRegularExpression::CaseInsensitiveOption);
    static const QRegularExpression reRealms(QStringLiteral(R"(Connecting to realms|RealmsClient)"),
                                             QRegularExpression::CaseInsensitiveOption);
    static const QRegularExpression reSingleplayer(QStringLiteral(R"(Starting integrated minecraft server|Loaded \d+ advancements)"),
                                                   QRegularExpression::CaseInsensitiveOption);
    static const QRegularExpression reWorldName(QStringLiteral(R"((?:ServerLevel\[([^\]]+)\]|Loading level ['"]([^'"]+)['"]))"),
                                                QRegularExpression::CaseInsensitiveOption);
    static const QRegularExpression reDimension(QStringLiteral(R"(Changing to dimension minecraft:([a-z_]+))"),
                                                QRegularExpression::CaseInsensitiveOption);
    static const QRegularExpression reDisconnectOrMenu(
        QStringLiteral(
            R"(Sound engine started|OpenAL initialized|Stopping integrated server|Stopping worker threads|Disconnected from server|Disconnecting from server|Lost connection|Failed to connect to server|AnnotatedConnectException|UnknownHostException)"),
        QRegularExpression::CaseInsensitiveOption);

    const bool showGameState = APPLICATION->settings()->get("DiscordRPCShowGameState").toBool();

    for (const auto& line : lines) {
        if (line.isEmpty() || !hasRelevantLogKeyword(line)) {
            continue;
        }

        // Once Minecraft creates its GLFW window, schedule IPC connection 5.5s later so Discord's 5s
        // RunningGameStore loop registers Official Play History (1402418491272986635) first.
        if (m_ipcDelayedUntilWindow && !m_windowDetected && reWindowInit.match(line).hasMatch()) {
            m_windowDetected = true;
            m_windowDetectedTimestamp = QDateTime::currentSecsSinceEpoch();
            m_ipcDelayTimer->start(5500);
        }

        if (!showGameState) {
            continue;
        }

        // Extract Singleplayer World Name
        auto matchWorld = reWorldName.match(line);
        if (matchWorld.hasMatch()) {
            QString world = !matchWorld.captured(1).isEmpty() ? matchWorld.captured(1).trimmed() : matchWorld.captured(2).trimmed();
            if (!world.isEmpty() && m_worldName != world) {
                m_worldName = world;
                if (m_inGameState == InGameState::Singleplayer) {
                    rebuildActivity();
                }
            }
        }

        // Realms
        if (reRealms.match(line).hasMatch()) {
            m_isLanServer = false;
            updateInGameState(InGameState::Realms);
            continue;
        }

        // Multiplayer
        auto matchConnect = reConnect.match(line);
        if (matchConnect.hasMatch()) {
            QString host = matchConnect.captured(1).trimmed();
            if (!host.isEmpty() && host.compare(QLatin1String("realms"), Qt::CaseInsensitive) != 0) {
                m_isLanServer = false;
                updateInGameState(InGameState::Multiplayer, host);
                continue;
            }
        }

        // Singleplayer
        if (reSingleplayer.match(line).hasMatch()) {
            m_isLanServer = false;
            QString dim = m_gameStateDetail.isEmpty() ? QStringLiteral("Overworld") : m_gameStateDetail;
            updateInGameState(InGameState::Singleplayer, dim);
            continue;
        }

        // Dimension
        auto matchDim = reDimension.match(line);
        if (matchDim.hasMatch()) {
            QString dim = matchDim.captured(1);
            if (dim == QLatin1String("the_nether")) {
                dim = QStringLiteral("The Nether");
            } else if (dim == QLatin1String("the_end")) {
                dim = QStringLiteral("The End");
            } else {
                dim = QStringLiteral("Overworld");
            }
            if (m_inGameState == InGameState::Singleplayer) {
                updateInGameState(InGameState::Singleplayer, dim);
            }
            continue;
        }

        // Return to Main Menu on disconnect or server stop
        if (reDisconnectOrMenu.match(line).hasMatch()) {
            m_worldName.clear();
            m_isLanServer = false;
            updateInGameState(InGameState::MainMenu);
            continue;
        }
    }
}
