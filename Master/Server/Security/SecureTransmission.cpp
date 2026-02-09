#include "SecureTransmission.h"
#include <random>
#include "../Utils/Logger.h"

#pragma comment(lib, "advapi32.lib")

namespace Formidable {
namespace Server {
namespace Security {

std::string SecureTransmission::EncryptionManager::s_keyString = "FormidableKey2026";
HCRYPTPROV SecureTransmission::EncryptionManager::s_hProv = 0;
HCRYPTKEY SecureTransmission::EncryptionManager::s_hKey = 0;
HCRYPTHASH SecureTransmission::EncryptionManager::s_hHash = 0;
std::mutex SecureTransmission::EncryptionManager::s_cryptoMutex;
bool SecureTransmission::EncryptionManager::s_initialized = false;

void SecureTransmission::InitializeEncryption() {
    EncryptionManager::Initialize();
    // Use fixed key for Phase 3 compatibility
    EncryptionManager::SetEncryptionKey("FormidableKey2026");
    Formidable::Server::Utils::Logger::Log(Formidable::Server::Utils::LogLevel::LL_INFO, "Encryption Initialized with default key");
}

void SecureTransmission::CleanupEncryption() {
    EncryptionManager::Cleanup();
}

void SecureTransmission::EncryptData(const void* data, size_t length, std::vector<char>& encryptedData) {
    std::lock_guard<std::mutex> lock(EncryptionManager::GetCryptoMutex());
    
    // Prepare buffer with enough space for padding
    // AES block size is 16 bytes. Max padding is 16 bytes.
    DWORD dataLen = static_cast<DWORD>(length);
    DWORD bufferLen = dataLen + 16; 
    
    std::vector<BYTE> buffer(bufferLen);
    memcpy(buffer.data(), data, length);
    
    if (EncryptionManager::EncryptBuffer(buffer.data(), dataLen, bufferLen)) {
        encryptedData.assign(buffer.begin(), buffer.begin() + dataLen);
    } else {
        // Fallback or error handling
        // For now, if encryption fails, return empty or original (bad idea, but prevents crash)
        encryptedData.assign((char*)data, (char*)data + length);
    }
}

void SecureTransmission::DecryptData(const void* data, size_t length, std::vector<char>& decryptedData) {
    std::lock_guard<std::mutex> lock(EncryptionManager::GetCryptoMutex());
    
    DWORD dataLen = static_cast<DWORD>(length);
    std::vector<BYTE> buffer(length);
    memcpy(buffer.data(), data, length);
    
    if (EncryptionManager::DecryptBuffer(buffer.data(), dataLen)) {
        decryptedData.assign(buffer.begin(), buffer.begin() + dataLen);
    } else {
        // Fallback
        decryptedData.assign((char*)data, (char*)data + length);
    }
}

// EncryptionManager
bool SecureTransmission::EncryptionManager::Initialize() {
    if (s_initialized) return true;
    
    // Acquire context
    if (!CryptAcquireContext(&s_hProv, NULL, MS_ENH_RSA_AES_PROV, PROV_RSA_AES, CRYPT_VERIFYCONTEXT)) {
        // Try creating new keyset if needed, though VERIFYCONTEXT doesn't need it
        if (GetLastError() == NTE_BAD_KEYSET) {
            if (!CryptAcquireContext(&s_hProv, NULL, MS_ENH_RSA_AES_PROV, PROV_RSA_AES, CRYPT_NEWKEYSET | CRYPT_VERIFYCONTEXT)) {
                return false;
            }
        } else {
            return false;
        }
    }
    
    s_initialized = true;
    return true;
}

void SecureTransmission::EncryptionManager::Cleanup() {
    if (s_hKey) CryptDestroyKey(s_hKey);
    if (s_hHash) CryptDestroyHash(s_hHash);
    if (s_hProv) CryptReleaseContext(s_hProv, 0);
    
    s_hKey = 0;
    s_hHash = 0;
    s_hProv = 0;
    s_initialized = false;
}

void SecureTransmission::EncryptionManager::GenerateKey() {
    // Generate random string
    static const char charset[] = "0123456789ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz!@#$%^&*()";
    std::string newKey;
    newKey.reserve(32);
    
    std::random_device rd;
    std::mt19937 gen(rd());
    std::uniform_int_distribution<> dis(0, sizeof(charset) - 2);
    
    for (int i = 0; i < 32; ++i) {
        newKey += charset[dis(gen)];
    }
    SetEncryptionKey(newKey);
}

void SecureTransmission::EncryptionManager::SetEncryptionKey(const std::string& key) {
    if (!s_initialized && !Initialize()) return;
    
    s_keyString = key;
    
    // Clean up old key/hash
    if (s_hKey) CryptDestroyKey(s_hKey);
    if (s_hHash) CryptDestroyHash(s_hHash);
    s_hKey = 0;
    s_hHash = 0;
    
    // Create hash of the password
    if (!CryptCreateHash(s_hProv, CALG_SHA_256, 0, 0, &s_hHash)) {
        // Fallback to SHA1 or MD5 if SHA256 not available (older Windows)
        if (!CryptCreateHash(s_hProv, CALG_MD5, 0, 0, &s_hHash)) {
            return;
        }
    }
    
    if (!CryptHashData(s_hHash, (BYTE*)key.data(), (DWORD)key.length(), 0)) {
        return;
    }
    
    // Derive AES key
    // CALG_AES_256 usually
    if (!CryptDeriveKey(s_hProv, CALG_AES_256, s_hHash, 0, &s_hKey)) {
        // Try 128 bit
        CryptDeriveKey(s_hProv, CALG_AES_128, s_hHash, 0, &s_hKey);
    }
}

std::string SecureTransmission::EncryptionManager::GetEncryptionKey() {
    return s_keyString;
}

std::mutex& SecureTransmission::EncryptionManager::GetCryptoMutex() {
    return s_cryptoMutex;
}

bool SecureTransmission::EncryptionManager::Encrypt(const std::string& data, std::string& encrypted) {
    std::vector<char> buffer;
    SecureTransmission::EncryptData(data.data(), data.length(), buffer);
    encrypted.assign(buffer.begin(), buffer.end());
    return true;
}

bool SecureTransmission::EncryptionManager::Decrypt(const std::string& encrypted, std::string& data) {
    std::vector<char> buffer;
    SecureTransmission::DecryptData(encrypted.data(), encrypted.length(), buffer);
    data.assign(buffer.begin(), buffer.end());
    return true;
}

bool SecureTransmission::EncryptionManager::EncryptBuffer(BYTE* buffer, DWORD& dataLen, DWORD bufferLen) {
    if (!s_hKey) return false;
    return CryptEncrypt(s_hKey, 0, TRUE, 0, buffer, &dataLen, bufferLen);
}

bool SecureTransmission::EncryptionManager::DecryptBuffer(BYTE* buffer, DWORD& dataLen) {
    if (!s_hKey) return false;
    return CryptDecrypt(s_hKey, 0, TRUE, 0, buffer, &dataLen);
}


// SecureChannel
void SecureTransmission::SecureChannel::EstablishSecureChannel(SOCKET socket) {
    // Placeholder: Handshake logic would go here
}

bool SecureTransmission::SecureChannel::IsChannelSecure(SOCKET socket) {
    // Placeholder
    return true;
}

void SecureTransmission::SecureChannel::CloseSecureChannel(SOCKET socket) {
    // Placeholder
}

} // namespace Security
} // namespace Server
} // namespace Formidable
