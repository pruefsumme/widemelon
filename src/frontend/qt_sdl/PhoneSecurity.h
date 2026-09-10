// Trusted-LAN pairing credentials and authentication throttling.
// Copyright (C) 2026 WideMelon contributors
// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

#include <QByteArray>
#include <QHash>
#include <QList>
#include <QString>

class PhonePairingCredentials
{
public:
    ~PhonePairingCredentials() { clear(); }
    PhonePairingCredentials() = default;
    PhonePairingCredentials(const PhonePairingCredentials&) = delete;
    PhonePairingCredentials& operator=(const PhonePairingCredentials&) = delete;
    void regenerate();
    void clear();
    bool matches(const QString& candidate) const;

    QByteArray secret() const { return secretValue; }
    QString code() const { return codeValue; }
    bool isEmpty() const { return secretValue.isEmpty(); }

private:
    QByteArray secretValue;
    QString codeValue;
    QByteArray secretHash;
    QByteArray codeHash;
};

class PhoneAuthenticationLimiter
{
public:
    static constexpr int PeerFailureLimit = 5;
    static constexpr int GlobalFailureLimit = 20;
    static constexpr qint64 FailureWindowMs = 60000;
    static constexpr qint64 CooldownMs = 60000;

    bool isBlocked(const QString& peer, qint64 now);
    void recordFailure(const QString& peer, qint64 now);
    void clear();

private:
    void prune(qint64 now);

    QHash<QString, QList<qint64>> failuresByPeer;
    QHash<QString, qint64> peerBlockedUntil;
    QList<qint64> globalFailures;
    qint64 globalBlockedUntil = 0;
};
