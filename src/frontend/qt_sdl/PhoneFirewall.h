// Conservative host-firewall diagnostics and user-invoked setup guidance.
// Copyright (C) 2026 WideMelon contributors
// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

#include <QHostAddress>

#include <QString>

enum class PhoneFirewallStatus
{
    Unknown,
    Allowed,
    Blocked,
};

struct PhoneFirewallResult
{
    bool detected = false;
    PhoneFirewallStatus status = PhoneFirewallStatus::Unknown;
    QString name;
    QString guidance;
};

struct PhoneFirewallGuide
{
    QString firewallName;
    QString scope;
    QString instructions;
    QString preparation;
    QString commands;
    QString verification;
    QString verificationHint;
    QString removal;
};

struct PhoneFirewallNetwork
{
    QHostAddress address;
    QString interface;
    int prefixLength = -1;
};

PhoneFirewallResult InspectPhoneFirewall(const QString& address, quint16 port);
PhoneFirewallNetwork FindPhoneFirewallNetwork(const QString& address);
// Pure command generation. Never runs firewall tools or requests authorization.
PhoneFirewallGuide BuildPhoneFirewallGuide(const PhoneFirewallResult& firewall,
                                           const PhoneFirewallNetwork& network, quint16 port,
                                           const QString& firewalldZone = {});
bool IsLikelyVpnInterface(const QString& address);
