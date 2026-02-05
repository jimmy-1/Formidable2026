#pragma once
#ifndef NETWORKHELPER_H
#define NETWORKHELPER_H

#include <windows.h>
#include <string>
#include <memory>
#include "../Common/ClientTypes.h"

// 网络相关函数
bool SendDataToClient(std::shared_ptr<Formidable::ConnectedClient> client, const void* pData, int iLength);
bool SendModuleToClient(uint32_t clientId, uint32_t cmd, const std::wstring& dllName, uint32_t arg2 = 0);
bool SendSimpleCommand(uint32_t clientId, uint32_t cmd, uint32_t arg1 = 0, uint32_t arg2 = 0, const std::string& data = "");
bool GetResourceData(int resourceId, std::vector<char>& buffer);
int GetResourceIdFromDllName(const std::wstring& dllName, bool is64Bit);

// 网络线程
void NetworkThread();
void HeartbeatThread();

// FRP 内网穿透
void StartFrp();
void StopFrp();

#endif // NETWORKHELPER_H
