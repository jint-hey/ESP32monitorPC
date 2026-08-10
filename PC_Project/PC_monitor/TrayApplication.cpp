#include "TrayApplication.h"

#include "resource.h"
#include "SerialPortEnumerator.h"

#include <cwchar>
#include <utility>
#include <vector>

namespace
{
    constexpr const wchar_t* APPLICATION_NAME =
        L"PC Hardware Monitor";

    constexpr const wchar_t* MENU_SERIAL_PORT =
        L"\u4e32\u53e3\u9009\u62e9";

    constexpr const wchar_t* MENU_EXIT =
        L"\u9000\u51fa";

    constexpr const wchar_t* NO_SERIAL_PORT =
        L"(\u672a\u53d1\u73b0\u4e32\u53e3)";

    HICON LoadApplicationIcon(
        HINSTANCE instance
    )
    {
        HICON icon = LoadIconW(
            instance,
            MAKEINTRESOURCEW(IDI_ICON1)
        );

        if (icon == nullptr)
        {
            icon = LoadIconW(
                nullptr,
                IDI_APPLICATION
            );
        }

        return icon;
    }
}

TrayApplication::TrayApplication(
    bool enableDebugLogging
)
    : enableDebugLogging_(
        enableDebugLogging
    )
{}

TrayApplication::~TrayApplication()
{
    Shutdown();
}

int TrayApplication::Run(
    HINSTANCE instance,
    int showCommand
)
{
    instance_ = instance;

    logger_.SetEnabled(
        enableDebugLogging_
    );
    ConfigureCommunication();

    if (!RegisterWindowClass() ||
        !CreateHiddenWindow(showCommand))
    {
        MessageBoxW(
            nullptr,
            L"Unable to create the tray application window.",
            APPLICATION_NAME,
            MB_OK | MB_ICONERROR
        );

        return 1;
    }

    if (!AddTrayIcon())
    {
        MessageBoxW(
            nullptr,
            L"Unable to add the notification-area icon.",
            APPLICATION_NAME,
            MB_OK | MB_ICONERROR
        );

        DestroyWindow(window_);
        window_ = nullptr;

        return 1;
    }

    if (!monitor_.Start(1000))
    {
        logger_.Error(
            L"HardwareMonitor start failed."
        );

        ShowNotification(
            APPLICATION_NAME,
            L"Hardware monitoring could not be started.",
            NIIF_ERROR
        );

        MessageBoxW(
            nullptr,
            L"Hardware monitoring could not be started.",
            APPLICATION_NAME,
            MB_OK | MB_ICONERROR
        );

        DestroyWindow(window_);
        window_ = nullptr;

        return 1;
    }

    if (enableDebugLogging_ &&
        !logger_.StartHardwareLogging(
            monitor_,
            1000))
    {
        logger_.Error(
            L"Hardware console logging could not be started."
        );
    }

    codexQuotaMonitor_.SetStatusCallback(
        [this](const CodexQuotaStatus status)
        {
            if (window_ != nullptr)
            {
                PostMessageW(
                    window_,
                    CODEX_STATUS_MESSAGE,
                    static_cast<WPARAM>(status),
                    0
                );
            }
        }
    );

    if (!codexQuotaMonitor_.Start(&logger_))
    {
        logger_.Error(
            L"Codex quota monitor thread could not be started."
        );
        HandleCodexStatus(CodexQuotaStatus::CollectorError);
    }

    if (!codexQuotaSerialSender_.Start(
            codexQuotaMonitor_,
            serial_,
            &logger_))
    {
        logger_.Error(
            L"Codex quota serial sender could not be started."
        );
    }

    TryAutomaticConnection();

    logger_.Info(
        L"Tray application started."
    );

    MSG message{};
    BOOL result = FALSE;

    while ((result = GetMessageW(
        &message,
        nullptr,
        0,
        0)) > 0)
    {
        TranslateMessage(&message);
        DispatchMessageW(&message);
    }

    Shutdown();

    if (window_ != nullptr)
    {
        DestroyWindow(window_);
        window_ = nullptr;
    }

    UnregisterClassW(
        WINDOW_CLASS_NAME,
        instance_
    );

    return result == -1 ? 1 : 0;
}

