#pragma once
#include <string>

struct ProgramConfig {
    std::wstring path;
    std::wstring arguments;
    std::wstring directory;
    bool hideOnStart = true;
    bool manageProcessTree = true;
};

std::wstring ExecutablePath();
std::wstring ParentDirectory(const std::wstring& path);
std::wstring SystemError(unsigned long code);
bool LoadConfig(ProgramConfig& config, std::wstring& error);
