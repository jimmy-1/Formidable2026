#include "StringUtils.h"
#include "../Common/Utils.h"
#include "Utils/StringHelper.h"

// 字符串转换工具函数
std::wstring ToWString(const std::string& str) {
    return Formidable::Utils::StringHelper::UTF8ToWide(str);
}

std::wstring ToWString(int val) {
    return std::to_wstring(val);
}
