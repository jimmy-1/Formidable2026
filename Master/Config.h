#pragma once
#ifndef CONFIG_H
#define CONFIG_H

// 配置持久化函数
void LoadSettings();
void SaveSettings();

// 备注持久化
void LoadClientRemarks();
void SaveClientRemarks();

void LoadHistoryHosts();
void SaveHistoryHosts();

#endif // CONFIG_H
