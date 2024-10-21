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

#include <stdint.h>
#include "securec.h"
#include "tls_binlog_id.h"
#include "bsl_log_internal.h"
#include "bsl_log.h"
#include "bsl_err_internal.h"
#include "bsl_bytes.h"
#include "bsl_sal.h"
#include "hitls_error.h"
#include "tls.h"
#include "crypt.h"
#include "cert_method.h"
#include "hs_ctx.h"
#include "hs_common.h"


#define APPROXIMATE_PREMASTER_LEN 128

static int32_t PackClientKxMsgNamedCurve(const TLS_Ctx *ctx, uint8_t *buf, uint32_t bufLen, uint32_t *usedLen)
{
    int32_t ret = HITLS_SUCCESS;
    uint32_t pubKeyLen;
    uint32_t offset = 0;
    EcdhParam *ecdh = &(ctx->hsCtx->kxCtx->keyExchParam.ecdh);
    HITLS_ECParameters *curveParams = &ecdh->curveParams;
    KeyExchCtx *kxCtx = ctx->hsCtx->kxCtx;

    pubKeyLen = HS_GetNamedCurvePubkeyLen(curveParams->param.namedcurve);
    if (pubKeyLen == 0u) {
        BSL_ERR_PUSH_ERROR(HITLS_PACK_INVALID_KX_PUBKEY_LENGTH);
        BSL_LOG_BINLOG_FIXLEN(BINLOG_ID15673, BSL_LOG_LEVEL_ERR, BSL_LOG_BINLOG_TYPE_RUN,
            "invalid key exchange pubKey length.", 0, 0, 0, 0);
        return HITLS_PACK_INVALID_KX_PUBKEY_LENGTH;
    }

    if (bufLen < (sizeof(uint8_t) + pubKeyLen)) {
        BSL_ERR_PUSH_ERROR(HITLS_PACK_NOT_ENOUGH_BUF_LENGTH);
        BSL_LOG_BINLOG_FIXLEN(BINLOG_ID15674, BSL_LOG_LEVEL_ERR, BSL_LOG_BINLOG_TYPE_RUN,
            "the buffer length is not enough.", 0, 0, 0, 0);
        return HITLS_PACK_NOT_ENOUGH_BUF_LENGTH;
    }
#ifndef HITLS_NO_TLCP11
    if (ctx->negotiatedInfo.version ==
        HITLS_VERSION_TLCP11) { /* Compatible with OpenSSL. Three bytes are added to the client key exchange. */
        if (bufLen < (sizeof(uint8_t) + pubKeyLen + sizeof(uint8_t) + sizeof(uint16_t))) {
            BSL_ERR_PUSH_ERROR(HITLS_INTERNAL_EXCEPTION);
            return HITLS_PACK_NOT_ENOUGH_BUF_LENGTH;
        }
        buf[offset] = HITLS_EC_CURVE_TYPE_NAMED_CURVE;
        offset += sizeof(uint8_t);
        BSL_Uint16ToByte(HITLS_EC_GROUP_SM2, &buf[offset]);
        offset += sizeof(uint16_t);
    }
#endif

    uint32_t pubKeyLenOffset = offset;
    offset += sizeof(uint8_t);
    uint32_t pubKeyUsedLen = 0;
    ret = SAL_CRYPT_EncodeEcdhPubKey(kxCtx->key, &buf[offset], pubKeyLen, &pubKeyUsedLen);
    if (ret != HITLS_SUCCESS || pubKeyLen != pubKeyUsedLen) {
        BSL_ERR_PUSH_ERROR(HITLS_CRYPT_ERR_ENCODE_ECDH_KEY);
        BSL_LOG_BINLOG_FIXLEN(BINLOG_ID15675, BSL_LOG_LEVEL_ERR, BSL_LOG_BINLOG_TYPE_RUN,
            "encode ecdh key fail.", 0, 0, 0, 0);
        return HITLS_CRYPT_ERR_ENCODE_ECDH_KEY;
    }
    offset += pubKeyUsedLen;
    buf[pubKeyLenOffset] = (uint8_t)pubKeyUsedLen;

    *usedLen = offset;
    return HITLS_SUCCESS;
}

