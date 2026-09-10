// Trusted-LAN pairing credentials and authentication throttling.
// Copyright (C) 2026 WideMelon contributors
// SPDX-License-Identifier: GPL-3.0-or-later
#include "PhoneSecurity.h"

#include <algorithm>
#include <limits>

#include <QCryptographicHash>
#include <QRandomGenerator>

namespace
{
QByteArray hash(const QByteArray& value)
{
    return QCryptographicHash::hash(value, QCryptographicHash::Sha256);
}

bool constantTimeEqual(const QByteArray& left, const QByteArray& right)
{
    const int count = std::max(left.size(), right.size());
    unsigned int difference = unsigned(left.size() ^ right.size());
    for (int i = 0; i < count; i++)
    {
        const unsigned char a = i < left.size() ? static_cast<unsigned char>(left[i]) : 0;
        const unsigned char b = i < right.size() ? static_cast<unsigned char>(right[i]) : 0;
        difference |= unsigned(a ^ b);
    }
    return difference == 0;
}
}

void PhonePairingCredentials::regenerate()
{
    clear();
    quint32 words[8];
    QRandomGenerator::system()->fillRange(words);
    secretValue = QByteArray(reinterpret_cast<const char*>(words), sizeof(words))
        .toBase64(QByteArray::Base64UrlEncoding | QByteArray::OmitTrailingEquals);

    constexpr quint64 range = 10000000000ULL;
    constexpr quint64 maximum = std::numeric_limits<quint64>::max();
    constexpr quint64 limit = maximum - (maximum % range);
    quint64 randomValue;
    do randomValue = QRandomGenerator::system()->generate64(); while (randomValue >= limit);
    codeValue = QString("%1").arg(randomValue % range, 10, 10, QLatin1Char('0'));
    secretHash = hash(secretValue);
    codeHash = hash(codeValue.toUtf8());
}

void PhonePairingCredentials::clear()
{
    secretValue.fill('\0');
    secretValue.clear();
    secretHash.fill('\0');
    secretHash.clear();
    codeHash.fill('\0');
    codeHash.clear();
    codeValue.fill(QLatin1Char('0'));
    codeValue.clear();
}

bool PhonePairingCredentials::matches(const QString& candidate) const
{
    const QByteArray candidateHash = hash(candidate.toUtf8());
    return constantTimeEqual(candidateHash, secretHash) | constantTimeEqual(candidateHash, codeHash);
}

void PhoneAuthenticationLimiter::prune(qint64 now)
{
    for (auto iterator = peerBlockedUntil.begin(); iterator != peerBlockedUntil.end();)
    {
        if (iterator.value() <= now) iterator = peerBlockedUntil.erase(iterator);
        else ++iterator;
    }
    while (!globalFailures.isEmpty() && globalFailures.first() <= now - FailureWindowMs)
        globalFailures.removeFirst();
    for (auto iterator = failuresByPeer.begin(); iterator != failuresByPeer.end();)
    {
        QList<qint64>& failures = iterator.value();
        while (!failures.isEmpty() && failures.first() <= now - FailureWindowMs) failures.removeFirst();
        if (failures.isEmpty()) iterator = failuresByPeer.erase(iterator);
        else ++iterator;
    }
}

bool PhoneAuthenticationLimiter::isBlocked(const QString& peer, qint64 now)
{
    prune(now);
    if (globalBlockedUntil > now) return true;
    if (globalBlockedUntil != 0) globalBlockedUntil = 0;
    if (peerBlockedUntil.value(peer) > now) return true;
    peerBlockedUntil.remove(peer);
    return false;
}

void PhoneAuthenticationLimiter::recordFailure(const QString& peer, qint64 now)
{
    // Already-open pairing sockets can still submit during a cooldown. They
    // must not extend it, including when they carry the correct credential.
    if (isBlocked(peer, now)) return;
    QList<qint64>& peerFailures = failuresByPeer[peer];
    peerFailures.append(now);
    globalFailures.append(now);
    if (peerFailures.size() >= PeerFailureLimit) peerBlockedUntil[peer] = now + CooldownMs;
    if (globalFailures.size() >= GlobalFailureLimit) globalBlockedUntil = now + CooldownMs;
}

void PhoneAuthenticationLimiter::clear()
{
    failuresByPeer.clear();
    peerBlockedUntil.clear();
    globalFailures.clear();
    globalBlockedUntil = 0;
}
