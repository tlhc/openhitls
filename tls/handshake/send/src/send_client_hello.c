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

#include <string.h>
#include "securec.h"
#include "tls_binlog_id.h"
#include "bsl_log_internal.h"
#include "bsl_log.h"
#include "bsl_err_internal.h"
#include "bsl_sal.h"
#include "crypt.h"
#include "bsl_uio.h"
#include "hitls_error.h"
#include "hitls_session.h"
#include "hitls.h"
#include "hs_ctx.h"
#include "hs_common.h"
#include "hs_verify.h"
#include "pack.h"
#include "send_process.h"
#include "session_mgr.h"
#include "bsl_bytes.h"


/* Check whether the resume function is supported */
static int32_t ClientPrepareSession(TLS_Ctx *ctx)
{
    HS_Ctx *hsCtx = (HS_Ctx *)ctx->hsCtx;

    /* If the session cannot be resumed during renegotiation, delete the session */
    if (ctx->negotiatedInfo.isRenegotiation && !ctx->config.tlsConfig.isResumptionOnRenego) {
        HITLS_SESS_Free(ctx->session);
        ctx->session = NULL;
    }

    if (ctx->session != NULL) {
        uint64_t curTime = (uint64_t)BSL_SAL_CurrentSysTimeGet();
        if (!SESS_CheckValidity(ctx->session, curTime)) {
            HITLS_SESS_Free(ctx->session);
            ctx->session = NULL;
        }
    }

    if (ctx->session != NULL) {
        uint8_t haveExtMasterSecret = 0;
        HITLS_SESS_GetHaveExtMasterSecret(ctx->session, &haveExtMasterSecret);
        if (haveExtMasterSecret == 0 && ctx->config.tlsConfig.isSupportExtendMasterSecret) {
            HITLS_SESS_Free(ctx->session);
            ctx->session = NULL;
            return HITLS_SUCCESS;
        }
        hsCtx->sessionId = (uint8_t *)BSL_SAL_Calloc(1u, HITLS_SESSION_ID_MAX_SIZE);
        if (hsCtx->sessionId == NULL) {
            HITLS_SESS_Free(ctx->session);
            ctx->session = NULL;
            BSL_ERR_PUSH_ERROR(HITLS_MEMALLOC_FAIL);
            BSL_LOG_BINLOG_FIXLEN(BINLOG_ID15624, BSL_LOG_LEVEL_ERR, BSL_LOG_BINLOG_TYPE_RUN,
                "session Id malloc fail.", 0, 0, 0, 0);
            return HITLS_MEMALLOC_FAIL;
        }

        hsCtx->sessionIdSize = HITLS_SESSION_ID_MAX_SIZE;
        int32_t ret = HITLS_SESS_GetSessionId(ctx->session, hsCtx->sessionId, &hsCtx->sessionIdSize);
        if (ret != HITLS_SUCCESS) {
            BSL_SAL_FREE(hsCtx->sessionId);
            HITLS_SESS_Free(ctx->session);
            ctx->session = NULL;
            return ret;
        }
    }

    return HITLS_SUCCESS;
}

static int32_t ClientChangeStateAfterSendClientHello(TLS_Ctx *ctx)
{
    if (ctx->session != NULL && IS_DTLS_VERSION(ctx->config.tlsConfig.maxVersion)) {
        /* In the DTLS scenario, enable the receiving of CCS messages to prevent CCS message disorder during session
         * resumption */
        ctx->method.ctrlCCS(ctx, CCS_CMD_RECV_READY);
    }
    return HS_ChangeState(ctx, TRY_RECV_SERVER_HELLO);
}