bool TrayApplication::RegisterWindowClass()
{
    WNDCLASSEXW windowClass{};
    windowClass.cbSize = sizeof(windowClass);
    windowClass.lpfnWndProc = WindowProcedure;
    windowClass.hInstance = instance_;
    windowClass.hIcon = LoadApplicationIcon(instance_);
    windowClass.hIconSm = windowClass.hIcon;
    windowClass.hCursor = LoadCursorW(nullptr, IDC_ARROW);
    windowClass.lpszClassName = WINDOW_CLASS_NAME;

    return RegisterClassExW(&windowClass) != 0;
}

bool TrayApplication::CreateHiddenWindow(
    int showCommand
)
{
    UNREFERENCED_PARAMETER(showCommand);

    window_ = CreateWindowExW(
        0,
        WINDOW_CLASS_NAME,
        APPLICATION_NAME,
        WS_OVERLAPPED,
        CW_USEDEFAULT,
        CW_USEDEFAULT,
        CW_USEDEFAULT,
        CW_USEDEFAULT,
        nullptr,
        nullptr,
        instance_,
        this
    );

    taskbarCreatedMessage_ =
        RegisterWindowMessageW(
            L"TaskbarCreated"
        );

    return window_ != nullptr;
}

LRESULT CALLBACK TrayApplication::WindowProcedure(
    HWND window,
    UINT message,
    WPARAM wParam,
    LPARAM lParam
)
{
    TrayApplication* application =
        reinterpret_cast<TrayApplication*>(
            GetWindowLongPtrW(
                window,
                GWLP_USERDATA
            )
            );

    if (message == WM_NCCREATE)
    {
        const auto* create =
            reinterpret_cast<CREATESTRUCTW*>(
                lParam
                );

        application =
            static_cast<TrayApplication*>(
                create->lpCreateParams
                );

        SetWindowLongPtrW(
            window,
            GWLP_USERDATA,
            reinterpret_cast<LONG_PTR>(
                application
                )
        );
    }

    if (application != nullptr)
    {
        return application->HandleWindowMessage(
            window,
            message,
            wParam,
            lParam
        );
    }

    return DefWindowProcW(
        window,
        message,
        wParam,
        lParam
    );
}

LRESULT TrayApplication::HandleWindowMessage(
    HWND window,
    UINT message,
    WPARAM wParam,
    LPARAM lParam
)
{
    UNREFERENCED_PARAMETER(wParam);

    if (taskbarCreatedMessage_ != 0 &&
        message == taskbarCreatedMessage_)
    {
        trayIconAdded_ = false;
        AddTrayIcon();
        return 0;
    }

    switch (message)
    {
    case TRAY_CALLBACK_MESSAGE:
    {
        const UINT eventMessage =
            LOWORD(lParam);

        if (eventMessage == WM_CONTEXTMENU ||
            eventMessage == WM_RBUTTONUP)
        {
            ShowContextMenu();
        }

        return 0;
    }

    case WM_QUERYENDSESSION:
        return TRUE;

    case CODEX_STATUS_MESSAGE:
        HandleCodexStatus(
            static_cast<CodexQuotaStatus>(wParam)
        );
        return 0;

    case WM_ENDSESSION:
        if (wParam != FALSE)
        {
            Shutdown();
        }
        return 0;

    case WM_CLOSE:
        Shutdown();
        DestroyWindow(window);
        return 0;

    case WM_DESTROY:
        Shutdown();
        window_ = nullptr;
        PostQuitMessage(0);
        return 0;

    default:
        break;
    }

    return DefWindowProcW(
        window,
        message,
        wParam,
        lParam
    );
}

bool TrayApplication::AddTrayIcon()
{
    if (window_ == nullptr)
    {
        return false;
    }

    trayIconData_ = {};
    trayIconData_.cbSize = sizeof(trayIconData_);
    trayIconData_.hWnd = window_;
    trayIconData_.uID = TRAY_ICON_ID;
    trayIconData_.uFlags =
        NIF_MESSAGE |
        NIF_ICON |
        NIF_TIP;
    trayIconData_.uCallbackMessage =
        TRAY_CALLBACK_MESSAGE;
    trayIconData_.hIcon =
        LoadApplicationIcon(instance_);

    wcscpy_s(
        trayIconData_.szTip,
        APPLICATION_NAME
    );

    if (!Shell_NotifyIconW(
        NIM_ADD,
        &trayIconData_))
    {
        return false;
    }

    trayIconData_.uVersion =
        NOTIFYICON_VERSION_4;

    Shell_NotifyIconW(
        NIM_SETVERSION,
        &trayIconData_
    );

    trayIconAdded_ = true;
    UpdateTrayTooltip();

    return true;
}

