/*
 * This file is part of the openHiTLS project.
 *
 * openHiTLS is licensed under the Mulan PSL v2.
 * You can use this software according to the terms and conditions of the Mulan PSL v2.
 * You may obtain a copy of Mulan PSL v2 at:
 *
 *     http://license.coscl.org.cn/MulanPSL2
 *
 * THIS SOFTWARE IS PROVIDED ON AN "AS IS" BASIS, WITHOUT WARRANTIES OF ANY KIND,
 * EITHER EXPRESS OR IMPLIED, INCLUDING BUT NOT LIMITED TO NON-INFRINGEMENT,
 * MERCHANTABILITY OR FIT FOR A PARTICULAR PURPOSE.
 * See the Mulan PSL v2 for more details.
 */

#include "hitls_build.h"
#ifdef HITLS_CRYPTO_CHACHA20POLY1305

#include <stdint.h>
#include "securec.h"
#include "bsl_sal.h"
#include "bsl_err_internal.h"
#include "crypt_utils.h"
#include "crypt_errno.h"
#include "poly1305_core.h"


void Poly1305SetKey(Poly1305Ctx *ctx, const uint8_t key[POLY1305_KEYSIZE])
{
    // RFC_7539-2.5
    ctx->r[0] = GET_UINT32_LE(key, 0);
    ctx->r[1] = GET_UINT32_LE(key, 4);
    ctx->r[2] = GET_UINT32_LE(key, 8);
    ctx->r[3] = GET_UINT32_LE(key, 12);
    ctx->s[0] = GET_UINT32_LE(key, 16);
    ctx->s[1] = GET_UINT32_LE(key, 20);
    ctx->s[2] = GET_UINT32_LE(key, 24);
    ctx->s[3] = GET_UINT32_LE(key, 28);

    /**
     * r[3], r[7], r[11], and r[15] are required to have their top four
     * bits clear (be smaller than 16)
     * r[4], r[8], and r[12] are required to have their bottom two bits
     * clear (be divisible by 4)
     */
    ctx->r[0] &= 0x0FFFFFFF;
    ctx->r[1] &= 0x0FFFFFFC;
    ctx->r[2] &= 0x0FFFFFFC;
    ctx->r[3] &= 0x0FFFFFFC;

    ctx->acc[0] = 0;
    ctx->acc[1] = 0;
    ctx->acc[2] = 0;
    ctx->acc[3] = 0;
    ctx->acc[4] = 0;
    ctx->acc[5] = 0;

    ctx->lastLen = 0;
}

void Poly1305Update(Poly1305Ctx *ctx, const uint8_t *data, uint32_t dataLen)
{
    uint32_t i;
    uint32_t len = dataLen;
    const uint8_t *off = data;
    if (ctx->lastLen != 0) {
        uint64_t end = (uint64_t)len + ctx->lastLen;
        if (end > POLY1305_BLOCKSIZE) {
            end = POLY1305_BLOCKSIZE;
        }
        for (i = ctx->lastLen; i < end; i++) {
            ctx->last[i] = *off;
            off++;
        }
        len -= (uint32_t)(end - ctx->lastLen);
        if (end == POLY1305_BLOCKSIZE) {
            (void)Poly1305Block(ctx, ctx->last, POLY1305_BLOCKSIZE, 1);
        } else {
            ctx->lastLen = (uint32_t)end;
            return;
        }
    }
    /**
     * Add one bit beyond the number of octets. For a 16-byte block,
     * this is equivalent to adding 2^128 to the number.
     */
    if (len >= POLY1305_BLOCKSIZE) {
        (void)Poly1305Block(ctx, off, len & 0xfffffff0, 1);
    }
    ctx->lastLen = len & 0x0f; // mod 16;
    off += len - ctx->lastLen;
    for (i = 0; i < ctx->lastLen; i++) {
        ctx->last[i] = off[i];
    }
    return;
}