int32_t ClientSendClientHelloProcess(TLS_Ctx *ctx)
{
    int32_t ret = HITLS_SUCCESS;
    /** Obtain client information */
    HS_Ctx *hsCtx = (HS_Ctx *)ctx->hsCtx;

    /** Determine whether the message needs to be packed */
    if (hsCtx->msgLen == 0) {
        /* If HelloVerifyRequest is used, the initial ClientHello and
           HelloVerifyRequest are not included in the calculation of the
           handshake_messages (for the CertificateVerify message) and
           verify_data (for the Finished message). */
        ret = VERIFY_Init(hsCtx);
        if (ret != HITLS_SUCCESS) {
            return ret;
        }

        /* 1. For DTLS, the Hello Verify Request message may be received. If the Client Hello message is sent for the
         * second time, the random and session of the previous session can be used directly. If the value of cookieSize
         * is not 0, the Hello Verify Request message is received.
         * 2. In the renegotiation state, the random and session need to be obtained again */
        if ((ctx->negotiatedInfo.cookieSize == 0) || (ctx->negotiatedInfo.isRenegotiation)) {
            ret = ClientPrepareSession(ctx);
            if (ret != HITLS_SUCCESS) {
                return ret;
            }

            ret = SAL_CRYPT_Rand(hsCtx->clientRandom, HS_RANDOM_SIZE);
            if (ret != HITLS_SUCCESS) {
                BSL_LOG_BINLOG_FIXLEN(BINLOG_ID15625, BSL_LOG_LEVEL_ERR, BSL_LOG_BINLOG_TYPE_RUN,
                    "generate random value fail.", 0, 0, 0, 0);
                return ret;
            }
        }

        ctx->negotiatedInfo.clientVersion = ctx->config.tlsConfig.maxVersion;
        ret = HS_PackMsg(ctx, CLIENT_HELLO, hsCtx->msgBuf, hsCtx->bufferLen, &hsCtx->msgLen);
        if (ret != HITLS_SUCCESS) {
            BSL_LOG_BINLOG_FIXLEN(BINLOG_ID15626, BSL_LOG_LEVEL_ERR, BSL_LOG_BINLOG_TYPE_RUN,
                "pack client hello fail.", 0, 0, 0, 0);
            return ret;
        }
    }

    ret = HS_SendMsg(ctx);
    if (ret != HITLS_SUCCESS) {
        return ret;
    }

    BSL_LOG_BINLOG_FIXLEN(BINLOG_ID15627, BSL_LOG_LEVEL_INFO, BSL_LOG_BINLOG_TYPE_RUN,
        "send client hello success.", 0, 0, 0, 0);

    return ClientChangeStateAfterSendClientHello(ctx);
}
static bool Tls13SelectGroup(TLS_Ctx *ctx, uint16_t *group)
{
    TLS_Config *tlsConfig = &ctx->config.tlsConfig;
    uint16_t version = (ctx->negotiatedInfo.version == 0) ?
        ctx->config.tlsConfig.maxVersion : ctx->negotiatedInfo.version;
    for (uint32_t i = 0; i < tlsConfig->groupsSize; ++i) {
        if (GroupConformToVersion(version, tlsConfig->groups[i])) {
            *group = tlsConfig->groups[i];
            return true;
        }
    }
    return false;
}