void TrayApplication::RemoveTrayIcon()
{
    if (!trayIconAdded_)
    {
        return;
    }

    Shell_NotifyIconW(
        NIM_DELETE,
        &trayIconData_
    );

    trayIconAdded_ = false;
}

void TrayApplication::UpdateTrayTooltip()
{
    if (!trayIconAdded_)
    {
        return;
    }

    std::wstring tooltip =
        APPLICATION_NAME;

    if (!currentPort_.empty() &&
        serial_.IsRunning())
    {
        tooltip += L" - ";
        tooltip += currentPort_;
    }
    else
    {
        tooltip +=
            L" - \u672a\u8fde\u63a5";
    }

    trayIconData_.uFlags = NIF_TIP;

    wcsncpy_s(
        trayIconData_.szTip,
        tooltip.c_str(),
        _TRUNCATE
    );

    Shell_NotifyIconW(
        NIM_MODIFY,
        &trayIconData_
    );
}

void TrayApplication::ShowContextMenu()
{
    const std::vector<std::wstring> ports =
        SerialPortEnumerator::Enumerate();

    HMENU rootMenu = CreatePopupMenu();
    HMENU portMenu = CreatePopupMenu();

    if (rootMenu == nullptr ||
        portMenu == nullptr)
    {
        if (portMenu != nullptr)
        {
            DestroyMenu(portMenu);
        }

        if (rootMenu != nullptr)
        {
            DestroyMenu(rootMenu);
        }

        return;
    }

    if (ports.empty())
    {
        AppendMenuW(
            portMenu,
            MF_STRING | MF_GRAYED,
            0,
            NO_SERIAL_PORT
        );
    }
    else
    {
        for (std::size_t index = 0;
            index < ports.size();
            ++index)
        {
            UINT flags = MF_STRING;

            if (ports[index] == currentPort_ &&
                serial_.IsRunning())
            {
                flags |= MF_CHECKED;
            }

            AppendMenuW(
                portMenu,
                flags,
                MENU_PORT_BASE_ID +
                static_cast<UINT>(index),
                ports[index].c_str()
            );
        }
    }

    AppendMenuW(
        rootMenu,
        MF_POPUP,
        reinterpret_cast<UINT_PTR>(
            portMenu
            ),
        MENU_SERIAL_PORT
    );

    AppendMenuW(
        rootMenu,
        MF_STRING,
        MENU_EXIT_ID,
        MENU_EXIT
    );

    POINT cursor{};
    GetCursorPos(&cursor);

    SetForegroundWindow(window_);

    const UINT command = TrackPopupMenuEx(
        rootMenu,
        TPM_RETURNCMD |
        TPM_RIGHTBUTTON |
        TPM_NONOTIFY,
        cursor.x,
        cursor.y,
        window_,
        nullptr
    );

    PostMessageW(
        window_,
        WM_NULL,
        0,
        0
    );

    DestroyMenu(rootMenu);

    if (command == MENU_EXIT_ID)
    {
        PostMessageW(
            window_,
            WM_CLOSE,
            0,
            0
        );

        return;
    }

    if (command >= MENU_PORT_BASE_ID)
    {
        const std::size_t index =
            command - MENU_PORT_BASE_ID;

        if (index < ports.size())
        {
            SelectPort(ports[index]);
        }
    }
}

void TrayApplication::ShowNotification(
    const std::wstring& title,
    const std::wstring& message,
    DWORD iconFlags
)
{
    if (!trayIconAdded_)
    {
        return;
    }

    NOTIFYICONDATAW notification =
        trayIconData_;

    notification.uFlags = NIF_INFO;
    notification.dwInfoFlags = iconFlags;

    wcsncpy_s(
        notification.szInfoTitle,
        title.c_str(),
        _TRUNCATE
    );

    wcsncpy_s(
        notification.szInfo,
        message.c_str(),
        _TRUNCATE
    );

    Shell_NotifyIconW(
        NIM_MODIFY,
        &notification
    );
}

