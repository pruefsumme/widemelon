// In-memory configuration and file-path boundary for real bridge tests.
// Copyright (C) 2026 WideMelon contributors
// SPDX-License-Identifier: GPL-3.0-or-later
#include "frontend/qt_sdl/Config.h"
#include "Platform.h"

#include <cstdlib>

namespace Config
{
Table::Table(toml::value& data, const std::string& path) : Data(data), PathPrefix(path) {}
Table GetLocalTable(int)
{
    static toml::value data;
    return Table(data, "");
}
int Table::GetInt(const std::string&) { return 0; }
bool Table::GetBool(const std::string&) { return false; }
std::string Table::GetString(const std::string&) { return {}; }
void Table::SetInt(const std::string&, int) { std::abort(); }
void Table::SetBool(const std::string&, bool) { std::abort(); }
void Table::SetString(const std::string&, const std::string&) { std::abort(); }
void Save() { std::abort(); }
}

namespace melonDS::Platform
{
std::string GetLocalFilePath(const std::string&) { std::abort(); }
}