static int32_t Tls13ClientPrepareKeyShare(TLS_Ctx *ctx, uint32_t tls13BasicKeyExMode)
{
    TLS_Config *tlsConfig = &ctx->config.tlsConfig;
    // Certificate authentication and PSK with DHE authentication require key share
    uint32_t needKeyShareMode = TLS13_KE_MODE_PSK_WITH_DHE | TLS13_CERT_AUTH_WITH_DHE;
    if ((tls13BasicKeyExMode & needKeyShareMode) == 0) {
        return HITLS_SUCCESS;
    }

    if (tlsConfig->groups == NULL) {
        BSL_ERR_PUSH_ERROR(HITLS_INTERNAL_EXCEPTION);
        BSL_LOG_BINLOG_FIXLEN(BINLOG_ID15628, BSL_LOG_LEVEL_ERR, BSL_LOG_BINLOG_TYPE_RUN,
            "tlsConfig->groups is null when prepare key share.", 0, 0, 0, 0);
        return HITLS_INTERNAL_EXCEPTION;
    }

    uint16_t selectGroup = tlsConfig->groups[0];
    /* The keyShare has passed the verification when receiving the HRR */
    KeyShareParam *keyShare = &ctx->hsCtx->kxCtx->keyExchParam.share;
    KeyExchCtx *kxCtx = ctx->hsCtx->kxCtx;
    if (ctx->hsCtx->haveHrr) {
        /** If the value of group is not updated in the hello retry request, the system directly returns */
        if (selectGroup == keyShare->group) {
            return HITLS_SUCCESS;
        }

        /** If the value of group is updated, use the updated group */
        selectGroup = keyShare->group;
    } else {
        if (!Tls13SelectGroup(ctx, &selectGroup)) {
            return HITLS_MSG_HANDLE_ILLEGAL_SELECTED_GROUP;
        }
        /** Send the client hello message for the first time and fill in the group in the key share extension */
        keyShare->group = selectGroup;
    }
    HITLS_ECParameters curveParams = {
        .type = HITLS_EC_CURVE_TYPE_NAMED_CURVE,
        .param.namedcurve = selectGroup,
    };
    HITLS_CRYPT_Key *key = NULL;
    // ecdhe and dhe groups can invoke the same interface to generate keys.
    key = SAL_CRYPT_GenEcdhKeyPair(&curveParams);
    if (key == NULL) {
        BSL_ERR_PUSH_ERROR(HITLS_CRYPT_ERR_ENCODE_ECDH_KEY);
        BSL_LOG_BINLOG_FIXLEN(BINLOG_ID15629, BSL_LOG_LEVEL_ERR, BSL_LOG_BINLOG_TYPE_RUN,
            "server generate key share key pair error.", 0, 0, 0, 0);
        return HITLS_CRYPT_ERR_ENCODE_ECDH_KEY;
    }
    if (kxCtx->key != NULL) {
        SAL_CRYPT_FreeEcdhKey(kxCtx->key);
    }
    kxCtx->key = key;

    return HITLS_SUCCESS;
}

static int32_t Tls13ClientPrepareSession(TLS_Ctx *ctx)
{
    int32_t ret = HITLS_SUCCESS;
    HS_Ctx *hsCtx = (HS_Ctx *)ctx->hsCtx;

    hsCtx->sessionId = (uint8_t *)BSL_SAL_Calloc(1u, HITLS_SESSION_ID_MAX_SIZE);
    if (hsCtx->sessionId == NULL) {
        BSL_ERR_PUSH_ERROR(HITLS_MEMALLOC_FAIL);
        BSL_LOG_BINLOG_FIXLEN(BINLOG_ID15630, BSL_LOG_LEVEL_ERR, BSL_LOG_BINLOG_TYPE_RUN,
            "session Id malloc fail.", 0, 0, 0, 0);
        return HITLS_MEMALLOC_FAIL;
    }
    ret = SAL_CRYPT_Rand(hsCtx->sessionId, HITLS_SESSION_ID_MAX_SIZE);
    if (ret != HITLS_SUCCESS) {
        BSL_SAL_FREE(hsCtx->sessionId);
        BSL_LOG_BINLOG_FIXLEN(BINLOG_ID15631, BSL_LOG_LEVEL_ERR, BSL_LOG_BINLOG_TYPE_RUN,
            "generate random session Id fail.", 0, 0, 0, 0);
        return ret;
    }
    hsCtx->sessionIdSize = HITLS_SESSION_ID_MAX_SIZE;

    return HITLS_SUCCESS;
}

