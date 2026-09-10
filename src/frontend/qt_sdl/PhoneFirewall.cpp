// Read-only, conservative host-firewall diagnostics for the phone bridge.
// Copyright (C) 2026 WideMelon contributors
// SPDX-License-Identifier: GPL-3.0-or-later
#include "PhoneFirewall.h"

#include <QHostAddress>
#include <QNetworkInterface>
#include <QProcess>
#include <QProcessEnvironment>
#include <QStandardPaths>

namespace
{
QString interfaceForAddress(const QString& address)
{
    const QHostAddress target(address);
    for (const QNetworkInterface& interface : QNetworkInterface::allInterfaces())
        for (const QNetworkAddressEntry& entry : interface.addressEntries())
            if (entry.ip() == target) return interface.name();
    return {};
}

struct ProcessResult
{
    bool started = false;
    bool finished = false;
    int exitCode = -1;
    QByteArray output;
};

ProcessResult run(const QString& program, const QStringList& arguments)
{
    QProcess process;
    process.setProcessChannelMode(QProcess::MergedChannels);
    process.setProgram(program);
    process.setArguments(arguments);
    process.setProcessEnvironment(QProcessEnvironment());
    process.start();
    ProcessResult result;
    result.started = process.waitForStarted(1000);
    result.finished = result.started && process.waitForFinished(6000);
    if (result.finished)
    {
        result.exitCode = process.exitCode();
        result.output = process.readAll().trimmed();
    }
    return result;
}
}

bool IsLikelyVpnInterface(const QString& address)
{
    const QString name = interfaceForAddress(address).toLower();
    return name.startsWith("tun") || name.startsWith("tap") || name.startsWith("wg")
        || name.startsWith("ppp") || name.contains("vpn") || name.contains("tailscale")
        || name.contains("zerotier");
}

PhoneFirewallResult InspectPhoneFirewall(const QString& address, quint16 port)
{
    PhoneFirewallResult result;
#ifdef Q_OS_LINUX
    const QString interfaceName = interfaceForAddress(address);
    const QString firewalld = QStandardPaths::findExecutable("firewall-cmd");
    if (!firewalld.isEmpty())
    {
        const ProcessResult zone = run(firewalld, {"--get-zone-of-interface=" + interfaceName});
        if (zone.finished && zone.exitCode == 0 && !zone.output.isEmpty())
        {
            result.detected = true;
            result.name = "firewalld";
            const QString zoneName = QString::fromUtf8(zone.output).trimmed();
            const ProcessResult query = run(firewalld,
                {"--zone=" + zoneName, "--query-port=" + QString::number(port) + "/tcp"});
            if (!query.finished) return result;
            if (query.exitCode == 0 && query.output == "yes") result.status = PhoneFirewallStatus::Allowed;
            else if (query.exitCode == 1 && query.output == "no")
            {
                result.status = PhoneFirewallStatus::Blocked;
                result.guidance = "firewalld appears to block WideMelon's phone port on the selected network. "
                    "Open your system firewall settings and allow WideMelon TCP traffic on this private home network, then retry.";
            }
            return result;
        }
    }

    const QString ufw = QStandardPaths::findExecutable("ufw");
    if (!ufw.isEmpty())
    {
        const ProcessResult status = run(ufw, {"status"});
        if (status.finished && status.exitCode == 0 && status.output.startsWith("Status: active"))
        {
            result.detected = true;
            result.name = "UFW";
            const QByteArray needle = QByteArray::number(port) + "/tcp";
            if (status.output.contains(needle) && status.output.contains("ALLOW"))
                result.status = PhoneFirewallStatus::Allowed;
            else
            {
                result.status = PhoneFirewallStatus::Blocked;
                result.guidance = "UFW appears to block WideMelon's phone port. Open your system firewall settings "
                    "and allow WideMelon TCP traffic on this private home network, then retry.";
            }
        }
    }
#else
    Q_UNUSED(address)
    Q_UNUSED(port)
#endif
    return result;
}
