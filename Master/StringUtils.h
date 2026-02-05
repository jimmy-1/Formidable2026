#pragma once
#ifndef STRINGUTILS_H
#define STRINGUTILS_H

#include <string>

// 字符串转换工具函数
std::wstring ToWString(const std::string& str);
std::wstring ToWString(int val);

#endif // STRINGUTILS_H