void Poly1305Final(Poly1305Ctx *ctx, uint8_t mac[POLY1305_TAGSIZE])
{
    uint32_t len = ctx->lastLen;
    if (len > 0) {
        /**
         * For the shorter block, it can be 2^120, 2^112, or any power of two that is
         * evenly divisible by 8, all the way down to 2^8
         */
        ctx->last[len++] = 1;
        /**
         * If the block is not 17 bytes long (the last block), pad it with
         * zeros. This is meaningless if you are treating the blocks as
         * numbers.
         */
        while (len < POLY1305_BLOCKSIZE) {
            ctx->last[len++] = 0;
        }
        Poly1305Block(ctx, ctx->last, POLY1305_BLOCKSIZE, 0);
        ctx->lastLen = 0;
    }
    Poly1305Last(ctx, mac);
}

int32_t MODES_CHACHA20POLY1305_InitCtx(MODES_CHACHA20POLY1305_Ctx *ctx, const struct EAL_CipherMethod *m)
{
    if (ctx == NULL || m == NULL) {
        BSL_ERR_PUSH_ERROR(CRYPT_NULL_INPUT);
        return CRYPT_NULL_INPUT;
    }
    (void)memset_s(ctx, sizeof(MODES_CHACHA20POLY1305_Ctx), 0, sizeof(MODES_CHACHA20POLY1305_Ctx));
    ctx->method = m;
    ctx->key = BSL_SAL_Malloc(m->ctxSize);
    if (ctx->key == NULL) {
        BSL_ERR_PUSH_ERROR(CRYPT_MEM_ALLOC_FAIL);
        return CRYPT_MEM_ALLOC_FAIL;
    }
    (void)memset_s(ctx->key, m->ctxSize, 0, m->ctxSize);
    return CRYPT_SUCCESS;
}

void MODES_CHACHA20POLY1305_DeinitCtx(MODES_CHACHA20POLY1305_Ctx *ctx)
{
    if (ctx == NULL) {
        return;
    }
    if (ctx->method != NULL) {
        BSL_SAL_CleanseData(ctx->key, ctx->method->ctxSize);
    }
    BSL_SAL_FREE(ctx->key);
    (void)memset_s(ctx, sizeof(MODES_CHACHA20POLY1305_Ctx), 0, sizeof(MODES_CHACHA20POLY1305_Ctx));
}

void MODES_CHACHA20POLY1305_Clean(MODES_CHACHA20POLY1305_Ctx *ctx)
{
    if (ctx == NULL) {
        return;
    }
    if (ctx->method != NULL) {
        BSL_SAL_CleanseData((void *)(ctx->key), ctx->method->ctxSize);
    }
    BSL_SAL_CleanseData((void *)(&(ctx->polyCtx)), sizeof(Poly1305Ctx));
    ctx->aadLen = 0;
    ctx->cipherTextLen = 0;
    Poly1305CleanRegister();
}

int32_t MODES_CHACHA20POLY1305_SetEncryptKey(MODES_CHACHA20POLY1305_Ctx *ctx, const uint8_t *key, uint32_t len)
{
    if (ctx == NULL) {
        BSL_ERR_PUSH_ERROR(CRYPT_NULL_INPUT);
        return CRYPT_NULL_INPUT;
    }
    return ctx->method->setEncryptKey(ctx->key, key, len);
}

int32_t MODES_CHACHA20POLY1305_SetDecryptKey(MODES_CHACHA20POLY1305_Ctx *ctx, const uint8_t *key, uint32_t len)
{
    if (ctx == NULL) {
        BSL_ERR_PUSH_ERROR(CRYPT_NULL_INPUT);
        return CRYPT_NULL_INPUT;
    }
    return ctx->method->setDecryptKey(ctx->key, key, len);
}

int32_t MODES_CHACHA20POLY1305_Encrypt(MODES_CHACHA20POLY1305_Ctx *ctx, const uint8_t *in, uint8_t *out, uint32_t len)
{
    if (ctx == NULL || in == NULL || out == NULL || len == 0) {
        BSL_ERR_PUSH_ERROR(CRYPT_NULL_INPUT);
        return CRYPT_NULL_INPUT;
    }
    int32_t ret = ctx->method->encrypt(ctx->key, in, out, len);
    if (ret != CRYPT_SUCCESS) {
        BSL_ERR_PUSH_ERROR(ret);
        return ret;
    }
    Poly1305Update(&(ctx->polyCtx), out, len);
    ctx->cipherTextLen += (uint64_t)len;
    return CRYPT_SUCCESS;
}