int32_t CreatePskSession(TLS_Ctx *ctx, uint8_t *id, uint32_t idLen, HITLS_Session **pskSession)
{
    if (ctx == NULL || pskSession == NULL) {
        return HITLS_NULL_INPUT;
    }

    if (ctx->config.tlsConfig.pskClientCb == NULL) {
        return HITLS_SUCCESS;
    }

    uint8_t psk[HS_PSK_MAX_LEN] = {0};
    uint32_t pskLen = ctx->config.tlsConfig.pskClientCb(ctx, NULL, id, idLen, psk, HS_PSK_MAX_LEN);
    if (pskLen == 0) {
        return HITLS_SUCCESS;
    }
    if (pskLen > HS_PSK_MAX_LEN) {
        return HITLS_MSG_HANDLE_ILLEGAL_PSK_LEN;
    }

    HITLS_Session *sess = HITLS_SESS_New();
    if (sess == NULL) {
        memset_s(psk, HS_PSK_MAX_LEN, 0, HS_PSK_MAX_LEN);
        return HITLS_MEMALLOC_FAIL;
    }

    HITLS_SESS_SetMasterKey(sess, psk, pskLen);
    HITLS_SESS_SetCipherSuite(sess, HITLS_AES_128_GCM_SHA256);
    HITLS_SESS_SetProtocolVersion(sess, HITLS_VERSION_TLS13);
    *pskSession = sess;
    memset_s(psk, HS_PSK_MAX_LEN, 0, HS_PSK_MAX_LEN);
    return HITLS_SUCCESS;
}

static bool IsTls13SessionValid(HITLS_HashAlgo hashAlgo, HITLS_Session* session, uint16_t *tls13CipherSuites,
    uint32_t tls13cipherSuitesSize)
{
    uint16_t version = 0;
    HITLS_SESS_GetProtocolVersion(session, &version);
    if (version != HITLS_VERSION_TLS13) {
        return false;
    }
    uint16_t cipherSuite = 0;
    CipherSuiteInfo cipherInfo = {0};
    (void)HITLS_SESS_GetCipherSuite(session, &cipherSuite); // only null input cause error
    int32_t ret = CFG_GetCipherSuiteInfo(cipherSuite, &cipherInfo);
    if (ret != HITLS_SUCCESS) {
        return false;
    }
    if (hashAlgo != HITLS_HASH_NULL) {
        return (hashAlgo == cipherInfo.hashAlg);
    }
    if (tls13CipherSuites != NULL) {
        for (uint32_t i = 0; i < tls13cipherSuitesSize; i++) {
            CipherSuiteInfo configCipher = {0};
            ret = CFG_GetCipherSuiteInfo(tls13CipherSuites[i], &configCipher);
            if (ret == HITLS_SUCCESS && configCipher.hashAlg == cipherInfo.hashAlg) {
                return true;
            }
        }
    }
    return false;
}

static UserPskList *ConstructUserPsk(HITLS_Session *sessoin, const uint8_t *identity, uint32_t identityLen,
    uint8_t curIndex)
{
    if (identityLen > HS_PSK_IDENTITY_MAX_LEN || sessoin == NULL) {
        return NULL;
    }
    UserPskList *userPsk = BSL_SAL_Calloc(1, sizeof(UserPskList));
    if (userPsk == NULL) {
        return NULL;
    }
    userPsk->pskSession = HITLS_SESS_Dup(sessoin);
    userPsk->identity = BSL_SAL_Calloc(1, identityLen);
    if (userPsk->identity == NULL) {
        BSL_SAL_FREE(userPsk);
        return NULL;
    }
    (void)memcpy_s(userPsk->identity, identityLen, identity, identityLen);
    userPsk->identityLen = identityLen;
    userPsk->num = curIndex;
    return userPsk;
}

