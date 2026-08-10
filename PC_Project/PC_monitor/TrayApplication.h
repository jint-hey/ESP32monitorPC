#ifndef TRAY_APPLICATION_H
#define TRAY_APPLICATION_H

#ifndef NOMINMAX
#define NOMINMAX
#endif

#include <Windows.h>
#include <shellapi.h>

#include "CodexQuotaMonitor.h"
#include "CodexQuotaSerialSender.h"
#include "ConsoleLogger.h"
#include "HardwareMonitor.h"
#include "HardwareSerialSender.h"
#include "SerialCommunicator.h"

#include <string>

class TrayApplication
{
public:
    explicit TrayApplication(
        bool enableDebugLogging
    );
    ~TrayApplication();

    TrayApplication(
        const TrayApplication&
    ) = delete;

    TrayApplication& operator=(
        const TrayApplication&
        ) = delete;

    int Run(
        HINSTANCE instance,
        int showCommand
    );

private:
    static constexpr UINT TRAY_ICON_ID = 1;
    static constexpr UINT TRAY_CALLBACK_MESSAGE = WM_APP + 1;
    static constexpr UINT CODEX_STATUS_MESSAGE = WM_APP + 2;
    static constexpr UINT MENU_EXIT_ID = 100;
    static constexpr UINT MENU_PORT_BASE_ID = 1000;
    static constexpr DWORD SERIAL_BAUD_RATE = 115200;

    static constexpr const wchar_t* WINDOW_CLASS_NAME =
        L"PcHardwareMonitorTrayWindow";

    HINSTANCE instance_ = nullptr;
    HWND window_ = nullptr;
    NOTIFYICONDATAW trayIconData_{};
    UINT taskbarCreatedMessage_ = 0;

    bool trayIconAdded_ = false;
    bool shutdownComplete_ = false;
    bool enableDebugLogging_ = false;
    bool codexUnavailableNotificationShown_ = false;
    bool codexAuthNotificationShown_ = false;

    std::wstring currentPort_;

    ConsoleLogger logger_;
    HardwareMonitor monitor_;
    SerialCommunicator serial_;
    HardwareSerialSender serialSender_;
    CodexQuotaMonitor codexQuotaMonitor_;
    CodexQuotaSerialSender codexQuotaSerialSender_;

private:
    static LRESULT CALLBACK WindowProcedure(
        HWND window,
        UINT message,
        WPARAM wParam,
        LPARAM lParam
    );

    LRESULT HandleWindowMessage(
        HWND window,
        UINT message,
        WPARAM wParam,
        LPARAM lParam
    );

    bool RegisterWindowClass();
    bool CreateHiddenWindow(int showCommand);

    bool AddTrayIcon();
    void RemoveTrayIcon();
    void UpdateTrayTooltip();

    void ShowContextMenu();
    void ShowNotification(
        const std::wstring& title,
        const std::wstring& message,
        DWORD iconFlags
    );
    void HandleCodexStatus(CodexQuotaStatus status);

    void ConfigureCommunication();
    void TryAutomaticConnection();
    bool SelectPort(const std::wstring& portName);
    bool OpenPort(const std::wstring& portName);
    void DisconnectPort();

    void Shutdown();
};

#endif // TRAY_APPLICATION_H
