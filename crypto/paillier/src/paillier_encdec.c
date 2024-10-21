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
#ifdef HITLS_CRYPTO_PAILLIER

#include "crypt_utils.h"
#include "crypt_paillier.h"
#include "paillier_local.h"
#include "crypt_errno.h"
#include "bsl_sal.h"
#include "securec.h"
#include "bsl_err_internal.h"

int32_t  CRYPT_PAILLIER_PubEnc(const CRYPT_PAILLIER_Ctx *ctx, const uint8_t *input, uint32_t inputLen,
    uint8_t *out, uint32_t *outLen)
{
    int32_t ret;
    CRYPT_PAILLIER_PubKey *pubKey = ctx->pubKey;
    if (pubKey == NULL) {
        BSL_ERR_PUSH_ERROR(CRYPT_NULL_INPUT);
        return CRYPT_NULL_INPUT;
    }

    uint32_t bits = CRYPT_PAILLIER_GetBits(ctx);
    BN_Optimizer *optimizer = BN_OptimizerCreate();
    if (optimizer == NULL) {
        BSL_ERR_PUSH_ERROR(CRYPT_MEM_ALLOC_FAIL);
        return CRYPT_MEM_ALLOC_FAIL;
    }

    BN_BigNum *m = BN_Create(bits);
    BN_BigNum *r = BN_Create(bits);

    BN_BigNum *gm = BN_Create(bits);
    BN_BigNum *rn = BN_Create(bits);

    BN_BigNum *result = BN_Create(bits);

    bool createFailed = (m == NULL || r == NULL || gm == NULL || rn == NULL || result == NULL);
    if (createFailed) {
        BSL_ERR_PUSH_ERROR(CRYPT_MEM_ALLOC_FAIL);
        ret = CRYPT_MEM_ALLOC_FAIL;
        goto OUT;
    }

    ret = BN_Bin2Bn(m, input, inputLen);
    if (ret != CRYPT_SUCCESS) {
        BSL_ERR_PUSH_ERROR(ret);
        goto OUT;
    }

    // Check whether m is less than n and non-negative
    if (BN_Cmp(m, pubKey->n) >= 0 || BN_IsNegative(m)) {
        BSL_ERR_PUSH_ERROR(CRYPT_PAILLIER_ERR_INPUT_VALUE);
        ret = CRYPT_PAILLIER_ERR_INPUT_VALUE;
        goto OUT;
    }

    ret = BN_RandRange(r, pubKey->n);
    if (ret != CRYPT_SUCCESS) {
        BSL_ERR_PUSH_ERROR(ret);
        goto OUT;
    }

    BN_BigNum *gcd_result = BN_Create(bits);
    if (gcd_result == NULL) {
        BSL_ERR_PUSH_ERROR(CRYPT_MEM_ALLOC_FAIL);
        ret = CRYPT_MEM_ALLOC_FAIL;
        BN_Destroy(gcd_result);
        goto OUT;
    }

    while (true) {
        // Check whether r is relatively prime to n, if not, regenerate r
        ret = BN_Gcd(gcd_result, r, pubKey->n, optimizer);
        if (ret != CRYPT_SUCCESS) {
            BSL_ERR_PUSH_ERROR(ret);
            BN_Destroy(gcd_result);
            goto OUT;
        }
        if (BN_IsOne(gcd_result)) {
            BN_Destroy(gcd_result);
            break;
        }
        ret = BN_RandRange(r, pubKey->n);
        if (ret != CRYPT_SUCCESS) {
            BSL_ERR_PUSH_ERROR(ret);
            BN_Destroy(gcd_result);
            goto OUT;
        }
    }

    ret = BN_ModExp(gm, pubKey->g, m, pubKey->n2, optimizer);
    if (ret != CRYPT_SUCCESS) {
        BSL_ERR_PUSH_ERROR(ret);
        goto OUT;
    }

    ret = BN_ModExp(rn, r, pubKey->n, pubKey->n2, optimizer);
    if (ret != CRYPT_SUCCESS) {
        BSL_ERR_PUSH_ERROR(ret);
        goto OUT;
    }

    ret = BN_ModMul(result, gm, rn, pubKey->n2, optimizer);
    if (ret != CRYPT_SUCCESS) {
        BSL_ERR_PUSH_ERROR(ret);
        goto OUT;
    }

    ret = BN_Bn2Bin(result, out, outLen);
    if (ret != CRYPT_SUCCESS) {
        BSL_ERR_PUSH_ERROR(ret);
        goto OUT;
    }
OUT :
    BN_Destroy(m);
    BN_Destroy(r);
    BN_Destroy(gm);
    BN_Destroy(rn);
    BN_Destroy(result);
    BN_OptimizerDestroy(optimizer);
    return ret;
}

