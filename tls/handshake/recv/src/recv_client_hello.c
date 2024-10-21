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

#include "securec.h"
#include "tls_binlog_id.h"
#include "bsl_log_internal.h"
#include "bsl_log.h"
#include "bsl_sal.h"
#include "bsl_err_internal.h"
#include "hitls.h"
#include "hitls_error.h"
#include "hitls_sni.h"
#include "hitls_alpn.h"
#include "hitls_security.h"
#include "bsl_uio.h"
#include "session_mgr.h"
#include "security.h"
#include "sni.h"
#include "hs_ctx.h"
#include "hs.h"
#include "hs_common.h"
#include "hs_verify.h"

#define HS_MAX_BINDER_SIZE 64

static void CheckRenegotiate(TLS_Ctx *ctx)
{
    /* For the server, sending a Hello Request message does not count the renegotiation. The server enters the
     * renegotiation state only after receiving a Hello message from the client. If the version number is not 0, it
     * indicates that a handshake has been performed once. In this case, the client hello process enters the
     * renegotiation state again. */
    if (ctx->negotiatedInfo.version != 0u) {
        ctx->negotiatedInfo.isRenegotiation = true;  // enters the renegotiation state.
    }
    return;
}

/**
* @brief Check the extension of the client hello point format.
*
* @param clientHello [IN] client hello message
*
* @retval HITLS_SUCCESS succeeded.
* @retval HITLS_MSG_HANDLE_UNSUPPORT_POINT_FORMAT Unsupported point format
*/
static int32_t ServerCheckPointFormats(const ClientHelloMsg *clientHello)
{
    /* Point format extension not received */
    if (!clientHello->extension.flag.havePointFormats) {
        return HITLS_SUCCESS;
    }
    /* Traverse the list of point formats */
    for (uint32_t i = 0u; i < clientHello->extension.content.pointFormatsSize; i++) {
        /* The point format list contains uncompressed (0) */
        if (clientHello->extension.content.pointFormats[i] == 0u) {
            return HITLS_SUCCESS;
        }
    }

    BSL_ERR_PUSH_ERROR(HITLS_MSG_HANDLE_UNSUPPORT_POINT_FORMAT);
    BSL_LOG_BINLOG_FIXLEN(BINLOG_ID15210, BSL_LOG_LEVEL_INFO, BSL_LOG_BINLOG_TYPE_RUN,
        "the point format extension in client hello is unsupported.", 0, 0, 0, 0);
    return HITLS_MSG_HANDLE_UNSUPPORT_POINT_FORMAT;
}

/**
* @brief Select elliptic curve
*
* @param ctx [IN] TLS context
* @param clientHello [IN] Client Hello message
*
* @return Return curveID. If the value is 0, the supported curve is not found.
*/
static uint16_t ServerSelectCurveId(const TLS_Ctx *ctx, const ClientHelloMsg *clientHello)
{
    uint32_t perferenceGroupsSize = 0;
    uint32_t normalGroupsSize = 0;
    uint16_t *perferenceGroups = NULL;
    uint16_t *normalGroups = NULL;
    if (ctx->config.tlsConfig.isSupportServerPreference) {
        perferenceGroupsSize = ctx->config.tlsConfig.groupsSize;
        normalGroupsSize = clientHello->extension.content.supportedGroupsSize;
        perferenceGroups = ctx->config.tlsConfig.groups;
        normalGroups = clientHello->extension.content.supportedGroups;
    } else {
        perferenceGroupsSize = clientHello->extension.content.supportedGroupsSize;
        normalGroupsSize = ctx->config.tlsConfig.groupsSize;
        perferenceGroups = clientHello->extension.content.supportedGroups;
        normalGroups = ctx->config.tlsConfig.groups;
    }

    /* Find supported curves */
    for (uint32_t i = 0u; i < perferenceGroupsSize; i++) {
        for (uint32_t j = 0u; j < normalGroupsSize; j++) {
            if (perferenceGroups[i] != normalGroups[j]) {
                continue;
            }

            /* Support group security check */
            int32_t id = (int32_t)perferenceGroups[i];
            int32_t ret = SECURITY_SslCheck((HITLS_Ctx *)ctx, HITLS_SECURITY_SECOP_CURVE_SHARED, 0, id, NULL);
            if (ret != SECURITY_SUCCESS ||
                !GroupConformToVersion(ctx->negotiatedInfo.version, perferenceGroups[i])) {
                continue;
            }

            return perferenceGroups[i];
        }
    }

    BSL_ERR_PUSH_ERROR(HITLS_MSG_HANDLE_UNSUPPORT_NAMED_CURVE);
    BSL_LOG_BINLOG_FIXLEN(BINLOG_ID15211, BSL_LOG_LEVEL_INFO, BSL_LOG_BINLOG_TYPE_RUN,
        "the curve id in client hello is unsupported.", 0, 0, 0, 0);
    return 0;
}

/**
* @brief Select a proper certificate based on the TLS cipher suite.
*
* @param ctx [IN] TLS context
* @param clientHello [IN] Client Hello message
* @param cipherInfo [IN] TLS cipher suite
*
* @retval HITLS_SUCCESS succeeded.
* @retval HITLS_MEMALLOC_FAIL Memory application failed.
*/
static int32_t HsServerSelectCert(TLS_Ctx *ctx, const ClientHelloMsg *clientHello, const CipherSuiteInfo *cipherInfo)
{
    uint16_t signHashAlgo = cipherInfo->signScheme;
    CERT_ExpectInfo expectCertInfo = {0};
    expectCertInfo.certType = CFG_GetCertTypeByCipherSuite(cipherInfo->cipherSuite);

    /* For TLCP1.1, ignore the signature extension of client hello */
    if (clientHello->extension.content.signatureAlgorithms != NULL &&
        (ctx->negotiatedInfo.version != HITLS_VERSION_TLCP11)) {
        expectCertInfo.signSchemeList = clientHello->extension.content.signatureAlgorithms;
        expectCertInfo.signSchemeNum = clientHello->extension.content.signatureAlgorithmsSize;
    } else {
        expectCertInfo.signSchemeList = &signHashAlgo;
        expectCertInfo.signSchemeNum = 1u;
    }
    /* The ECDSA certificate must match the supported_groups and ec_point_format extensions */
    expectCertInfo.ellipticCurveList = clientHello->extension.content.supportedGroups;
    expectCertInfo.ellipticCurveNum = clientHello->extension.content.supportedGroupsSize;
    /* Only the uncompressed format is supported */
    uint8_t pointFormat = HITLS_POINT_FORMAT_UNCOMPRESSED;
    expectCertInfo.ecPointFormatList = &pointFormat;
    expectCertInfo.ecPointFormatNum = 1u;

    return SAL_CERT_SelectCertByInfo(ctx, &expectCertInfo);
}

static int32_t Tls13ServerSelectCert(TLS_Ctx *ctx, const ClientHelloMsg *clientHello)
{
    /* If a PSK exists, no certificate needs to be sent regardless of whether the PSK is psk_only or psk_with_dhe */
    if (ctx->hsCtx->kxCtx->pskInfo13.psk != NULL) {
        return HITLS_SUCCESS;
    }

    /* rfc 8446 4.2.3. If the client does not provide the signature algorithm extension, an alert message must be sent.
     */
    if (clientHello->extension.content.signatureAlgorithms == NULL) {
        ctx->method.sendAlert(ctx, ALERT_LEVEL_FATAL, ALERT_MISSING_EXTENSION);
        return HITLS_MSG_HANDLE_MISSING_EXTENSION;
    }

    CERT_ExpectInfo expectCertInfo = {0};
    expectCertInfo.certType = CERT_TYPE_UNKNOWN; /* Do not specify the certificate type */
    expectCertInfo.signSchemeList = clientHello->extension.content.signatureAlgorithms;
    expectCertInfo.signSchemeNum = clientHello->extension.content.signatureAlgorithmsSize;

    /* Only the uncompressed format is supported */
    uint8_t pointFormat = HITLS_POINT_FORMAT_UNCOMPRESSED;
    expectCertInfo.ecPointFormatList = &pointFormat;
    expectCertInfo.ecPointFormatNum = 1u;

    int32_t ret =  SAL_CERT_SelectCertByInfo(ctx, &expectCertInfo);
    if (ret != HITLS_SUCCESS) {
        /* No proper certificate */
        BSL_LOG_BINLOG_FIXLEN(BINLOG_ID15219, BSL_LOG_LEVEL_INFO, BSL_LOG_BINLOG_TYPE_RUN,
            "have no suitable cert.", 0, 0, 0, 0);
        ctx->method.sendAlert(ctx, ALERT_LEVEL_FATAL, ALERT_HANDSHAKE_FAILURE);
        return HITLS_MSG_HANDLE_ERR_NO_SERVER_CERTIFICATE;
    }
    return HITLS_SUCCESS;
}

#ifndef HITLS_NO_TLCP11
static bool CheckLocalContainCurveType(const uint16_t *groups, uint32_t groupsSize, uint16_t exp)
{
    for (uint32_t i = 0; i < groupsSize; ++i) {
        if (groups[i] == exp) {
            return true;
        }
    }
    return false;
}
#endif

/**
* @brief Process the ECDHE cipher suite.
*
* @param ctx [IN] TLS context
* @param clientHello [IN] Client Hello message
* @param cipherSuiteInfo [OUT] Cipher suite information
*
* @retval HITLS_SUCCESS succeeded.
* @retval HITLS_MSG_HANDLE_UNSUPPORT_CIPHER_SUITE Unsupported cipher suites
*/
static int32_t ProcessEcdheCipherSuite(TLS_Ctx *ctx, const ClientHelloMsg *clientHello)
{
    /* If the curve id is not set, ECDHE cannot be used. */
    if ((ctx->config.tlsConfig.groupsSize == 0u) || (ctx->config.tlsConfig.groups == NULL)) {
        BSL_LOG_BINLOG_FIXLEN(BINLOG_ID15212, BSL_LOG_LEVEL_INFO, BSL_LOG_BINLOG_TYPE_RUN,
            "can not used ecdhe whitout curve id.", 0, 0, 0, 0);
        return HITLS_MSG_HANDLE_UNSUPPORT_CIPHER_SUITE;
    }
#ifndef HITLS_NO_TLCP11
    if (ctx->negotiatedInfo.version == HITLS_VERSION_TLCP11) {
        if (CheckLocalContainCurveType(ctx->config.tlsConfig.groups,
            ctx->config.tlsConfig.groupsSize, HITLS_EC_GROUP_SM2) != true) {
            BSL_LOG_BINLOG_FIXLEN(BINLOG_ID15220, BSL_LOG_LEVEL_INFO, BSL_LOG_BINLOG_TYPE_RUN,
                "TLCP need sm2 curve.", 0, 0, 0, 0);
            return HITLS_MSG_HANDLE_UNSUPPORT_CIPHER_SUITE;
        }
        ctx->hsCtx->kxCtx->keyExchParam.ecdh.curveParams.type = HITLS_EC_CURVE_TYPE_NAMED_CURVE;
        ctx->hsCtx->kxCtx->keyExchParam.ecdh.curveParams.param.namedcurve = HITLS_EC_GROUP_SM2;
        return HITLS_SUCCESS; /* TLCP negotiation does not focus on extended information. */
    }
#endif
    /* Check the Point format extension of the clientHello. This extension is not included in the TLCP */
    int32_t ret = ServerCheckPointFormats(clientHello);
    if (ret != HITLS_SUCCESS) {
        BSL_LOG_BINLOG_FIXLEN(BINLOG_ID15213, BSL_LOG_LEVEL_INFO, BSL_LOG_BINLOG_TYPE_RUN,
            "server check client hello point formats fail.", 0, 0, 0, 0);
        return HITLS_MSG_HANDLE_UNSUPPORT_CIPHER_SUITE;
    }

    uint16_t selectedEcCurveId = ServerSelectCurveId(ctx, clientHello);
    if (selectedEcCurveId == 0) {
        BSL_LOG_BINLOG_FIXLEN(BINLOG_ID15214, BSL_LOG_LEVEL_INFO, BSL_LOG_BINLOG_TYPE_RUN,
            "server select curve id fail.", 0, 0, 0, 0);
        return HITLS_MSG_HANDLE_UNSUPPORT_CIPHER_SUITE;
    }
    if (ctx->negotiatedInfo.version == HITLS_VERSION_TLS13) {
        ctx->hsCtx->kxCtx->keyExchParam.share.group = selectedEcCurveId;
    } else {
        ctx->hsCtx->kxCtx->keyExchParam.ecdh.curveParams.type = HITLS_EC_CURVE_TYPE_NAMED_CURVE;
        ctx->hsCtx->kxCtx->keyExchParam.ecdh.curveParams.param.namedcurve = selectedEcCurveId;
    }

    ctx->negotiatedInfo.negotiatedGroup = selectedEcCurveId;
    return HITLS_SUCCESS;
}