int32_t MODES_CHACHA20POLY1305_Decrypt(MODES_CHACHA20POLY1305_Ctx *ctx, const uint8_t *in, uint8_t *out, uint32_t len)
{
    if (ctx == NULL || in == NULL || out == NULL || len == 0) {
        BSL_ERR_PUSH_ERROR(CRYPT_NULL_INPUT);
        return CRYPT_NULL_INPUT;
    }
    Poly1305Update(&(ctx->polyCtx), in, len);
    ctx->cipherTextLen += (uint64_t)len;
    int32_t ret = ctx->method->decrypt(ctx->key, in, out, len);
    if (ret != CRYPT_SUCCESS) {
        BSL_ERR_PUSH_ERROR(ret);
    }
    return ret;
}

static void CipherTextPad(MODES_CHACHA20POLY1305_Ctx *ctx)
{
    /**
     * The ciphertext
     * padding2 -- the padding is up to 15 zero bytes, and it brings
     * the total length so far to an integral multiple of 16. If the
     * length of the ciphertext was already an integral multiple of 16
     * bytes, this field is zero-length.
    */
    Poly1305Ctx *polyCtx = &(ctx->polyCtx);
    uint32_t len = polyCtx->lastLen;
    if (len != 0) {
        const uint8_t pad2[POLY1305_BLOCKSIZE] = {0};
        Poly1305Update(polyCtx, pad2, POLY1305_BLOCKSIZE - len);
    }

    uint8_t pad[POLY1305_BLOCKSIZE];
    /**
     * The length of the additional data in octets (as a 64-bit
     * little-endian integer).
     */
    PUT_UINT64_LE(ctx->aadLen, pad, 0); // The first eight bytes are padded with the length of the AAD.
    /**
     * The length of the ciphertext in octets (as a 64-bit littleendian
     * integer).
     */
    PUT_UINT64_LE(ctx->cipherTextLen, pad, 8); // The last 8 bytes are padded the length of the ciphertext.
    Poly1305Update(polyCtx, pad, POLY1305_BLOCKSIZE);
}

static int32_t GetTag(MODES_CHACHA20POLY1305_Ctx *ctx, uint8_t *val, uint32_t len)
{
    if (val == NULL) {
        BSL_ERR_PUSH_ERROR(CRYPT_NULL_INPUT);
        return CRYPT_NULL_INPUT;
    }
    if (len != POLY1305_TAGSIZE) {
        BSL_ERR_PUSH_ERROR(CRYPT_MODES_TAGLEN_ERROR);
        return CRYPT_MODES_TAGLEN_ERROR;
    }
    CipherTextPad(ctx);
    Poly1305Final(&(ctx->polyCtx), val);
    return CRYPT_SUCCESS;
}

