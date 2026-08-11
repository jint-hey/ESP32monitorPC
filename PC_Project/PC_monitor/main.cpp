#ifndef NOMINMAX
#define NOMINMAX
#endif

#include <Windows.h>

#include <cstdio>
#include <fcntl.h>
#include <io.h>
#include <iostream>

#include "TrayApplication.h"

namespace
{
#ifdef _DEBUG
    constexpr bool ENABLE_DEBUG_LOGGING = true;
#else
    constexpr bool ENABLE_DEBUG_LOGGING = false;
#endif


    constexpr const wchar_t* SINGLE_INSTANCE_MUTEX_NAME =
        L"Local\\PC_monitor_{28CEC82D-439F-4397-84B3-AE0D5DB83316}";

    constexpr const wchar_t* APPLICATION_NAME =
        L"PC Hardware Monitor";

    class SingleInstanceGuard
    {
    public:
        SingleInstanceGuard()
        {
            handle_ = CreateMutexW(
                nullptr,
                FALSE,
                SINGLE_INSTANCE_MUTEX_NAME
            );

            if (handle_ != nullptr)
            {
                alreadyExists_ =
                    GetLastError() == ERROR_ALREADY_EXISTS;
            }
        }

        ~SingleInstanceGuard()
        {
            if (handle_ != nullptr)
            {
                CloseHandle(handle_);
            }
        }

        SingleInstanceGuard(const SingleInstanceGuard&) = delete;
        SingleInstanceGuard& operator=(const SingleInstanceGuard&) = delete;

        bool IsValid() const
        {
            return handle_ != nullptr;
        }

        bool AlreadyExists() const
        {
            return alreadyExists_;
        }

    private:
        HANDLE handle_ = nullptr;
        bool alreadyExists_ = false;
    };

    class DebugConsole
    {
    public:
        explicit DebugConsole(
            bool enabled
        )
        {
            if (!enabled)
            {
                return;
            }

            if (GetConsoleWindow() == nullptr)
            {
                ownsConsole_ =
                    AllocConsole() != FALSE;
            }

            if (GetConsoleWindow() == nullptr)
            {
                return;
            }

            FILE* output = nullptr;
            FILE* error = nullptr;

            const errno_t outputResult =
                freopen_s(
                    &output,
                    "CONOUT$",
                    "w",
                    stdout
                );

            const errno_t errorResult =
                freopen_s(
                    &error,
                    "CONOUT$",
                    "w",
                    stderr
                );

            if (outputResult != 0 ||
                errorResult != 0)
            {
                return;
            }

            _setmode(
                _fileno(stdout),
                _O_U16TEXT
            );

            _setmode(
                _fileno(stderr),
                _O_U16TEXT
            );

            SetConsoleTitleW(
                L"PC Hardware Monitor - Debug Log"
            );

            available_ = true;
        }

        ~DebugConsole()
        {
            if (!available_)
            {
                return;
            }

            std::wcout.flush();
            std::wcerr.flush();

            if (ownsConsole_)
            {
                FreeConsole();
            }
        }

        DebugConsole(
            const DebugConsole&
        ) = delete;

        DebugConsole& operator=(
            const DebugConsole&
            ) = delete;

        bool IsAvailable() const
        {
            return available_;
        }

    private:
        bool ownsConsole_ = false;
        bool available_ = false;
    };
}

int WINAPI wWinMain(
    HINSTANCE instance,
    HINSTANCE previousInstance,
    PWSTR commandLine,
    int showCommand
)
{
    UNREFERENCED_PARAMETER(previousInstance);
    UNREFERENCED_PARAMETER(commandLine);


    SingleInstanceGuard singleInstance;

    if (!singleInstance.IsValid())
    {
        MessageBoxW(
            nullptr,
            L"Unable to create the single-instance guard.",
            APPLICATION_NAME,
            MB_OK | MB_ICONERROR | MB_SETFOREGROUND
        );

        return 1;
    }

    if (singleInstance.AlreadyExists())
    {
        MessageBoxW(
            nullptr,
            L"PC Hardware Monitor \u5df2\u5728\u540e\u53f0\u8fd0\u884c\u3002",
            APPLICATION_NAME,
            MB_OK |
                MB_ICONINFORMATION |
                MB_SETFOREGROUND
        );

        return 0;
    }

    DebugConsole debugConsole(
        ENABLE_DEBUG_LOGGING
    );

    TrayApplication application(
        ENABLE_DEBUG_LOGGING &&
        debugConsole.IsAvailable()
    );

    return application.Run(
        instance,
        showCommand
    );
}
