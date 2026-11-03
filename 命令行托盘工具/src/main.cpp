#include <windows.h>
#include <shellapi.h>
#include <string>
#include <vector>
#include <cwctype>
#include "config.h"
#include "startup.h"
#include "resource.h"

static const UINT TrayMessage = WM_APP + 1;
enum MenuCommand { Show = 100, Hide, Start, Restart, Startup, Exit };
static HWND mainWindow = NULL;
static HANDLE childProcess = NULL;
static HANDLE childJob = NULL;
static bool manageProcessTree = true;
static bool visible = false;
static UINT taskbarCreated = 0;
static NOTIFYICONDATAW tray = {};

static void Report(const std::wstring& text) {
    MessageBoxW(mainWindow, text.c_str(), L"命令行托盘工具", MB_OK | MB_ICONERROR);
}

static bool Running() {
    return childProcess && WaitForSingleObject(childProcess, 0) == WAIT_TIMEOUT;
}

static bool StopProgram() {
    if (childJob) {
        if (!TerminateJobObject(childJob, 0)) {
            Report(L"停止进程树失败：" + SystemError(GetLastError()));
            return false;
        }
        CloseHandle(childJob);
        childJob = NULL;
    }
    if (childProcess) {
        if (WaitForSingleObject(childProcess, 0) == WAIT_TIMEOUT &&
            !TerminateProcess(childProcess, 0)) {
            if (WaitForSingleObject(childProcess, 0) != WAIT_OBJECT_0) {
                Report(L"停止目标进程失败：" + SystemError(GetLastError()));
                return false;
            }
        }
        if (WaitForSingleObject(childProcess, 5000) != WAIT_OBJECT_0) {
            Report(L"目标进程尚未退出，已取消本次操作。");
            return false;
        }
        CloseHandle(childProcess);
        childProcess = NULL;
    }
    return true;
}

static BOOL CALLBACK SetChildVisibility(HWND window, LPARAM show) {
    DWORD processId = 0;
    GetWindowThreadProcessId(window, &processId);
    HANDLE process = OpenProcess(PROCESS_QUERY_LIMITED_INFORMATION, FALSE, processId);
    BOOL belongs = childProcess && processId == GetProcessId(childProcess);
    if (process && childJob) IsProcessInJob(process, childJob, &belongs);
    if (process) CloseHandle(process);
    if (belongs && !GetWindow(window, GW_OWNER)) {
        ShowWindow(window, show ? SW_SHOW : SW_HIDE);
        if (show) SetForegroundWindow(window);
    }
    return TRUE;
}

static void SetVisibility(bool show) {
    visible = show;
    HWND console = GetConsoleWindow();
    if (console) {
        ShowWindow(console, show ? SW_SHOW : SW_HIDE);
        if (show) SetForegroundWindow(console);
    }
    EnumWindows(SetChildVisibility, show);
}

static bool StartProgram() {
    if (Running()) return true;
    ProgramConfig config;
    std::wstring error;
    if (!LoadConfig(config, error)) { Report(error); return false; }
    visible = !config.hideOnStart;
    if (!StopProgram()) return false;
    manageProcessTree = config.manageProcessTree;
    if (manageProcessTree) {
        childJob = CreateJobObjectW(NULL, NULL);
        if (!childJob) { Report(SystemError(GetLastError())); return false; }
        JOBOBJECT_EXTENDED_LIMIT_INFORMATION limits = {};
        limits.BasicLimitInformation.LimitFlags = JOB_OBJECT_LIMIT_KILL_ON_JOB_CLOSE;
        if (!SetInformationJobObject(childJob, JobObjectExtendedLimitInformation, &limits, sizeof(limits))) {
            error = SystemError(GetLastError()); StopProgram(); Report(error); return false;
        }
    }
    std::wstring command = L"\"" + config.path + L"\"";
    if (!config.arguments.empty()) command += L" " + config.arguments;
    if (command.size() >= 32767) {
        StopProgram(); Report(L"程序路径与参数总长度超过 Windows 限制。"); return false;
    }
    std::vector<wchar_t> mutableCommand(command.begin(), command.end());
    mutableCommand.push_back(L'\0');
    STARTUPINFOW startup = {};
    startup.cb = sizeof(startup);
    startup.dwFlags = STARTF_USESHOWWINDOW;
    startup.wShowWindow = visible ? SW_SHOW : SW_HIDE;
    PROCESS_INFORMATION process = {};
    if (!CreateProcessW(config.path.c_str(), mutableCommand.data(), NULL, NULL, FALSE,
            CREATE_SUSPENDED | CREATE_UNICODE_ENVIRONMENT, NULL, config.directory.c_str(), &startup, &process)) {
        error = L"启动失败：" + SystemError(GetLastError()); StopProgram(); Report(error); return false;
    }
    childProcess = process.hProcess;
    if (manageProcessTree && !AssignProcessToJobObject(childJob, childProcess)) {
        error = SystemError(GetLastError());
        TerminateProcess(childProcess, 1);
        CloseHandle(process.hThread); StopProgram(); Report(error); return false;
    }
    if (ResumeThread(process.hThread) == static_cast<DWORD>(-1)) {
        error = SystemError(GetLastError());
        CloseHandle(process.hThread); StopProgram(); Report(error); return false;
    }
    CloseHandle(process.hThread);
    SetVisibility(visible);
    return true;
}