static int32_t Tls13ClientPreparePSK(TLS_Ctx *ctx)
{
    int32_t ret = 0;
    HS_Ctx *hsCtx = ctx->hsCtx;
    HITLS_HashAlgo hashAlgo = hsCtx->haveHrr ? ctx->negotiatedInfo.cipherSuiteInfo.hashAlg : HITLS_HASH_NULL;
    uint8_t identity[HS_PSK_IDENTITY_MAX_LEN + 1] = {0};
    const uint8_t *id = NULL;
    uint32_t idLen = 0;
    HITLS_Session *pskSession = NULL;
    UserPskList *userPsk = NULL;

    /* Obtain the resume psk information from the session */
    HITLS_SESS_Free(hsCtx->kxCtx->pskInfo13.resumeSession);
    hsCtx->kxCtx->pskInfo13.resumeSession = NULL;
    if (HITLS_SESS_HasTicket(ctx->session) &&
        IsTls13SessionValid(hashAlgo, ctx->session, ctx->config.tlsConfig.tls13CipherSuites,
                            ctx->config.tlsConfig.tls13cipherSuitesSize) &&
        SESS_CheckValidity(ctx->session, (uint64_t)BSL_SAL_CurrentSysTimeGet())) {
        hsCtx->kxCtx->pskInfo13.resumeSession = HITLS_SESS_Dup(ctx->session);
    }

    uint8_t index = (hsCtx->kxCtx->pskInfo13.resumeSession == NULL) ? 0 : 1;
    if (ctx->config.tlsConfig.pskUseSessionCb != NULL) {
        ret = ctx->config.tlsConfig.pskUseSessionCb(ctx, hashAlgo, &id, &idLen, &pskSession);
        if (ret != HITLS_PSK_USE_SESSION_CB_SUCCESS) {
            return HITLS_MSG_HANDLE_PSK_USE_SESSION_FAIL;
        }
    }

    if (pskSession == NULL) {
        // use 1.2 psk callback default hashalgo == sha256
        ret = CreatePskSession(ctx, identity, HS_PSK_IDENTITY_MAX_LEN, &pskSession);
        if (ret != HITLS_SUCCESS) {
            return ret;
        }
        id = identity;
        idLen = (uint32_t)strlen((char *)identity);
    }

    if (pskSession != NULL && IsTls13SessionValid(hashAlgo, pskSession, ctx->config.tlsConfig.tls13CipherSuites,
            ctx->config.tlsConfig.tls13cipherSuitesSize)) {
        userPsk = ConstructUserPsk(pskSession, id, idLen, index);
    }
    HITLS_SESS_Free(pskSession);
    pskSession = NULL;

    if (ctx->hsCtx->kxCtx->pskInfo13.userPskSess != NULL) {
        BSL_SAL_FREE(ctx->hsCtx->kxCtx->pskInfo13.userPskSess->identity);
        HITLS_SESS_Free(ctx->hsCtx->kxCtx->pskInfo13.userPskSess->pskSession);
        BSL_SAL_FREE(ctx->hsCtx->kxCtx->pskInfo13.userPskSess);
    }
    ctx->hsCtx->kxCtx->pskInfo13.userPskSess = userPsk;
    return HITLS_SUCCESS;
}