int32_t CRYPT_PAILLIER_PrvDec(const CRYPT_PAILLIER_Ctx *ctx, const BN_BigNum *ciphertext, uint32_t bits,
    uint8_t *out, uint32_t *outLen)
{
    int32_t ret;
    CRYPT_PAILLIER_PrvKey *prvKey = ctx->prvKey;
    if (prvKey == NULL) {
        BSL_ERR_PUSH_ERROR(CRYPT_NULL_INPUT);
        return CRYPT_NULL_INPUT;
    }

    BN_Optimizer *optimizer = BN_OptimizerCreate();
    if (optimizer == NULL) {
        BSL_ERR_PUSH_ERROR(CRYPT_MEM_ALLOC_FAIL);
        return CRYPT_MEM_ALLOC_FAIL;
    }

    BN_BigNum *m = BN_Create(bits);
    BN_BigNum *result = BN_Create(bits);

    bool createFailed = (m == NULL || result == NULL);
    
    if (createFailed) {
        BSL_ERR_PUSH_ERROR(CRYPT_MEM_ALLOC_FAIL);
        ret = CRYPT_MEM_ALLOC_FAIL;
        goto OUT;
    }

    ret = BN_ModExp(m, ciphertext, prvKey->lambda, prvKey->n2, optimizer);
    if (ret != CRYPT_SUCCESS) {
        BSL_ERR_PUSH_ERROR(ret);
        goto OUT;
    }

    ret = BN_SubLimb(result, m, 1);
    if (ret != CRYPT_SUCCESS) {
        BSL_ERR_PUSH_ERROR(ret);
        goto OUT;
    }

    ret = BN_Div(result, NULL, result, prvKey->n, optimizer);
    if (ret != CRYPT_SUCCESS) {
        BSL_ERR_PUSH_ERROR(ret);
        goto OUT;
    }

    ret = BN_ModMul(result, result, prvKey->mu, prvKey->n, optimizer);
    if (ret != CRYPT_SUCCESS) {
        BSL_ERR_PUSH_ERROR(ret);
        goto OUT;
    }

    ret = BN_Bn2Bin(result, out, outLen);
    if (ret != CRYPT_SUCCESS) {
        BSL_ERR_PUSH_ERROR(ret);
        goto OUT;
    }
OUT :
    BN_Destroy(m);
    BN_Destroy(result);
    BN_OptimizerDestroy(optimizer);
    return ret;
}

static int32_t EncryptInputCheck(const CRYPT_PAILLIER_Ctx *ctx, const uint8_t *input, uint32_t inputLen,
    const uint8_t *out, const uint32_t *outLen)
{
    if (ctx == NULL || (input == NULL && inputLen != 0) || out == NULL || outLen == 0) {
        BSL_ERR_PUSH_ERROR(CRYPT_NULL_INPUT);
        return CRYPT_NULL_INPUT;
    }

    if (ctx->pubKey == NULL) {
        // Check whether the public key information exists.
        BSL_ERR_PUSH_ERROR(CRYPT_PAILLIER_NO_KEY_INFO);
        return CRYPT_PAILLIER_NO_KEY_INFO;
    }
    // Check whether the length of the out is sufficient to place the encryption information.
    uint32_t bits = CRYPT_PAILLIER_GetBits(ctx);
    if ((*outLen) < BN_BITS_TO_BYTES(bits)) {
        BSL_ERR_PUSH_ERROR(CRYPT_PAILLIER_BUFF_LEN_NOT_ENOUGH);
        return CRYPT_PAILLIER_BUFF_LEN_NOT_ENOUGH;
    }
    if (inputLen > BN_BITS_TO_BYTES(bits)) {
        BSL_ERR_PUSH_ERROR(CRYPT_PAILLIER_ERR_ENC_BITS);
        return CRYPT_PAILLIER_ERR_ENC_BITS;
    }

    return CRYPT_SUCCESS;
}

int32_t CRYPT_PAILLIER_Encrypt(CRYPT_PAILLIER_Ctx *ctx, const uint8_t *data, uint32_t dataLen,
    uint8_t *out, uint32_t *outLen)
{
    int32_t ret = EncryptInputCheck(ctx, data, dataLen, out, outLen);
    if (ret != CRYPT_SUCCESS) {
        return ret;
    }
    ret = CRYPT_PAILLIER_PubEnc(ctx, data, dataLen, out, outLen);
    if (ret != CRYPT_SUCCESS) {
        BSL_ERR_PUSH_ERROR(ret);
    }
    return ret;
}