void TrayApplication::HandleCodexStatus(
    const CodexQuotaStatus status
)
{
    if (status == CodexQuotaStatus::AuthRequired)
    {
        if (!codexAuthNotificationShown_)
        {
            codexAuthNotificationShown_ = true;
            ShowNotification(
                APPLICATION_NAME,
                L"Codex 额度不可用：请先在 Codex 中登录 ChatGPT。",
                NIIF_WARNING
            );
        }
        return;
    }

    if ((status == CodexQuotaStatus::Unavailable ||
         status == CodexQuotaStatus::CollectorError) &&
        !codexUnavailableNotificationShown_)
    {
        codexUnavailableNotificationShown_ = true;
        ShowNotification(
            APPLICATION_NAME,
            L"Codex 额度采集暂不可用，硬件监控仍会继续运行。",
            NIIF_WARNING
        );
    }
}

void TrayApplication::ConfigureCommunication()
{
    serial_.SetLogger(&logger_);

    serial_.SetReceiveCallback(
        [this](const Packet& packet)
        {
            if (packet.type == PacketType::Ping)
            {
                serial_.SendPacket(
                    PacketType::Pong
                );
            }
        }
    );
}

void TrayApplication::TryAutomaticConnection()
{
    const std::vector<std::wstring> ports =
        SerialPortEnumerator::Enumerate();

    if (ports.empty())
    {
        UpdateTrayTooltip();

        ShowNotification(
            APPLICATION_NAME,
            L"\u672a\u53d1\u73b0\u53ef\u7528\u4e32\u53e3\uff0c\u8bf7\u7a0d\u540e\u4ece\u6258\u76d8\u83dc\u5355\u9009\u62e9\u3002",
            NIIF_WARNING
        );

        return;
    }

    std::wstring preferredPort =
        ports.front();

    for (const auto& port : ports)
    {
        if (port == L"COM4")
        {
            preferredPort = port;
            break;
        }
    }

    SelectPort(preferredPort);
}

bool TrayApplication::SelectPort(
    const std::wstring& portName
)
{
    if (portName.empty())
    {
        return false;
    }

    if (portName == currentPort_ &&
        serial_.IsRunning())
    {
        return true;
    }

    const std::wstring previousPort =
        currentPort_;

    const bool hadPreviousConnection =
        !previousPort.empty() &&
        serial_.IsRunning();

    DisconnectPort();

    if (OpenPort(portName))
    {
        ShowNotification(
            APPLICATION_NAME,
            L"\u5df2\u8fde\u63a5 " + portName,
            NIIF_INFO
        );

        return true;
    }

    std::wstring failureMessage =
        L"\u65e0\u6cd5\u6253\u5f00 " + portName;

    if (hadPreviousConnection &&
        OpenPort(previousPort))
    {
        failureMessage +=
            L"\uff0c\u5df2\u6062\u590d " + previousPort;
    }

    ShowNotification(
        APPLICATION_NAME,
        failureMessage,
        NIIF_ERROR
    );

    UpdateTrayTooltip();
    return false;
}

bool TrayApplication::OpenPort(
    const std::wstring& portName
)
{
    if (!serial_.Open(
        portName,
        SERIAL_BAUD_RATE))
    {
        currentPort_.clear();
        UpdateTrayTooltip();
        return false;
    }

    serial_.SendPacket(
        PacketType::Ping
    );

    if (!serialSender_.Start(
        monitor_,
        serial_,
        1000))
    {
        serial_.Close();
        currentPort_.clear();
        UpdateTrayTooltip();
        return false;
    }

    currentPort_ = portName;
    codexQuotaSerialSender_.NotifySerialConnected();
    UpdateTrayTooltip();
    return true;
}

void TrayApplication::DisconnectPort()
{
    serialSender_.Stop();
    serial_.Close();
    currentPort_.clear();
    UpdateTrayTooltip();
}

void TrayApplication::Shutdown()
{
    if (shutdownComplete_)
    {
        return;
    }

    shutdownComplete_ = true;

    logger_.Info(
        L"Tray application is shutting down."
    );

    codexQuotaSerialSender_.Stop();
    serialSender_.Stop();
    serial_.Close();
    serial_.SetReceiveCallback({});
    codexQuotaMonitor_.SetStatusCallback({});
    codexQuotaMonitor_.Stop();
    logger_.StopHardwareLogging();
    monitor_.Stop();

    currentPort_.clear();
    RemoveTrayIcon();
}
