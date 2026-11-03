#pragma once

#include <string>

// Returns whether this executable has its per-user logon task registered.
bool IsStartupEnabled();

// Creates or removes this executable's per-user logon task.
// The caller supplies an error message only when the operation fails.
bool SetStartupEnabled(bool enabled, std::wstring& error);