static bool AddTray() {
    tray = NOTIFYICONDATAW();
    tray.cbSize = sizeof(tray);
    tray.hWnd = mainWindow;
    tray.uID = 1;
    tray.uFlags = NIF_MESSAGE | NIF_ICON | NIF_TIP;
    tray.uCallbackMessage = TrayMessage;
    tray.hIcon = static_cast<HICON>(LoadImageW(GetModuleHandleW(NULL), MAKEINTRESOURCEW(IDI_TRAY),
        IMAGE_ICON, GetSystemMetrics(SM_CXSMICON), GetSystemMetrics(SM_CYSMICON), LR_SHARED));
    if (!tray.hIcon) return false;
    lstrcpynW(tray.szTip, L"命令行托盘工具", ARRAYSIZE(tray.szTip));
    return Shell_NotifyIconW(NIM_ADD, &tray) != FALSE;
}

static void ShowMenu() {
    HMENU menu = CreatePopupMenu();
    if (!menu) return;
    bool running = Running();
    AppendMenuW(menu, MF_STRING | MF_DISABLED, 0, running ? L"状态：正在运行" : L"状态：已停止");
    AppendMenuW(menu, MF_SEPARATOR, 0, NULL);
    AppendMenuW(menu, MF_STRING, Show, L"打开界面");
    AppendMenuW(menu, MF_STRING, Hide, L"隐藏界面");
    AppendMenuW(menu, MF_STRING | (running ? MF_GRAYED : 0), Start, L"启动");
    AppendMenuW(menu, MF_STRING, Restart, L"重启（重新读取配置）");
    AppendMenuW(menu, MF_SEPARATOR, 0, NULL);
    AppendMenuW(menu, MF_STRING | (IsStartupEnabled() ? MF_CHECKED : 0), Startup, L"开机自启（登录时）");
    AppendMenuW(menu, MF_SEPARATOR, 0, NULL);
    AppendMenuW(menu, MF_STRING, Exit, L"退出并停止程序");
    POINT cursor;
    GetCursorPos(&cursor);
    SetForegroundWindow(mainWindow);
    UINT command = TrackPopupMenu(menu, TPM_RETURNCMD | TPM_RIGHTBUTTON,
        cursor.x, cursor.y, 0, mainWindow, NULL);
    DestroyMenu(menu);
    PostMessageW(mainWindow, WM_NULL, 0, 0);
    if (command) SendMessageW(mainWindow, WM_COMMAND, command, 0);
}

