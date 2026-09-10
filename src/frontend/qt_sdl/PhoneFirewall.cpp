// Conservative host-firewall diagnostics and user-invoked setup guidance.
// Copyright (C) 2026 WideMelon contributors
// SPDX-License-Identifier: GPL-3.0-or-later
#include "PhoneFirewall.h"

#include <QFile>
#include <QHostAddress>
#include <QNetworkInterface>
#include <QProcess>
#include <QProcessEnvironment>
#include <QStandardPaths>

#include "PhoneProtocol.h"

namespace
{
QString shellQuote(QString value)
{
    value.replace('\'', "'\"'\"'");
    return '\'' + value + '\'';
}

bool validFirewalldZone(const QString& value)
{
    if (value.isEmpty() || value.size() > 64) return false;
    for (const QChar character : value)
        if (!((character >= 'a' && character <= 'z') || (character >= 'A' && character <= 'Z')
            || (character >= '0' && character <= '9') || character == '-' || character == '_')) return false;
    return true;
}

bool ufwEnabledAtBoot()
{
    QFile configuration("/etc/ufw/ufw.conf");
    if (!configuration.open(QIODevice::ReadOnly | QIODevice::Text)) return false;
    for (QByteArray line : configuration.read(16384).split('\n'))
    {
        line = line.trimmed();
        if (line.startsWith('#')) continue;
        const int separator = line.indexOf('=');
        if (separator < 0) continue;
        if (line.left(separator).trimmed().compare("ENABLED", Qt::CaseInsensitive) == 0)
            return line.mid(separator + 1).trimmed().compare("yes", Qt::CaseInsensitive) == 0;
    }
    return false;
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

PhoneFirewallNetwork FindPhoneFirewallNetwork(const QString& address)
{
    PhoneFirewallNetwork network;
    network.address = QHostAddress(address);
    for (const QNetworkInterface& interface : QNetworkInterface::allInterfaces())
    {
        if (!(interface.flags() & QNetworkInterface::IsUp)
            || !(interface.flags() & QNetworkInterface::IsRunning)) continue;
        for (const QNetworkAddressEntry& entry : interface.addressEntries())
        {
            if (entry.ip() != network.address) continue;
            network.interface = interface.name();
            network.prefixLength = entry.prefixLength();
            return network;
        }
    }
    return network;
}

bool IsLikelyVpnInterface(const QString& address)
{
    const QString name = FindPhoneFirewallNetwork(address).interface.toLower();
    return name.startsWith("tun") || name.startsWith("tap") || name.startsWith("wg")
        || name.startsWith("ppp") || name.contains("vpn") || name.contains("tailscale")
        || name.contains("zerotier");
}

PhoneFirewallResult InspectPhoneFirewall(const QString& address, quint16 port)
{
    PhoneFirewallResult result;
#ifdef Q_OS_LINUX
    Q_UNUSED(port)
    if (QHostAddress(address).isLoopback()) return result;
    const QString ufw = QStandardPaths::findExecutable("ufw");
    if (!ufw.isEmpty())
    {
        const ProcessResult status = run(ufw, {"status"});
        const bool active = status.finished && status.exitCode == 0
            && status.output.startsWith("Status: active");
        // A successful runtime query wins over the boot configuration. Boot
        // enabled is only a hint when the unprivileged query is unavailable.
        const bool enabledAtBoot = (!status.finished || status.exitCode != 0) && ufwEnabledAtBoot();
        if (active || enabledAtBoot)
        {
            result.detected = true;
            result.name = "UFW";
            // The human-readable rule list cannot prove reachability: ALLOW
            // may refer to a different port, source, address family or interface.
            result.guidance = active ? "UFW is active; its rules may affect the phone connection."
                : "UFW is enabled at boot; its current state and rules could not be checked without administrator access.";
            return result;
        }
    }

    const QString firewalld = QStandardPaths::findExecutable("firewall-cmd");
    if (!firewalld.isEmpty())
    {
        // firewalld's read methods can request PolicyKit authentication with
        // AllowUserInteraction on the server, regardless of client D-Bus flags.
        // Detect installation only: never call firewall-cmd automatically.
        result.detected = true;
        result.name = "firewalld";
        result.guidance = "firewalld may be preventing the phone from reaching WideMelon's TCP port.";
    }
#else
    Q_UNUSED(address)
    Q_UNUSED(port)
#endif
    return result;
}

PhoneFirewallGuide BuildPhoneFirewallGuide(const PhoneFirewallResult& firewall,
                                           const PhoneFirewallNetwork& network, quint16 port,
                                           const QString& firewalldZone)
{
    PhoneFirewallGuide guide;
    guide.firewallName = firewall.name.isEmpty() ? QStringLiteral("System firewall") : firewall.name;
    guide.scope = QString("Incoming TCP port %1 for WideMelon on the selected private home network").arg(port);
    guide.instructions = "Open your system's firewall settings and allow WideMelon's incoming TCP port on this "
        "private home network. Keep the firewall enabled. If another security product manages the firewall, "
        "use that product's settings. A managed computer may require help from its administrator.";
#ifdef Q_OS_WIN
    guide.instructions = "In Windows Security, open Firewall & network protection > Allow an app through firewall "
        "> Change settings > Allow another app. Select the WideMelon executable and allow it on Private networks "
        "only. Keep Public unchecked. Remove the app's permission there when no longer needed.";
#elif defined(Q_OS_MACOS) || defined(Q_OS_MAC)
    guide.instructions = "In System Settings > Network > Firewall > Options, add WideMelon and choose Allow "
        "incoming connections. Use the bridge only on your trusted home network. Remove WideMelon from the "
        "list or block its incoming connections there when no longer needed.";
#endif
    if (network.address.isLoopback())
    {
        guide.instructions = "This address is for this computer only. Select a private LAN address in the phone "
            "settings so the phone can connect. No firewall rule is needed for the loopback test address.";
        return guide;
    }
    if (network.address.protocol() != QAbstractSocket::IPv4Protocol
        || !PhoneProtocol::IsPrivateIPv4(network.address.toIPv4Address()) || port < 1024
        || network.interface.isEmpty() || network.prefixLength < 1 || network.prefixLength > 32)
    {
        guide.instructions = "A current private IPv4 interface and subnet could not be determined. Refresh the "
            "network list and select the home-network address before generating a firewall rule.";
        return guide;
    }
    const quint32 mask = 0xFFFFFFFFU << (32 - network.prefixLength);
    const quint32 subnetAddress = network.address.toIPv4Address() & mask;
    const quint32 subnetLastAddress = subnetAddress | ~mask;
    // A malformed netmask must never turn the home-LAN rule into 0.0.0.0/0
    // or a range reaching outside the selected RFC1918 block.
    const int privatePrefix = (network.address.toIPv4Address() & 0xFF000000U) == 0x0A000000U ? 8
        : (network.address.toIPv4Address() & 0xFFF00000U) == 0xAC100000U ? 12 : 16;
    if (network.prefixLength < privatePrefix || !PhoneProtocol::IsPrivateIPv4(subnetLastAddress))
    {
        guide.instructions = "The selected subnet extends outside a private IPv4 range. Check the network "
            "configuration before adding a firewall rule.";
        return guide;
    }
    const QString address = network.address.toString();
    const QString subnet = QHostAddress(subnetAddress).toString() + '/' + QString::number(network.prefixLength);
    const QString portText = QString::number(port);

    if (firewall.name.compare("firewalld", Qt::CaseInsensitive) == 0)
    {
        guide.preparation = "sudo firewall-cmd --get-zone-of-interface=" + shellQuote(network.interface)
            + "\nsudo firewall-cmd --get-active-zones";
        guide.instructions = "Run the check commands in your terminal and enter the zone for the selected "
            "interface below. If a source-based zone applies to your phone, use that zone. If the interface "
            "has no assigned zone, check sudo firewall-cmd --get-default-zone. Do not guess a zone when a query fails.";
        if (!validFirewalldZone(firewalldZone)) return guide;
        const QString zoneOption = " --zone=" + shellQuote(firewalldZone);
        QString rule = "rule family=\"ipv4\" source address=\"" + subnet + "\" destination address=\"" + address + "\"";
        rule += " port port=\"" + portText + "\" protocol=\"tcp\" accept";
        guide.commands = "sudo firewall-cmd --permanent" + zoneOption
            + " --add-rich-rule='" + rule + "'\n"
              "sudo firewall-cmd" + zoneOption + " --add-rich-rule='" + rule + "'";
        guide.verification = "sudo firewall-cmd" + zoneOption + " --query-rich-rule='" + rule + "'\n"
            "sudo firewall-cmd --permanent" + zoneOption + " --query-rich-rule='" + rule + "'";
        guide.removal = "sudo firewall-cmd" + zoneOption + " --remove-rich-rule='" + rule + "'\n"
            "sudo firewall-cmd --permanent" + zoneOption + " --remove-rich-rule='" + rule + "'";
        guide.scope = QString("Incoming TCP %1 to %2 from %3 in firewalld's %4 zone")
            .arg(port).arg(address, subnet, firewalldZone);
        guide.verificationHint = "Both the runtime and permanent queries should print yes. If either fails, "
            "read the error before continuing.";
    }
    else if (firewall.name.compare("UFW", Qt::CaseInsensitive) == 0)
    {
        QString command = "sudo ufw allow in";
        command += " on " + shellQuote(network.interface) + " from " + subnet;
        command += " to " + address + " port " + portText + " proto tcp comment 'WideMelon phone controller'";
        guide.commands = command;
        guide.verification = "sudo ufw status";
        guide.removal = "sudo ufw delete " + command.mid(QString("sudo ufw ").size());
        guide.scope = QString("Incoming TCP %1 to %2 from %3 on interface %4")
            .arg(port).arg(address, subnet, network.interface);
        guide.instructions = "The command adds one persistent UFW rule. It does not enable, disable, or reset UFW.";
        guide.verificationHint = "Check that UFW reports Status: active and lists the WideMelon ALLOW IN rule "
            "for the displayed address, interface, port and source subnet. UFW prints a rule list, not yes.";
    }
    else
    {
        guide.commands.clear();
        guide.verification.clear();
        guide.removal.clear();
    }
    return guide;
}
