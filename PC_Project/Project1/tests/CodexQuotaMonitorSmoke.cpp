#include "CodexQuotaMonitor.h"

#include <chrono>
#include <iostream>
#include <thread>

int main()
{
    CodexQuotaMonitor monitor;
    if (!monitor.Start(nullptr))
    {
        std::cerr << "Could not start CodexQuotaMonitor.\n";
        return 1;
    }

    CodexQuotaSnapshot snapshot;
    const auto deadline = std::chrono::steady_clock::now() + std::chrono::seconds(30);
    while (std::chrono::steady_clock::now() < deadline)
    {
        snapshot = monitor.GetSnapshot();
        if (snapshot.sequence != 0 &&
            (snapshot.status == CodexQuotaStatus::Valid ||
             snapshot.status == CodexQuotaStatus::AuthRequired))
        {
            break;
        }
        std::this_thread::sleep_for(std::chrono::milliseconds(100));
    }

    monitor.Stop();

    std::cout << "status=" << static_cast<unsigned int>(snapshot.status)
              << " sequence=" << snapshot.sequence
              << " primary_valid=" << snapshot.primary.valid
              << " secondary_valid=" << snapshot.secondary.valid << '\n';

    if (snapshot.status == CodexQuotaStatus::AuthRequired)
    {
        std::cout << "Codex App Server responded, but ChatGPT login is required.\n";
        return 0;
    }

    if (snapshot.status != CodexQuotaStatus::Valid || !snapshot.primary.valid)
    {
        std::cerr << "A valid primary Codex quota window was not received.\n";
        return 1;
    }

    std::cout << "Codex quota monitor smoke test passed.\n";
    return 0;
}