static int32_t SetIv(MODES_CHACHA20POLY1305_Ctx *ctx, const uint8_t *iv, uint32_t ivLen)
{
    /**
     * RFC_7539-2.6
     * ChaCha20 as specified here requires a 96-bit nonce.
     * So if the provided nonce is only 64-bit, then the first 32
     * bits of the nonce will be set to a constant number. This will
     * usually be zero, but for protocols with multiple senders it may be
     * different for each sender, but should be the same for all
     * invocations of the function with the same key by a particular
     * sender.
     */
    if (iv == NULL) {
        BSL_ERR_PUSH_ERROR(CRYPT_NULL_INPUT);
        return CRYPT_NULL_INPUT;
    }
    // 96 bits or 64 bits, that is, 12 bytes or 8 bytes.
    if (ivLen != 12 && ivLen != 8) {
        BSL_ERR_PUSH_ERROR(CRYPT_MODES_IVLEN_ERROR);
        return CRYPT_MODES_IVLEN_ERROR;
    }
    int32_t ret;
    uint8_t block[POLY1305_KEYSIZE] = { 0 };
    if (ivLen == 8) { // If the length of the IV is 8, 0 data must be padded before.
        uint8_t tmpBuff[12] = { 0 };
        (void)memcpy_s(tmpBuff + 4, 12 - 4, iv, ivLen);
        ret = ctx->method->ctrl(ctx->key, CRYPT_CTRL_SET_IV, tmpBuff, 12);
        // Clear sensitive data.
        (void)BSL_SAL_CleanseData(tmpBuff, sizeof(tmpBuff));
    } else {
        ret = ctx->method->ctrl(ctx->key, CRYPT_CTRL_SET_IV, (void *)(uintptr_t)iv, ivLen);
    }
    if (ret != CRYPT_SUCCESS) {
        BSL_ERR_PUSH_ERROR(ret);
        return ret;
    }
    /**
     * RFC_7539-2.6
     * The block counter is set to zero
     */
    uint8_t initCount[4] = { 0 };
    ret = ctx->method->ctrl(ctx->key, CRYPT_CTRL_SET_COUNT, initCount, sizeof(initCount));
    if (ret != CRYPT_SUCCESS) {
        BSL_ERR_PUSH_ERROR(ret);
        return ret;
    }
    ret = ctx->method->encrypt(ctx->key, block, block, POLY1305_KEYSIZE);
    if (ret != CRYPT_SUCCESS) {
        BSL_ERR_PUSH_ERROR(ret);
        return ret;
    }
    Poly1305SetKey(&(ctx->polyCtx), block);
    /**
     * RFC_7539-2.8
     * the ChaCha20 encryption function is called to encrypt the
     * plaintext, using the same key and nonce, and with the initial
     * counter set to 1
     */
    initCount[0] = 0x01;
    ret = ctx->method->ctrl(ctx->key, CRYPT_CTRL_SET_COUNT, initCount, sizeof(initCount)); // 4bytes count
    if (ret != CRYPT_SUCCESS) {
        BSL_ERR_PUSH_ERROR(ret);
    }
    // Information that needs to be reset.
    ctx->aadLen = 0;
    ctx->cipherTextLen = 0;
    return ret;
}

// Set the AAD information. The AAD information can be set only once.
static int32_t SetAad(MODES_CHACHA20POLY1305_Ctx *ctx, const uint8_t *aad, uint32_t aadLen)
{
    /**
     * RFC_7539-2.8
     * The AAD
     * padding1 -- the padding is up to 15 zero bytes, and it brings
     * the total length so far to an integral multiple of 16. If the
     * length of the AAD was already an integral multiple of 16 bytes,
     * this field is zero-length.
     */
    if (aadLen == 0) {
        return CRYPT_SUCCESS;
    }
    if (aad == NULL) {
        BSL_ERR_PUSH_ERROR(CRYPT_NULL_INPUT);
        return CRYPT_NULL_INPUT;
    }
    if (ctx->aadLen != 0) { // aad is set
        BSL_ERR_PUSH_ERROR(CRYPT_MODES_AAD_REPEAT_SET_ERROR);
        return CRYPT_MODES_AAD_REPEAT_SET_ERROR;
    }
    const uint8_t pad[16] = { 0 };
    ctx->aadLen = aadLen;
    Poly1305Update(&(ctx->polyCtx), aad, aadLen);
    uint32_t padLen = (16 - aadLen % 16) % 16;
    Poly1305Update(&(ctx->polyCtx), pad, padLen);
    return CRYPT_SUCCESS;
}

int32_t MODES_CHACHA20POLY1305_Ctrl(MODES_CHACHA20POLY1305_Ctx *ctx, CRYPT_CipherCtrl opt, void *val, uint32_t len)
{
    if (ctx == NULL) {
        BSL_ERR_PUSH_ERROR(CRYPT_NULL_INPUT);
        return CRYPT_NULL_INPUT;
    }
    switch (opt) {
        case CRYPT_CTRL_SET_IV:
            return SetIv(ctx, val, len);
        case CRYPT_CTRL_GET_TAG:
            return GetTag(ctx, val, len);
        case CRYPT_CTRL_SET_AAD:
            return SetAad(ctx, val, len);
        default:
            return ctx->method->ctrl(ctx->key, opt, val, len);
    }
}
#endif