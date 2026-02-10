#ifndef _AES_MINI_H_
#define _AES_MINI_H_

#include <stdint.h>

#define AES_BLOCKLEN 16

struct AES_ctx {
    uint8_t RoundKey[176];
    uint8_t Iv[16];
};

#ifdef __cplusplus
extern "C" {
#endif

void AES_init_ctx_iv(struct AES_ctx* ctx, const uint8_t* key, const uint8_t* iv);
void AES_CBC_decrypt_buffer(struct AES_ctx* ctx, uint8_t* buf, uint32_t length);

#ifdef __cplusplus
}
#endif

#endif // _AES_MINI_H_
