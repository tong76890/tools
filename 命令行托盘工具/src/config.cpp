#include "config.h"
#include <windows.h>
#include <vector>

static bool ParseBool(const std::wstring& value, bool& result) {
    if (value == L"1" || value == L"true" || value == L"yes" || value == L"on") { result = true; return true; }
    if (value == L"0" || value == L"false" || value == L"no" || value == L"off") { result = false; return true; }
    return false;
}

std::wstring ExecutablePath() {
    std::vector<wchar_t> buffer(32768);
    DWORD length = GetModuleFileNameW(NULL, buffer.data(), static_cast<DWORD>(buffer.size()));
    return std::wstring(buffer.data(), length);
}

std::wstring ParentDirectory(const std::wstring& path) {
    size_t separator = path.find_last_of(L"\\/");
    if (separator == std::wstring::npos) return L"";
    if (separator == 2 && path[1] == L':') return path.substr(0, 3);
    return path.substr(0, separator);
}

std::wstring SystemError(unsigned long code) {
    wchar_t* message = NULL;
    FormatMessageW(FORMAT_MESSAGE_ALLOCATE_BUFFER | FORMAT_MESSAGE_FROM_SYSTEM |
        FORMAT_MESSAGE_IGNORE_INSERTS, NULL, code, 0, reinterpret_cast<wchar_t*>(&message), 0, NULL);
    std::wstring result = message ? message : L"未知系统错误";
    LocalFree(message);
    return result + L" (" + std::to_wstring(code) + L")";
}

static std::wstring Trim(const std::wstring& value) {
    size_t first = value.find_first_not_of(L" \t\r\n");
    if (first == std::wstring::npos) return L"";
    return value.substr(first, value.find_last_not_of(L" \t\r\n") - first + 1);
}

static bool ResolvePath(const std::wstring& value, const std::wstring& base,
                        std::wstring& result, std::wstring& error) {
    DWORD length = ExpandEnvironmentStringsW(value.c_str(), NULL, 0);
    if (!length) { error = SystemError(GetLastError()); return false; }
    std::vector<wchar_t> expanded(length);
    if (!ExpandEnvironmentStringsW(value.c_str(), expanded.data(), length)) {
        error = SystemError(GetLastError()); return false;
    }
    std::wstring path(expanded.data());
    if (path.size() >= 2 && path.front() == L'"' && path.back() == L'"')
        path = path.substr(1, path.size() - 2);
    bool absolute = (path.size() >= 3 && path[1] == L':' &&
        (path[2] == L'\\' || path[2] == L'/')) || path.compare(0, 2, L"\\\\") == 0;
    if (!absolute) {
        if (path.empty() || path[0] == L'\\' || path[0] == L'/' || path.find(L':') != std::wstring::npos) {
            error = L"路径必须是完整绝对路径或普通相对路径。"; return false;
        }
        path = base + L"\\" + path;
    }
    std::vector<wchar_t> full(32768);
    DWORD count = GetFullPathNameW(path.c_str(), static_cast<DWORD>(full.size()), full.data(), NULL);
    if (!count || count >= full.size()) { error = L"无法解析路径：" + path; return false; }
    result.assign(full.data(), count);
    return true;
}

bool LoadConfig(ProgramConfig& config, std::wstring& error) {
    const std::wstring base = ParentDirectory(ExecutablePath());
    HANDLE file = CreateFileW((base + L"\\tray.ini").c_str(), GENERIC_READ,
        FILE_SHARE_READ, NULL, OPEN_EXISTING, FILE_ATTRIBUTE_NORMAL, NULL);
    if (file == INVALID_HANDLE_VALUE) { error = L"读取 tray.ini 失败：" + SystemError(GetLastError()); return false; }
    DWORD size = GetFileSize(file, NULL);
    if (size == INVALID_FILE_SIZE || size > 65536) {
        CloseHandle(file); error = L"tray.ini 必须小于 64 KiB。"; return false;
    }
    std::vector<char> bytes(size);
    DWORD read = 0;
    BOOL success = ReadFile(file, bytes.data(), size, &read, NULL);
    CloseHandle(file);
    if (!success || read != size) { error = L"无法完整读取 tray.ini。"; return false; }
    int offset = size >= 3 && static_cast<unsigned char>(bytes[0]) == 0xef &&
        static_cast<unsigned char>(bytes[1]) == 0xbb && static_cast<unsigned char>(bytes[2]) == 0xbf ? 3 : 0;
    int count = MultiByteToWideChar(CP_UTF8, MB_ERR_INVALID_CHARS, bytes.data() + offset, size - offset, NULL, 0);
    if (!count) { error = L"tray.ini 必须是非空 UTF-8 文件。"; return false; }
    std::wstring content(count, L'\0');
    MultiByteToWideChar(CP_UTF8, MB_ERR_INVALID_CHARS, bytes.data() + offset, size - offset, &content[0], count);
    if (content.find(L'\0') != std::wstring::npos) { error = L"配置含有无效的 NUL 字符。"; return false; }
    config = ProgramConfig();
    bool inProgram = false;
    size_t start = 0;
    while (start < content.size()) {
        size_t end = content.find(L'\n', start);
        std::wstring line = Trim(content.substr(start, end == std::wstring::npos ? end : end - start));
        start = end == std::wstring::npos ? content.size() : end + 1;
        if (line.empty() || line[0] == L';' || line[0] == L'#') continue;
        if (line[0] == L'[') { inProgram = line == L"[program]"; continue; }
        if (!inProgram) continue;
        size_t equal = line.find(L'=');
        if (equal == std::wstring::npos) { error = L"配置行缺少等号：" + line; return false; }
        std::wstring key = Trim(line.substr(0, equal));
        std::wstring value = Trim(line.substr(equal + 1));
        if (key == L"path") config.path = value;
        else if (key == L"arguments") config.arguments = value;
        else if (key == L"working_directory") config.directory = value;
        else if (key == L"hide_on_start") {
            if (!ParseBool(value, config.hideOnStart)) { error = L"hide_on_start 必须是 true 或 false。"; return false; }
        }
        else if (key == L"manage_process_tree") {
            if (!ParseBool(value, config.manageProcessTree)) { error = L"manage_process_tree 必须是 true 或 false。"; return false; }
        }
        else { error = L"未知配置项：" + key; return false; }
    }
    if (config.path.empty()) { error = L"请在 [program] 中设置 path。"; return false; }
    if (!ResolvePath(config.path, base, config.path, error)) return false;
    if (config.directory.empty()) config.directory = ParentDirectory(config.path);
    else if (!ResolvePath(config.directory, base, config.directory, error)) return false;
    DWORD attributes = GetFileAttributesW(config.path.c_str());
    if (attributes == INVALID_FILE_ATTRIBUTES || (attributes & FILE_ATTRIBUTE_DIRECTORY)) {
        error = L"目标程序不存在：" + config.path; return false;
    }
    attributes = GetFileAttributesW(config.directory.c_str());
    if (attributes == INVALID_FILE_ATTRIBUTES || !(attributes & FILE_ATTRIBUTE_DIRECTORY)) {
        error = L"工作目录不存在：" + config.directory; return false;
    }
    return true;
}
