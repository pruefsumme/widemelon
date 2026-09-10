// Read-only, conservative host-firewall diagnostics for the phone bridge.
// Copyright (C) 2026 WideMelon contributors
// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

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

PhoneFirewallResult InspectPhoneFirewall(const QString& address, quint16 port);
bool IsLikelyVpnInterface(const QString& address);
