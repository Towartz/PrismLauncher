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
#include <QFileInfo>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QJsonValue>
#include <QRandomGenerator>
#include <QRegularExpression>
#include <QStandardPaths>

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
#include <bcrypt.h>
#include <wincrypt.h>
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

bool isModOrChatNoiseLine(const QString& line)
{
    return line.contains(QLatin1String("[CHAT]"), Qt::CaseInsensitive) || line.contains(QLatin1String("voicechat"), Qt::CaseInsensitive) ||
           line.contains(QLatin1String("voice server"), Qt::CaseInsensitive) ||
           line.contains(QLatin1String("voice chat"), Qt::CaseInsensitive) ||
           line.contains(QLatin1String("plasmovoice"), Qt::CaseInsensitive) ||
           line.contains(QLatin1String("[Essential"), Qt::CaseInsensitive) ||
           line.contains(QLatin1String("CraftPresence"), Qt::CaseInsensitive) ||
           line.contains(QLatin1String("websocket"), Qt::CaseInsensitive) || line.contains(QLatin1String("telemetry"), Qt::CaseInsensitive);
}

QString extractHostFromSocketAddr(const QString& rawHost)
{
    QString host = rawHost.trimmed();
    // Java InetSocketAddress.toString() formats as "hostname/ip" or "/ip"
    int slashIdx = host.indexOf('/');
    if (slashIdx != -1) {
        QString beforeSlash = host.left(slashIdx).trimmed();
        QString afterSlash = host.mid(slashIdx + 1).trimmed();
        host = !beforeSlash.isEmpty() ? beforeSlash : afterSlash;
    }
    if (host.startsWith('[')) {
        int closeBracket = host.indexOf(']');
        if (closeBracket > 1) {
            host = host.mid(1, closeBracket - 1);
        }
    } else {
        int firstColon = host.indexOf(':');
        int lastColon = host.lastIndexOf(':');
        if (firstColon != -1 && firstColon == lastColon) {
            host = host.left(firstColon).trimmed();
        }
    }
    while (host.endsWith('.')) {
        host.chop(1);
    }
    return host;
}

bool isValidMinecraftServerHost(const QString& rawHost, const QString& portStr)
{
    bool portOk = false;
    int port = portStr.toInt(&portOk);
    if (!portOk || port < 1 || port > 65535) {
        return false;
    }

    QString host = extractHostFromSocketAddr(rawHost);
    if (host.isEmpty()) {
        return false;
    }
    if (host.compare(QLatin1String("localhost"), Qt::CaseInsensitive) == 0) {
        return true;
    }
    // IPv6
    if (host.contains(':')) {
        return true;
    }
    // Must contain at least one dot (domain name or IPv4), never a bare word like "voice"
    if (!host.contains('.')) {
        return false;
    }
    static const QRegularExpression reValidHost(QStringLiteral(R"(^(?:(?:\d{1,3}\.){3}\d{1,3}|(?:[a-zA-Z0-9_-]+\.)+[a-zA-Z]{2,})$)"));
    return reValidHost.match(host).hasMatch();
}