static int32_t PackClientKxMsgEcdhe(const TLS_Ctx *ctx, uint8_t *buf, uint32_t bufLen, uint32_t *usedLen)
{
    HITLS_ECCurveType type = ctx->hsCtx->kxCtx->keyExchParam.ecdh.curveParams.type;
    switch (type) {
        case HITLS_EC_CURVE_TYPE_NAMED_CURVE:
            return PackClientKxMsgNamedCurve(ctx, buf, bufLen, usedLen);
        default:
            break;
    }

    BSL_ERR_PUSH_ERROR(HITLS_PACK_UNSUPPORT_KX_CURVE_TYPE);
    BSL_LOG_BINLOG_FIXLEN(BINLOG_ID15676, BSL_LOG_LEVEL_ERR, BSL_LOG_BINLOG_TYPE_RUN,
        "unsupport key exchange curve type.", 0, 0, 0, 0);
    return HITLS_PACK_UNSUPPORT_KX_CURVE_TYPE;
}

static int32_t PackClientKxMsgDhe(const TLS_Ctx *ctx, uint8_t *buf, uint32_t bufLen, uint32_t *usedLen)
{
    int32_t ret = HITLS_SUCCESS;
    DhParam *dh = &ctx->hsCtx->kxCtx->keyExchParam.dh;
    KeyExchCtx *kxCtx = ctx->hsCtx->kxCtx;

    uint32_t pubkeyLen = dh->plen;
    if (pubkeyLen == 0u) {
        BSL_ERR_PUSH_ERROR(HITLS_PACK_INVALID_KX_PUBKEY_LENGTH);
        BSL_LOG_BINLOG_FIXLEN(BINLOG_ID15677, BSL_LOG_LEVEL_ERR, BSL_LOG_BINLOG_TYPE_RUN,
            "invalid key exchange pubKey length.", 0, 0, 0, 0);
        return HITLS_PACK_INVALID_KX_PUBKEY_LENGTH;
    }

    if (bufLen < (sizeof(uint16_t) + pubkeyLen)) {
        BSL_ERR_PUSH_ERROR(HITLS_PACK_NOT_ENOUGH_BUF_LENGTH);
        BSL_LOG_BINLOG_FIXLEN(BINLOG_ID15678, BSL_LOG_LEVEL_ERR, BSL_LOG_BINLOG_TYPE_RUN,
            "the buffer length is not enough.", 0, 0, 0, 0);
        return HITLS_PACK_NOT_ENOUGH_BUF_LENGTH;
    }

    uint32_t offset = sizeof(uint16_t);
    /* fill pubkey */
    ret = SAL_CRYPT_EncodeDhPubKey(kxCtx->key, &buf[offset], pubkeyLen, &pubkeyLen);
    if (ret != HITLS_SUCCESS) {
        BSL_ERR_PUSH_ERROR(HITLS_CRYPT_ERR_ENCODE_DH_KEY);
        BSL_LOG_BINLOG_FIXLEN(BINLOG_ID15679, BSL_LOG_LEVEL_ERR, BSL_LOG_BINLOG_TYPE_RUN,
            "encode dh pub key fail.", 0, 0, 0, 0);
        return HITLS_CRYPT_ERR_ENCODE_DH_KEY;
    }
    offset += pubkeyLen;
    /* fill pubkey length */
    BSL_Uint16ToByte((uint16_t)pubkeyLen, buf);
    *usedLen = offset;

    return HITLS_SUCCESS;
}

