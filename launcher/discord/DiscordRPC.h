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

#pragma once

#include <QDateTime>
#include <QJsonObject>
#include <QLocalSocket>
#include <QObject>
#include <QString>
#include <QStringList>
#include <QTimer>

class BaseInstance;

struct DiscordActivity {
    QString details;
    QString state;
    qint64 startTimestamp = 0;
    QString largeImage;
    QString largeText;
    QString smallImage;
    QString smallText;
    qint64 processId = 0;
};

class DiscordRPC : public QObject {
    Q_OBJECT

   public:
    static constexpr const char* DEFAULT_APPLICATION_ID = "1402418491272986635";

    enum class Opcode : quint32 {
        Handshake = 0,
        Frame = 1,
        Close = 2,
        Ping = 3,
        Pong = 4,
    };

    enum class InGameState {
        Starting,
        MainMenu,
        Singleplayer,
        Multiplayer,
        Realms,
    };

    explicit DiscordRPC(QObject* parent = nullptr);
    ~DiscordRPC() override;

    void setActivityForInstance(BaseInstance* instance, qint64 pid);
    void setActivity(const DiscordActivity& activity);
    void handleLogLines(const QStringList& lines);
    void updateInGameState(InGameState state, const QString& detail = QString());
    void clearActivity();

    bool isConnected() const { return m_ready; }

   private slots:
    void onConnected();
    void onDisconnected();
    void onReadyRead();
    void onErrorOccurred(QLocalSocket::LocalSocketError socketError);
    void onReconnectTimeout();

   private:
    void attemptConnection();
    void sendHandshake();
    void sendFrame(Opcode op, const QJsonObject& payload);
    void handleMessage(Opcode op, const QByteArray& data);
    void sendActivityPayload();
    void rebuildActivity();
    QString resolvePipePath(int index) const;
    QString getEffectiveClientId() const;

    QLocalSocket* m_socket = nullptr;
    QTimer* m_reconnectTimer = nullptr;
    QTimer* m_ipcDelayTimer = nullptr;
    QByteArray m_receiveBuffer;
    DiscordActivity m_currentActivity;
    bool m_hasActiveActivity = false;
    bool m_ipcDelayedUntilWindow = false;
    bool m_ready = false;
    int m_pipeIndex = 0;
    quint64 m_nonce = 0;

    InGameState m_inGameState = InGameState::Starting;
    QString m_gameStateDetail;
    QString m_worldName;
    QString m_instanceName;
    QString m_mcVersion;
    QString m_loaderStr;
    int m_modCount = 0;
    qint64 m_sessionStartTimestamp = 0;
    qint64 m_gamePid = 0;
};