int32_t Tls13ClientHelloPrepare(TLS_Ctx *ctx)
{
    int32_t ret = HITLS_SUCCESS;
    HS_Ctx *hsCtx = ctx->hsCtx;
    /** After receiving the hello retry request message, the client needs to send the second clientHello. In this case,
     * the following initialization is not required */
    if (hsCtx->haveHrr == false) {
        ret = VERIFY_Init(hsCtx);
        if (ret != HITLS_SUCCESS) {
            return ret;
        }

        ret = SAL_CRYPT_Rand(hsCtx->clientRandom, HS_RANDOM_SIZE);
        if (ret != HITLS_SUCCESS) {
            BSL_LOG_BINLOG_FIXLEN(BINLOG_ID15632, BSL_LOG_LEVEL_ERR, BSL_LOG_BINLOG_TYPE_RUN,
                "generate random value fail.", 0, 0, 0, 0);
            return ret;
        }

        /** In section 4.1.2 of RFC8446, a random session ID is required in middlebox mode. In nomiddlebox mode, the
         * session ID is empty */
        ret = Tls13ClientPrepareSession(ctx);
        if (ret != HITLS_SUCCESS) {
            return ret;
        }
    } else {
        /** If the middlebox is used, a CCS message must be sent before the second clientHello message is sent */
        ret = ctx->method.sendCCS(ctx);
        if (ret != HITLS_SUCCESS) {
            return ret;
        }
    }
    ret = Tls13ClientPreparePSK(ctx);
    if (ret != HITLS_SUCCESS) {
        ctx->method.sendAlert(ctx, ALERT_LEVEL_FATAL, ALERT_INTERNAL_ERROR);
        return ret;
    }

    uint32_t tls13BasicKeyExMode = 0;
    PskInfo13 *pskInfo = &ctx->hsCtx->kxCtx->pskInfo13;
    if (pskInfo->resumeSession != NULL || pskInfo->userPskSess != NULL) {
        tls13BasicKeyExMode |= ctx->config.tlsConfig.keyExchMode; // keyExchMode must not be 0
    }

    if (ctx->config.tlsConfig.signAlgorithmsSize != 0) { // base cert auth
        tls13BasicKeyExMode |= TLS13_CERT_AUTH_WITH_DHE;
    }

    /** Prepare the key share extension. The keyshares in two clientHello messages are different. Therefore,
     * both the keyshares must be prepared */
    ret = Tls13ClientPrepareKeyShare(ctx, tls13BasicKeyExMode);
    if (ret != HITLS_SUCCESS) {
        ctx->method.sendAlert(ctx, ALERT_LEVEL_FATAL, ALERT_INTERNAL_ERROR);
        return ret;
    }

    if (tls13BasicKeyExMode == 0) {
        BSL_LOG_BINLOG_FIXLEN(BINLOG_ID15463, BSL_LOG_LEVEL_ERR, BSL_LOG_BINLOG_TYPE_RUN,
            "tls config error: can not decide tls13BasicKeyExMode ", 0, 0, 0, 0);
        return HITLS_CONFIG_INVALID_SET;
    }
    ctx->negotiatedInfo.tls13BasicKeyExMode = tls13BasicKeyExMode;

    return HITLS_SUCCESS;
}

static uint32_t GetBindersOffset(const TLS_Ctx *ctx)
{
    uint32_t ret = sizeof(uint16_t);
    PskInfo13 *pskInfo = &ctx->hsCtx->kxCtx->pskInfo13;
    uint32_t binderLen = 0;
    if (pskInfo->resumeSession != NULL) {
        HITLS_HashAlgo hashAlg = HITLS_HASH_NULL;
        binderLen = HS_GetBinderLen(pskInfo->resumeSession, &hashAlg);  // Success guaranteed by the context
        ret += binderLen + sizeof(uint8_t);
    }
    if (pskInfo->userPskSess != NULL) {
        HITLS_HashAlgo hashAlg = HITLS_HASH_NULL;
        binderLen = HS_GetBinderLen(pskInfo->userPskSess->pskSession, &hashAlg);  // Success guaranteed by the context
        ret += binderLen + sizeof(uint8_t);
    }
    return ret;
}

