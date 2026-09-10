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
    QProcessEnvironment environment;
    environment.insert("LC_ALL", "C");
    process.setProcessEnvironment(environment);
    process.start();
    ProcessResult result;
    result.started = process.waitForStarted(1000);
    result.finished = result.started && process.waitForFinished(6000);
    if (result.finished)
    {
        result.exitCode = process.exitCode();
        result.output = process.readAll().trimmed();
    }
    else if (result.started)
    {
        process.kill();
        process.waitForFinished(1000);
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
    Q_UNUSED(address)
    Q_UNUSED(port)
    const QString firewalld = QStandardPaths::findExecutable("firewall-cmd");
    if (!firewalld.isEmpty())
    {
        // firewalld's read methods can request PolicyKit authentication with
        // AllowUserInteraction on the server, regardless of client D-Bus flags.
        // Detect installation only: never call firewall-cmd automatically.
        result.detected = true;
        result.name = "firewalld";
        return result;
    }

    const QString ufw = QStandardPaths::findExecutable("ufw");
    if (!ufw.isEmpty())
    {
        const ProcessResult status = run(ufw, {"status"});
        if (status.finished && status.exitCode == 0 && status.output.startsWith("Status: active"))
        {
            result.detected = true;
            result.name = "UFW";
            // The human-readable rule list cannot prove reachability: ALLOW
            // may refer to a different port, source, address family or interface.
            result.guidance = "UFW is active. If the phone cannot connect, check that its address is allowed "
                "to reach WideMelon's TCP port on the selected private home network.";
        }
    }
#else
    Q_UNUSED(address)
    Q_UNUSED(port)
#endif
    return result;
}
