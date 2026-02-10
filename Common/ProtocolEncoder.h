#pragma once
#include "Config.h"
#include <vector>
#include <string>

#include <ctime>

namespace Formidable {

// 加密函数
inline void hell_encrypt(unsigned char* data, size_t length, unsigned char key)
{
    if (key == 0) return;
    for (size_t i = 0; i < length; ++i) {
        unsigned char k = static_cast<unsigned char>(key ^ (i * 31));
        int value = static_cast<int>(data[i]);
        switch (i % 4) {
        case 0: value += k; break;
        case 1: value = value ^ k; break;
        case 2: value -= k; break;
        case 3: value = ~(value ^ k); break;
        }
        data[i] = static_cast<unsigned char>(value & 0xFF);
    }
}

// 解密函数
inline void hell_decrypt(unsigned char* data, size_t length, unsigned char key)
{
    if (key == 0) return;
    for (size_t i = 0; i < length; ++i) {
        unsigned char k = static_cast<unsigned char>(key ^ (i * 31));
        int value = static_cast<int>(data[i]);
        switch (i % 4) {
        case 0: value -= k; break;
        case 1: value = value ^ k; break;
        case 2: value += k; break;
        case 3: value = ~(value) ^ k; break;
        }
        data[i] = static_cast<unsigned char>(value & 0xFF);
    }
}

class ProtocolEncoder {
public:
    virtual ~ProtocolEncoder() {}
    virtual std::string GetHead() const {
        return "Shine";
    }
    virtual int GetHeadLen() const {
        return 13;
    }
    virtual int GetFlagLen() const {
        return 5;
    }
    virtual void Encode(unsigned char* data, int len, unsigned char* param = nullptr) {}
    virtual void Decode(unsigned char* data, int len, unsigned char* param = nullptr) {}
    virtual void GetHeaderData(unsigned char* buffer) const {
        memcpy(buffer, GetHead().c_str(), GetFlagLen());
    }
};

// ShineEncoder is actually the base ProtocolEncoder
class ShineEncoder : public ProtocolEncoder {
public:
    virtual std::string GetHead() const override { return "Shine"; }
    virtual int GetHeadLen() const override { return 13; }
    virtual int GetFlagLen() const override { return 5; }
};

class HellEncoder : public ProtocolEncoder {
public:
    virtual std::string GetHead() const override { return "HELL"; }
    virtual int GetHeadLen() const override { return 16; }
    virtual int GetFlagLen() const override { return 8; }
    
    virtual void Encode(unsigned char* data, int len, unsigned char* param = nullptr) override {
        if (param) {
            hell_encrypt(data, len, *param);
        }
    }
    
    virtual void Decode(unsigned char* data, int len, unsigned char* param = nullptr) override {
        if (param) {
            hell_decrypt(data, len, *param);
        }
    }

    virtual void GetHeaderData(unsigned char* buffer) const override {
        memset(buffer, 0, 8);
        memcpy(buffer, "HELL", 4);
        unsigned char key = (unsigned char)(time(NULL) % 255 + 1); // Avoid 0
        buffer[6] = key;
        buffer[7] = ~key;
    }
};

inline ProtocolEncoder* GetEncoderByType(int type) {
    if (type == PROTOCOL_SHINE) {
        return new ShineEncoder();
    }
    if (type == PROTOCOL_HELL) {
        return new HellEncoder();
    }
    return new ShineEncoder();
}

inline ProtocolEncoder* CheckHead(const char* data, int len) {
    if (len < 4) return nullptr;
    if (len >= 5 && memcmp(data, "Shine", 5) == 0) {
        return new ShineEncoder();
    }
    if (len >= 8 && memcmp(data, "HELL", 4) == 0) {
        // Verify key and ~key
        unsigned char key = (unsigned char)data[6];
        unsigned char nkey = (unsigned char)data[7];
        if (key == (unsigned char)(~nkey) && key != 0) {
            return new HellEncoder();
        }
    }
    return nullptr;
}

} // namespace Formidable