static int32_t PackClientPreSharedKeyBinders(const TLS_Ctx *ctx, uint8_t *buf, uint32_t bufLen)
{
    uint32_t trucatedLen = (bufLen - GetBindersOffset(ctx));
    buf = buf + trucatedLen;
    PskInfo13 *pskInfo = &ctx->hsCtx->kxCtx->pskInfo13;
    uint32_t offset = sizeof(uint16_t); // skip binders len
    uint8_t psk[HS_PSK_MAX_LEN] = {0};
    uint32_t pskLen = HS_PSK_MAX_LEN;
    uint32_t binderLen = 0;
    int32_t ret = HITLS_SUCCESS;
    if (pskInfo->resumeSession != NULL) {
        HITLS_HashAlgo hashAlg = HITLS_HASH_NULL;
        binderLen = HS_GetBinderLen(pskInfo->resumeSession, &hashAlg);  // Success guaranteed by the context
        buf[offset] = binderLen;
        offset++;
        ret = HITLS_SESS_GetMasterKey(pskInfo->resumeSession, psk, &pskLen);
        if (ret != HITLS_SUCCESS) {
            return ret;
        }
        ret = VERIFY_CalcPskBinder(ctx, hashAlg, false, psk, pskLen,
            ctx->hsCtx->msgBuf, trucatedLen, &buf[offset], binderLen);
        BSL_SAL_CleanseData(psk, HS_PSK_MAX_LEN);
        if (ret != HITLS_SUCCESS) {
            return ret;
        }
        offset += binderLen;
    }

    if (pskInfo->userPskSess != NULL) {
        pskLen = HS_PSK_MAX_LEN;
        HITLS_HashAlgo hashAlg = HITLS_HASH_NULL;
        binderLen = HS_GetBinderLen(pskInfo->userPskSess->pskSession, &hashAlg);  // context is guaranteed to succeed
        buf[offset] = (uint8_t)binderLen;
        offset++;
        ret = HITLS_SESS_GetMasterKey(pskInfo->userPskSess->pskSession, psk, &pskLen);
        if (ret != HITLS_SUCCESS) {
            return ret;
        }
        ret = VERIFY_CalcPskBinder(ctx, hashAlg, true, psk, pskLen,
            ctx->hsCtx->msgBuf, trucatedLen, &buf[offset], binderLen);
        BSL_SAL_CleanseData(psk, HS_PSK_MAX_LEN);
        if (ret != HITLS_SUCCESS) {
            return ret;
        }
        offset += binderLen;
    }

    BSL_Uint16ToByte((uint16_t)(offset - sizeof(uint16_t)), &buf[0]); // pack binder len
    return HITLS_SUCCESS;
}

int32_t Tls13ClientSendClientHelloProcess(TLS_Ctx *ctx)
{
    int32_t ret = HITLS_SUCCESS;
    HS_Ctx *hsCtx = ctx->hsCtx;

    /* Determine whether the message needs to be packed */
    if (hsCtx->msgLen == 0) {
        ret = Tls13ClientHelloPrepare(ctx);
        if (ret != HITLS_SUCCESS) {
            return ret;
        }

        ctx->negotiatedInfo.clientVersion = HITLS_VERSION_TLS12;
        /* The packed message is placed in the hsCtx->msgBuf. The length of the packed message is hsCtx->msgLen,
         * including the CH message header and body */
        ret = HS_PackMsg(ctx, CLIENT_HELLO, hsCtx->msgBuf, hsCtx->bufferLen, &hsCtx->msgLen);
        if (ret != HITLS_SUCCESS) {
            BSL_LOG_BINLOG_FIXLEN(BINLOG_ID15633, BSL_LOG_LEVEL_ERR, BSL_LOG_BINLOG_TYPE_RUN,
                "pack tls1.3 client hello fail.", 0, 0, 0, 0);
            return ret;
        }

        if (hsCtx->extFlag.havePreShareKey == true) {
            /* Calculate the binder */
            ret = PackClientPreSharedKeyBinders(ctx, hsCtx->msgBuf, hsCtx->msgLen);
            if (ret != HITLS_SUCCESS) {
                return ret;
            }
        }
        if (hsCtx->extFlag.havePostHsAuth && ctx->phaState == PHA_NONE) {
            ctx->phaState = PHA_EXTENSION;
        }
    }

    if (!ctx->method.isRecvCCS(ctx)) {
        /* Unencrypted CCS can be received after the first ClientHello is sent or received according to RFC 8446 */
        ctx->method.ctrlCCS(ctx, CCS_CMD_RECV_READY);
    }

    ret = HS_SendMsg(ctx);
    if (ret != HITLS_SUCCESS) {
        return ret;
    }

    BSL_LOG_BINLOG_FIXLEN(BINLOG_ID15634, BSL_LOG_LEVEL_INFO, BSL_LOG_BINLOG_TYPE_RUN,
        "send tls1.3 client hello success.", 0, 0, 0, 0);

    return HS_ChangeState(ctx, TRY_RECV_SERVER_HELLO);
}