/**
* @brief Check whether the server supports the cipher suite.
*
* @param ctx [IN] TLS context
* @param clientHello [IN] client hello message
* @param cipher [IN] cipher suite ID
*
* @retval HITLS_SUCCESS succeeded.
* @retval HITLS_MEMCPY_FAIL Memory Copy Failure
* @retval HITLS_MSG_HANDLE_UNSUPPORT_CIPHER_SUITE Unsupported cipher suites
*/
static int32_t ServerNegotiateCipher(TLS_Ctx *ctx, const ClientHelloMsg *clientHello, uint16_t cipher)
{
    CipherSuiteInfo cipherSuiteInfo = {0};
    int32_t ret = 0;

    ret = CFG_GetCipherSuiteInfo(cipher, &cipherSuiteInfo);
    if (ret != HITLS_SUCCESS) {
        BSL_LOG_BINLOG_FIXLEN(BINLOG_ID15215, BSL_LOG_LEVEL_INFO, BSL_LOG_BINLOG_TYPE_RUN,
            "get cipher suite info fail when processing client hello.", 0, 0, 0, 0);
        return HITLS_MSG_HANDLE_UNSUPPORT_CIPHER_SUITE;
    }

    /* If the key exchange algorithm is not PSK, DHE_PSK, or ECDHE_PSK, select a certificate. */
    if (IsNeedCertPrepare(&cipherSuiteInfo) == true) {
        ret = HsServerSelectCert(ctx, clientHello, &cipherSuiteInfo);
        if (ret != HITLS_SUCCESS) {
            /* No proper certificate */
            BSL_LOG_BINLOG_FIXLEN(BINLOG_ID15216, BSL_LOG_LEVEL_INFO, BSL_LOG_BINLOG_TYPE_RUN,
                "have no suitable cert.", 0, 0, 0, 0);
            return HITLS_MSG_HANDLE_ERR_NO_SERVER_CERTIFICATE;
        }
    }

    switch (cipherSuiteInfo.kxAlg) {
        case HITLS_KEY_EXCH_ECDHE: /* the ECDHE of TLCP is also in this branch */
        case HITLS_KEY_EXCH_ECDHE_PSK:
            /* The ECC cipher suite needs to process the supported_groups and ec_point_formats extensions */
            ret = ProcessEcdheCipherSuite(ctx, clientHello);
            break;
        case HITLS_KEY_EXCH_DHE:
        case HITLS_KEY_EXCH_DHE_PSK:
            ret = HITLS_SUCCESS;
            break;
        case HITLS_KEY_EXCH_RSA:
#ifndef HITLS_NO_TLCP11
        case HITLS_KEY_EXCH_ECC:
#endif
            ret = HITLS_SUCCESS;
            break;
        case HITLS_KEY_EXCH_PSK:
        case HITLS_KEY_EXCH_RSA_PSK:
            ret = HITLS_SUCCESS;
            break;
        default:
            ret = HITLS_MSG_HANDLE_UNSUPPORT_CIPHER_SUITE;
    }
    if (ret != HITLS_SUCCESS) {
        BSL_LOG_BINLOG_FIXLEN(BINLOG_ID15217, BSL_LOG_LEVEL_INFO, BSL_LOG_BINLOG_TYPE_RUN,
            "server process ecdhe cipher suite fail.", 0, 0, 0, 0);
        return ret;
    }

    ctx->hsCtx->kxCtx->keyExchAlgo = cipherSuiteInfo.kxAlg;
    (void)memcpy_s(&ctx->negotiatedInfo.cipherSuiteInfo, sizeof(CipherSuiteInfo),
                   &cipherSuiteInfo, sizeof(CipherSuiteInfo));
    return ret;
}

static int32_t Tls13ServerNegotiateCipher(TLS_Ctx *ctx, const ClientHelloMsg *clientHello, uint16_t cipher)
{
    (void)clientHello;
    int32_t ret = 0;
    CipherSuiteInfo cipherSuiteInfo = {0};

    ret = CFG_GetCipherSuiteInfo(cipher, &cipherSuiteInfo);
    if (ret != HITLS_SUCCESS) {
        BSL_LOG_BINLOG_FIXLEN(BINLOG_ID15218, BSL_LOG_LEVEL_INFO, BSL_LOG_BINLOG_TYPE_RUN,
            "get cipher suite info fail when processing client hello.", 0, 0, 0, 0);
        return HITLS_MSG_HANDLE_UNSUPPORT_CIPHER_SUITE;
    }

    (void)memcpy_s(&ctx->negotiatedInfo.cipherSuiteInfo, sizeof(CipherSuiteInfo),
        &cipherSuiteInfo, sizeof(CipherSuiteInfo));
    return HITLS_SUCCESS;
}

static int32_t CheckCipherSuite(TLS_Ctx *ctx, const ClientHelloMsg *clientHello, uint16_t version, uint16_t cipherSuite)
{
    if ((CFG_CheckCipherSuiteSupported(cipherSuite) != true) ||
        (CFG_CheckCipherSuiteVersion(cipherSuite, version, version) != true)) {
        return HITLS_CONFIG_UNSUPPORT_CIPHER_SUITE;
    }
    int32_t ret = 0;
    if (ctx->negotiatedInfo.version == HITLS_VERSION_TLS13) {
        ret = Tls13ServerNegotiateCipher(ctx, clientHello, cipherSuite);
    } else {
        ret = ServerNegotiateCipher(ctx, clientHello, cipherSuite);
    }
    if (ret != HITLS_SUCCESS) {
        return ret;
    }

    /* Check the security level of ciphersuites */
    CipherSuiteInfo *cipherSuiteInfo = &ctx->negotiatedInfo.cipherSuiteInfo;
    ret = SECURITY_SslCheck((HITLS_Ctx *)ctx, HITLS_SECURITY_SECOP_CIPHER_SHARED, 0, 0, (void *)cipherSuiteInfo);
    if (ret != SECURITY_SUCCESS) {
        BSL_ERR_PUSH_ERROR(HITLS_MSG_HANDLE_UNSECURE_CIPHER_SUITE);
        return HITLS_MSG_HANDLE_UNSECURE_CIPHER_SUITE;
    }
    BSL_LOG_BINLOG_FIXLEN(BINLOG_ID15221, BSL_LOG_LEVEL_INFO, BSL_LOG_BINLOG_TYPE_RUN, "chosen ciphersuite 0x%04x",
        cipherSuiteInfo->cipherSuite, 0, 0, 0);
    BSL_LOG_BINLOG_VARLEN(BINLOG_ID15894, BSL_LOG_LEVEL_INFO, BSL_LOG_BINLOG_TYPE_RUN, "chosen ciphersuite: %s",
        cipherSuiteInfo->name);

    return HITLS_SUCCESS;
}

// Select the cipher suite.
int32_t ServerSelectCipherSuite(TLS_Ctx *ctx, const ClientHelloMsg *clientHello)
{
    /* Obtain server information */
    uint16_t version = ctx->negotiatedInfo.version;
    uint16_t *cfgCipherSuites = ctx->config.tlsConfig.cipherSuites;
    uint32_t cfgCipherSuitesSize = ctx->config.tlsConfig.cipherSuitesSize;
    if (version == HITLS_VERSION_TLS13) {
        cfgCipherSuites = ctx->config.tlsConfig.tls13CipherSuites;
        cfgCipherSuitesSize = ctx->config.tlsConfig.tls13cipherSuitesSize;
    }

    const uint16_t *preferenceCipherSuites = clientHello->cipherSuites;
    uint16_t preferenceCipherSuitesSize = clientHello->cipherSuitesSize;
    const uint16_t *normalCipherSuites = cfgCipherSuites;
    uint16_t normalCipherSuitesSize = (uint16_t)cfgCipherSuitesSize;

    if (ctx->config.tlsConfig.isSupportServerPreference) {
        preferenceCipherSuites = cfgCipherSuites;
        preferenceCipherSuitesSize = (uint16_t)cfgCipherSuitesSize;
        normalCipherSuites = clientHello->cipherSuites;
        normalCipherSuitesSize = clientHello->cipherSuitesSize;
    }

    /* Select the supported cipher suite. If the cipher suite is found, return success */
    for (uint16_t i = 0u; i < preferenceCipherSuitesSize; i++) {
        for (uint32_t j = 0u; j < normalCipherSuitesSize; j++) {
            if (normalCipherSuites[j] != preferenceCipherSuites[i]) {
                continue;
            }
            if (CheckCipherSuite(ctx, clientHello, version, normalCipherSuites[j]) != HITLS_SUCCESS) {
                break;
            }
            return HITLS_SUCCESS;
        }
    }

    BSL_ERR_PUSH_ERROR(HITLS_MSG_HANDLE_CIPHER_SUITE_ERR);
    BSL_LOG_BINLOG_FIXLEN(BINLOG_ID15222, BSL_LOG_LEVEL_ERR, BSL_LOG_BINLOG_TYPE_RUN,
        "can not find a appropriate cipher suite.", 0, 0, 0, 0);
    ctx->method.sendAlert(ctx, ALERT_LEVEL_FATAL, ALERT_HANDSHAKE_FAILURE);
    return HITLS_MSG_HANDLE_CIPHER_SUITE_ERR;
}

/**
 * @brief Select the negotiation version based on the client Hello message.
 *
 * @param ctx [IN] TLS context
 * @param clientHello [IN] client Hello message
 *
 * @retval HITLS_SUCCESS succeeded.
 * @retval HITLS_MSG_HANDLE_UNSUPPORT_VERSION Unsupported version number
 */
static int32_t ServerSelectNegoVersion(TLS_Ctx *ctx, const ClientHelloMsg *clientHello)
{
    int32_t ret = 0;
    uint16_t legacyVersion = clientHello->version;
    if (legacyVersion > HITLS_VERSION_TLS13 && !IS_DTLS_VERSION(HS_GetVersion(ctx))) {
        legacyVersion = HITLS_VERSION_TLS12;
    }
    /* Check whether DTLS is used */
    if (IS_DTLS_VERSION(legacyVersion)) {
        if (legacyVersion > ctx->config.tlsConfig.minVersion) {
            /** The DTLS version supported by the client is too early and the negotiation cannot be continued */
            BSL_ERR_PUSH_ERROR(HITLS_MSG_HANDLE_UNSUPPORT_VERSION);
            BSL_LOG_BINLOG_FIXLEN(BINLOG_ID15223, BSL_LOG_LEVEL_ERR, BSL_LOG_BINLOG_TYPE_RUN,
                "client want a unsupported protocol version 0x%02x.", legacyVersion, 0, 0, 0);
            ctx->method.sendAlert(ctx, ALERT_LEVEL_FATAL, ALERT_PROTOCOL_VERSION);
            return HITLS_MSG_HANDLE_UNSUPPORT_VERSION;
        }
        /** Continue the version negotiation and obtain the earlier DTLS version between the latest versions of the
         * client and server */
        if (legacyVersion < ctx->config.tlsConfig.maxVersion) {
            ctx->negotiatedInfo.version = ctx->config.tlsConfig.maxVersion;
            BSL_LOG_BINLOG_FIXLEN(BINLOG_ID15224, BSL_LOG_LEVEL_INFO, BSL_LOG_BINLOG_TYPE_RUN,
                "client want a unsupported protocol version 0x%02x.", legacyVersion, 0, 0, 0);
        } else {
            ctx->negotiatedInfo.version = legacyVersion;
        }
    } else {
        if (legacyVersion < ctx->config.tlsConfig.minVersion) {
            /* The TLS version supported by the client is too early and cannot be negotiated */
            BSL_ERR_PUSH_ERROR(HITLS_MSG_HANDLE_UNSUPPORT_VERSION);
            BSL_LOG_BINLOG_FIXLEN(BINLOG_ID15225, BSL_LOG_LEVEL_ERR, BSL_LOG_BINLOG_TYPE_RUN,
                "client version = 0x%02x, min version = 0x%02x.",
                legacyVersion, ctx->config.tlsConfig.minVersion, 0, 0);
            ctx->method.sendAlert(ctx, ALERT_LEVEL_FATAL, ALERT_PROTOCOL_VERSION);
            return HITLS_MSG_HANDLE_UNSUPPORT_VERSION;
        }
        /* Continue the version negotiation. Obtain the earlier version between the latest versions of the client and
         * server */
        if (legacyVersion > ctx->config.tlsConfig.maxVersion) {
            ctx->negotiatedInfo.version = ctx->config.tlsConfig.maxVersion;
            BSL_LOG_BINLOG_FIXLEN(BINLOG_ID15226, BSL_LOG_LEVEL_INFO, BSL_LOG_BINLOG_TYPE_RUN,
                "client version = 0x%02x, max version = 0x%02x.",
                legacyVersion, ctx->config.tlsConfig.maxVersion, 0, 0);
        } else {
            ctx->negotiatedInfo.version = legacyVersion;
        }
    }

    /* Version security check */
    ret = SECURITY_SslCheck((HITLS_Ctx *)ctx, HITLS_SECURITY_SECOP_VERSION, 0, ctx->negotiatedInfo.version, NULL);
    if (ret != SECURITY_SUCCESS) {
        BSL_ERR_PUSH_ERROR(HITLS_MSG_HANDLE_UNSECURE_VERSION);
        ctx->method.sendAlert(ctx, ALERT_LEVEL_FATAL, ALERT_INSUFFICIENT_SECURITY);
        return HITLS_MSG_HANDLE_UNSECURE_VERSION;
    }

    return HITLS_SUCCESS;
}

