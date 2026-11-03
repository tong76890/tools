#include "startup.h"
#include "config.h"

#ifndef UNICODE
#define UNICODE
#endif
#ifndef _UNICODE
#define _UNICODE
#endif

#include <windows.h>
#include <taskschd.h>
#include <sddl.h>
#include <string>
#include <sstream>
#include <iomanip>
#include <vector>

#ifdef _MSC_VER
#pragma comment(lib, "taskschd.lib")
#pragma comment(lib, "ole32.lib")
#pragma comment(lib, "oleaut32.lib")
#pragma comment(lib, "advapi32.lib")
#endif

namespace {

class Bstr {
public:
    explicit Bstr(const wchar_t* text) : value_(SysAllocString(text ? text : L"")) {}
    ~Bstr() { SysFreeString(value_); }
    operator BSTR() const { return value_; }
private:
    BSTR value_;
};

class ComScope {
public:
    ComScope() : hr_(CoInitializeEx(nullptr, COINIT_MULTITHREADED)) {}
    ~ComScope() { if (SUCCEEDED(hr_)) CoUninitialize(); }
    bool Ok() const { return SUCCEEDED(hr_) || hr_ == RPC_E_CHANGED_MODE; }
private:
    HRESULT hr_;
};

struct Variant {
    VARIANT value;
    Variant() { VariantInit(&value); }
    ~Variant() { VariantClear(&value); }
};

std::wstring ModulePath() {
    std::wstring path(512, L'\0');
    for (;;) {
        DWORD length = GetModuleFileNameW(nullptr, &path[0], static_cast<DWORD>(path.size()));
        if (!length) return std::wstring();
        if (length < path.size() - 1) {
            path.resize(length);
            return path;
        }
        path.resize(path.size() * 2);
    }
}

std::wstring TaskName(const std::wstring& userSid = L"") {
    const std::wstring path = ModulePath();
    unsigned long long hash = 1469598103934665603ULL;
    for (wchar_t c : path) {
        wchar_t folded = (c >= L'A' && c <= L'Z') ? static_cast<wchar_t>(c + (L'a' - L'A')) : c;
        hash ^= static_cast<unsigned long long>(folded);
        hash *= 1099511628211ULL;
    }
    std::wstringstream stream;
    stream << L"CommandTray_" << std::hex << std::setfill(L'0') << std::setw(16) << hash;
    return stream.str() + (userSid.empty() ? L"" : L"_" + userSid);
}

std::wstring ErrorText(HRESULT hr, const wchar_t* operation) {
    wchar_t* systemMessage = nullptr;
    FormatMessageW(FORMAT_MESSAGE_ALLOCATE_BUFFER | FORMAT_MESSAGE_FROM_SYSTEM |
                       FORMAT_MESSAGE_IGNORE_INSERTS, nullptr, hr, 0,
                   reinterpret_cast<LPWSTR>(&systemMessage), 0, nullptr);
    std::wstring result(operation);
    result += L" (0x";
    std::wstringstream stream;
    stream << std::hex << static_cast<unsigned long>(hr);
    result += stream.str();
    result += L")";
    if (systemMessage) {
        result += L": ";
        result += systemMessage;
        LocalFree(systemMessage);
    }
    return result;
}

HRESULT ConnectRoot(ITaskService** service, ITaskFolder** root) {
    *service = nullptr;
    *root = nullptr;
    HRESULT hr = CoCreateInstance(CLSID_TaskScheduler, nullptr, CLSCTX_INPROC_SERVER,
                                  IID_ITaskService, reinterpret_cast<void**>(service));
    if (FAILED(hr)) return hr;
    Variant empty;
    hr = (*service)->Connect(empty.value, empty.value, empty.value, empty.value);
    if (FAILED(hr)) return hr;
    return (*service)->GetFolder(Bstr(L"\\"), root);
}

HRESULT CurrentUserSid(std::wstring& userSid) {
    HANDLE token = nullptr;
    if (!OpenProcessToken(GetCurrentProcess(), TOKEN_QUERY, &token))
        return HRESULT_FROM_WIN32(GetLastError());
    DWORD tokenSize = 0;
    GetTokenInformation(token, TokenUser, nullptr, 0, &tokenSize);
    std::vector<BYTE> tokenBuffer(tokenSize);
    BOOL tokenOk = GetTokenInformation(token, TokenUser, tokenBuffer.data(), tokenSize, &tokenSize);
    DWORD tokenError = GetLastError();
    CloseHandle(token);
    if (!tokenOk) return HRESULT_FROM_WIN32(tokenError);
    wchar_t* sidText = nullptr;
    if (!ConvertSidToStringSidW(reinterpret_cast<TOKEN_USER*>(tokenBuffer.data())->User.Sid, &sidText))
        return HRESULT_FROM_WIN32(GetLastError());
    userSid = sidText;
    LocalFree(sidText);
    return S_OK;
}

HRESULT GetOwnedTask(ITaskFolder* root, const std::wstring& name,
                     const std::wstring& userSid, IRegisteredTask** task) {
    *task = nullptr;
    IRegisteredTask* candidate = nullptr;
    HRESULT hr = root->GetTask(Bstr(name.c_str()), &candidate);
    if (hr == HRESULT_FROM_WIN32(ERROR_FILE_NOT_FOUND) || hr == HRESULT_FROM_WIN32(ERROR_PATH_NOT_FOUND)) return S_FALSE;
    if (FAILED(hr)) return hr;
    ITaskDefinition* definition = nullptr;
    IPrincipal* principal = nullptr;
    BSTR account = nullptr;
    hr = candidate->get_Definition(&definition);
    if (SUCCEEDED(hr)) hr = definition->get_Principal(&principal);
    if (SUCCEEDED(hr)) hr = principal->get_UserId(&account);
    bool owned = false;
    if (SUCCEEDED(hr) && account) {
        owned = userSid == account;
        if (!owned) {
            PSID accountSid = nullptr;
            if (ConvertStringSidToSidW(account, &accountSid)) {
                LocalFree(accountSid);
            } else {
                DWORD sidSize = 0, domainSize = 0;
                SID_NAME_USE accountType;
                LookupAccountNameW(nullptr, account, nullptr, &sidSize, nullptr, &domainSize, &accountType);
                std::vector<BYTE> sidBuffer(sidSize);
                std::vector<wchar_t> domain(domainSize);
                if (sidSize && LookupAccountNameW(nullptr, account, sidBuffer.data(), &sidSize,
                                                  domain.data(), &domainSize, &accountType)) {
                    wchar_t* sidText = nullptr;
                    if (ConvertSidToStringSidW(sidBuffer.data(), &sidText)) {
                        owned = userSid == sidText;
                        LocalFree(sidText);
                    } else hr = HRESULT_FROM_WIN32(GetLastError());
                } else hr = HRESULT_FROM_WIN32(GetLastError());
            }
        }
    }
    SysFreeString(account);
    if (principal) principal->Release();
    if (definition) definition->Release();
    if (FAILED(hr) || !owned) {
        candidate->Release();
        return FAILED(hr) ? hr : S_FALSE;
    }
    *task = candidate;
    return S_OK;
}

bool RegisterTask(ITaskService* service, ITaskFolder* root, const std::wstring& userSid,
                  std::wstring& error) {
    const std::wstring taskName = TaskName(userSid);
    IRegisteredTask* existing = nullptr;
    HRESULT existingStatus = root->GetTask(Bstr(taskName.c_str()), &existing);
    if (existing) existing->Release();
    if (SUCCEEDED(existingStatus)) {
        existingStatus = GetOwnedTask(root, taskName, userSid, &existing);
        if (existing) existing->Release();
        if (existingStatus != S_OK) {
            error = L"同名自启任务不属于当前用户或无法确认归属，未修改。";
            return false;
        }
    } else if (existingStatus != HRESULT_FROM_WIN32(ERROR_FILE_NOT_FOUND) &&
               existingStatus != HRESULT_FROM_WIN32(ERROR_PATH_NOT_FOUND)) {
        error = ErrorText(existingStatus, L"检查开机自启任务失败"); return false;
    }
    ITaskDefinition* definition = nullptr;
    HRESULT hr = service->NewTask(0, &definition);
    if (FAILED(hr)) return (error = ErrorText(hr, L"创建任务定义失败"), false);
    IRegistrationInfo* registration = nullptr;
    ITaskSettings* settings = nullptr;
    IPrincipal* principal = nullptr;
    ITriggerCollection* triggers = nullptr;
    ITrigger* trigger = nullptr;
    IActionCollection* actions = nullptr;
    IAction* action = nullptr;
    IExecAction* exec = nullptr;
    do {
        hr = definition->get_RegistrationInfo(&registration); if (FAILED(hr)) break;
        hr = registration->put_Author(Bstr(L"Command Tray")); if (FAILED(hr)) break;
        hr = definition->get_Settings(&settings); if (FAILED(hr)) break;
        hr = settings->put_StartWhenAvailable(VARIANT_TRUE); if (FAILED(hr)) break;
        hr = settings->put_ExecutionTimeLimit(Bstr(L"PT0S")); if (FAILED(hr)) break;
        hr = settings->put_DisallowStartIfOnBatteries(VARIANT_FALSE); if (FAILED(hr)) break;
        hr = settings->put_StopIfGoingOnBatteries(VARIANT_FALSE); if (FAILED(hr)) break;
        hr = definition->get_Principal(&principal); if (FAILED(hr)) break;
        hr = principal->put_RunLevel(TASK_RUNLEVEL_HIGHEST); if (FAILED(hr)) break;
        hr = principal->put_LogonType(TASK_LOGON_INTERACTIVE_TOKEN); if (FAILED(hr)) break;
        hr = principal->put_UserId(Bstr(userSid.c_str())); if (FAILED(hr)) break;
        hr = definition->get_Triggers(&triggers); if (FAILED(hr)) break;
        hr = triggers->Create(TASK_TRIGGER_LOGON, &trigger); if (FAILED(hr)) break;
        hr = definition->get_Actions(&actions); if (FAILED(hr)) break;
        hr = actions->Create(TASK_ACTION_EXEC, &action); if (FAILED(hr)) break;
        hr = action->QueryInterface(IID_IExecAction, reinterpret_cast<void**>(&exec)); if (FAILED(hr)) break;
        const std::wstring path = ModulePath();
        if (path.empty()) { hr = E_FAIL; break; }
        hr = exec->put_Path(Bstr(path.c_str())); if (FAILED(hr)) break;
        const std::wstring directory = ParentDirectory(path);
        hr = exec->put_WorkingDirectory(Bstr(directory.c_str())); if (FAILED(hr)) break;
        BSTR xmlText = nullptr;
        hr = definition->get_XmlText(&xmlText); if (FAILED(hr)) break;
        std::wstring xml(xmlText, SysStringLen(xmlText));
        SysFreeString(xmlText);
        size_t triggerEnd = xml.find(L"</LogonTrigger>");
        if (triggerEnd == std::wstring::npos) { hr = E_FAIL; break; }
        xml.insert(triggerEnd, L"<UserId>" + userSid + L"</UserId>");
        hr = definition->put_XmlText(Bstr(xml.c_str())); if (FAILED(hr)) break;
        Variant empty;
        IRegisteredTask* registered = nullptr;
        const std::wstring name = TaskName(userSid);
        hr = root->RegisterTaskDefinition(Bstr(name.c_str()), definition,
                                          TASK_CREATE_OR_UPDATE, empty.value, empty.value,
                                          TASK_LOGON_INTERACTIVE_TOKEN, empty.value, &registered);
        if (registered) registered->Release();
    } while (false);
    if (exec) exec->Release();
    if (action) action->Release();
    if (actions) actions->Release();
    if (trigger) trigger->Release();
    if (triggers) triggers->Release();
    if (principal) principal->Release();
    if (settings) settings->Release();
    if (registration) registration->Release();
    definition->Release();
    if (FAILED(hr)) { error = ErrorText(hr, L"注册开机自启任务失败"); return false; }
    return true;
}

bool DeleteOwnedTask(ITaskFolder* root, const std::wstring& name,
                     const std::wstring& userSid, std::wstring& error) {
    IRegisteredTask* task = nullptr;
    HRESULT hr = GetOwnedTask(root, name, userSid, &task);
    if (task) task->Release();
    if (hr == S_FALSE) return true;
    if (FAILED(hr)) { error = ErrorText(hr, L"检查自启任务归属失败"); return false; }
    hr = root->DeleteTask(Bstr(name.c_str()), 0);
    if (FAILED(hr) && hr != HRESULT_FROM_WIN32(ERROR_FILE_NOT_FOUND)) {
        error = ErrorText(hr, L"删除开机自启任务失败"); return false;
    }
    return true;
}

} // namespace

