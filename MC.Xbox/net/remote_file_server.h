#pragma once

#include <string>

void StartRemoteFileServer(const std::wstring& runtimeRoot);
void StopRemoteFileServer();
bool RemoteFileServerRunning();
std::wstring RemoteFileServerUrl();
std::string RemoteFileServerPin();

bool RemoteFileImportRunning();
