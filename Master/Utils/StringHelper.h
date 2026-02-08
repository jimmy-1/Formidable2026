#pragma once
#include <string>
#include <vector>
#include <sstream>
#include <Windows.h>

namespace Formidable {
namespace Utils {

class StringHelper {
public:
    // 宽字符转UTF8
    static std::string WideToUTF8(const std::wstring& wstr) {
        if (wstr.empty()) return "";
        int len = WideCharToMultiByte(CP_UTF8, 0, wstr.c_str(), -1, nullptr, 0, nullptr, nullptr);
        if (len <= 0) return "";
        std::string result(len - 1, 0);
        WideCharToMultiByte(CP_UTF8, 0, wstr.c_str(), -1, &result[0], len, nullptr, nullptr);
        return result;
    }

    // 宽字符转ANSI
    static std::string WideToANSI(const std::wstring& wstr) {
        if (wstr.empty()) return "";
        int len = WideCharToMultiByte(CP_ACP, 0, wstr.c_str(), -1, nullptr, 0, nullptr, nullptr);
        if (len <= 0) return "";
        std::string result(len - 1, 0);
        WideCharToMultiByte(CP_ACP, 0, wstr.c_str(), -1, &result[0], len, nullptr, nullptr);
        return result;
    }

    // ANSI转宽字符
    static std::wstring ANSIToWide(const std::string& str) {
        if (str.empty()) return L"";
        int len = MultiByteToWideChar(CP_ACP, 0, str.c_str(), -1, nullptr, 0);
        if (len <= 0) return L"";
        std::wstring result(len - 1, 0);
        MultiByteToWideChar(CP_ACP, 0, str.c_str(), -1, &result[0], len);
        return result;
    }
    
    // UTF8转宽字符
    static std::wstring UTF8ToWide(const std::string& str) {
        if (str.empty()) return L"";
        int len = MultiByteToWideChar(CP_UTF8, 0, str.c_str(), -1, nullptr, 0);
        if (len <= 0) return L"";
        std::wstring result(len - 1, 0);
        MultiByteToWideChar(CP_UTF8, 0, str.c_str(), -1, &result[0], len);
        return result;
    }
    
    // 转换为宽字符串（支持多种类型）
    static std::wstring ToWString(const std::string& str) {
        return UTF8ToWide(str);
    }
    
    static std::wstring ToWString(int value) {
        return std::to_wstring(value);
    }
    
    static std::wstring ToWString(uint32_t value) {
        return std::to_wstring(value);
    }
    
    static std::wstring ToWString(uint64_t value) {
        return std::to_wstring(value);
    }
    
    static std::wstring ToWString(double value) {
        return std::to_wstring(value);
    }
    
    // 格式化字符串
    template<typename... Args>
    static std::wstring Format(const wchar_t* format, Args... args) {
        int size = _snwprintf(nullptr, 0, format, args...) + 1;
        if (size <= 0) return L"";
        std::wstring result(size, 0);
        _snwprintf(&result[0], size, format, args...);
        result.resize(size - 1); // 移除末尾的\0
        return result;
    }
    
    // 字符串分割
    static std::vector<std::string> Split(const std::string& str, char delimiter) {
        std::vector<std::string> tokens;
        std::stringstream ss(str);
        std::string token;
        while (std::getline(ss, token, delimiter)) {
            tokens.push_back(token);
        }
        return tokens;
    }
    
    // 字符串替换
    static std::string Replace(const std::string& str, const std::string& from, const std::string& to) {
        std::string result = str;
        size_t pos = 0;
        while ((pos = result.find(from, pos)) != std::string::npos) {
            result.replace(pos, from.length(), to);
            pos += to.length();
        }
        return result;
    }
    
    // 去除首尾空格
    static std::string Trim(const std::string& str) {
        size_t first = str.find_first_not_of(" \t\n\r");
        if (first == std::string::npos) return "";
        size_t last = str.find_last_not_of(" \t\n\r");
        return str.substr(first, last - first + 1);
    }
    
    static std::wstring Trim(const std::wstring& str) {
        size_t first = str.find_first_not_of(L" \t\n\r");
        if (first == std::wstring::npos) return L"";
        size_t last = str.find_last_not_of(L" \t\n\r");
        return str.substr(first, last - first + 1);
    }
};

} // namespace Utils
} // namespace Formidable
