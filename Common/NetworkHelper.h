#pragma once
#include <winsock2.h>
#include <vector>
#include <string>
#include "Config.h"
#include "ProtocolEncoder.h"

namespace Formidable {

extern ProtocolEncoder* g_pProtocolEncoder;

inline void SetProtocolEncoder(ProtocolEncoder* encoder) {
    g_pProtocolEncoder = encoder;
}

inline void SendPkg(SOCKET s, uint32_t cmd, const void* data, int len, uint32_t arg1 = 0, uint32_t arg2 = 0, ProtocolEncoder* encoder = nullptr) {
    if (!encoder) encoder = g_pProtocolEncoder;
    if (!encoder) {
        static ShineEncoder defaultShine;
        encoder = &defaultShine;
    }

    int flagLen = encoder->GetFlagLen();
    int headLen = encoder->GetHeadLen();

    size_t bodySize = sizeof(CommandPkg) - 1 + len;
    int totalLen = (int)(headLen + bodySize);
    
    std::vector<char> buffer(totalLen);
    encoder->GetHeaderData((unsigned char*)buffer.data());
    memcpy(buffer.data() + flagLen, &totalLen, sizeof(int));
    memcpy(buffer.data() + flagLen + sizeof(int), &bodySize, sizeof(int));

    CommandPkg* pkg = (CommandPkg*)(buffer.data() + headLen);
    pkg->cmd = cmd;
    pkg->arg1 = arg1 == 0 ? len : arg1;
    pkg->arg2 = arg2;
    if (len > 0 && data) {
        memcpy(pkg->data, data, len);
    }

    unsigned char* pKey = nullptr;
    if (encoder->GetHead() == "HELL") {
        pKey = (unsigned char*)buffer.data() + 6;
    }

    encoder->Encode((unsigned char*)buffer.data() + headLen, (int)bodySize, pKey);

    send(s, buffer.data(), (int)buffer.size(), 0);
}

// Overload for cases where data is already a CommandPkg or raw bytes
inline void SendRawPkg(SOCKET s, const void* data, int len, ProtocolEncoder* encoder = nullptr) {
    if (!encoder) encoder = g_pProtocolEncoder;
    if (!encoder) {
        static ShineEncoder defaultShine;
        encoder = &defaultShine;
    }

    int flagLen = encoder->GetFlagLen();
    int headLen = encoder->GetHeadLen();

    int totalLen = headLen + len;
    
    std::vector<char> buffer(totalLen);
    encoder->GetHeaderData((unsigned char*)buffer.data());
    memcpy(buffer.data() + flagLen, &totalLen, sizeof(int));
    memcpy(buffer.data() + flagLen + sizeof(int), &len, sizeof(int));

    if (len > 0) {
        memcpy(buffer.data() + headLen, data, len);
        
        unsigned char* pKey = nullptr;
        if (encoder->GetHead() == "HELL") {
            pKey = (unsigned char*)buffer.data() + 6;
        }
        encoder->Encode((unsigned char*)buffer.data() + headLen, len, pKey);
    }

    send(s, buffer.data(), totalLen, 0);
}

} // namespace Formidable