int32_t PackClientKxMsgRsa(TLS_Ctx *ctx, uint8_t *buf, uint32_t bufLen, uint32_t *usedLen)
{
    int32_t ret = HITLS_SUCCESS;
    uint32_t encLen, offset = 0;
    HS_Ctx *hsCtx = ctx->hsCtx;
    KeyExchCtx *kxCtx = hsCtx->kxCtx;
    uint8_t *preMasterSecret = kxCtx->keyExchParam.rsa.preMasterSecret;

    if (bufLen < sizeof(uint16_t)) {
        BSL_ERR_PUSH_ERROR(HITLS_PACK_NOT_ENOUGH_BUF_LENGTH);
        BSL_LOG_BINLOG_FIXLEN(BINLOG_ID15680, BSL_LOG_LEVEL_ERR, BSL_LOG_BINLOG_TYPE_RUN,
            "bufLen = %u is not enough to encrypt PreMasterSecret.", bufLen, 0, 0, 0);
        return HITLS_PACK_NOT_ENOUGH_BUF_LENGTH;
    }

    offset = sizeof(uint16_t);
    encLen = bufLen - offset;

    HITLS_Config *config = &ctx->config.tlsConfig;
    CERT_MgrCtx *mgrCtx = config->certMgrCtx;
    HITLS_CERT_X509 *cert = SAL_CERT_PairGetX509(hsCtx->peerCert);
    if (ctx->config.tlsConfig.needCheckKeyUsage == true &&
        SAL_CERT_CheckCertKeyUsage(ctx, cert, CERT_KEY_CTRL_IS_KEYENC_USAGE) != true) {
        return HITLS_CERT_ERR_KEYUSAGE;
    }

    HITLS_CERT_Key *pubkey = NULL;
    ret = SAL_CERT_X509Ctrl(config, cert, CERT_CTRL_GET_PUB_KEY, NULL, (void *)&pubkey);
    if (ret != HITLS_SUCCESS) {
        return ret;
    }
    ret = SAL_CERT_KeyEncrypt(ctx, pubkey, preMasterSecret, MASTER_SECRET_LEN, &buf[offset], &encLen);
    SAL_CERT_KeyFree(mgrCtx, pubkey);
    if (ret != HITLS_SUCCESS) {
        return ret;
    }
    offset += encLen;
    BSL_Uint16ToByte((uint16_t)encLen, buf);

    *usedLen = offset;
    return HITLS_SUCCESS;
}

#ifndef HITLS_NO_TLCP11
static int32_t PackClientKxMsgEcc(TLS_Ctx *ctx, uint8_t *buf, uint32_t bufLen, uint32_t *usedLen)
{
    int32_t ret = HITLS_SUCCESS;
    HS_Ctx *hsCtx = ctx->hsCtx;
    KeyExchCtx *kxCtx = hsCtx->kxCtx;
    uint8_t *preMasterSecret = kxCtx->keyExchParam.ecc.preMasterSecret;

    if (bufLen < sizeof(uint16_t)) {
        BSL_ERR_PUSH_ERROR(HITLS_INTERNAL_EXCEPTION);
        return HITLS_PACK_NOT_ENOUGH_BUF_LENGTH;
    }

    uint32_t offset = sizeof(uint16_t);
    uint32_t encLen = bufLen - offset;

    /* Encrypt the PreMasterSecret using the public key of the server certificate */
    HITLS_Config *config = &ctx->config.tlsConfig;
    CERT_MgrCtx *certMgrCtx = config->certMgrCtx;
    HITLS_CERT_X509 *certEnc = SAL_CERT_GetTlcpEncCert(hsCtx->peerCert);
    if (ctx->config.tlsConfig.needCheckKeyUsage == true &&
        SAL_CERT_CheckCertKeyUsage(ctx, certEnc, CERT_KEY_CTRL_IS_KEYENC_USAGE) != true) {
        return HITLS_CERT_ERR_KEYUSAGE;
    }
    HITLS_CERT_Key *pubkey = NULL;
    ret = SAL_CERT_X509Ctrl(config, certEnc, CERT_CTRL_GET_PUB_KEY, NULL, (void *)&pubkey);
    if (ret != HITLS_SUCCESS) {
        return ret;
    }
    ret = SAL_CERT_KeyEncrypt(ctx, pubkey, preMasterSecret, MASTER_SECRET_LEN, &buf[offset], &encLen);
    SAL_CERT_KeyFree(certMgrCtx, pubkey);
    if (ret != HITLS_SUCCESS) {
        return ret;
    }
    offset += encLen;

    /* fill the ciphertext length */
    BSL_Uint16ToByte((uint16_t)encLen, buf);

    *usedLen = offset;
    return HITLS_SUCCESS;
}
#endif

