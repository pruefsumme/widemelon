// Read-only firewall probes exercised with isolated command fixtures.
// Copyright (C) 2026 WideMelon contributors
// SPDX-License-Identifier: GPL-3.0-or-later
#include "frontend/qt_sdl/PhoneFirewall.h"
#include <QCoreApplication>
#include <QFile>
#include <QProcess>
#include <QTemporaryDir>
#include <iostream>

int main(int argc, char** argv)
{
    QCoreApplication application(argc, argv);
#ifdef Q_OS_LINUX
    QTemporaryDir directory;
    if (!directory.isValid()) return 1;
    qputenv("PATH", directory.path().toUtf8());
    auto command = [&](const QString& name, const QByteArray& script) {
        QFile file(directory.filePath(name));
        if (!file.open(QIODevice::WriteOnly)) return false;
        const QByteArray content = "#!/bin/sh\n" + script;
        return file.write(content) == content.size()
            && file.setPermissions(QFile::ReadOwner | QFile::WriteOwner | QFile::ExeOwner);
    };
    const QString called = directory.filePath("interactive-probe-called");
    if (!command("firewall-cmd", "echo called > '" + called.toUtf8() + "'\n"
                                 "case \"$1\" in\n"
                                 "  --get-zone-of-interface=*) echo public; exit 0;;\n"
                                 "  --get-default-zone) echo public; exit 0;;\n"
                                 "esac\n"
                                 "exit 1\n")) return 2;
    if (InspectPhoneFirewall("127.0.0.1", 24800).detected || QFile::exists(called)) return 14;
    // Repeated checks, including a new session, must never launch an
    // authorization-capable firewalld command, even with read-only arguments.
    for (int session = 0; session < 3; session++)
    {
        const auto result = InspectPhoneFirewall("192.168.1.10", 24800);
        if (!result.detected || result.name != "firewalld"
            || result.status != PhoneFirewallStatus::Unknown || result.guidance.isEmpty()) return 3;
        if (QFile::exists(called)) return 4;
    }
    const auto firewalld = InspectPhoneFirewall("192.168.1.10", 24800);
    const PhoneFirewallNetwork network{QHostAddress("192.168.1.10"), "wlan0", 24};
    const auto noZoneGuide = BuildPhoneFirewallGuide(firewalld, network, 24800);
    if (!noZoneGuide.commands.isEmpty() || noZoneGuide.preparation.isEmpty() || QFile::exists(called)) return 19;
    const auto firewalldGuide = BuildPhoneFirewallGuide(firewalld, network, 24800, "public");
    if (QFile::exists(called) || !firewalldGuide.commands.contains("--permanent --zone='public'")
        || !firewalldGuide.commands.contains("sudo firewall-cmd --zone='public'")
        || !firewalldGuide.commands.contains("source address=\"192.168.1.0/24\"")
        || !firewalldGuide.commands.contains("destination address=\"192.168.1.10\"")
        || !firewalldGuide.commands.contains("port port=\"24800\"")
        || firewalldGuide.commands.contains("--reload")
        || !firewalldGuide.verification.contains("--query-rich-rule")
        || !firewalldGuide.removal.contains("--remove-rich-rule")) return 5;
    for (const QString& badZone : {QString(), QString("public; touch /tmp/no"), QString("$(echo public)"),
            QString("public\ntrusted"), QString("public'"), QString(65, 'a')})
        if (!BuildPhoneFirewallGuide(firewalld, network, 24800, badZone).commands.isEmpty()) return 20;
    for (const PhoneFirewallNetwork& invalid : {
            PhoneFirewallNetwork{QHostAddress("127.0.0.1"), "lo", 8},
            PhoneFirewallNetwork{QHostAddress("8.8.8.8"), "eth0", 24},
            PhoneFirewallNetwork{QHostAddress("192.168.1.10"), "", 24},
            PhoneFirewallNetwork{QHostAddress("192.168.1.10"), "eth0", -1},
            PhoneFirewallNetwork{QHostAddress("192.168.1.10"), "eth0", 0},
            PhoneFirewallNetwork{QHostAddress("192.168.1.10"), "eth0", 8},
            PhoneFirewallNetwork{QHostAddress("192.168.1.10"), "eth0", 33},
            PhoneFirewallNetwork{QHostAddress("::1"), "lo", 32}})
    {
        const auto guide = BuildPhoneFirewallGuide(firewalld, invalid, 24800, "public");
        if (!guide.commands.isEmpty() || !guide.removal.isEmpty()) return 21;
    }
    if (!BuildPhoneFirewallGuide(firewalld, network, 0, "public").commands.isEmpty()) return 22;
    if (!BuildPhoneFirewallGuide({}, network, 24800).commands.isEmpty()) return 23;
    if (!FindPhoneFirewallNetwork("192.0.2.1").interface.isEmpty()) return 24;
    if (!QFile::remove(directory.filePath("firewall-cmd"))) return 6;
    for (const QByteArray& rules : {QByteArray("24800/tcp DENY Anywhere\n22/tcp ALLOW Anywhere"),
            QByteArray("24800/tcp ALLOW 192.168.9.1"), QByteArray("24800/tcp (v6) ALLOW Anywhere (v6)")})
    {
        if (!command("ufw", "printf '%s\\n' 'Status: active' '" + rules + "'\n")) return 7;
        const auto result = InspectPhoneFirewall("192.168.1.10", 24800);
        if (!result.detected || result.status != PhoneFirewallStatus::Unknown || result.guidance.isEmpty()) return 8;
        const auto guide = BuildPhoneFirewallGuide(result, network, 24800);
        if (!guide.commands.contains("ufw allow in") || !guide.commands.contains("24800")
            || !guide.commands.contains("on 'wlan0' from 192.168.1.0/24 to 192.168.1.10")
            || guide.verification != "sudo ufw status"
            || !guide.verificationHint.contains("ALLOW IN")
            || !guide.removal.startsWith("sudo ufw delete allow in")) return 13;
    }
    if (!command("firewall-cmd", "echo should-not-run > '" + called.toUtf8() + "'\nexit 1\n")) return 15;
    if (InspectPhoneFirewall("192.168.1.10", 24800).name != "UFW" || QFile::exists(called)) return 16;
    if (!QFile::remove(directory.filePath("firewall-cmd"))) return 17;
    if (!command("ufw", "echo 'Status: inactive'\n")) return 9;
    if (InspectPhoneFirewall("192.168.1.10", 24800).detected) return 10;
    if (!command("ufw", "echo 'ERROR: You need to be root to run this script'\nexit 1\n")) return 11;
    if (InspectPhoneFirewall("192.168.1.10", 24800).status != PhoneFirewallStatus::Unknown) return 12;

    // Execute generated text with a fake sudo in an isolated PATH. It only
    // records argv; no firewall utility or real elevation tool can run.
    const QString arguments = directory.filePath("arguments");
    if (!command("sudo", "printf '%s\\0' \"$@\" >> '" + arguments.toUtf8() + "'\n")) return 25;
    PhoneFirewallResult ufw;
    ufw.name = "UFW";
    const PhoneFirewallNetwork unusual{QHostAddress("10.2.3.4"), "wl'an;$(false)", 16};
    const auto quoted = BuildPhoneFirewallGuide(ufw, unusual, 24800);
    for (const auto& guide : {firewalldGuide, quoted})
    {
        QProcess shell;
        shell.start("/bin/sh", {"-s"});
        if (!shell.waitForStarted(1000)) return 26;
        shell.write((guide.commands + '\n' + guide.verification + '\n' + guide.removal + '\n').toUtf8());
        shell.closeWriteChannel();
        if (!shell.waitForFinished(1000) || shell.exitCode() != 0) return 27;
    }
    QFile argvFile(arguments);
    if (!argvFile.open(QIODevice::ReadOnly)) return 28;
    const auto recordedArguments = argvFile.readAll().split('\0');
    if (!recordedArguments.contains(unusual.interface.toUtf8()) || !recordedArguments.contains("--zone=public")
        || !recordedArguments.contains("--add-rich-rule=rule family=\"ipv4\" source address=\"192.168.1.0/24\" destination address=\"192.168.1.10\" port port=\"24800\" protocol=\"tcp\" accept")
        || QFile::exists(called)) return 29;
    std::cout << "Firewall probe classification passed without inspecting or changing host rules\n";
#endif
}