static LRESULT CALLBACK WindowProcedure(HWND window, UINT message, WPARAM parameter, LPARAM data) {
    if (taskbarCreated && message == taskbarCreated) {
        if (AddTray()) KillTimer(window, 1);
        else SetTimer(window, 1, 2000, NULL);
        return 0;
    }
    switch (message) {
    case WM_TIMER:
        if (AddTray()) KillTimer(window, 1);
        return 0;
    case TrayMessage:
        if (data == WM_RBUTTONUP || data == WM_CONTEXTMENU) ShowMenu();
        else if (data == WM_LBUTTONDBLCLK) SetVisibility(!visible);
        return 0;
    case WM_COMMAND:
        switch (LOWORD(parameter)) {
        case Show: SetVisibility(true); break;
        case Hide: SetVisibility(false); break;
        case Start: StartProgram(); break;
        case Restart: if (StopProgram()) StartProgram(); break;
        case Startup: {
            std::wstring error;
            if (!SetStartupEnabled(!IsStartupEnabled(), error)) Report(error);
            break;
        }
        case Exit: if (StopProgram()) DestroyWindow(window); break;
        }
        return 0;
    case WM_CLOSE: if (StopProgram()) DestroyWindow(window); return 0;
    case WM_DESTROY:
        KillTimer(window, 1);
        Shell_NotifyIconW(NIM_DELETE, &tray);
        StopProgram();
        PostQuitMessage(0);
        return 0;
    }
    return DefWindowProcW(window, message, parameter, data);
}

int WINAPI wWinMain(HINSTANCE instance, HINSTANCE, PWSTR, int) {
    std::wstring identity = ExecutablePath();
    for (size_t index = 0; index < identity.size(); ++index) {
        if (identity[index] == L'\\' || identity[index] == L'/') identity[index] = L'_';
        identity[index] = static_cast<wchar_t>(towlower(identity[index]));
    }
    HANDLE mutex = CreateMutexW(NULL, TRUE, (L"Local\\CommandTray_" + identity).c_str());
    DWORD mutexError = GetLastError();
    if (!mutex || mutexError == ERROR_ALREADY_EXISTS) {
        if (mutex) CloseHandle(mutex);
        MessageBoxW(NULL, mutex ? L"此目录的托盘工具已在运行。" : L"无法创建单实例锁。",
            L"命令行托盘工具", MB_OK | MB_ICONINFORMATION);
        return 1;
    }
    if (!AllocConsole()) { Report(L"无法创建目标程序控制台。"); CloseHandle(mutex); return 1; }
    HWND console = GetConsoleWindow();
    ProgramConfig initialConfig;
    std::wstring initialError;
    if (LoadConfig(initialConfig, initialError)) visible = !initialConfig.hideOnStart;
    ShowWindow(console, visible ? SW_SHOW : SW_HIDE);
    SetConsoleTitleW(L"命令行托盘工具 — 程序输出");
    HMENU consoleMenu = GetSystemMenu(console, FALSE);
    if (consoleMenu) DeleteMenu(consoleMenu, SC_CLOSE, MF_BYCOMMAND);
    SetConsoleCtrlHandler(NULL, TRUE);
    WNDCLASSW windowClass = {};
    windowClass.lpfnWndProc = WindowProcedure;
    windowClass.hInstance = instance;
    windowClass.hIcon = LoadIconW(instance, MAKEINTRESOURCEW(IDI_TRAY));
    windowClass.lpszClassName = L"CommandTrayWindow";
    if (!RegisterClassW(&windowClass)) {
        Report(SystemError(GetLastError())); FreeConsole(); CloseHandle(mutex); return 1;
    }
    taskbarCreated = RegisterWindowMessageW(L"TaskbarCreated");
    mainWindow = CreateWindowExW(WS_EX_TOOLWINDOW, windowClass.lpszClassName,
        L"命令行托盘工具", WS_OVERLAPPED, 0, 0, 0, 0, NULL, NULL, instance, NULL);
    if (!mainWindow) {
        Report(L"无法创建托盘窗口。");
        if (mainWindow) DestroyWindow(mainWindow);
        FreeConsole(); CloseHandle(mutex); return 1;
    }
    ChangeWindowMessageFilterEx(mainWindow, taskbarCreated, MSGFLT_ALLOW, NULL);
    if (!AddTray()) SetTimer(mainWindow, 1, 2000, NULL);
    StartProgram();
    MSG message = {};
    BOOL result;
    while ((result = GetMessageW(&message, NULL, 0, 0)) > 0) {
        TranslateMessage(&message);
        DispatchMessageW(&message);
    }
    if (result == -1) { Shell_NotifyIconW(NIM_DELETE, &tray); StopProgram(); }
    FreeConsole();
    ReleaseMutex(mutex);
    CloseHandle(mutex);
    return result == -1 ? 1 : 0;
}