static int32_t ServerSelectAlpnProtocol(TLS_Ctx *ctx, const ClientHelloMsg *clientHello)
{
    uint8_t *alpnSelected = NULL;
    uint8_t alpnSelectedLen = 0u;

    /* If the callback is empty, the server does not have the ALPN processing capability. In this case, return success
     */
    if (ctx->globalConfig != NULL && ctx->globalConfig->alpnSelectCb != NULL) {
        int32_t alpnCbRet = ctx->globalConfig->alpnSelectCb(ctx, &alpnSelected, &alpnSelectedLen,
            clientHello->extension.content.alpnList, clientHello->extension.content.alpnListSize,
            ctx->globalConfig->alpnUserData);
        if (alpnCbRet == HITLS_ALPN_ERR_OK) {
            uint8_t *alpnSelectedTmp = (uint8_t *)BSL_SAL_Calloc(alpnSelectedLen + 1, sizeof(uint8_t));
            if (alpnSelectedTmp == NULL) {
                BSL_ERR_PUSH_ERROR(HITLS_MEMALLOC_FAIL);
                BSL_LOG_BINLOG_FIXLEN(BINLOG_ID15227, BSL_LOG_LEVEL_ERR, BSL_LOG_BINLOG_TYPE_RUN,
                    "server malloc alpn buffer failed.", 0, 0, 0, 0);
                ctx->method.sendAlert(ctx, ALERT_LEVEL_FATAL, ALERT_INTERNAL_ERROR);
                return HITLS_MEMALLOC_FAIL;
            }
            if (memcpy_s(alpnSelectedTmp, alpnSelectedLen + 1, alpnSelected, alpnSelectedLen) != EOK) {
                BSL_SAL_FREE(alpnSelectedTmp);
                BSL_ERR_PUSH_ERROR(HITLS_MEMCPY_FAIL);
                BSL_LOG_BINLOG_FIXLEN(BINLOG_ID15243, BSL_LOG_LEVEL_ERR, BSL_LOG_BINLOG_TYPE_RUN,
                    "server copy selected alpn failed.", 0, 0, 0, 0);
                ctx->method.sendAlert(ctx, ALERT_LEVEL_FATAL, ALERT_INTERNAL_ERROR);
                return HITLS_MEMCPY_FAIL;
            }
            BSL_SAL_FREE(ctx->negotiatedInfo.alpnSelected);
            ctx->negotiatedInfo.alpnSelected = alpnSelectedTmp;
            ctx->negotiatedInfo.alpnSelectedSize = alpnSelectedLen;

            BSL_LOG_BINLOG_VARLEN(BINLOG_ID15228, BSL_LOG_LEVEL_INFO, BSL_LOG_BINLOG_TYPE_RUN,
                "select ALPN protocol: %s.", ctx->negotiatedInfo.alpnSelected);
            /* Based on RFC7301, if the server cannot match the application layer protocol in the client alpn list, it
             * sends a fatal alert to the peer.
             * If the returned value is not HITLS_ALPN_ERR_NOACK, the system sends a fatal alert message to the peer
             */
        } else if (alpnCbRet != HITLS_ALPN_ERR_NOACK) {
            BSL_ERR_PUSH_ERROR(HITLS_MSG_HANDLE_ALPN_PROTOCOL_NO_MATCH);
            BSL_LOG_BINLOG_FIXLEN(BINLOG_ID15229, BSL_LOG_LEVEL_ERR, BSL_LOG_BINLOG_TYPE_RUN,
                "server invoke alpn select cb error.", 0, 0, 0, 0);
            ctx->method.sendAlert(ctx, ALERT_LEVEL_FATAL, ALERT_NO_APPLICATION_PROTOCOL);
            return HITLS_MSG_HANDLE_ALPN_PROTOCOL_NO_MATCH;
        }
    }

    return HITLS_SUCCESS;
}

static int32_t ServerDealServerName(TLS_Ctx *ctx, const ClientHelloMsg *clientHello)
{
    int32_t ret = 0;
    int32_t alert = ALERT_UNRECOGNIZED_NAME;
    uint32_t serverNameSize = clientHello->extension.content.serverNameSize;

    if (clientHello->extension.flag.haveServerName == false) {
        return HITLS_SUCCESS;
    }

    BSL_SAL_FREE(ctx->hsCtx->serverName);
    ctx->hsCtx->serverName = (uint8_t *)BSL_SAL_Dump(clientHello->extension.content.serverName,
        serverNameSize * sizeof(uint8_t));

    if (ctx->hsCtx->serverName == NULL) {
        BSL_ERR_PUSH_ERROR(HITLS_MEMCPY_FAIL);
        BSL_LOG_BINLOG_FIXLEN(BINLOG_ID15230, BSL_LOG_LEVEL_ERR, BSL_LOG_BINLOG_TYPE_RUN,
            "server_name malloc fail when parse extensions msg.", 0, 0, 0, 0);
        return HITLS_MEMCPY_FAIL;
    }

    ctx->hsCtx->serverNameSize = serverNameSize;

    /* The product does not have the registered server_name callback processing function */
    if (ctx->globalConfig == NULL || ctx->globalConfig->sniDealCb == NULL) {
        /* Rejected, but continued handshake */
        ctx->negotiatedInfo.isSniStateOK = false;
        BSL_LOG_BINLOG_FIXLEN(BINLOG_ID15231, BSL_LOG_LEVEL_INFO, BSL_LOG_BINLOG_TYPE_RUN,
            "during first handshake, server did not set sni callback deal, but continue handshake", 0, 0, 0, 0);
        return HITLS_SUCCESS;
    }

    /* Execute the product callback function */
    ret = ctx->globalConfig->sniDealCb(ctx, &alert, ctx->globalConfig->sniArg);
    switch (ret) {
        case HITLS_ACCEPT_SNI_ERR_OK:
            ctx->negotiatedInfo.isSniStateOK = true;
            BSL_LOG_BINLOG_FIXLEN(BINLOG_ID15232, BSL_LOG_LEVEL_INFO, BSL_LOG_BINLOG_TYPE_RUN,
                "during first handshake, server accept server_name from client hello msg ", 0, 0, 0, 0);
            break;
        case HITLS_ACCEPT_SNI_ERR_NOACK:
            ctx->negotiatedInfo.isSniStateOK = false;
            BSL_LOG_BINLOG_FIXLEN(BINLOG_ID15233, BSL_LOG_LEVEL_WARN, BSL_LOG_BINLOG_TYPE_RUN,"during first handshake, \
                server did not accept server_name from client hello msg, but continue handshake", 0, 0, 0, 0);
            break;
        case HITLS_ACCEPT_SNI_ERR_ALERT_FATAL:
        default:
            BSL_LOG_BINLOG_FIXLEN(BINLOG_ID15234, BSL_LOG_LEVEL_ERR, BSL_LOG_BINLOG_TYPE_RUN,
                "during first handshake, server did not accept server_name from client hello msg, stop handshake",
                0, 0, 0, 0);
            ctx->negotiatedInfo.isSniStateOK = false;
            ctx->method.sendAlert(ctx, ALERT_LEVEL_FATAL, ALERT_UNRECOGNIZED_NAME);
            return  HITLS_MSG_HANDLE_SNI_UNRECOGNIZED_NAME;
    }

    return HITLS_SUCCESS;
}

static int32_t DealResumeAlpnEx(TLS_Ctx *ctx, const ClientHelloMsg *clientHello)
{
    if (clientHello->extension.flag.haveAlpn) {
        return ServerSelectAlpnProtocol(ctx, clientHello);
    }
    return HITLS_SUCCESS;
}

static int32_t DealResumeServerName(TLS_Ctx *ctx, const ClientHelloMsg *clientHello,
    uint32_t serverNameSize, uint8_t *serverName)
{
    /* Continue processing only when the TLS protocol version <=TLS1.2 */
    if (ctx->negotiatedInfo.version >= HITLS_VERSION_TLS13 && ctx->negotiatedInfo.version != HITLS_VERSION_DTLS12) {
        return HITLS_SUCCESS;
    }

    if (ctx->globalConfig != NULL && ctx->globalConfig->sniDealCb == NULL && serverNameSize == 0) {
        ctx->negotiatedInfo.isSniStateOK = false;
        return HITLS_SUCCESS;
    }

    if (serverName != NULL && serverNameSize != 0 && clientHello->extension.flag.haveServerName == false) {
        BSL_LOG_BINLOG_VARLEN(BINLOG_ID15246, BSL_LOG_LEVEL_ERR, BSL_LOG_BINLOG_TYPE_RUN,
            "during session resumption, session server name is [%s]", (char *)serverName);
        BSL_LOG_BINLOG_FIXLEN(BINLOG_ID15933, BSL_LOG_LEVEL_ERR, BSL_LOG_BINLOG_TYPE_RUN,
            "There is no server name in client hello msg.", 0, 0, 0, 0);
        ctx->negotiatedInfo.isSniStateOK = false;

        return  HITLS_MSG_HANDLE_SNI_UNRECOGNIZED_NAME;
    }

    /* Compare the extended value of client hello server_name and the value of server_name in the session during session
     * resumption */
    if (clientHello->extension.content.serverNameSize != serverNameSize ||
        SNI_StrcaseCmp((char *)clientHello->extension.content.serverName, (char *)serverName) != 0) {
        BSL_LOG_BINLOG_VARLEN(BINLOG_ID15235,
            BSL_LOG_LEVEL_ERR,
            BSL_LOG_BINLOG_TYPE_RUN,
            "during session resume ,session servername is [%s]",
            (char *)serverName);
        BSL_LOG_BINLOG_VARLEN(BINLOG_ID15254,
            BSL_LOG_LEVEL_ERR,
            BSL_LOG_BINLOG_TYPE_RUN,
            "server did not accept server_name [%s] from client hello msg",
            (char *)clientHello->extension.content.serverName);
        ctx->negotiatedInfo.isSniStateOK = false;

        return HITLS_MSG_HANDLE_SNI_UNRECOGNIZED_NAME;
    }

    BSL_LOG_BINLOG_FIXLEN(BINLOG_ID15236, BSL_LOG_LEVEL_INFO, BSL_LOG_BINLOG_TYPE_RUN,
        "during session resume, server accept server_name [%s] from client hello msg.", (char *)serverName, 0, 0, 0);

    return HITLS_SUCCESS;
}

int32_t ServerCheckResumeCipherSuite(const ClientHelloMsg *clientHello, uint16_t cipherSuite)
{
    for (uint16_t i = 0u; i < clientHello->cipherSuitesSize; i++) {
        if (cipherSuite == clientHello->cipherSuites[i]) {
            return HITLS_SUCCESS;
        }
    }

    BSL_LOG_BINLOG_FIXLEN(BINLOG_ID15237, BSL_LOG_LEVEL_DEBUG, BSL_LOG_BINLOG_TYPE_RUN,
        "Client's cipher suites do not match resume cipher suite.", 0, 0, 0, 0);
    return HITLS_MSG_HANDLE_ILLEGAL_CIPHER_SUITE;
}

/* Check whether the session ID ctx matches */
bool ServerCmpSessionIdCtx(TLS_Ctx *ctx, HITLS_Session *sess)
{
    uint8_t sessionIdCtx[HITLS_SESSION_ID_CTX_MAX_SIZE];
    uint32_t sessionIdCtxSize = HITLS_SESSION_ID_CTX_MAX_SIZE;

    if (HITLS_SESS_GetSessionIdCtx(sess, sessionIdCtx, &sessionIdCtxSize) != HITLS_SUCCESS) {
        return false;
    }

    /* The session ID ctx lengths are not equal */
    if (sessionIdCtxSize != ctx->config.tlsConfig.sessionIdCtxSize) {
        return false;
    }

    /* The session ID ctx is not equal */
    if (sessionIdCtxSize != 0 && memcmp(sessionIdCtx, ctx->config.tlsConfig.sessionIdCtx, sessionIdCtxSize) != 0) {
        return false;
    }

    return true;
}