bool hasRelevantLogKeyword(const QString& line)
{
    if (isModOrChatNoiseLine(line)) {
        return false;
    }
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
    m_gatewaySocket = new QSslSocket(this);
    m_gatewayHeartbeatTimer = new QTimer(this);
    m_gatewayHeartbeatTimer->setSingleShot(false);
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
    connect(m_gatewaySocket, &QSslSocket::encrypted, this, &DiscordRPC::onGatewayEncrypted);
    connect(m_gatewaySocket, &QSslSocket::readyRead, this, &DiscordRPC::onGatewayReadyRead);
    connect(m_gatewaySocket, &QSslSocket::disconnected, this, &DiscordRPC::onGatewayDisconnected);
    connect(m_gatewayHeartbeatTimer, &QTimer::timeout, this, &DiscordRPC::onGatewayHeartbeat);
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
    QString host = extractHostFromSocketAddr(rawAddress);
    if (host.isEmpty()) {
        return {};
    }

    if (host.compare(QLatin1String("localhost"), Qt::CaseInsensitive) == 0 || host.startsWith(QLatin1String("127.")) ||
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

QJsonObject DiscordRPC::buildActivityJson(bool forGateway) const
{
    QJsonObject activityObj;
    if (forGateway) {
        activityObj["name"] = QStringLiteral("Minecraft");
        activityObj["type"] = 0;
        activityObj["application_id"] = getEffectiveClientId();
    }
    activityObj["platform"] = QStringLiteral("embedded");
    activityObj["supported_platforms"] = QJsonArray{ QStringLiteral("desktop"), QStringLiteral("embedded") };
    activityObj["instance"] = true;
    activityObj["flags"] = 1;

    if (!m_currentActivity.details.isEmpty()) {
        activityObj["details"] = m_currentActivity.details;
    }
    if (!m_currentActivity.state.isEmpty()) {
        activityObj["state"] = m_currentActivity.state;
    }
    if (m_currentActivity.startTimestamp > 0) {
        QJsonObject timestampsObj;
        timestampsObj["start"] = forGateway ? (m_currentActivity.startTimestamp * 1000) : m_currentActivity.startTimestamp;
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
    return activityObj;
}

void DiscordRPC::sendActivityPayload()
{
    if (!m_ready || m_ipcDelayedUntilWindow) {
        return;
    }

    if (m_hasSentActivity && m_currentActivity == m_lastSentActivity) {
        return;
    }

    QJsonObject argsObj;
    argsObj["pid"] =
        m_currentActivity.processId > 0 ? m_currentActivity.processId : static_cast<qint64>(QCoreApplication::applicationPid());
    argsObj["activity"] = buildActivityJson(false);

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
    m_serverHost.clear();
    m_lastConnectAttemptMs = 0;
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

    disconnectEmbeddedGateway();

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

void DiscordRPC::setActivityForInstance(BaseInstance* instance, qint64 pid, const QString& initialServerAddress)
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
    m_gameStateDetail.clear();
    m_serverHost = sanitizeServerAddress(initialServerAddress);
    m_lastConnectAttemptMs = !m_serverHost.isEmpty() ? QDateTime::currentMSecsSinceEpoch() : 0;
    // Start at Multiplayer if launched directly into a server, otherwise MainMenu
    m_inGameState = !m_serverHost.isEmpty() ? InGameState::Multiplayer : InGameState::MainMenu;
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

    if (APPLICATION->settings()->get("DiscordRPCEmbeddedStatus").toBool()) {
        connectEmbeddedGateway();
    }
}

void DiscordRPC::refreshActivity()
{
    if (!APPLICATION->settings()->get("DiscordRPCEnabled").toBool()) {
        if (m_hasActiveActivity) {
            clearActivity();
        }
        return;
    }
    if (m_hasActiveActivity) {
        m_hasSentActivity = false;
        rebuildActivity();
        if (APPLICATION->settings()->get("DiscordRPCEmbeddedStatus").toBool()) {
            connectEmbeddedGateway();
        } else {
            disconnectEmbeddedGateway();
        }
    }
}

QString DiscordRPC::discoverLocalDiscordToken()
{
    QString overrideToken = APPLICATION->settings()->get("DiscordRPCEmbeddedToken").toString().trimmed();
    if (overrideToken.startsWith('"') && overrideToken.endsWith('"') && overrideToken.size() > 2) {
        overrideToken = overrideToken.mid(1, overrideToken.size() - 2).trimmed();
    }
    if (!overrideToken.isEmpty()) {
        return overrideToken;
    }

    static const QRegularExpression reTokenValid(QStringLiteral(R"(^[A-Za-z0-9_-]{20,}\.[A-Za-z0-9_-]{4,}\.[A-Za-z0-9_-]{20,}$)"));
    static const QRegularExpression rePlainToken(QStringLiteral(R"((?:mfa\.[\w-]{80,}|[\w-]{24,28}\.[\w-]{6,7}\.[\w-]{27,}))"));

    QStringList baseDirs;
#ifdef Q_OS_WIN
    const QString appData = qEnvironmentVariable("APPDATA");
    if (!appData.isEmpty()) {
        baseDirs << appData;
    }
#elif defined(Q_OS_MACOS)
    baseDirs << (QDir::homePath() + QStringLiteral("/Library/Application Support"));
#else
    const QString xdgConfig = qEnvironmentVariable("XDG_CONFIG_HOME", QDir::homePath() + QStringLiteral("/.config"));
    baseDirs << xdgConfig;
    baseDirs << (QDir::homePath() + QStringLiteral("/.var/app/com.discordapp.Discord/config"));
    baseDirs << (QDir::homePath() + QStringLiteral("/.var/app/dev.vencord.Vesktop/config"));
#endif

    const QStringList clientFolders = {
        QStringLiteral("discord"), QStringLiteral("discordcanary"), QStringLiteral("discordptb"),
        QStringLiteral("vesktop"), QStringLiteral("equibop"),       QStringLiteral("VencordDesktop"),
    };

    for (const QString& baseDir : baseDirs) {
        for (const QString& clientFolder : clientFolders) {
            const QString clientPath = QDir(baseDir).filePath(clientFolder);
            const QDir levelDbDir(QDir(clientPath).filePath(QStringLiteral("Local Storage/leveldb")));
            if (!levelDbDir.exists()) {
                continue;
            }

#ifdef Q_OS_WIN
            QByteArray masterKey;
            QFile localStateFile(QDir(clientPath).filePath(QStringLiteral("Local State")));
            if (localStateFile.open(QIODevice::ReadOnly)) {
                const QJsonDocument stateDoc = QJsonDocument::fromJson(localStateFile.readAll());
                localStateFile.close();
                const QString encKeyB64 =
                    stateDoc.object().value(QStringLiteral("os_crypt")).toObject().value(QStringLiteral("encrypted_key")).toString();
                if (!encKeyB64.isEmpty()) {
                    const QByteArray rawKey = QByteArray::fromBase64(encKeyB64.toLatin1());
                    if (rawKey.size() > 5 && rawKey.startsWith("DPAPI")) {
                        DATA_BLOB inBlob{};
                        inBlob.pbData = reinterpret_cast<BYTE*>(const_cast<char*>(rawKey.constData() + 5));
                        inBlob.cbData = static_cast<DWORD>(rawKey.size() - 5);
                        DATA_BLOB outBlob{};
                        if (CryptUnprotectData(&inBlob, nullptr, nullptr, nullptr, nullptr, 0, &outBlob)) {
                            masterKey = QByteArray(reinterpret_cast<const char*>(outBlob.pbData), static_cast<int>(outBlob.cbData));
                            LocalFree(outBlob.pbData);
                        }
                    }
                }
            }
#endif

            const QFileInfoList dbFiles =
                levelDbDir.entryInfoList({ QStringLiteral("*.ldb"), QStringLiteral("*.log") }, QDir::Files, QDir::Time);
            for (const QFileInfo& fi : dbFiles) {
                QFile dbFile(fi.absoluteFilePath());
                if (!dbFile.open(QIODevice::ReadOnly)) {
                    continue;
                }
                const QByteArray rawBytes = dbFile.readAll();
                dbFile.close();
                const QString content = QString::fromLatin1(rawBytes);

#ifdef Q_OS_WIN
                if (masterKey.size() == 32) {
                    static const QRegularExpression reEncToken(QStringLiteral(R"(dQw4w9WgXcQ:([A-Za-z0-9+/=]+))"));
                    auto it = reEncToken.globalMatch(content);
                    QString lastDecryptedToken;
                    while (it.hasNext()) {
                        const QString b64Cipher = it.next().captured(1);
                        const QByteArray encData = QByteArray::fromBase64(b64Cipher.toLatin1());
                        if (encData.size() <= 31 || !encData.startsWith("v10")) {
                            continue;
                        }
                        const QByteArray nonce = encData.mid(3, 12);
                        const QByteArray ciphertext = encData.mid(15, encData.size() - 31);
                        const QByteArray authTag = encData.right(16);

                        BCRYPT_ALG_HANDLE hAlg = nullptr;
                        if (BCryptOpenAlgorithmProvider(&hAlg, BCRYPT_AES_ALGORITHM, nullptr, 0) == 0) {
                            if (BCryptSetProperty(hAlg, BCRYPT_CHAINING_MODE,
                                                  reinterpret_cast<PUCHAR>(const_cast<wchar_t*>(BCRYPT_CHAIN_MODE_GCM)),
                                                  sizeof(BCRYPT_CHAIN_MODE_GCM), 0) == 0) {
                                BCRYPT_KEY_HANDLE hKey = nullptr;
                                if (BCryptGenerateSymmetricKey(hAlg, &hKey, nullptr, 0,
                                                               reinterpret_cast<PUCHAR>(const_cast<char*>(masterKey.constData())),
                                                               static_cast<ULONG>(masterKey.size()), 0) == 0) {
                                    BCRYPT_AUTHENTICATED_CIPHER_MODE_INFO authInfo;
                                    memset(&authInfo, 0, sizeof(authInfo));
                                    authInfo.cbSize = sizeof(BCRYPT_AUTHENTICATED_CIPHER_MODE_INFO);
                                    authInfo.dwInfoVersion = BCRYPT_AUTHENTICATED_CIPHER_MODE_INFO_VERSION;
                                    authInfo.pbNonce = reinterpret_cast<PUCHAR>(const_cast<char*>(nonce.constData()));
                                    authInfo.cbNonce = static_cast<ULONG>(nonce.size());
                                    authInfo.pbTag = reinterpret_cast<PUCHAR>(const_cast<char*>(authTag.constData()));
                                    authInfo.cbTag = static_cast<ULONG>(authTag.size());

                                    QByteArray plainText(ciphertext.size(), Qt::Uninitialized);
                                    ULONG outLen = 0;
                                    const NTSTATUS status = BCryptDecrypt(
                                        hKey, reinterpret_cast<PUCHAR>(const_cast<char*>(ciphertext.constData())),
                                        static_cast<ULONG>(ciphertext.size()), &authInfo, nullptr, 0,
                                        reinterpret_cast<PUCHAR>(plainText.data()), static_cast<ULONG>(plainText.size()), &outLen, 0);
                                    if (status == 0 && outLen > 0) {
                                        const QString candidate =
                                            QString::fromUtf8(plainText.constData(), static_cast<int>(outLen)).trimmed();
                                        if (reTokenValid.match(candidate).hasMatch()) {
                                            lastDecryptedToken = candidate;
                                        }
                                    }
                                    BCryptDestroyKey(hKey);
                                }
                            }
                            BCryptCloseAlgorithmProvider(hAlg, 0);
                        }
                    }
                    if (!lastDecryptedToken.isEmpty()) {
                        return lastDecryptedToken;
                    }
                }
#endif

                auto plainIt = rePlainToken.globalMatch(content);
                QString lastPlainToken;
                while (plainIt.hasNext()) {
                    const QString candidate = plainIt.next().captured(0).trimmed();
                    if (reTokenValid.match(candidate).hasMatch()) {
                        lastPlainToken = candidate;
                    }
                }
                if (!lastPlainToken.isEmpty()) {
                    return lastPlainToken;
                }
            }
        }
    }
    return {};
}

void DiscordRPC::connectEmbeddedGateway()
{
    if (!m_hasActiveActivity || !APPLICATION->settings()->get("DiscordRPCEmbeddedStatus").toBool()) {
        return;
    }

    if (m_gatewaySocket->state() == QAbstractSocket::ConnectedState || m_gatewaySocket->state() == QAbstractSocket::ConnectingState) {
        if (m_gatewayReady) {
            sendGatewayPresenceUpdate();
        }
        return;
    }

    m_embeddedToken = discoverLocalDiscordToken();
    if (m_embeddedToken.isEmpty()) {
        return;
    }

    m_gatewayBuffer.clear();
    m_gatewayUpgraded = false;
    m_gatewayReady = false;
    m_gatewaySeq = -1;
    m_gatewaySocket->connectToHostEncrypted(QStringLiteral("gateway.discord.gg"), 443);
}

void DiscordRPC::disconnectEmbeddedGateway()
{
    if (m_gatewayHeartbeatTimer) {
        m_gatewayHeartbeatTimer->stop();
    }
    if (m_gatewaySocket && m_gatewaySocket->state() != QAbstractSocket::UnconnectedState) {
        if (m_gatewayUpgraded) {
            // Send RFC 6455 Close frame (Opcode 0x8) with status 1000
            QByteArray closePayload;
            closePayload.append(static_cast<char>(0x03));
            closePayload.append(static_cast<char>(0xE8));
            sendGatewayWebSocketFrame(0x8, closePayload);
            m_gatewaySocket->flush();
        }
        m_gatewaySocket->disconnectFromHost();
        if (m_gatewaySocket->state() != QAbstractSocket::UnconnectedState) {
            m_gatewaySocket->abort();
        }
    }
    m_gatewayBuffer.clear();
    m_gatewayUpgraded = false;
    m_gatewayReady = false;
    m_gatewaySeq = -1;
}

void DiscordRPC::onGatewayEncrypted()
{
    QByteArray randomBytes(16, Qt::Uninitialized);
    for (int i = 0; i < 16; ++i) {
        randomBytes[i] = static_cast<char>(QRandomGenerator::global()->bounded(256));
    }
    const QByteArray secKey = randomBytes.toBase64();

    QByteArray handshake;
    handshake.append("GET /?v=10&encoding=json HTTP/1.1\r\n");
    handshake.append("Host: gateway.discord.gg\r\n");
    handshake.append("Upgrade: websocket\r\n");
    handshake.append("Connection: Upgrade\r\n");
    handshake.append("Sec-WebSocket-Key: " + secKey + "\r\n");
    handshake.append("Sec-WebSocket-Version: 13\r\n");
    handshake.append("User-Agent: Discord-Embedded/1.0\r\n\r\n");

    m_gatewaySocket->write(handshake);
    m_gatewaySocket->flush();
}

void DiscordRPC::sendGatewayWebSocketFrame(quint8 opcode, const QByteArray& payload)
{
    if (!m_gatewaySocket || m_gatewaySocket->state() != QAbstractSocket::ConnectedState || !m_gatewayUpgraded) {
        return;
    }

    QByteArray frame;
    frame.append(static_cast<char>(0x80 | (opcode & 0x0F)));

    const qint64 len = payload.size();
    if (len <= 125) {
        frame.append(static_cast<char>(0x80 | static_cast<quint8>(len)));
    } else if (len <= 65535) {
        frame.append(static_cast<char>(0x80 | 126));
        frame.append(static_cast<char>((len >> 8) & 0xFF));
        frame.append(static_cast<char>(len & 0xFF));
    } else {
        frame.append(static_cast<char>(0x80 | 127));
        for (int shift = 56; shift >= 0; shift -= 8) {
            frame.append(static_cast<char>((len >> shift) & 0xFF));
        }
    }

    quint8 mask[4];
    const quint32 randVal = QRandomGenerator::global()->generate();
    mask[0] = static_cast<quint8>((randVal >> 24) & 0xFF);
    mask[1] = static_cast<quint8>((randVal >> 16) & 0xFF);
    mask[2] = static_cast<quint8>((randVal >> 8) & 0xFF);
    mask[3] = static_cast<quint8>(randVal & 0xFF);
    frame.append(reinterpret_cast<const char*>(mask), 4);

    QByteArray maskedPayload(payload.size(), Qt::Uninitialized);
    for (int i = 0; i < payload.size(); ++i) {
        maskedPayload[i] = static_cast<char>(static_cast<quint8>(payload[i]) ^ mask[i % 4]);
    }
    frame.append(maskedPayload);

    m_gatewaySocket->write(frame);
    m_gatewaySocket->flush();
}

void DiscordRPC::onGatewayReadyRead()
{
    m_gatewayBuffer.append(m_gatewaySocket->readAll());

    if (!m_gatewayUpgraded) {
        const int headerEnd = m_gatewayBuffer.indexOf("\r\n\r\n");
        if (headerEnd == -1) {
            return;
        }
        const QByteArray header = m_gatewayBuffer.left(headerEnd);
        m_gatewayBuffer.remove(0, headerEnd + 4);
        if (!header.contains("101")) {
            disconnectEmbeddedGateway();
            return;
        }
        m_gatewayUpgraded = true;
    }

    while (m_gatewayBuffer.size() >= 2) {
        const quint8 byte0 = static_cast<quint8>(m_gatewayBuffer[0]);
        const quint8 byte1 = static_cast<quint8>(m_gatewayBuffer[1]);
        const quint8 opcode = byte0 & 0x0F;
        const bool masked = (byte1 & 0x80) != 0;
        quint64 payloadLen = byte1 & 0x7F;
        int headerOffset = 2;

        if (payloadLen == 126) {
            if (m_gatewayBuffer.size() < 4) {
                return;
            }
            payloadLen = (static_cast<quint64>(static_cast<quint8>(m_gatewayBuffer[2])) << 8) |
                         static_cast<quint64>(static_cast<quint8>(m_gatewayBuffer[3]));
            headerOffset = 4;
        } else if (payloadLen == 127) {
            if (m_gatewayBuffer.size() < 10) {
                return;
            }
            payloadLen = 0;
            for (int i = 0; i < 8; ++i) {
                payloadLen = (payloadLen << 8) | static_cast<quint8>(m_gatewayBuffer[2 + i]);
            }
            headerOffset = 10;
        }

        if (masked) {
            headerOffset += 4;
        }

        if (static_cast<quint64>(m_gatewayBuffer.size()) < static_cast<quint64>(headerOffset) + payloadLen) {
            return;
        }

        QByteArray payload = m_gatewayBuffer.mid(headerOffset, static_cast<int>(payloadLen));
        if (masked) {
            const char* maskKey = m_gatewayBuffer.constData() + headerOffset - 4;
            for (int i = 0; i < payload.size(); ++i) {
                payload[i] = static_cast<char>(static_cast<quint8>(payload[i]) ^ static_cast<quint8>(maskKey[i % 4]));
            }
        }
        m_gatewayBuffer.remove(0, headerOffset + static_cast<int>(payloadLen));

        if (opcode == 0x1) {
            handleGatewayJson(payload);
        } else if (opcode == 0x8) {
            disconnectEmbeddedGateway();
            return;
        } else if (opcode == 0x9) {
            sendGatewayWebSocketFrame(0xA, payload);
        }
    }
}

void DiscordRPC::handleGatewayJson(const QByteArray& jsonBytes)
{
    const QJsonDocument doc = QJsonDocument::fromJson(jsonBytes);
    if (!doc.isObject()) {
        return;
    }
    const QJsonObject root = doc.object();
    if (!root.value(QStringLiteral("s")).isNull()) {
        m_gatewaySeq = root.value(QStringLiteral("s")).toVariant().toLongLong();
    }

    const int op = root.value(QStringLiteral("op")).toInt(-1);
    if (op == 10) {
        // Opcode 10: Hello -> start heartbeat & send IDENTIFY with "Discord Embedded"
        const int intervalMs = root.value(QStringLiteral("d")).toObject().value(QStringLiteral("heartbeat_interval")).toInt(41250);
        m_gatewayHeartbeatTimer->start(qMax(10000, intervalMs));

        QJsonObject props;
        props["os"] = QStringLiteral("Windows");
        props["browser"] = QStringLiteral("Discord Embedded");
        props["device"] = QStringLiteral("Discord Embedded");

        QJsonObject presence;
        presence["activities"] = QJsonArray{ buildActivityJson(true) };
        presence["status"] = QStringLiteral("online");
        presence["since"] = 0;
        presence["afk"] = false;

        QJsonObject d;
        d["token"] = m_embeddedToken;
        d["capabilities"] = 16381;
        d["properties"] = props;
        d["presence"] = presence;
        d["compress"] = false;

        QJsonObject identify;
        identify["op"] = 2;
        identify["d"] = d;

        m_gatewayReady = true;
        sendGatewayWebSocketFrame(0x1, QJsonDocument(identify).toJson(QJsonDocument::Compact));
    } else if (op == 1) {
        onGatewayHeartbeat();
    }
}

void DiscordRPC::onGatewayHeartbeat()
{
    if (!m_gatewayUpgraded) {
        return;
    }
    QJsonObject hb;
    hb["op"] = 1;
    hb["d"] = (m_gatewaySeq >= 0) ? QJsonValue(m_gatewaySeq) : QJsonValue::Null;
    sendGatewayWebSocketFrame(0x1, QJsonDocument(hb).toJson(QJsonDocument::Compact));
}

void DiscordRPC::onGatewayDisconnected()
{
    if (m_gatewayHeartbeatTimer) {
        m_gatewayHeartbeatTimer->stop();
    }
    m_gatewayUpgraded = false;
    m_gatewayReady = false;
}

void DiscordRPC::sendGatewayPresenceUpdate()
{
    if (!m_gatewayReady || !m_gatewayUpgraded || !m_hasActiveActivity) {
        return;
    }
    QJsonObject d;
    d["since"] = 0;
    d["activities"] = QJsonArray{ buildActivityJson(true) };
    d["status"] = QStringLiteral("online");
    d["afk"] = false;

    QJsonObject pkt;
    pkt["op"] = 3;
    pkt["d"] = d;
    sendGatewayWebSocketFrame(0x1, QJsonDocument(pkt).toJson(QJsonDocument::Compact));
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
        m_serverHost.clear();
        if (m_inGameState != InGameState::Singleplayer) {
            QString dim = m_gameStateDetail.isEmpty() ? QStringLiteral("Overworld") : m_gameStateDetail;
            updateInGameState(InGameState::Singleplayer, dim);
        }
    } else if (title.contains(QLatin1String(" - Multiplayer (Realms)"), Qt::CaseInsensitive)) {
        m_isLanServer = false;
        m_serverHost.clear();
        updateInGameState(InGameState::Realms);
    } else if (title.contains(QLatin1String(" - Multiplayer (LAN)"), Qt::CaseInsensitive)) {
        if (!m_isLanServer || m_inGameState != InGameState::Multiplayer) {
            m_isLanServer = true;
            updateInGameState(InGameState::Multiplayer, m_serverHost);
        }
    } else if (title.contains(QLatin1String(" - Multiplayer"), Qt::CaseInsensitive)) {
        if (m_isLanServer || m_inGameState != InGameState::Multiplayer) {
            m_isLanServer = false;
            updateInGameState(InGameState::Multiplayer, m_serverHost);
        }
    } else if (title.startsWith(QLatin1String("Minecraft"), Qt::CaseInsensitive) &&
               !title.contains(QLatin1String(" - "), Qt::CaseInsensitive)) {
        // Do NOT reset Multiplayer state or wipe m_serverHost while still on the "Connecting to server..." handshake screen
        const qint64 nowMs = QDateTime::currentMSecsSinceEpoch();
        if (m_inGameState == InGameState::Multiplayer && m_lastConnectAttemptMs > 0 && (nowMs - m_lastConnectAttemptMs) < 20000) {
            return;
        }
        if (m_inGameState != InGameState::MainMenu) {
            m_worldName.clear();
            m_serverHost.clear();
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

    // Avoid repeating the MC version or loader if the Instance Name already contains it
    const bool instanceHasVersion = showInstanceName && !m_instanceName.isEmpty() && !m_mcVersion.isEmpty() &&
                                    m_instanceName.contains(m_mcVersion, Qt::CaseInsensitive);
    const bool instanceHasLoader = showInstanceName && !m_instanceName.isEmpty() && !m_loaderStr.isEmpty() &&
                                   m_instanceName.contains(m_loaderStr, Qt::CaseInsensitive);

    const bool effectiveShowVersion = showVersion && !m_mcVersion.isEmpty() && !instanceHasVersion;
    const bool effectiveShowLoader = showLoader && !m_loaderStr.isEmpty() && !instanceHasLoader;

    // Build concise visible Line 2 (state)
    QStringList metaParts;
    if (effectiveShowLoader) {
        if (effectiveShowVersion) {
            metaParts << QString("%1 %2").arg(m_loaderStr, m_mcVersion);
        } else {
            metaParts << m_loaderStr;
        }
    } else if (effectiveShowVersion) {
        metaParts << QString("Minecraft %1").arg(m_mcVersion);
    }

    if (showLoader && m_modCount > 0) {
        metaParts << QString("%1 %2").arg(m_modCount).arg(m_modCount == 1 ? "mod" : "mods");
    }

    QString techSummary = metaParts.join(QStringLiteral(" • "));

    // Full technical tooltip for the large icon hover
    QStringList fullTooltipParts;
    if (!m_loaderStr.isEmpty() && !m_mcVersion.isEmpty()) {
        fullTooltipParts << QString("%1 %2").arg(m_loaderStr, m_mcVersion);
    } else if (!m_mcVersion.isEmpty()) {
        fullTooltipParts << QString("Minecraft %1").arg(m_mcVersion);
    } else if (!m_loaderStr.isEmpty()) {
        fullTooltipParts << m_loaderStr;
    }
    if (m_modCount > 0) {
        fullTooltipParts << QString("%1 %2").arg(m_modCount).arg(m_modCount == 1 ? "mod" : "mods");
    }
    QString fullTooltip = fullTooltipParts.join(QStringLiteral(" • "));

    QString stateLine;
    if (showInstanceName && !m_instanceName.isEmpty()) {
        if (!techSummary.isEmpty() && m_instanceName.compare(techSummary, Qt::CaseInsensitive) != 0) {
            stateLine = QString("%1 • %2").arg(m_instanceName, techSummary);
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
                const QString hostToShow = !m_serverHost.isEmpty() ? m_serverHost : sanitizeServerAddress(m_gameStateDetail);
                if (m_isLanServer) {
                    activity.details = QStringLiteral("Playing Multiplayer (LAN)");
                } else if (showServerAddress && !hostToShow.isEmpty()) {
                    activity.details = QString("Playing Multiplayer on %1").arg(hostToShow);
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

    activity.largeText = !fullTooltip.isEmpty() ? fullTooltip : QStringLiteral("Minecraft: Java Edition");

    m_currentActivity = activity;
    if (m_ready && !m_ipcDelayedUntilWindow) {
        sendActivityPayload();
    }
    if (m_gatewayReady) {
        sendGatewayPresenceUpdate();
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
    static const QRegularExpression reConnect(QStringLiteral(R"(Connecting to\s+([^\s,]+?)\.?(?:,\s*|:)(\d{1,5})\b)"),
                                              QRegularExpression::CaseInsensitiveOption);
    static const QRegularExpression reRealms(QStringLiteral(R"(Connecting to realms|RealmsClient)"),
                                             QRegularExpression::CaseInsensitiveOption);
    static const QRegularExpression reSingleplayer(QStringLiteral(R"(Starting integrated minecraft server|Saving and pausing game)"),
                                                   QRegularExpression::CaseInsensitiveOption);
    static const QRegularExpression reWorldName(QStringLiteral(R"((?:ServerLevel\[([^\]]+)\]|Loading level ['"]([^'"]+)['"]))"),
                                                QRegularExpression::CaseInsensitiveOption);
    static const QRegularExpression reDimension(QStringLiteral(R"(Changing to dimension minecraft:([a-z_]+))"),
                                                QRegularExpression::CaseInsensitiveOption);
    static const QRegularExpression reDisconnectOrMenu(
        QStringLiteral(
            R"(Stopping integrated server|Stopping worker threads|Disconnected from server|Disconnecting from server|Failed to connect to server|AnnotatedConnectException|UnknownHostException)"),
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
            m_serverHost.clear();
            updateInGameState(InGameState::Realms);
            continue;
        }

        // Multiplayer (supports Vanilla "Connecting to <host>, <port>", InetSocketAddress "<host>/<ip>, <port>", and "<host>:<port>")
        auto matchConnect = reConnect.match(line);
        if (matchConnect.hasMatch()) {
            QString rawHost = matchConnect.captured(1).trimmed();
            QString port = matchConnect.captured(2).trimmed();
            if (isValidMinecraftServerHost(rawHost, port)) {
                QString cleanHost = sanitizeServerAddress(rawHost);
                if (!cleanHost.isEmpty() && cleanHost.compare(QLatin1String("realms"), Qt::CaseInsensitive) != 0) {
                    m_isLanServer = false;
                    m_serverHost = cleanHost;
                    m_lastConnectAttemptMs = QDateTime::currentMSecsSinceEpoch();
                    updateInGameState(InGameState::Multiplayer, m_serverHost);
                    continue;
                }
            }
        }

        // Singleplayer
        if (reSingleplayer.match(line).hasMatch()) {
            m_isLanServer = false;
            m_serverHost.clear();
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
            m_serverHost.clear();
            m_lastConnectAttemptMs = 0;
            m_isLanServer = false;
            updateInGameState(InGameState::MainMenu);
            continue;
        }
    }
}