static int32_t PackClientKxMsgIdentity(const TLS_Ctx *ctx, uint8_t *buf, uint32_t bufLen, uint32_t *usedLen)
{
    uint8_t *pskIdentity = ctx->hsCtx->kxCtx->pskInfo->identity;
    uint32_t pskIdentitySize = ctx->hsCtx->kxCtx->pskInfo->identityLen;
    uint32_t dataLen = sizeof(uint16_t) + pskIdentitySize;

    if (bufLen < dataLen) {
        BSL_ERR_PUSH_ERROR(HITLS_PACK_NOT_ENOUGH_BUF_LENGTH);
        return HITLS_PACK_NOT_ENOUGH_BUF_LENGTH;
    }

    /* append identity */
    uint32_t offset = 0u;
    BSL_Uint16ToByte((uint16_t)pskIdentitySize, &buf[offset]);
    offset += sizeof(uint16_t);

    if (bufLen - offset < pskIdentitySize) {
        BSL_ERR_PUSH_ERROR(HITLS_PACK_NOT_ENOUGH_BUF_LENGTH);
        return HITLS_PACK_NOT_ENOUGH_BUF_LENGTH;
    }
    if (memcpy_s(&buf[offset], bufLen - offset, pskIdentity, pskIdentitySize) != EOK) {
        BSL_ERR_PUSH_ERROR(HITLS_MEMCPY_FAIL);
        return HITLS_MEMCPY_FAIL;
    }
    offset += pskIdentitySize;

    *usedLen = offset;
    return HITLS_SUCCESS;
}

// Pack the ClientKeyExchange message.

int32_t PackClientKeyExchange(TLS_Ctx *ctx, uint8_t *buf, uint32_t bufLen, uint32_t *usedLen)
{
    int32_t ret = HITLS_SUCCESS;
    uint32_t len = 0u;
    uint32_t offset = 0u;

    /* PSK negotiation pre act: append identity */
    if (IsPskNegotiation(ctx)) {
        ret = PackClientKxMsgIdentity(ctx, buf, bufLen, &len);
        if (ret != HITLS_SUCCESS) {
            return ret;
        }
        offset += len;
    }

    len = 0u;
    /* Pack the key exchange message */
    switch (ctx->negotiatedInfo.cipherSuiteInfo.kxAlg) {
        case HITLS_KEY_EXCH_ECDHE: /* TLCP is also included */
        case HITLS_KEY_EXCH_ECDHE_PSK:
            ret = PackClientKxMsgEcdhe(ctx, &buf[offset], bufLen - offset, &len);
            break;
        case HITLS_KEY_EXCH_DHE:
        case HITLS_KEY_EXCH_DHE_PSK:
            ret = PackClientKxMsgDhe(ctx, &buf[offset], bufLen - offset, &len);
            break;
        case HITLS_KEY_EXCH_RSA:
        case HITLS_KEY_EXCH_RSA_PSK:
            ret = PackClientKxMsgRsa(ctx, &buf[offset], bufLen - offset, &len);
            break;
#ifndef HITLS_NO_TLCP11
        case HITLS_KEY_EXCH_ECC:
            ret = PackClientKxMsgEcc(ctx, &buf[offset], bufLen - offset, &len);
            break;
#endif
        case HITLS_KEY_EXCH_PSK:
            ret = HITLS_SUCCESS;
            break;
        default:
            BSL_ERR_PUSH_ERROR(HITLS_PACK_UNSUPPORT_KX_ALG);
            BSL_LOG_BINLOG_FIXLEN(BINLOG_ID15681, BSL_LOG_LEVEL_ERR, BSL_LOG_BINLOG_TYPE_RUN,
                "unsupport key exchange algorithm when pack client key exchange.", 0, 0, 0, 0);
            return HITLS_PACK_UNSUPPORT_KX_ALG;
    }

    if (ret != HITLS_SUCCESS) {
        return ret;
    }
    offset += len;

    *usedLen = offset;
    return HITLS_SUCCESS;
}