static int32_t ServerCheckResumeParam(TLS_Ctx *ctx, const ClientHelloMsg *clientHello)
{
    int32_t ret = HITLS_SUCCESS;
    uint16_t version = 0;
    uint16_t cipherSuite = 0;
    HITLS_Session *sess = ctx->session;

    HITLS_SESS_GetProtocolVersion(sess, &version);
    HITLS_SESS_GetCipherSuite(sess, &cipherSuite);

    if (ServerCmpSessionIdCtx(ctx, sess) != true) {
        BSL_LOG_BINLOG_FIXLEN(BINLOG_ID15886, BSL_LOG_LEVEL_ERR, BSL_LOG_BINLOG_TYPE_RUN,
            "Resuming Sessions: session id ctx is inconsistent.", 0, 0, 0, 0);
        ctx->method.sendAlert(ctx, ALERT_LEVEL_FATAL, ALERT_ILLEGAL_PARAMETER);
        return HITLS_MSG_HANDLE_SESSION_ID_CTX_ILLEGAL;
    }

    if (ctx->negotiatedInfo.version != version) {
        BSL_LOG_BINLOG_FIXLEN(BINLOG_ID15887, BSL_LOG_LEVEL_ERR, BSL_LOG_BINLOG_TYPE_RUN,
            "Resuming Sessions: version is inconsistent.", 0, 0, 0, 0);
        ctx->method.sendAlert(ctx, ALERT_LEVEL_FATAL, ALERT_PROTOCOL_VERSION);
        return HITLS_MSG_HANDLE_ILLEGAL_VERSION;
    }

    ret = ServerCheckResumeCipherSuite(clientHello, cipherSuite);
    if (ret != HITLS_SUCCESS) {
        ctx->method.sendAlert(ctx, ALERT_LEVEL_FATAL, ALERT_ILLEGAL_PARAMETER);
        return ret;
    }

    ret = CFG_GetCipherSuiteInfo(cipherSuite, &ctx->negotiatedInfo.cipherSuiteInfo);
    if (ret != HITLS_SUCCESS) {
        ctx->method.sendAlert(ctx, ALERT_LEVEL_FATAL, ALERT_INTERNAL_ERROR);
        return ret;
    }
    /* During session resumption, the server processes the ALPN extension in the ClientHello message */
    return DealResumeAlpnEx(ctx, clientHello);
}

static int32_t ServerCheckResumeSni(TLS_Ctx *ctx, const ClientHelloMsg *clientHello, HITLS_Session **sess)
{
    if (*sess == NULL || ctx->config.tlsConfig.maxVersion == HITLS_VERSION_TLCP11) {
        return HITLS_SUCCESS;
    }
    int32_t ret = HITLS_SUCCESS;
    uint8_t *serverName = NULL;
    uint32_t serverNameSize = 0;

    SESS_GetHostName(*sess, &serverNameSize, &serverName);

    /* During session resumption, the server processes the server_name extension in the ClientHello */
    ret = DealResumeServerName(ctx, clientHello, serverNameSize, serverName);
    if (ret != HITLS_SUCCESS) {
        HITLS_SESS_Free(*sess);
        *sess = NULL;
    }
    return HITLS_SUCCESS;
}

/**
    rfc7627 5.3
    If a server receives a ClientHello for an abbreviated handshake
offering to resume a known previous session, it behaves as follows:
--------------------------------------------------------------------------------------------------------
| original sesson | abbreviated handshake  |                     Server behavior                        |
| :-------------: | :--------------------: | :---------------------------------------------------------:|
|      true       |          true          |                 SH with ems, agree resume                  |
|      true       |         false          |                     abort handshake                        |
|      false      |          true          |            disagre resume, full handshake                  |
|      false      |         false          | depend cnf: abort handshake(true) / agree resume (false)   |
*/
static int32_t ResumeCheckExtendedMasterScret(TLS_Ctx *ctx, const ClientHelloMsg *clientHello, HITLS_Session **sess)
{
    if (*sess == NULL || ctx->config.tlsConfig.maxVersion == HITLS_VERSION_TLCP11) {
        return HITLS_SUCCESS;
    }
    uint8_t haveExtMasterSecret = false;
    HITLS_SESS_GetHaveExtMasterSecret(*sess, &haveExtMasterSecret);
    if (haveExtMasterSecret != 0) {
        if (!clientHello->extension.flag.haveExtendedMasterSecret) {
            ctx->method.sendAlert(ctx, ALERT_LEVEL_FATAL, ALERT_HANDSHAKE_FAILURE);
            return HITLS_MSG_HANDLE_INVALID_EXTENDED_MASTER_SECRET;
        }
        ctx->negotiatedInfo.isExtendedMasterSecret = true;
    } else {
        if (clientHello->extension.flag.haveExtendedMasterSecret) {
            HITLS_SESS_Free(*sess);
            *sess = NULL;
        } else if (ctx->config.tlsConfig.isSupportExtendMasterSecret) {
            ctx->method.sendAlert(ctx, ALERT_LEVEL_FATAL, ALERT_HANDSHAKE_FAILURE);
            return HITLS_MSG_HANDLE_INVALID_EXTENDED_MASTER_SECRET;
        }
        ctx->negotiatedInfo.isExtendedMasterSecret = clientHello->extension.flag.haveExtendedMasterSecret;
    }
    return ServerCheckResumeSni(ctx, clientHello, sess);
}

static int32_t ServerCheckResumeTicket(TLS_Ctx *ctx, const ClientHelloMsg *clientHello)
{
    TLS_SessionMgr *sessMgr = ctx->config.tlsConfig.sessMgr;
    HITLS_Session *sess = NULL;
    uint8_t *ticketBuf = clientHello->extension.content.ticket;
    uint32_t ticketBufSize = clientHello->extension.content.ticketSize;
    bool isTicketExpect = false;
    int32_t ret = SESSMGR_DecryptSessionTicket(sessMgr, &sess, ticketBuf, ticketBufSize, &isTicketExpect);
    if (ret != HITLS_SUCCESS) {
        BSL_LOG_BINLOG_FIXLEN(BINLOG_ID16045, BSL_LOG_LEVEL_ERR, BSL_LOG_BINLOG_TYPE_RUN,
            "SESSMGR_DecryptSessionTicket return fail when process client hello.", 0, 0, 0, 0);
        ctx->method.sendAlert(ctx, ALERT_LEVEL_FATAL, ALERT_INTERNAL_ERROR);
        return ret;
    }
    ctx->negotiatedInfo.isTicket = isTicketExpect;
    ret = ResumeCheckExtendedMasterScret(ctx, clientHello, &sess);
    if (ret != HITLS_SUCCESS) {
        HITLS_SESS_Free(sess);
        sess = NULL;
        return ret;
    }
    if (sess != NULL) {
        /* Check whether the session is valid */
        if (SESS_CheckValidity(sess, (uint64_t)BSL_SAL_CurrentSysTimeGet()) == false) {
            /* If the session is invalid, a message is returned and the session is not resume. The complete connection
             * is established */
            ctx->negotiatedInfo.isTicket = true;
            HITLS_SESS_Free(sess);
            return HITLS_SUCCESS;
        }
        HITLS_SESS_Free(ctx->session);
        ctx->session = sess;
        ctx->negotiatedInfo.isResume = true;
        /* If the session is resumed and the session ID of the clientHello is not empty, the session ID needs to be
         * filled in the serverHello and returned */
        HITLS_SESS_SetSessionId(ctx->session, clientHello->sessionId, clientHello->sessionIdSize);
    }
    return HITLS_SUCCESS;
}

/* Check whether the resume function is supported */
static int32_t ServerCheckResume(TLS_Ctx *ctx, const ClientHelloMsg *clientHello)
{
    ctx->negotiatedInfo.isResume = false;
    ctx->negotiatedInfo.isTicket = false;
    /* If session resumption is not allowed in the renegotiation state, return */
    if (ctx->negotiatedInfo.isRenegotiation && !ctx->config.tlsConfig.isResumptionOnRenego) {
        return HITLS_SUCCESS;
    }
    /* Obtain the session resumption information */
    TLS_SessionMgr *sessMgr = ctx->config.tlsConfig.sessMgr;
    /* Create a null session handle */
    HITLS_Session *sess = NULL;
    uint32_t ticketBufSize = clientHello->extension.content.ticketSize;
    bool supportTicket = IsTicketSupport(ctx);
    /* rfc5077 3.4 If a ticket is presented by the client, the server
    MUST NOT attempt to use the Session ID in the ClientHello for stateful
    session resumption. */
    if (ticketBufSize == 0u) {
        if (supportTicket && clientHello->extension.flag.haveTicket) {
            ctx->negotiatedInfo.isTicket = true;
        }
        sess = HITLS_SESS_Dup(SESSMGR_Find(sessMgr, clientHello->sessionId, clientHello->sessionIdSize));
        int32_t ret = ResumeCheckExtendedMasterScret(ctx, clientHello, &sess);
        if (ret != HITLS_SUCCESS) {
            HITLS_SESS_Free(sess);
            sess = NULL;
            return ret;
        }
        if (sess != NULL) {
            /* Update session handle information */
            HITLS_SESS_Free(ctx->session);
            ctx->session = sess; // has ensured that it will not fail
            sess = NULL;
            ctx->negotiatedInfo.isResume = true;
        }
        return HITLS_SUCCESS;
    }
    if (supportTicket) {
        return ServerCheckResumeTicket(ctx, clientHello);
    }
    return HITLS_SUCCESS;
}

static int32_t ServerCheckRenegoInfoDuringFirstHandshake(TLS_Ctx *ctx, const ClientHelloMsg *clientHello)
{
    /* If the peer does not support security renegotiation, the system returns */
    if (!clientHello->haveScsvCipher && !clientHello->extension.flag.haveSecRenego) {
        if (ctx->config.tlsConfig.noSecRenegotiationCb != NULL) {
            /* The user can determine whether to terminate the connection when the peer does not support security
             * renegotiation */
            int32_t ret = ctx->config.tlsConfig.noSecRenegotiationCb(ctx);
            if (ret != HITLS_SUCCESS) {
                BSL_ERR_PUSH_ERROR(ret);
                BSL_LOG_BINLOG_FIXLEN(BINLOG_ID15957, BSL_LOG_LEVEL_ERR, BSL_LOG_BINLOG_TYPE_RUN,
                    "noSecRenegotiationCb return fail when process client hello.", 0, 0, 0, 0);
                ctx->method.sendAlert(ctx, ALERT_LEVEL_FATAL, ALERT_HANDSHAKE_FAILURE);
                return ret;
            }
        }
        return HITLS_SUCCESS;
    }

    /* For the first handshake, if the security renegotiation information is not empty, return an error code. */
    if (clientHello->extension.content.secRenegoInfoSize != 0) {
        BSL_ERR_PUSH_ERROR(HITLS_MSG_HANDLE_RENEGOTIATION_FAIL);
        BSL_LOG_BINLOG_FIXLEN(BINLOG_ID15889, BSL_LOG_LEVEL_ERR, BSL_LOG_BINLOG_TYPE_RUN,
            "secRenegoInfoSize should be 0 in server initial handhsake.", 0, 0, 0, 0);
        ctx->method.sendAlert(ctx, ALERT_LEVEL_FATAL, ALERT_HANDSHAKE_FAILURE);
        return HITLS_MSG_HANDLE_RENEGOTIATION_FAIL;
    }

    /* Setting the Support for Security Renegotiation */
    ctx->negotiatedInfo.isSecureRenegotiation = true;
    return HITLS_SUCCESS;
}

static int32_t ServerCheckRenegoInfoDuringRenegotiation(TLS_Ctx *ctx, const ClientHelloMsg *clientHello)
{
    /* If the renegotiation status contains the SCSV cipher suite, return an error code. */
    if (clientHello->haveScsvCipher) {
        BSL_ERR_PUSH_ERROR(HITLS_MSG_HANDLE_RENEGOTIATION_FAIL);
        BSL_LOG_BINLOG_FIXLEN(BINLOG_ID15890, BSL_LOG_LEVEL_ERR, BSL_LOG_BINLOG_TYPE_RUN,
            "SCSV cipher should not be in server secure renegotiation.", 0, 0, 0, 0);
        ctx->method.sendAlert(ctx, ALERT_LEVEL_FATAL, ALERT_HANDSHAKE_FAILURE);
        return HITLS_MSG_HANDLE_RENEGOTIATION_FAIL;
    }

    /* Verify the security renegotiation information */
    if (clientHello->extension.content.secRenegoInfoSize != ctx->negotiatedInfo.clientVerifyDataSize) {
        BSL_ERR_PUSH_ERROR(HITLS_MSG_HANDLE_RENEGOTIATION_FAIL);
        BSL_LOG_BINLOG_FIXLEN(BINLOG_ID15891, BSL_LOG_LEVEL_ERR, BSL_LOG_BINLOG_TYPE_RUN,
            "secRenegoInfoSize verify failed during server renegotiation.", 0, 0, 0, 0);
        ctx->method.sendAlert(ctx, ALERT_LEVEL_FATAL, ALERT_HANDSHAKE_FAILURE);
        return HITLS_MSG_HANDLE_RENEGOTIATION_FAIL;
    }
    if (memcmp(clientHello->extension.content.secRenegoInfo, ctx->negotiatedInfo.clientVerifyData,
        ctx->negotiatedInfo.clientVerifyDataSize) != 0) {
        BSL_ERR_PUSH_ERROR(HITLS_MSG_HANDLE_RENEGOTIATION_FAIL);
        BSL_LOG_BINLOG_FIXLEN(BINLOG_ID15892, BSL_LOG_LEVEL_ERR, BSL_LOG_BINLOG_TYPE_RUN,
            "secRenegoInfo verify failed during server renegotiation.", 0, 0, 0, 0);
        ctx->method.sendAlert(ctx, ALERT_LEVEL_FATAL, ALERT_HANDSHAKE_FAILURE);
        return HITLS_MSG_HANDLE_RENEGOTIATION_FAIL;
    }

    return HITLS_SUCCESS;
}

