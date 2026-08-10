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
