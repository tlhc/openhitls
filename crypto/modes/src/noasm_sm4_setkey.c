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
#ifdef HITLS_CRYPTO_SM4

#include "bsl_err_internal.h"
#include "crypt_sm4.h"
#include "crypt_modes.h"

int32_t MODES_SM4_SetEncryptKey(MODE_CipherCtx *ctx, const uint8_t *key, uint32_t len)
{
    return MODE_SetEncryptKey(ctx, key, len);
}

int32_t MODES_SM4_SetDecryptKey(MODE_CipherCtx *ctx, const uint8_t *key, uint32_t len)
{
    return MODE_SetDecryptKey(ctx, key, len);
}

#endif // HITLS_CRYPTO_SM4