static int32_t ServerCheckCompressionMethods(TLS_Ctx *ctx, const ClientHelloMsg *clientHello)
{
    /* If the compression method list contains no compression, return success */
    for (uint32_t i = 0; i < clientHello->compressionMethodsSize; i++) {
        // If the compression method contains no compression (0), a parsing success message is returne
        if (clientHello->compressionMethods[i] == 0u) {
            return HITLS_SUCCESS;
        }
    }

    BSL_ERR_PUSH_ERROR(HITLS_MSG_HANDLE_INVALID_COMPRESSION_METHOD);
    BSL_LOG_BINLOG_FIXLEN(BINLOG_ID15706, BSL_LOG_LEVEL_ERR, BSL_LOG_BINLOG_TYPE_RUN,
        "can not find a appropriate compression method in client hello.", 0, 0, 0, 0);
    ctx->method.sendAlert(ctx, ALERT_LEVEL_FATAL, ALERT_ILLEGAL_PARAMETER);
    return HITLS_MSG_HANDLE_INVALID_COMPRESSION_METHOD;
}

static int32_t Tls13ServerCheckCompressionMethods(TLS_Ctx *ctx, const ClientHelloMsg *clientHello)
{
    if (clientHello->compressionMethodsSize != 1u) {
        BSL_ERR_PUSH_ERROR(HITLS_MSG_HANDLE_INVALID_COMPRESSION_METHOD);
        BSL_LOG_BINLOG_FIXLEN(BINLOG_ID15842, BSL_LOG_LEVEL_ERR, BSL_LOG_BINLOG_TYPE_RUN,
            "the compression length of client hello is incorrect.", 0, 0, 0, 0);
        ctx->method.sendAlert(ctx, ALERT_LEVEL_FATAL, ALERT_ILLEGAL_PARAMETER);
        return HITLS_MSG_HANDLE_INVALID_COMPRESSION_METHOD;
    }

    /* If the compression method list contains no compression, return success */
    // If the compression method contains no compression (0), a parsing success message is returne
    if (clientHello->compressionMethods[0] == 0u) {
        return HITLS_SUCCESS;
    }
    BSL_ERR_PUSH_ERROR(HITLS_MSG_HANDLE_INVALID_COMPRESSION_METHOD);
    BSL_LOG_BINLOG_FIXLEN(BINLOG_ID15843, BSL_LOG_LEVEL_ERR, BSL_LOG_BINLOG_TYPE_RUN,
        "can not find a appropriate compression method in client hello.", 0, 0, 0, 0);
    ctx->method.sendAlert(ctx, ALERT_LEVEL_FATAL, ALERT_ILLEGAL_PARAMETER);
    return HITLS_MSG_HANDLE_INVALID_COMPRESSION_METHOD;
}

static int32_t ServerCheckAndProcessRenegoInfo(TLS_Ctx *ctx, const ClientHelloMsg *clientHello)
{
    /* Not in the renegotiation state */
    if (!ctx->negotiatedInfo.isRenegotiation) {
        return ServerCheckRenegoInfoDuringFirstHandshake(ctx, clientHello);
    }

    /* in the renegotiation state */
    return ServerCheckRenegoInfoDuringRenegotiation(ctx, clientHello);
}

static int32_t ServerCheckEncryptThenMac(TLS_Ctx *ctx, const ClientHelloMsg *clientHello)
{
    bool haveEncryptThenMac = clientHello->extension.flag.haveEncryptThenMac;
    /* Renegotiation cannot be downgraded from EncryptThenMac to MacThenEncrypt */
    if (ctx->negotiatedInfo.isRenegotiation && ctx->negotiatedInfo.isEncryptThenMac && !haveEncryptThenMac) {
        BSL_ERR_PUSH_ERROR(HITLS_MSG_HANDLE_ENCRYPT_THEN_MAC_ERR);
        BSL_LOG_BINLOG_FIXLEN(BINLOG_ID15919, BSL_LOG_LEVEL_ERR, BSL_LOG_BINLOG_TYPE_RUN,
            "regotiation should not change encrypt then mac to mac then encrypt.", 0, 0, 0, 0);
        ctx->method.sendAlert(ctx, ALERT_LEVEL_FATAL, ALERT_HANDSHAKE_FAILURE);
        return HITLS_MSG_HANDLE_ENCRYPT_THEN_MAC_ERR;
    }

    /* If EncryptThenMac is not configured, return HITLS_SUCCESS. */
    if (!ctx->config.tlsConfig.isEncryptThenMac) {
        return HITLS_SUCCESS;
    }

    /* TLS 1.3 does not need to negotiate this expansion. */
    if (ctx->negotiatedInfo.version == HITLS_VERSION_TLS13) {
        return HITLS_SUCCESS;
    }

    /* Only the CBC cipher suite has the EncryptThenMac setting. */
    if (haveEncryptThenMac && ctx->negotiatedInfo.cipherSuiteInfo.cipherType == HITLS_CBC_CIPHER) {
        ctx->negotiatedInfo.isEncryptThenMac = true;
    } else {
        ctx->negotiatedInfo.isEncryptThenMac = false;
    }

    return HITLS_SUCCESS;
}

static int32_t ServerSelectCipherSuiteInfo(TLS_Ctx *ctx, const ClientHelloMsg *clientHello)
{
    int32_t ret = ServerSelectCipherSuite(ctx, clientHello);
    if (ret != HITLS_SUCCESS) {
        BSL_LOG_BINLOG_FIXLEN(BINLOG_ID15239, BSL_LOG_LEVEL_ERR, BSL_LOG_BINLOG_TYPE_RUN,
            "server select cipher suite fail.", 0, 0, 0, 0);
        return ret;
    }

    /* Select the encryption mode (EncryptThenMac/MacThenEncrypt) */
    ret = ServerCheckEncryptThenMac(ctx, clientHello);
    if (ret != HITLS_SUCCESS) {
        return ret;
    }

    return HITLS_SUCCESS;
}

static int32_t ServerProcessClientHelloExt(TLS_Ctx *ctx, const ClientHelloMsg *clientHello)
{
    int32_t ret;
    /* Sets the extended master key flag */
    if (ctx->negotiatedInfo.version > HITLS_VERSION_SSL30 && ctx->config.tlsConfig.isSupportExtendMasterSecret &&
        !clientHello->extension.flag.haveExtendedMasterSecret) {
        BSL_LOG_BINLOG_FIXLEN(BINLOG_ID15566, BSL_LOG_LEVEL_ERR, BSL_LOG_BINLOG_TYPE_RUN,
            "The peer does not support the extended master key.", 0, 0, 0, 0);
        ctx->method.sendAlert(ctx, ALERT_LEVEL_FATAL, ALERT_HANDSHAKE_FAILURE);
        return HITLS_MSG_HANDLE_INVALID_EXTENDED_MASTER_SECRET;
    }
    ctx->negotiatedInfo.isExtendedMasterSecret = clientHello->extension.flag.haveExtendedMasterSecret;

    /* The message contains a server_name extension whose length is greater than 0 */
    ret = ServerDealServerName(ctx, clientHello);
    if (ret != HITLS_SUCCESS) {
        return ret;
    }

    if (clientHello->extension.flag.haveAlpn) {
        ret = ServerSelectAlpnProtocol(ctx, clientHello);
        if (ret != HITLS_SUCCESS) {
            /* Logs have been recorded internally */
            return ret;
        }
    }

    return HITLS_SUCCESS;
}

// Check client Hello messages
static int32_t ServerCheckAndProcessClientHello(TLS_Ctx *ctx, const ClientHelloMsg *clientHello)
{
    /* Obtain the server information */
    HS_Ctx *hsCtx = (HS_Ctx *)ctx->hsCtx;

    /* Negotiated version */
    int32_t ret = ServerSelectNegoVersion(ctx, clientHello);
    if (ret != HITLS_SUCCESS) {
        BSL_LOG_BINLOG_FIXLEN(BINLOG_ID15238, BSL_LOG_LEVEL_ERR, BSL_LOG_BINLOG_TYPE_RUN,
            "server select negotiated version fail.", 0, 0, 0, 0);
        return ret;
    }

    ret = ServerCheckCompressionMethods(ctx, clientHello);
    if (ret != HITLS_SUCCESS) {
        return ret;
    }

    /* Copy random numbers */
    if (memcpy_s(hsCtx->clientRandom, HS_RANDOM_SIZE, clientHello->randomValue, HS_RANDOM_SIZE) != EOK) {
        return HITLS_MEMCPY_FAIL;
    }

    ret = ServerCheckAndProcessRenegoInfo(ctx, clientHello);
    if (ret != HITLS_SUCCESS) {
        return ret;
    }

    ret = ServerCheckResume(ctx, clientHello);
    if (ret != HITLS_SUCCESS) {
        return ret;
    }

    if (ctx->negotiatedInfo.isResume) {
        return ServerCheckResumeParam(ctx, clientHello);
    }

    ret = ServerSelectCipherSuiteInfo(ctx, clientHello);
    if (ret != HITLS_SUCCESS) {
        return ret;
    }
    /* TLCP does not pay attention to the extension */

#ifndef HITLS_NO_TLCP11
    if (ctx->negotiatedInfo.version == HITLS_VERSION_TLCP11) {
        return HITLS_SUCCESS;
    }
#endif
    return ServerProcessClientHelloExt(ctx, clientHello);
}

static int32_t ClientHelloCbCheck(TLS_Ctx *ctx)
{
    int32_t ret;
    int32_t alert = ALERT_INTERNAL_ERROR;
    const TLS_Config *tlsConfig = ctx->globalConfig;
    if (tlsConfig != NULL && tlsConfig->clientHelloCb != NULL) {
        ret = tlsConfig->clientHelloCb(ctx, &alert, tlsConfig->clientHelloCbArg);
        if (ret != HITLS_CONTINUE_HANDHSAKE) {
            BSL_ERR_PUSH_ERROR(HITLS_CLIENT_HELLO_CHECK_ERROR);
            BSL_LOG_BINLOG_FIXLEN(BINLOG_ID15240, BSL_LOG_LEVEL_ERR, BSL_LOG_BINLOG_TYPE_RUN,
                "The result of ClientHello callback is %d, and the reason is %d.", ret, alert, 0, 0);
            if (alert >= ALERT_CLOSE_NOTIFY && alert <= ALERT_UNKNOWN) {
                ctx->method.sendAlert(ctx, ALERT_LEVEL_FATAL, alert);
            }
            return HITLS_CLIENT_HELLO_CHECK_ERROR;
        }
    }
    return HITLS_SUCCESS;
}

int32_t Tls12ServerRecvClientHelloProcess(TLS_Ctx *ctx, const HS_Msg *msg)
{
    const ClientHelloMsg *clientHello = &msg->body.clientHello;

    CheckRenegotiate(ctx);

    /* Perform the ClientHello callback. The pause handshake status is not considered */
    int32_t ret = ClientHelloCbCheck(ctx);
    if (ret != HITLS_SUCCESS) {
        return ret;
    }

    /* Process the client Hello message */
    ret = ServerCheckAndProcessClientHello(ctx, clientHello);
    if (ret != HITLS_SUCCESS) {
        return ret;
    }

    return HS_ChangeState(ctx, TRY_SEND_SERVER_HELLO);
}

// The server processes the DTLS client hello message.
#ifndef HITLS_NO_DTLS12
int32_t DtlsServerRecvClientHelloProcess(TLS_Ctx *ctx, const HS_Msg *msg)
{
    int32_t ret;
    const ClientHelloMsg *clientHello = &msg->body.clientHello;

    CheckRenegotiate(ctx);

    /* Process the client Hello message */
    ret = ServerCheckAndProcessClientHello(ctx, clientHello);
    if (ret != HITLS_SUCCESS) {
        BSL_LOG_BINLOG_FIXLEN(BINLOG_ID15244, BSL_LOG_LEVEL_ERR, BSL_LOG_BINLOG_TYPE_RUN,
            "server process clientHello fail.", 0, 0, 0, 0);
        return ret;
    }

    return HS_ChangeState(ctx, TRY_SEND_SERVER_HELLO);
}
#endif

