// Read-only firewall probes exercised with isolated command fixtures.
// Copyright (C) 2026 WideMelon contributors
// SPDX-License-Identifier: GPL-3.0-or-later
#include "frontend/qt_sdl/PhoneFirewall.h"
#include <QCoreApplication>
#include <QFile>
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
    if (!command("firewall-cmd", "echo called > '" + called.toUtf8() + "'\nexit 1\n")) return 2;
    // Repeated checks, including a new session, must never launch an
    // authorization-capable firewalld command, even with read-only arguments.
    for (int session = 0; session < 3; session++)
    {
        const auto result = InspectPhoneFirewall("192.168.1.10", 24800);
        if (!result.detected || result.name != "firewalld"
            || result.status != PhoneFirewallStatus::Unknown || !result.guidance.isEmpty()) return 3;
        if (QFile::exists(called)) return 4;
    }
    if (!QFile::remove(directory.filePath("firewall-cmd"))) return 6;
    for (const QByteArray& rules : {QByteArray("24800/tcp DENY Anywhere\n22/tcp ALLOW Anywhere"),
            QByteArray("24800/tcp ALLOW 192.168.9.1"), QByteArray("24800/tcp (v6) ALLOW Anywhere (v6)")})
    {
        if (!command("ufw", "printf '%s\\n' 'Status: active' '" + rules + "'\n")) return 7;
        const auto result = InspectPhoneFirewall("192.168.1.10", 24800);
        if (!result.detected || result.status != PhoneFirewallStatus::Unknown || result.guidance.isEmpty()) return 8;
    }
    if (!command("ufw", "echo 'Status: inactive'\n")) return 9;
    if (InspectPhoneFirewall("192.168.1.10", 24800).detected) return 10;
    if (!command("ufw", "echo 'ERROR: You need to be root to run this script'\nexit 1\n")) return 11;
    if (InspectPhoneFirewall("192.168.1.10", 24800).status != PhoneFirewallStatus::Unknown) return 12;
    std::cout << "Firewall probe classification passed without inspecting or changing host rules\n";
#endif
}