static int32_t DecryptInputCheck(const CRYPT_PAILLIER_Ctx *ctx, const uint8_t *data, uint32_t dataLen,
    const uint8_t *out, const uint32_t *outLen)
{
    if (ctx == NULL || data == NULL || out == NULL || outLen == NULL) {
        BSL_ERR_PUSH_ERROR(CRYPT_NULL_INPUT);
        return CRYPT_NULL_INPUT;
    }

    if (ctx->prvKey == NULL) {
        // Check whether the private key information exists.
        BSL_ERR_PUSH_ERROR(CRYPT_PAILLIER_NO_KEY_INFO);
        return CRYPT_PAILLIER_NO_KEY_INFO;
    }
    // Check whether the length of the out is sufficient to place the decryption information.
    uint32_t bits = CRYPT_PAILLIER_GetBits(ctx);
    if ((*outLen) < BN_BITS_TO_BYTES(bits)) {
        BSL_ERR_PUSH_ERROR(CRYPT_PAILLIER_BUFF_LEN_NOT_ENOUGH);
        return CRYPT_PAILLIER_BUFF_LEN_NOT_ENOUGH;
    }
    if (dataLen != BN_BITS_TO_BYTES(bits) * 2) {
        BSL_ERR_PUSH_ERROR(CRYPT_PAILLIER_ERR_DEC_BITS);
        return CRYPT_PAILLIER_ERR_DEC_BITS;
    }

    return CRYPT_SUCCESS;
}

static int32_t CRYPT_PAILLIER_CheckCiphertext(const BN_BigNum* ciphertext, const CRYPT_PAILLIER_PrvKey* prvKey)
{
    if (BN_Cmp(ciphertext, prvKey->n2) >= 0 || BN_IsNegative(ciphertext)) {
        BSL_ERR_PUSH_ERROR(CRYPT_PAILLIER_ERR_INPUT_VALUE);
        return CRYPT_PAILLIER_ERR_INPUT_VALUE;
    }
    int32_t ret = CRYPT_SUCCESS;
    BN_BigNum *gcd_result = BN_Create(BN_Bits(ciphertext));
    BN_Optimizer *optimizer = BN_OptimizerCreate();
    if (gcd_result == NULL || optimizer == NULL) {
        BSL_ERR_PUSH_ERROR(CRYPT_MEM_ALLOC_FAIL);
        ret = CRYPT_MEM_ALLOC_FAIL;
        goto OUT;
    }
    ret = BN_Gcd(gcd_result, ciphertext, prvKey->n2, optimizer);
    if (ret != CRYPT_SUCCESS) {
        BSL_ERR_PUSH_ERROR(ret);
        goto OUT;
    }
    if (BN_IsOne(gcd_result) == false) {
        BSL_ERR_PUSH_ERROR(CRYPT_PAILLIER_ERR_INPUT_VALUE);
        ret = CRYPT_PAILLIER_ERR_INPUT_VALUE;
        goto OUT;
    }
OUT:
    BN_Destroy(gcd_result);
    BN_OptimizerDestroy(optimizer);
    return ret;
}

int32_t CRYPT_PAILLIER_Decrypt(CRYPT_PAILLIER_Ctx *ctx, const uint8_t *data, uint32_t dataLen,
    uint8_t *out, uint32_t *outLen)
{
    int32_t ret = DecryptInputCheck(ctx, data, dataLen, out, outLen);
    if (ret != CRYPT_SUCCESS) {
        return ret;
    }

    uint32_t bits = CRYPT_PAILLIER_GetBits(ctx);
    BN_BigNum *c = BN_Create(bits);

    if (c == NULL) {
        BSL_ERR_PUSH_ERROR(CRYPT_MEM_ALLOC_FAIL);
        ret = CRYPT_MEM_ALLOC_FAIL;
        goto OUT;
    }

    ret = BN_Bin2Bn(c, data, dataLen);
    if (ret != CRYPT_SUCCESS) {
        BSL_ERR_PUSH_ERROR(ret);
        goto OUT;
    }

    // check whether c is in Zn2*
    ret = CRYPT_PAILLIER_CheckCiphertext(c, ctx->prvKey);
    if (ret != CRYPT_SUCCESS) {
        BSL_ERR_PUSH_ERROR(ret);
        goto OUT;
    }

    ret = CRYPT_PAILLIER_PrvDec(ctx, c, bits, out, outLen);
    if (ret != CRYPT_SUCCESS) {
        BSL_ERR_PUSH_ERROR(ret);
    }

OUT:
    BN_Destroy(c);
    return ret;
}

#endif  // HITLS_CRYPTO_PAILLIER