static uint32_t GetClientKeMode(const ExtensionContent *extension)
{
    uint32_t clientKeMode = 0;
    for (uint32_t i = 0; i < extension->keModesSize; i++) {
        /* Ignore the received keMode of other types */
        if (extension->keModes[i] == PSK_KE) {
            clientKeMode |= TLS13_KE_MODE_PSK_ONLY;
        } else if (extension->keModes[i] == PSK_DHE_KE) {
            clientKeMode |= TLS13_KE_MODE_PSK_WITH_DHE;
        }
    }
    return clientKeMode;
}

static bool CheckClientHelloKeyShareValid(const ClientHelloMsg *clientHello, uint16_t keyShareGroup)
{
    for (uint32_t i = 0; i < clientHello->extension.content.supportedGroupsSize; i++) {
        if (keyShareGroup == clientHello->extension.content.supportedGroups[i]) {
            return true;
        }
    }
    return false;
}

static int32_t ServerCheckKeyShare(TLS_Ctx *ctx, const ClientHelloMsg *clientHello)
{
    /* Prerequisite. If the PSK is not negotiated or the PSK requires dhe, a handshake failure message needs to be
     * reported if the keyshare does not exist */
    if (clientHello->extension.flag.haveKeyShare == false || clientHello->extension.content.supportedGroupsSize == 0u ||
        ProcessEcdheCipherSuite(ctx, clientHello) != HITLS_SUCCESS) {
        BSL_ERR_PUSH_ERROR(HITLS_MSG_HANDLE_HANDSHAKE_FAILURE);
        BSL_LOG_BINLOG_FIXLEN(BINLOG_ID15881, BSL_LOG_LEVEL_ERR, BSL_LOG_BINLOG_TYPE_RUN,
            "unable to negotiate a supported set of parameters.", 0, 0, 0, 0);
        ctx->method.sendAlert(ctx, ALERT_LEVEL_FATAL, ALERT_HANDSHAKE_FAILURE);
        return HITLS_MSG_HANDLE_HANDSHAKE_FAILURE;
    }

    /* ProcessEcdheCipherSuite returns a success response. There must be a public group */
    KeyShareParam *keyShare = &ctx->hsCtx->kxCtx->keyExchParam.share;
    uint16_t selectGroup = keyShare->group;
    KeyShare *cache = clientHello->extension.content.keyShare;
    /*  rfc8446 4.2.8 Otherwise, when sending the new ClientHello, the client MUST
        replace the original "key_share" extension with one containing only a
        new KeyShareEntry for the group indicated in the selected_group field
        of the triggering HelloRetryRequest. */
    if (ctx->hsCtx->haveHrr) {
        if (cache == NULL || cache->head.next != cache->head.prev || // parse must contain elements.
            LIST_ENTRY(cache->head.next, KeyShare, head)->group != selectGroup) {
            BSL_ERR_PUSH_ERROR(HITLS_MSG_HANDLE_ILLEGAL_SELECTED_GROUP);
            BSL_LOG_BINLOG_FIXLEN(BINLOG_ID15844, BSL_LOG_LEVEL_ERR, BSL_LOG_BINLOG_TYPE_RUN,
                "hrr client hello key Share error.", 0, 0, 0, 0);
            ctx->method.sendAlert(ctx, ALERT_LEVEL_FATAL, ALERT_ILLEGAL_PARAMETER);
            return HITLS_MSG_HANDLE_ILLEGAL_SELECTED_GROUP;
        }
    }
    return HITLS_SUCCESS;
}

static int32_t Tls13ServerProcessKeyShare(TLS_Ctx *ctx, const ClientHelloMsg *clientHello, bool *isNeedSendHrr)
{
    /* Prerequisite. If the PSK is not negotiated or the PSK requires dhe, a handshake failure message needs to be
     * reported if the keyshare does not exist */
    int32_t ret = ServerCheckKeyShare(ctx, clientHello);
    if (ret != HITLS_SUCCESS) {
        return ret;
    }
    /* ServerCheckKeyShare returns a success response. There must be a public group */
    KeyShareParam *keyShare = &ctx->hsCtx->kxCtx->keyExchParam.share;
    KeyExchCtx *kxCtx = ctx->hsCtx->kxCtx;
    uint16_t selectGroup = keyShare->group;
    ListHead *node = NULL;
    ListHead *tmpNode = NULL;
    KeyShare *cur = NULL;
    KeyShare *cache = clientHello->extension.content.keyShare;
    if (cache == NULL) {
        /* According to section 4.2.8 in RFC8446, if the client requests HelloRetryRequest, keyShare can be empty */
        *isNeedSendHrr = true;
        return HITLS_SUCCESS;
    }

    LIST_FOR_EACH_ITEM_SAFE(node, tmpNode, &(cache->head)) {
        cur = LIST_ENTRY(node, KeyShare, head);
        /*  rfc8446 4.2.8 Clients MUST NOT offer any KeyShareEntry values
            for groups not listed in the client's "supported_groups" extension.
            Servers MAY check for violations of these rules and abort the
            handshake with an "illegal_parameter" alert if one is violated. */
        if (!CheckClientHelloKeyShareValid(clientHello, cur->group)) {
            BSL_ERR_PUSH_ERROR(HITLS_MSG_HANDLE_ILLEGAL_SELECTED_GROUP);
            BSL_LOG_BINLOG_FIXLEN(BINLOG_ID15882, BSL_LOG_LEVEL_ERR, BSL_LOG_BINLOG_TYPE_RUN,
                "The group in the keyshare does not exist in the support group extension.", 0, 0, 0, 0);
            ctx->method.sendAlert(ctx, ALERT_LEVEL_FATAL, ALERT_ILLEGAL_PARAMETER);
            return HITLS_MSG_HANDLE_ILLEGAL_SELECTED_GROUP;
        }
        if (cur->group != selectGroup) {
            continue;
        }

        *isNeedSendHrr = false;
        /* Obtain the peer public key */
        kxCtx->pubKeyLen = cur->keyExchangeSize;
        if (HS_GetNamedCurvePubkeyLen(keyShare->group) != kxCtx->pubKeyLen) {
            BSL_ERR_PUSH_ERROR(HITLS_MSG_HANDLE_ILLEGAL_SELECTED_GROUP);
            BSL_LOG_BINLOG_FIXLEN(BINLOG_ID15345, BSL_LOG_LEVEL_ERR, BSL_LOG_BINLOG_TYPE_RUN,
                "invalid keyShare length.", 0, 0, 0, 0);
            ctx->method.sendAlert(ctx, ALERT_LEVEL_FATAL, ALERT_ILLEGAL_PARAMETER);
            return HITLS_MSG_HANDLE_ILLEGAL_SELECTED_GROUP;
        }
        BSL_SAL_FREE(kxCtx->peerPubkey);
        kxCtx->peerPubkey = BSL_SAL_Dump(cur->keyExchange, cur->keyExchangeSize);
        if (kxCtx->peerPubkey == NULL) {
            BSL_ERR_PUSH_ERROR(HITLS_MEMALLOC_FAIL);
            BSL_LOG_BINLOG_FIXLEN(BINLOG_ID15245, BSL_LOG_LEVEL_ERR, BSL_LOG_BINLOG_TYPE_RUN,
                "malloc peerPubkey fail when process client key share.", 0, 0, 0, 0);
            return HITLS_MEMALLOC_FAIL;
        }

        ctx->negotiatedInfo.negotiatedGroup = selectGroup;
        return HITLS_SUCCESS;
    }

    /* If the server selects a group that does not exist in the keyshare, the server needs to send a hello retry request
     */
    *isNeedSendHrr = true;
    return HITLS_SUCCESS;
}

static int32_t GetPskFromSession(TLS_Ctx *ctx, HITLS_Session *pskSession, uint8_t *psk, uint32_t pskLen,
    uint32_t *usedLen)
{
    /* The session is available and the PSK is obtained */
    uint32_t tmpLen = pskLen;
    int32_t ret = HITLS_SESS_GetMasterKey(pskSession, psk, &tmpLen);
    if (ret != HITLS_SUCCESS) {
        /* An internal error occurs and cannot be continued. Return an error code and an alert message is sent
         */
        ctx->method.sendAlert(ctx, ALERT_LEVEL_FATAL, ALERT_INTERNAL_ERROR);
        return ret;
    }

    *usedLen = tmpLen;
    return HITLS_SUCCESS;
}

int32_t PskFindSession(TLS_Ctx *ctx, const uint8_t *id, uint32_t idLen, HITLS_Session **pskSession)
{
    if (ctx->config.tlsConfig.pskFindSessionCb == NULL) {
        /* No callback is set */
        return HITLS_SUCCESS;
    }

    int32_t ret = ctx->config.tlsConfig.pskFindSessionCb(ctx, id, idLen, pskSession);
    if (ret != HITLS_PSK_FIND_SESSION_CB_SUCCESS) {
        /* Internal error, cannot continue */
        ctx->method.sendAlert(ctx, ALERT_LEVEL_FATAL, ALERT_INTERNAL_ERROR);
        return HITLS_MSG_HANDLE_PSK_FIND_SESSION_FAIL;
    }

    return HITLS_SUCCESS;
}

int32_t GetPskByIdentity(TLS_Ctx *ctx, const uint8_t *id, uint32_t idLen, uint8_t *psk, uint32_t *pskLen)
{
    if (ctx->config.tlsConfig.pskServerCb == NULL) {
        *pskLen = 0;
        return HITLS_SUCCESS;
    }

    uint8_t *strId = BSL_SAL_Calloc(1u, idLen + 1);
    if (strId == NULL) {
        ctx->method.sendAlert(ctx, ALERT_LEVEL_FATAL, ALERT_INTERNAL_ERROR);
        return HITLS_MEMALLOC_FAIL;
    }
    (void)memcpy_s(strId, idLen + 1, id, idLen);
    strId[idLen] = '\0';

    uint32_t usedLen = ctx->config.tlsConfig.pskServerCb(ctx, strId, psk, *pskLen);
    BSL_SAL_FREE(strId);
    if (usedLen > HS_PSK_MAX_LEN) {
        ctx->method.sendAlert(ctx, ALERT_LEVEL_FATAL, ALERT_INTERNAL_ERROR);
        return HITLS_MSG_HANDLE_ILLEGAL_PSK_LEN;
    }

    *pskLen = usedLen;
    return HITLS_SUCCESS;
}

static int32_t Tls13ServerSetPskInfo(TLS_Ctx *ctx, uint8_t *psk, uint32_t pskLen, uint16_t index)
{
    PskInfo13 *pskInfo13 = &ctx->hsCtx->kxCtx->pskInfo13;
    BSL_SAL_FREE(pskInfo13->psk);
    pskInfo13->psk = BSL_SAL_Dump(psk, pskLen);
    if (pskInfo13->psk == NULL) {
        ctx->method.sendAlert(ctx, ALERT_LEVEL_FATAL, ALERT_INTERNAL_ERROR);
        return HITLS_MEMALLOC_FAIL;
    }
    pskInfo13->pskLen = pskLen;
    pskInfo13->selectIndex = index;
    return HITLS_SUCCESS;
}

static bool IsPSKValid(const TLS_Ctx *ctx, HITLS_Session *pskSession)
{
    uint16_t version, cipherSuite;
    HITLS_SESS_GetProtocolVersion(pskSession, &version);
    if (version != HITLS_VERSION_TLS13) {
        return false;
    }

    HITLS_SESS_GetCipherSuite(pskSession, &cipherSuite);
    CipherSuiteInfo cipherInfo = {0};
    int32_t ret = CFG_GetCipherSuiteInfo(cipherSuite, &cipherInfo);
    if (ret != HITLS_SUCCESS) {
        return false;
    }

    if (cipherInfo.hashAlg != ctx->negotiatedInfo.cipherSuiteInfo.hashAlg) {
        return false;
    }

    return true;
}

