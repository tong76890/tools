#include "../../src/main.cpp"
#include <cstdio>

int wmain(int argumentCount, wchar_t**) {
    if (argumentCount > 1) { Sleep(60000); return 0; }
    if (ParentDirectory(L"C:\\sshd.exe") != L"C:\\" ||
        ParentDirectory(L"C:/sshd.exe") != L"C:/" ||
        ParentDirectory(L"C:\\tools\\sshd.exe") != L"C:\\tools") return 3;
    WNDCLASSW windowClass = {};
    windowClass.lpfnWndProc = DefWindowProcW;
    windowClass.hInstance = GetModuleHandleW(NULL);
    windowClass.lpszClassName = L"ReviewVisibilityWindow";
    RegisterClassW(&windowClass);
    HWND window = CreateWindowW(windowClass.lpszClassName, L"review", WS_OVERLAPPEDWINDOW,
        0, 0, 200, 100, NULL, NULL, windowClass.hInstance, NULL);
    if (!window) return 4;
    childProcess = GetCurrentProcess();
    SetChildVisibility(window, TRUE);
    bool shown = IsWindowVisible(window) != FALSE;
    SetChildVisibility(window, FALSE);
    bool hidden = IsWindowVisible(window) == FALSE;
    childProcess = NULL;
    DestroyWindow(window);
    if (!shown || !hidden) return 5;
    std::printf("root paths and no-job window visibility: PASS\n");
    std::wstring command = L"\"" + ExecutablePath() + L"\" --child";
    std::vector<wchar_t> buffer(command.begin(), command.end());
    buffer.push_back(0);
    STARTUPINFOW startup = {};
    startup.cb = sizeof(startup);
    PROCESS_INFORMATION process = {};
    if (!CreateProcessW(NULL, buffer.data(), NULL, NULL, FALSE, CREATE_NO_WINDOW,
        NULL, NULL, &startup, &process)) return 2;
    CloseHandle(process.hThread);
    HANDLE observer = NULL;
    DuplicateHandle(GetCurrentProcess(), process.hProcess, GetCurrentProcess(),
        &observer, 0, FALSE, DUPLICATE_SAME_ACCESS);
    childProcess = process.hProcess;
    childJob = NULL;
    manageProcessTree = false;
    StopProgram();
    bool stillRunning = WaitForSingleObject(observer, 0) == WAIT_TIMEOUT;
    std::printf("manage_process_tree=false: process alive after StopProgram: %s\n", stillRunning ? "YES" : "NO");
    TerminateProcess(observer, 0);
    WaitForSingleObject(observer, 5000);
    CloseHandle(observer);
    return stillRunning ? 1 : 0;
}
