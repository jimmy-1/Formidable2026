#pragma once
#ifndef _WINSOCKAPI_
#define _WINSOCKAPI_
#endif
#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#include <windows.h>
#include <winsock2.h>
#include <wincrypt.h>
#include <vector>
#include <string>
#include <mutex>

namespace Formidable {
namespace Server {
namespace Security {

class SecureTransmission {
public:
    static void InitializeEncryption();
    static void CleanupEncryption();
    static void EncryptData(const void* data, size_t length, std::vector<char>& encryptedData);
    static void DecryptData(const void* data, size_t length, std::vector<char>& decryptedData);
    
    class EncryptionManager {
    public:
        static bool Initialize();
        static void Cleanup();
        static void GenerateKey(); // Generates a random key for AES
        static void SetEncryptionKey(const std::string& key); // Derives AES key from string
        static std::string GetEncryptionKey();
        static std::mutex& GetCryptoMutex();
        
        static bool Encrypt(const std::string& data, std::string& encrypted);
        static bool Decrypt(const std::string& encrypted, std::string& data);
        
        // Low level helpers
        static bool EncryptBuffer(BYTE* buffer, DWORD& dataLen, DWORD bufferLen);
        static bool DecryptBuffer(BYTE* buffer, DWORD& dataLen);
        
    private:
        static std::string s_keyString;
        static HCRYPTPROV s_hProv;
        static HCRYPTKEY s_hKey;
        static HCRYPTHASH s_hHash;
        static std::mutex s_cryptoMutex;
        static bool s_initialized;
    };
    
    class SecureChannel {
    public:
        static void EstablishSecureChannel(SOCKET socket);
        static bool IsChannelSecure(SOCKET socket);
        static void CloseSecureChannel(SOCKET socket);
    };
};

} // namespace Security
} // namespace Server
} // namespace Formidable