static int32_t TLS13ServerProcessTicket(TLS_Ctx *ctx, PreSharedKey *cur,
    uint8_t *psk, uint32_t *pskLen)
{
    const uint8_t *ticket = cur->identity;
    uint32_t ticketLen = cur->identitySize;
    bool isTicketExcept = 0;
    HITLS_Session *pskSession = NULL;

    int32_t ret = SESSMGR_DecryptSessionTicket(ctx->config.tlsConfig.sessMgr, &pskSession,
        ticket, ticketLen, &isTicketExcept);
    if (ret != HITLS_SUCCESS) {
        BSL_LOG_BINLOG_FIXLEN(BINLOG_ID16048, BSL_LOG_LEVEL_ERR, BSL_LOG_BINLOG_TYPE_RUN,
            "Decrypt Ticket fail when processing client hello.", 0, 0, 0, 0);
        ctx->method.sendAlert(ctx, ALERT_LEVEL_FATAL, ALERT_INTERNAL_ERROR);
        return ret;
    }

    /* Do not resume the session. TLS1.3 does not need to check isTicketExceptt */
    if (pskSession == NULL) {
        *pskLen = 0;
        return HITLS_SUCCESS;
    }

    /* Check whether the session is valid */
    if (!IsPSKValid(ctx, pskSession) ||
        !SESS_CheckObfuscatedTicketAge(pskSession, (uint64_t)BSL_SAL_CurrentSysTimeGet(), cur->obfuscatedTicketAge)) {
        /* Do not resume the session */
        *pskLen = 0;
        HITLS_SESS_Free(pskSession);
        return HITLS_SUCCESS;
    }

    if (ServerCmpSessionIdCtx(ctx, pskSession) != true) {
        BSL_LOG_BINLOG_FIXLEN(BINLOG_ID15462, BSL_LOG_LEVEL_ERR, BSL_LOG_BINLOG_TYPE_RUN,
            "TLS1.3 Resuming Session: session id ctx is inconsistent.", 0, 0, 0, 0);
        ctx->method.sendAlert(ctx, ALERT_LEVEL_FATAL, ALERT_ILLEGAL_PARAMETER);
        HITLS_SESS_Free(pskSession);
        return HITLS_MSG_HANDLE_SESSION_ID_CTX_ILLEGAL;
    }

    ret = GetPskFromSession(ctx, pskSession, psk, *pskLen, pskLen);
    if (ret != HITLS_SUCCESS) {
        HITLS_SESS_Free(pskSession);
        return ret;
    }

    if (*pskLen == 0) {
        HITLS_SESS_Free(pskSession);
        return HITLS_SUCCESS;
    }

    HITLS_SESS_Free(ctx->session);
    ctx->session = pskSession;
    ctx->negotiatedInfo.isResume = true;
    return HITLS_SUCCESS;
}

static int32_t ServerFindPsk(TLS_Ctx *ctx, PreSharedKey *cur,
    uint8_t *psk, uint32_t *pskLen)
{
    const uint8_t *identity = cur->identity;
    uint32_t identitySize = cur->identitySize;
    int32_t ret;
    HITLS_Session *pskSession = NULL;
    uint32_t pskSize = *pskLen;
    ctx->negotiatedInfo.isResume = false;

    ret = PskFindSession(ctx, identity, identitySize, &pskSession);
    if (ret != HITLS_SUCCESS) {
        return ret;
    }

    /* TLS 1.3 processing */
    if (pskSession != NULL) {
        /* In TLS1.3, pskSession is transferred by the user. Check the corresponding version and cipher suite */
        if (IsPSKValid(ctx, pskSession) == false) {
            HITLS_SESS_Free(pskSession); /* Unsuitable sessions are released. */
            *pskLen = 0;
            return HITLS_SUCCESS;
        }

        ret = GetPskFromSession(ctx, pskSession, psk, pskSize, pskLen);
        HITLS_SESS_Free(pskSession); /* After the session is used, the session is released. */
        return ret;
    }

    /*
     * By default, the hash algorithm used by the pskSession cipher suite is SHA_256.
     * In this case, you only need to check whether the hash algorithm of the negotiated cipher suite is SHA_256.
     */
    if (ctx->negotiatedInfo.cipherSuiteInfo.hashAlg == HITLS_HASH_SHA_256) {
        ret = GetPskByIdentity(ctx, identity, identitySize, psk, &pskSize);
        if (ret != HITLS_SUCCESS) {
            /* An internal error occurs and the process cannot be continued. return an error code */
            return ret;
        }
        if (pskSize > 0u) {
            *pskLen = pskSize;
            return HITLS_SUCCESS;
        }
    }

    /* Try to decrypt the ticket for session resumption */
    ret = TLS13ServerProcessTicket(ctx, cur, psk, pskLen);
    return ret;
}

int32_t CompareBinder(TLS_Ctx *ctx, const PreSharedKey *pskNode, uint8_t *psk, uint32_t pskLen,
    uint32_t truncateHelloLen)
{
    int32_t ret;
    uint8_t *recvBinder = pskNode->binder;
    uint32_t recvBinderLen = pskNode->binderSize;
    HITLS_HashAlgo hashAlg = ctx->negotiatedInfo.cipherSuiteInfo.hashAlg;
    bool isExternalPsk = !(ctx->negotiatedInfo.isResume);
    uint8_t computedBinder[HS_MAX_BINDER_SIZE] = {0};

    uint32_t binderLen = HS_GetBinderLen(NULL, &hashAlg);
    if (binderLen == 0 || binderLen != recvBinderLen || binderLen > HS_MAX_BINDER_SIZE) {
        return HITLS_INTERNAL_EXCEPTION;
    }

    ret = VERIFY_CalcPskBinder(ctx, hashAlg, isExternalPsk, psk, pskLen, ctx->hsCtx->msgBuf, truncateHelloLen,
        computedBinder, binderLen);
    if (ret != HITLS_SUCCESS) {
        return ret;
    }
    ret = memcmp(computedBinder, recvBinder, binderLen);
    if (ret != 0) {
        return HITLS_INTERNAL_EXCEPTION;
    }
    return ret;
}

/* Prior to accepting PSK key establishment, the server MUST validate the corresponding binder value (see
 * Section 4.2.11.2 below). If this value is not present or does not validate, the server MUST abort the handshake.
 * Servers SHOULD NOT attempt to validate multiple binders; rather, they SHOULD select a single PSK and validate solely
 * the binder that corresponds to that PSK.
 */
static int32_t ServerSelectPskAndCheckBinder(TLS_Ctx *ctx, const ClientHelloMsg *clientHello)
{
    int32_t ret = HITLS_SUCCESS;
    uint16_t index = 0;

    uint8_t psk[HS_PSK_MAX_LEN] = {0};

    ListHead *node = NULL;
    ListHead *tmpNode = NULL;
    PreSharedKey *cur = NULL;
    PreSharedKey *offeredPsks = clientHello->extension.content.preSharedKey;

    LIST_FOR_EACH_ITEM_SAFE(node, tmpNode, &(offeredPsks->pskNode))
    {
        uint32_t pskLen = HS_PSK_MAX_LEN;
        cur = LIST_ENTRY(node, PreSharedKey, pskNode);

        ret = ServerFindPsk(ctx, cur, psk, &pskLen);
        if (ret != HITLS_SUCCESS) {
            return ret;
        }

        if (pskLen == 0) {
            index++;
            /* The corresponding psk cannot be found. Search for the next psk */
            continue;
        }
        /* An available psk is found */
        ret = Tls13ServerSetPskInfo(ctx, psk, pskLen, index);
        if (ret != HITLS_SUCCESS) {
            (void)memset_s(psk, HS_PSK_MAX_LEN, 0, HS_PSK_MAX_LEN); /* Clear sensitive memory */
            return ret;
        }
        ret = CompareBinder(ctx, cur, psk, pskLen, clientHello->truncateHelloLen);
        (void)memset_s(psk, HS_PSK_MAX_LEN, 0, HS_PSK_MAX_LEN); /* Clear sensitive memory */
        if (ret != HITLS_SUCCESS) {
            /* RFC8446 Section 6.2:decrypt_error:  A handshake (not record layer) cryptographic
                operation failed, including being unable to correctly verify a
                signature or validate a Finished message or a PSK binder. */
            ctx->method.sendAlert(ctx, ALERT_LEVEL_FATAL, ALERT_DECRYPT_ERROR);
            return ret;
        }
        cur->isValid = true;
        break;
    }
    return ret;
}

static int32_t Tls13ServerSetSessionId(TLS_Ctx *ctx, const uint8_t *sessionId, uint32_t sessionIdSize)
{

    if (sessionIdSize == 0) {
        ctx->hsCtx->sessionIdSize = sessionIdSize;
        return HITLS_SUCCESS;
    }

    uint8_t *tmpSession = BSL_SAL_Dump(sessionId, sessionIdSize);
    if (tmpSession == NULL) {
        BSL_ERR_PUSH_ERROR(HITLS_MEMALLOC_FAIL);
        BSL_LOG_BINLOG_FIXLEN(BINLOG_ID15248, BSL_LOG_LEVEL_ERR, BSL_LOG_BINLOG_TYPE_RUN,
            "malloc sessionId fail when process client hello.", 0, 0, 0, 0);
        ctx->method.sendAlert(ctx, ALERT_LEVEL_FATAL, ALERT_INTERNAL_ERROR);
        return HITLS_MEMALLOC_FAIL;
    }
    BSL_SAL_FREE(ctx->hsCtx->sessionId); // Clearing old memory
    ctx->hsCtx->sessionId = tmpSession;
    ctx->hsCtx->sessionIdSize = sessionIdSize;
    return HITLS_SUCCESS;
}

static int32_t Tls13ServerCheckClientHelloExtension(TLS_Ctx *ctx, const ClientHelloMsg *clientHello)
{
    do {
        /* If not containing a "pre_shared_key" extension, it MUST contain
        both a "signature_algorithms" extension and a "supported_groups"
        extension. */
        if ((!clientHello->extension.flag.havePreShareKey) && (!clientHello->extension.flag.haveSignatureAlgorithms ||
            !clientHello->extension.flag.haveSupportedGroups)) {
            break;
        }

        /* If containing a "supported_groups" extension, it MUST also contain
        a "key_share" extension, and vice versa. */
        if ((clientHello->extension.flag.haveSupportedGroups && !clientHello->extension.flag.haveKeyShare) ||
            (!clientHello->extension.flag.haveSupportedGroups && clientHello->extension.flag.haveKeyShare)) {
            break;
        }

        /* A client MUST provide a "psk_key_exchange_modes" extension if it
            offers a "pre_shared_key" extension. */
        if (clientHello->extension.flag.havePreShareKey && !clientHello->extension.flag.havePskExMode) {
            break;
        }

        // with psk && psk mode is dhe && without keyshare
        uint32_t clientKeMode = GetClientKeMode(&clientHello->extension.content);
        if (clientHello->extension.flag.havePreShareKey &&
            (clientKeMode & TLS13_KE_MODE_PSK_WITH_DHE) == TLS13_KE_MODE_PSK_WITH_DHE &&
            !clientHello->extension.flag.haveKeyShare) {
            break;
        }
        return HITLS_SUCCESS;
    } while (false);
    BSL_ERR_PUSH_ERROR(HITLS_MSG_HANDLE_MISSING_EXTENSION);
    BSL_LOG_BINLOG_FIXLEN(BINLOG_ID15883, BSL_LOG_LEVEL_ERR, BSL_LOG_BINLOG_TYPE_RUN,
        "invalid client hello: minssing extension.", 0, 0, 0, 0);
    ctx->method.sendAlert(ctx, ALERT_LEVEL_FATAL, ALERT_MISSING_EXTENSION);
    return HITLS_MSG_HANDLE_MISSING_EXTENSION;
}

static int32_t Tls13ServerCheckSecondClientHello(TLS_Ctx *ctx, ClientHelloMsg *clientHello)
{
    if (ctx->hsCtx->haveHrr) {
        if (ctx->hsCtx->firstClientHello->cipherSuitesSize != clientHello->cipherSuitesSize ||
            memcmp(ctx->hsCtx->firstClientHello->cipherSuites, clientHello->cipherSuites,
            clientHello->cipherSuitesSize * sizeof(uint16_t)) != 0) {
            ctx->method.sendAlert(ctx, ALERT_LEVEL_FATAL, ALERT_ILLEGAL_PARAMETER);
            return HITLS_MSG_HANDLE_ILLEGAL_CIPHER_SUITE;
        }
        return HITLS_SUCCESS;
    }
    if (ctx->hsCtx->firstClientHello != NULL) {
        ctx->method.sendAlert(ctx, ALERT_LEVEL_FATAL, ALERT_INTERNAL_ERROR);
        return HITLS_INTERNAL_EXCEPTION;
    }
    ctx->hsCtx->firstClientHello = (ClientHelloMsg *)BSL_SAL_Dump(clientHello, sizeof(ClientHelloMsg));
    if (ctx->hsCtx->firstClientHello == NULL) {
        BSL_ERR_PUSH_ERROR(HITLS_MEMALLOC_FAIL);
        BSL_LOG_BINLOG_FIXLEN(BINLOG_ID15538, BSL_LOG_LEVEL_ERR, BSL_LOG_BINLOG_TYPE_RUN, "clientHello malloc fail.", 0,
            0, 0, 0);
        return HITLS_MEMALLOC_FAIL;
    }
    clientHello->refCnt = 1;
    return HITLS_SUCCESS;
}
static int32_t Tls13ServerBasicCheckClientHello(TLS_Ctx *ctx, ClientHelloMsg *clientHello)
{
    int32_t ret = Tls13ServerCheckSecondClientHello(ctx, clientHello);
    if (ret != HITLS_SUCCESS) {
        return ret;
    }

    /* Set the negotiated version number */
    ctx->negotiatedInfo.version = HITLS_VERSION_TLS13;

    ret = Tls13ServerCheckCompressionMethods(ctx, clientHello);
    if (ret != HITLS_SUCCESS) {
        return ret;
    }

    /* Copy random numbers */
    ret = memcpy_s(ctx->hsCtx->clientRandom, HS_RANDOM_SIZE, clientHello->randomValue, HS_RANDOM_SIZE);
    if (ret != EOK) {
        return ret;
    }

    /* Copy the session ID */
    ret = Tls13ServerSetSessionId(ctx, clientHello->sessionId, clientHello->sessionIdSize);
    if (ret != HITLS_SUCCESS) {
        return ret;
    }

    ret = ServerSelectCipherSuite(ctx, clientHello);
    return ret;
}