bool IsStartupEnabled() {
    ComScope com;
    if (!com.Ok()) return false;
    std::wstring userSid;
    if (FAILED(CurrentUserSid(userSid))) return false;
    ITaskService* service = nullptr; ITaskFolder* root = nullptr;
    if (FAILED(ConnectRoot(&service, &root))) { if (service) service->Release(); return false; }
    bool exists = false;
    const std::wstring names[] = { TaskName(userSid), TaskName() };
    for (const std::wstring& name : names) {
        IRegisteredTask* task = nullptr;
        VARIANT_BOOL enabled = VARIANT_FALSE;
        if (GetOwnedTask(root, name, userSid, &task) == S_OK &&
            SUCCEEDED(task->get_Enabled(&enabled)) && enabled == VARIANT_TRUE) exists = true;
        if (task) task->Release();
    }
    root->Release();
    service->Release();
    return exists;
}

bool SetStartupEnabled(bool enabled, std::wstring& error) {
    error.clear();
    ComScope com;
    if (!com.Ok()) { error = L"初始化 COM 失败"; return false; }
    std::wstring userSid;
    HRESULT hr = CurrentUserSid(userSid);
    if (FAILED(hr)) { error = ErrorText(hr, L"读取当前用户 SID 失败"); return false; }
    ITaskService* service = nullptr; ITaskFolder* root = nullptr;
    hr = ConnectRoot(&service, &root);
    if (FAILED(hr)) { error = ErrorText(hr, L"连接任务计划程序失败"); if (service) service->Release(); return false; }
    bool ok = true;
    if (enabled) {
        ok = RegisterTask(service, root, userSid, error);
        if (ok) ok = DeleteOwnedTask(root, TaskName(), userSid, error);
    } else {
        ok = DeleteOwnedTask(root, TaskName(userSid), userSid, error);
        if (ok) ok = DeleteOwnedTask(root, TaskName(), userSid, error);
    }
    root->Release(); service->Release();
    return ok;
}
