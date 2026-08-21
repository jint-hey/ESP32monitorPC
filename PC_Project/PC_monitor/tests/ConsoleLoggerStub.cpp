#include "ConsoleLogger.h"

void ConsoleLogger::Info(const std::wstring&)
{}

void ConsoleLogger::Warning(const std::wstring&)
{}

void ConsoleLogger::Error(const std::wstring&)
{}

void ConsoleLogger::LogCodexQuery(
    const std::wstring&,
    const std::wstring&)
{}

void ConsoleLogger::LogCodexAccount(bool)
{}

void ConsoleLogger::LogCodexQuota(const CodexQuotaSnapshot&)
{}