static int32_t Tls13ServerCheckClientHello(TLS_Ctx *ctx, ClientHelloMsg *clientHello, bool *isNeedSendHrr)
{
    uint32_t selectKeMode = 0;

    int32_t ret = Tls13ServerBasicCheckClientHello(ctx, clientHello);
    if (ret != HITLS_SUCCESS) {
        return ret;
    }

    /* rfc8446 9.2.  Mandatory-to-Implement Extensions */
    ret = Tls13ServerCheckClientHelloExtension(ctx, clientHello);
    if (ret != HITLS_SUCCESS) {
        return ret;
    }

    uint32_t clientKeMode = GetClientKeMode(&clientHello->extension.content);
    selectKeMode = clientKeMode & ctx->config.tlsConfig.keyExchMode;
    if (clientHello->extension.flag.havePreShareKey && selectKeMode != 0) {
        /* calculate the binder value and compare it with the received binder value. */
        ret = ServerSelectPskAndCheckBinder(ctx, clientHello);
        if (ret != HITLS_SUCCESS) {
            BSL_ERR_PUSH_ERROR(HITLS_MSG_HANDLE_PSK_INVALID);
            BSL_LOG_BINLOG_FIXLEN(BINLOG_ID15940, BSL_LOG_LEVEL_ERR, BSL_LOG_BINLOG_TYPE_RUN,
                "ServerSelectPskAndCheckBinder failed.", 0, 0, 0, 0);
            return HITLS_MSG_HANDLE_PSK_INVALID;
        }
    }

    if (ctx->hsCtx->kxCtx->pskInfo13.psk == NULL ||
        (selectKeMode & TLS13_KE_MODE_PSK_WITH_DHE) == TLS13_KE_MODE_PSK_WITH_DHE) {
        ret = Tls13ServerProcessKeyShare(ctx, clientHello, isNeedSendHrr);
        /* The group has been selected during the cipher suite selection. Therefore, the keyshare can be processed here
         */
        if (ret != HITLS_SUCCESS) {
            return ret;
        }
    }

    /* Process the server_name extension whose length is greater than 0 in the TLS1.3 message */
    ret = ServerDealServerName(ctx, clientHello);
    if (ret != HITLS_SUCCESS) {
        return ret;
    }

    if (clientHello->extension.flag.haveAlpn && !(*isNeedSendHrr)) {
        ret = ServerSelectAlpnProtocol(ctx, clientHello);
        if (ret != HITLS_SUCCESS) {
            /* Logs have been recorded internally */
            return ret;
        }
    }

    return Tls13ServerSelectCert(ctx, clientHello);
}

static int32_t CheckVersion(uint16_t version, uint16_t minVersion, uint16_t maxVersion, uint16_t *selectVersion)
{
    if (version >= HITLS_VERSION_TLS13 && !IS_DTLS_VERSION(maxVersion)) {
        version = HITLS_VERSION_TLS12;
    }
#ifndef HITLS_NO_TLCP11
    if (((version > HITLS_VERSION_SSL30) || (version == HITLS_VERSION_TLCP11)) &&
#else
    if ((version > HITLS_VERSION_SSL30) &&
#endif
        (minVersion <= version) && (version <= maxVersion)) {
        *selectVersion = version;
        return HITLS_SUCCESS;
    }

    return HITLS_MSG_HANDLE_UNSUPPORT_VERSION;
}

bool IsTls13KeyExchAvailable(TLS_Ctx *ctx)
{
    TLS_Config *config = &ctx->config.tlsConfig;
    CERT_MgrCtx *certMgrCtx = config->certMgrCtx;

    if (config->pskServerCb != NULL) {
        return true;
    }

    if (config->pskFindSessionCb != NULL) {
        return true;
    }

    /* The PSK is not used. The certificate must be set */
    for (uint32_t i = 0; i < TLS_CERT_KEY_TYPE_NUM; i++) {
        if (i == TLS_CERT_KEY_TYPE_DSA) {
            /* in TLS1.3, Do not use the DSA certificate. */
            continue;
        }
        if ((SAL_CERT_GetCert(certMgrCtx, i) != NULL) && (SAL_CERT_GetPrivateKey(certMgrCtx, i) != NULL)) {
            return true;
        }
    }
    return false;
}

static int32_t SelectVersion(TLS_Ctx *ctx, const ClientHelloMsg *clientHello, uint16_t minVersion, uint16_t maxVersion,
    uint16_t *selectVersion)
{
    int32_t ret;
    uint16_t version = clientHello->version;

    /**
     * According to rfc8446 section 4.2.1 if the ClientHello does not have the supportedVersions extension,
     * Then the server must negotiate TLS 1.2 or earlier as specified in rfc5246.
     */
    if (clientHello->extension.content.supportedVersionsCount == 0) {
        ret = CheckVersion(version, minVersion, maxVersion, selectVersion);
        if (ret != HITLS_SUCCESS) {
            BSL_ERR_PUSH_ERROR(HITLS_MSG_HANDLE_UNSUPPORT_VERSION);
            BSL_LOG_BINLOG_FIXLEN(BINLOG_ID15885, BSL_LOG_LEVEL_ERR, BSL_LOG_BINLOG_TYPE_RUN,
                "server cannot negotiate a version.", 0, 0, 0, 0);
            ctx->method.sendAlert(ctx, ALERT_LEVEL_FATAL, ALERT_PROTOCOL_VERSION);
        }
        return ret;
    }

    /* If the received message is not an earlier version, the version byte in the tls1.3 must be 0x0303 according to
     * section 4.1.2 in RFC 8446 */
    if (version != HITLS_VERSION_TLS12) {
        BSL_ERR_PUSH_ERROR(HITLS_MSG_HANDLE_UNSUPPORT_VERSION);
        BSL_LOG_BINLOG_FIXLEN(BINLOG_ID15249, BSL_LOG_LEVEL_ERR, BSL_LOG_BINLOG_TYPE_RUN,
            "illegal client legacy_version(0x%02x).", version, 0, 0, 0);
        ctx->method.sendAlert(ctx, ALERT_LEVEL_FATAL, ALERT_PROTOCOL_VERSION);
        return HITLS_MSG_HANDLE_UNSUPPORT_VERSION;
    }

    /* Find the supported version in the extended field supportedVersions. */
    for (version = maxVersion; version >= minVersion; version--) {
        for (int i = 0; i < clientHello->extension.content.supportedVersionsCount; i++) {
            if (clientHello->extension.content.supportedVersions[i] != version) {
                continue;
            }
            if (((version == HITLS_VERSION_TLS13) && (!IsTls13KeyExchAvailable(ctx))) ||
                (version <= HITLS_VERSION_SSL30)) {
                /* TLS1.3 must have an available PSK or certificate, and TLS1.3 cannot negotiate SSL versions earlier
                 * than SSL3.0. */
                continue;
            }
            /* rfc8446 4.2.1 The server must be ready to receive ClientHello that contains the supportedVersions
             * extension but does not contain 0x0304 in the version list, Therefore, if a matching version is found,
             * even if the version is an earlier version, the system directly returns */
            *selectVersion = version;
            return HITLS_SUCCESS;
        }
    }

    BSL_ERR_PUSH_ERROR(HITLS_MSG_HANDLE_UNSUPPORT_VERSION);
    BSL_LOG_BINLOG_FIXLEN(BINLOG_ID15250, BSL_LOG_LEVEL_ERR, BSL_LOG_BINLOG_TYPE_RUN,
        "server cannot negotiate a version.", 0, 0, 0, 0);
    ctx->method.sendAlert(ctx, ALERT_LEVEL_FATAL, ALERT_PROTOCOL_VERSION);
    return HITLS_MSG_HANDLE_UNSUPPORT_VERSION;
}

static int32_t UpdateServerBaseKeyExMode(TLS_Ctx *ctx)
{
    uint32_t tls13BasicKeyExMode = 0;
    KeyExchCtx *kxCtx = ctx->hsCtx->kxCtx;
    if (kxCtx->pskInfo13.psk != NULL && kxCtx->peerPubkey != NULL) {
        tls13BasicKeyExMode = TLS13_KE_MODE_PSK_WITH_DHE;
    } else if (kxCtx->pskInfo13.psk != NULL) {
        tls13BasicKeyExMode = TLS13_KE_MODE_PSK_ONLY;
    } else if (kxCtx->peerPubkey != NULL) {
        tls13BasicKeyExMode = TLS13_CERT_AUTH_WITH_DHE;
    } else {
        // kxCtx->pskInfo13.psk == NULL && kxCtx->peerPubkey == NULL is guaranteed by Tls13ServerCheckClientHello
        BSL_ERR_PUSH_ERROR(HITLS_INTERNAL_EXCEPTION);
        return HITLS_INTERNAL_EXCEPTION;
    }
    ctx->negotiatedInfo.tls13BasicKeyExMode = tls13BasicKeyExMode;
    return HITLS_SUCCESS;
}

static int32_t Tls13ServerProcessClientHello(TLS_Ctx *ctx, HS_Msg *msg)
{
    int32_t ret = HITLS_SUCCESS;
    TLS_Config *tlsConfig = &ctx->config.tlsConfig;
    ClientHelloMsg *clientHello = &msg->body.clientHello;
    if (ctx->hsCtx->haveHrr) {
        /* In middlebox mode, CCS messages cannot be received after the second clientHello message is received. */
        ctx->method.ctrlCCS(ctx, CCS_CMD_RECV_EXIT_READY);
    } else if (!ctx->method.isRecvCCS(ctx)) {
        /* An unencrypted CCS may be received after sending or receiving the first ClientHello according to RFC 8446 */
        ctx->method.ctrlCCS(ctx, CCS_CMD_RECV_READY);
    }

    bool isNeedSendHrr = false;
    /* Processing Client Hello messages */
    ret = Tls13ServerCheckClientHello(ctx, clientHello, &isNeedSendHrr);
    if (ret != HITLS_SUCCESS) {
        return ret;
    }

    if (isNeedSendHrr) {
        return HS_ChangeState(ctx, TRY_SEND_HELLO_RETRY_REQUEST);
    }
    ret = UpdateServerBaseKeyExMode(ctx);
    if (ret != HITLS_SUCCESS) {
        return ret;
    }
    if (ctx->phaState == PHA_NONE && tlsConfig->isSupportClientVerify && tlsConfig->isSupportPostHandshakeAuth &&
        msg->body.clientHello.extension.flag.havePostHsAuth) {
        ctx->phaState = PHA_EXTENSION;
    }
    return HS_ChangeState(ctx, TRY_SEND_SERVER_HELLO);
}

int32_t Tls13ServerRecvClientHelloProcess(TLS_Ctx *ctx, HS_Msg *msg)
{
    int32_t ret = 0;
    uint16_t selectedVersion = 0;
    ClientHelloMsg *clientHello = &msg->body.clientHello;
    TLS_Config *tlsConfig = &ctx->config.tlsConfig;

    /* Perform the ClientHello callback. The pause handshake status is not considered */
    ret = ClientHelloCbCheck(ctx);
    if (ret != HITLS_SUCCESS) {
        return ret;
    }

    ret = SelectVersion(ctx, clientHello, tlsConfig->minVersion, tlsConfig->maxVersion, &selectedVersion);
    if (ret != HITLS_SUCCESS) {
        return ret;
    }

    /* If the TLS version is earlier than 1.3, the ServerHello.version parameter must be set on the server and the
     * supported_versions extension cannot be sent */
    clientHello->version = selectedVersion;

    switch (selectedVersion) {
        case HITLS_VERSION_TLS12:
            BSL_LOG_BINLOG_FIXLEN(BINLOG_ID15251, BSL_LOG_LEVEL_INFO, BSL_LOG_BINLOG_TYPE_RUN,
                "tls1.3 server receive a tls1.2 clientHello.", 0, 0, 0, 0);
            return Tls12ServerRecvClientHelloProcess(ctx, msg);
        case HITLS_VERSION_TLS13:
            return Tls13ServerProcessClientHello(ctx, msg);
        default:
            break;
    }
    BSL_ERR_PUSH_ERROR(HITLS_MSG_HANDLE_UNSUPPORT_VERSION);
    BSL_LOG_BINLOG_FIXLEN(BINLOG_ID15252, BSL_LOG_LEVEL_ERR, BSL_LOG_BINLOG_TYPE_RUN,
        "server select an unsupported version.", 0, 0, 0, 0);
    return HITLS_MSG_HANDLE_UNSUPPORT_VERSION;
}