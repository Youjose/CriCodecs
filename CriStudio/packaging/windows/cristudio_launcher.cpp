#include <windows.h>

#include <string>
#include <vector>

namespace {

std::wstring executable_path() {
    std::vector<wchar_t> buffer(512);
    for (;;) {
        const auto length = GetModuleFileNameW(nullptr, buffer.data(), static_cast<DWORD>(buffer.size()));
        if (length == 0) {
            return {};
        }
        if (length < buffer.size() - 1) {
            return {buffer.data(), length};
        }
        buffer.resize(buffer.size() * 2);
    }
}

std::wstring error_message(const wchar_t* prefix, DWORD error) {
    wchar_t* system_message = nullptr;
    const auto length = FormatMessageW(
        FORMAT_MESSAGE_ALLOCATE_BUFFER | FORMAT_MESSAGE_FROM_SYSTEM | FORMAT_MESSAGE_IGNORE_INSERTS,
        nullptr,
        error,
        0,
        reinterpret_cast<wchar_t*>(&system_message),
        0,
        nullptr);

    std::wstring message(prefix);
    if (length != 0 && system_message != nullptr) {
        message.append(L"\n\n").append(system_message, length);
    }
    if (system_message != nullptr) {
        LocalFree(system_message);
    }
    return message;
}

int show_error(const wchar_t* prefix, DWORD error = GetLastError()) {
    const auto message = error_message(prefix, error);
    MessageBoxW(nullptr, message.c_str(), L"CriStudio", MB_OK | MB_ICONERROR);
    return 1;
}

} // namespace

int WINAPI wWinMain(HINSTANCE, HINSTANCE, PWSTR arguments, int show_command) {
    const auto launcher = executable_path();
    const auto separator = launcher.find_last_of(L"\\/");
    if (launcher.empty() || separator == std::wstring::npos) {
        return show_error(L"Could not determine the CriStudio installation directory.");
    }

    const auto application = launcher.substr(0, separator) + L"\\_internal\\CriStudio.exe";
    if (GetFileAttributesW(application.c_str()) == INVALID_FILE_ATTRIBUTES) {
        const auto error = GetLastError();
        return show_error(L"The _internal\\CriStudio.exe application could not be accessed.", error);
    }

    std::wstring command_line;
    command_line.reserve(application.size() + 3);
    command_line.push_back(L'"');
    command_line.append(application);
    command_line.push_back(L'"');
    if (arguments != nullptr && *arguments != L'\0') {
        command_line.append(L" ").append(arguments);
    }

    STARTUPINFOW startup_info = {};
    startup_info.cb = sizeof(startup_info);
    if (show_command != SW_SHOWDEFAULT) {
        startup_info.dwFlags = STARTF_USESHOWWINDOW;
        startup_info.wShowWindow = static_cast<WORD>(show_command);
    }
    PROCESS_INFORMATION process_info = {};
    if (!CreateProcessW(
            application.c_str(),
            command_line.data(),
            nullptr,
            nullptr,
            FALSE,
            0,
            nullptr,
            nullptr,
            &startup_info,
            &process_info)) {
        return show_error(L"CriStudio could not be started.");
    }

    CloseHandle(process_info.hThread);
    if (WaitForSingleObject(process_info.hProcess, INFINITE) != WAIT_OBJECT_0) {
        const auto error = GetLastError();
        CloseHandle(process_info.hProcess);
        return show_error(L"Waiting for CriStudio to close failed.", error);
    }

    DWORD exit_code = 1;
    if (!GetExitCodeProcess(process_info.hProcess, &exit_code)) {
        const auto error = GetLastError();
        CloseHandle(process_info.hProcess);
        return show_error(L"CriStudio closed, but its exit status could not be read.", error);
    }
    CloseHandle(process_info.hProcess);
    return static_cast<int>(exit_code);
}
