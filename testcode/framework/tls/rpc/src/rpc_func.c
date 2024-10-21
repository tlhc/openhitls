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

#include <stdio.h>
#include <stdlib.h>
#include <pthread.h>
#include "securec.h"
#include "hlt.h"
#include "handle_cmd.h"
#include "tls_res.h"
#include "logger.h"
#include "lock.h"
#include "hitls_error.h"
#include "hitls_type.h"
#include "tls.h"
#include "alert.h"
#include "hitls.h"
#include "common_func.h"
#include "sctp_channel.h"
#include "rpc_func.h"

#define HITLS_READBUF_MAXLEN (20 * 1024) /* 20K */
#define SUCCESS 0
#define ERROR (-1)

#define ASSERT_RETURN(condition)          \
    do {                                  \
        if (!(condition)) {               \
            LOG_ERROR("sprintf_s Error"); \
            return ERROR;                 \
        }                                 \
    } while (0)

RpcFunList g_rpcFuncList[] = {
    {"HLT_RpcTlsNewCtx", RpcTlsNewCtx},
    {"HLT_RpcTlsSetCtx", RpcTlsSetCtx},
    {"HLT_RpcTlsNewSsl", RpcTlsNewSsl},
    {"HLT_RpcTlsSetSsl", RpcTlsSetSsl},
    {"HLT_RpcTlsListen", RpcTlsListen},
    {"HLT_RpcTlsAccept", RpcTlsAccept},
    {"HLT_RpcTlsConnect", RpcTlsConnect},
    {"HLT_RpcTlsRead", RpcTlsRead},
    {"HLT_RpcTlsWrite", RpcTlsWrite},
    {"HLT_RpcTlsRenegotiate", RpcTlsRenegotiate},
    {"HLT_RpcDataChannelAccept", RpcDataChannelAccept},
    {"HLT_RpcDataChannelConnect", RpcDataChannelConnect},
    {"HLT_RpcProcessExit", RpcProcessExit},
    {"HLT_RpcTlsRegCallback", RpcTlsRegCallback},
    {"HLT_RpcTlsGetStatus", RpcTlsGetStatus},
    {"HLT_RpcTlsGetAlertFlag", RpcTlsGetAlertFlag},
    {"HLT_RpcTlsGetAlertLevel", RpcTlsGetAlertLevel},
    {"HLT_RpcTlsGetAlertDescription", RpcTlsGetAlertDescription},
    {"HLT_RpcTlsClose", RpcTlsClose},
    {"HLT_RpcFreeResFormSsl", RpcFreeResFormSsl},
    {"HLT_RpcSctpClose", RpcSctpClose},
    {"HLT_RpcCloseFd", RpcCloseFd},
    {"HLT_RpcTlsSetMtu", RpcTlsSetMtu},
    {"HLT_RpcTlsGetErrorCode", RpcTlsGetErrorCode},
    {"HLT_RpcDataChannelBind", RpcDataChannelBind},
    {"HLT_RpcTlsVerifyClientPostHandshake", RpcTlsVerifyClientPostHandshake},
};

RpcFunList *GetRpcFuncList(void)
{
    return g_rpcFuncList;
}

int GetRpcFuncNum(void)
{
    return sizeof(g_rpcFuncList) / sizeof(g_rpcFuncList[0]);
}

int RpcTlsNewCtx(CmdData *cmdData)
{
    int id, ret;
    TLS_VERSION tlsVersion;
    (void)memset_s(cmdData->result, sizeof(cmdData->result), 0, sizeof(cmdData->result));

    tlsVersion = atoi(cmdData->paras[0]);
    // Invoke the corresponding function.
    void* ctx = HLT_TlsNewCtx(tlsVersion);
    if (ctx == NULL) {
        LOG_ERROR("HLT_TlsNewCtx Return NULL");
        id = ERROR;
        goto ERR;
    }

    // Insert to CTX linked list
    id = InsertCtxToList(ctx);

ERR:
    // Return Result
    ret = sprintf_s(cmdData->result, sizeof(cmdData->result), "%s|%s|%d", cmdData->id, cmdData->funcId, id);
    if (ret <= 0) {
        return ERROR;
    }
    return SUCCESS;
}

int RpcTlsSetCtx(CmdData *cmdData)
{
    int ret;
    (void)memset_s(cmdData->result, sizeof(cmdData->result), 0, sizeof(cmdData->result));

    // Find the corresponding CTX.
    ResList *ctxList = GetCtxList();
    int ctxId = atoi(cmdData->paras[0]);
    void *ctx = GetTlsResFromId(ctxList, ctxId);
    if (ctx == NULL) {
        LOG_ERROR("GetResFromId Error");
        ret = ERROR;
        goto ERR;
    }

    // Configurations related to parsing
    HLT_Ctx_Config ctxConfig = {0};
    ret = ParseCtxConfigFromString(cmdData->paras, &ctxConfig);
    if (ret != SUCCESS) {
        LOG_ERROR("ParseCtxConfigFromString Error");
        ret = ERROR;
        goto ERR;
    }

    // Configure the data
    ret = HLT_TlsSetCtx(ctx, &ctxConfig);

ERR:
    // Return the result
    ret = sprintf_s(cmdData->result, sizeof(cmdData->result), "%s|%s|%d", cmdData->id, cmdData->funcId, ret);
    ASSERT_RETURN(ret > 0);
    return SUCCESS;
}

int RpcTlsNewSsl(CmdData *cmdData)
{
    int id, ret;
    (void)memset_s(cmdData->result, sizeof(cmdData->result), 0, sizeof(cmdData->result));

    // Invoke the corresponding function.
    ResList *ctxList = GetCtxList();
    int ctxId = atoi(cmdData->paras[0]);
    void *ctx = GetTlsResFromId(ctxList, ctxId);
    if (ctx == NULL) {
        LOG_ERROR("Not Find Ctx");
        id = ERROR;
        goto ERR;
    }

    void *ssl = HLT_TlsNewSsl(ctx);
    if (ssl == NULL) {
        LOG_ERROR("HLT_TlsNewSsl Return NULL");
        id = ERROR;
        goto ERR;
    }

    // Insert to the SSL linked list.
    id = InsertSslToList(ctx, ssl);

ERR:
    // Return the result.
    ret = sprintf_s(cmdData->result, sizeof(cmdData->result), "%s|%s|%d", cmdData->id, cmdData->funcId, id);
    ASSERT_RETURN(ret > 0);
    return SUCCESS;
}

int RpcTlsSetSsl(CmdData *cmdData)
{
    int ret;

    (void)memset_s(cmdData->result, sizeof(cmdData->result), 0, sizeof(cmdData->result));

    ResList *sslList = GetSslList();
    int sslId = atoi(cmdData->paras[0]);
    void *ssl = GetTlsResFromId(sslList, sslId);
    if (ssl == NULL) {
        LOG_ERROR("Not Find Ssl");
        ret = ERROR;
        goto ERR;
    }

    HLT_Ssl_Config sslConfig = {0};
    sslConfig.sockFd = atoi(cmdData->paras[1]); // The first parameter indicates the FD value.
    sslConfig.connType = atoi(cmdData->paras[2]); // The second parameter indicates the link type.
    // The third parameter of indicates the Ctrl command that needs to register the hook.
    sslConfig.connPort = atoi(cmdData->paras[3]);
    ret = HLT_TlsSetSsl(ssl, &sslConfig);
ERR:
    // Return the result.
    ret = sprintf_s(cmdData->result, sizeof(cmdData->result), "%s|%s|%d", cmdData->id, cmdData->funcId, ret);
    ASSERT_RETURN(ret > 0);
    return SUCCESS;
}

int RpcTlsListen(CmdData *cmdData)
{
    int ret;
    int sslId;
    (void)memset_s(cmdData->result, sizeof(cmdData->result), 0, sizeof(cmdData->result));
    ResList *sslList = GetSslList();
    sslId = strtol(cmdData->paras[0], NULL, 10); // Convert to a decimal number
    void *ssl = GetTlsResFromId(sslList, sslId);
    if (ssl == NULL) {
        LOG_ERROR("Not Find Ssl");
        ret = ERROR;
        goto ERR;
    }

    ret = HLT_TlsListenBlock(ssl);

ERR:
    // Return the result
    ret = sprintf_s(cmdData->result, sizeof(cmdData->result), "%s|%s|%d", cmdData->id, cmdData->funcId, ret);
    ASSERT_RETURN(ret > 0);
    return SUCCESS;
}

int RpcTlsAccept(CmdData *cmdData)
{
    int ret;

    (void)memset_s(cmdData->result, sizeof(cmdData->result), 0, sizeof(cmdData->result));
    ResList *sslList = GetSslList();
    int sslId = atoi(cmdData->paras[0]);
    void *ssl = GetTlsResFromId(sslList, sslId);
    if (ssl == NULL) {
        LOG_ERROR("Not Find Ssl");
        ret = ERROR;
        goto ERR;
    }

    // If there is a problem, the user must use non-blocking, and the remote call must use blocking
    ret = HLT_TlsAcceptBlock(ssl);

ERR:
    // Return the result
    ret = sprintf_s(cmdData->result, sizeof(cmdData->result), "%s|%s|%d", cmdData->id, cmdData->funcId, ret);
    ASSERT_RETURN(ret > 0);
    return SUCCESS;
}

int RpcTlsConnect(CmdData *cmdData)
{
    int ret;

    (void)memset_s(cmdData->result, sizeof(cmdData->result), 0, sizeof(cmdData->result));

    ResList *sslList = GetSslList();
    int sslId = atoi(cmdData->paras[0]);
    void *ssl = GetTlsResFromId(sslList, sslId);
    if (ssl == NULL) {
        LOG_ERROR("Not Find Ssl");
        ret = ERROR;
        goto ERR;
    }

    ret = HLT_TlsConnect(ssl);

ERR:
    // Return the result
    ret = sprintf_s(cmdData->result, sizeof(cmdData->result), "%s|%s|%d", cmdData->id, cmdData->funcId, ret);
    ASSERT_RETURN(ret > 0);
    return SUCCESS;
}

int RpcTlsRead(CmdData *cmdData)
{
    int ret = SUCCESS;

    (void)memset_s(cmdData->result, sizeof(cmdData->result), 0, sizeof(cmdData->result));
    ResList *sslList = GetSslList();
    int sslId = atoi(cmdData->paras[0]);
    void *ssl = GetTlsResFromId(sslList, sslId);
    if (ssl == NULL) {
        LOG_ERROR("Not Find Ssl");
        ret = ERROR;
        goto ERR;
    }

    int dataLen = atoi(cmdData->paras[1]);
    uint32_t readLen = 0;
    if (dataLen == 0) {
        LOG_ERROR("dataLen is 0");
        ret = ERROR;
        goto ERR;
    }
    uint8_t *data = (uint8_t *)calloc(1u, dataLen);
    if (data == NULL) {
        LOG_ERROR("Calloc Error");
        ret = sprintf_s(cmdData->result, sizeof(cmdData->result), "%s|%s|%d", cmdData->id, cmdData->funcId, ret);
        ASSERT_RETURN(ret > 0);
    }
    (void)memset_s(data, dataLen, 0, dataLen);
    ret = HLT_TlsRead(ssl, data, dataLen, &readLen);

    ret = sprintf_s(cmdData->result, sizeof(cmdData->result), "%s|%s|%d|%u|%s",
                    cmdData->id, cmdData->funcId, ret, readLen, data);
    free(data);
    return SUCCESS;
ERR:
    // Return the result
    ret = sprintf_s(cmdData->result, sizeof(cmdData->result), "%s|%s|%d|", cmdData->id, cmdData->funcId, ret);
    ASSERT_RETURN(ret > 0);
    return SUCCESS;
}

int RpcTlsWrite(CmdData *cmdData)
{
    int ret;
    (void)memset_s(cmdData->result, sizeof(cmdData->result), 0, sizeof(cmdData->result));

    ResList *sslList = GetSslList();
    int sslId = atoi(cmdData->paras[0]);
    void *ssl = GetTlsResFromId(sslList, sslId);
    if (ssl == NULL) {
        LOG_ERROR("Not Find Ssl");
        ret = ERROR;
        goto ERR;
    }

    int dataLen = atoi(cmdData->paras[1]); // The first parameter indicates the data length.
    if (dataLen == 0) {
        LOG_ERROR("dataLen is 0");
        ret = ERROR;
        goto ERR;
    }
    uint8_t *data = (uint8_t *)calloc(1u, dataLen);
    if (data == NULL) {
        LOG_ERROR("Calloc Error");
        ret = ERROR;
        goto ERR;
    }
    if (dataLen >= CONTROL_CHANNEL_MAX_MSG_LEN) {
        free(data);
        goto ERR;
    }
    // The second parameter of indicates the content of the write data.
    ret = memcpy_s(data, dataLen, cmdData->paras[2], dataLen);
    if (ret != EOK) {
        LOG_ERROR("memcpy_s Error");
        free(data);
        goto ERR;
    }
    ret = HLT_TlsWrite(ssl, data, dataLen);
    free(data);
    ret = sprintf_s(cmdData->result, sizeof(cmdData->result), "%s|%s|%d", cmdData->id, cmdData->funcId, ret);
    ASSERT_RETURN(ret > 0);
    return SUCCESS;
ERR:
    // Return the result
    ret = sprintf_s(cmdData->result, sizeof(cmdData->result), "%s|%s|%d", cmdData->id, cmdData->funcId, ret);
    ASSERT_RETURN(ret > 0);
    return SUCCESS;
}

int RpcTlsRenegotiate(CmdData *cmdData)
{
    int ret = ERROR;
    ResList *sslList = GetSslList();
    int sslId = (int)strtol(cmdData->paras[0], NULL, 10); // Convert to a decimal number
    void *ssl = GetTlsResFromId(sslList, sslId);
    if (ssl == NULL) {
        LOG_ERROR("Not Find Ssl");
        goto ERR;
    }

    ret = HLT_TlsRenegotiate(ssl);

ERR:
    // Return the result
    ret = sprintf_s(cmdData->result, sizeof(cmdData->result), "%s|%s|%d", cmdData->id, cmdData->funcId, ret);
    ASSERT_RETURN(ret > 0);
    return SUCCESS;
}

int RpcTlsVerifyClientPostHandshake(CmdData *cmdData)
{
    int ret = ERROR;
    ResList *sslList = GetSslList();
    int sslId = (int)strtol(cmdData->paras[0], NULL, 10); // Convert to a decimal number
    void *ssl = GetTlsResFromId(sslList, sslId);
    if (ssl == NULL) {
        LOG_ERROR("Not Find Ssl");
        goto ERR;
    }

    ret = HLT_TlsVerifyClientPostHandshake(ssl);

ERR:
    // Return Result
    ret = sprintf_s(cmdData->result, sizeof(cmdData->result), "%s|%s|%d", cmdData->id, cmdData->funcId, ret);
    ASSERT_RETURN(ret > 0);
    return SUCCESS;
}

int RpcProcessExit(CmdData *cmdData)
{
    int ret;
    // If 1 is returned, the process needs to exit
    (void)memset_s(cmdData->result, sizeof(cmdData->result), 0, sizeof(cmdData->result));
    ret = sprintf_s(cmdData->result, sizeof(cmdData->result), "%s|%s|%d", cmdData->id, cmdData->funcId, getpid());
    ASSERT_RETURN(ret > 0);
    return 1;
}

int RpcDataChannelAccept(CmdData *cmdData)
{
    int sockFd, ret;
    DataChannelParam channelParam;

    (void)memset_s(cmdData->result, sizeof(cmdData->result), 0, sizeof(cmdData->result));
    (void)memset_s(&channelParam, sizeof(DataChannelParam), 0, sizeof(DataChannelParam));

    channelParam.type = atoi(cmdData->paras[0]);
    channelParam.port = atoi(cmdData->paras[1]); // The first parameter of indicates the port number
    channelParam.isBlock = atoi(cmdData->paras[2]); // The second parameter of indicates whether to block
    channelParam.bindFd = atoi(cmdData->paras[3]); // The third parameter of indicates whether the cis blocked.

    // Invoke the blocking interface
    sockFd = RunDataChannelAccept(&channelParam);

    // Return the result.
    ret = sprintf_s(cmdData->result, sizeof(cmdData->result), "%s|%s|%d", cmdData->id, cmdData->funcId, sockFd);
    ASSERT_RETURN(ret > 0);
    return SUCCESS;
}

int RpcDataChannelBind(CmdData *cmdData)
{
    int sockFd, ret;
    DataChannelParam channelParam;

    (void)memset_s(cmdData->result, sizeof(cmdData->result), 0, sizeof(cmdData->result));
    (void)memset_s(&channelParam, sizeof(DataChannelParam), 0, sizeof(DataChannelParam));

    channelParam.type = atoi(cmdData->paras[0]);
    channelParam.port = atoi(cmdData->paras[1]); // The first parameter of  indicates the port number
    channelParam.isBlock = atoi(cmdData->paras[2]); // The second parameter of indicates whether to block
    channelParam.bindFd = atoi(cmdData->paras[3]); // The third parameter of indicates whether the cis blocked.

    // Invoke the blocking interface
    sockFd = RunDataChannelBind(&channelParam);

    // Return the result.
    ret = sprintf_s(cmdData->result, sizeof(cmdData->result), "%s|%s|%d|%d", cmdData->id, cmdData->funcId,
        sockFd, channelParam.port);
    ASSERT_RETURN(ret > 0);
    return SUCCESS;
}


int RpcDataChannelConnect(CmdData *cmdData)
{
    int ret, sockFd;
    DataChannelParam channelParam;

    (void)memset_s(cmdData->result, sizeof(cmdData->result), 0, sizeof(cmdData->result));
    (void)memset_s(&channelParam, sizeof(DataChannelParam), 0, sizeof(DataChannelParam));

    channelParam.type = atoi(cmdData->paras[0]);
    channelParam.port = atoi(cmdData->paras[1]); // The first parameter of  indicates the port number.
    channelParam.isBlock = atoi(cmdData->paras[2]); // The second parameter of indicates whether the is blocked

    sockFd = HLT_DataChannelConnect(&channelParam);

    // Return the result.
    ret = sprintf_s(cmdData->result, sizeof(cmdData->result), "%s|%s|%d", cmdData->id, cmdData->funcId, sockFd);
    ASSERT_RETURN(ret > 0);
    return SUCCESS;
}

int RpcTlsRegCallback(CmdData *cmdData)
{
    int ret;
    TlsCallbackType type;
    (void)memset_s(cmdData->result, sizeof(cmdData->result), 0, sizeof(cmdData->result));

    type = atoi(cmdData->paras[0]);
    // Invoke the corresponding function
    ret = HLT_TlsRegCallback(type);

    ret = sprintf_s(cmdData->result, sizeof(cmdData->result), "%s|%s|%d", cmdData->id, cmdData->funcId, ret);
    ASSERT_RETURN(ret > 0);
    return SUCCESS;
}

int RpcTlsGetStatus(CmdData *cmdData)
{
    int ret, sslId;
    uint32_t sslState = 0;
    (void)memset_s(cmdData->result, sizeof(cmdData->result), 0, sizeof(cmdData->result));

    ResList *sslList = GetSslList();
    sslId = atoi(cmdData->paras[0]);
    void *ssl = GetTlsResFromId(sslList, sslId);
    if (ssl != NULL) {
        sslState = ((HITLS_Ctx *)ssl)->state;
    }

    ret = sprintf_s(cmdData->result, sizeof(cmdData->result), "%s|%s|%u", cmdData->id, cmdData->funcId, sslState);
    ASSERT_RETURN(ret > 0);
    return SUCCESS;
}

int RpcTlsGetAlertFlag(CmdData *cmdData)
{
    int ret, sslId;
    ALERT_Info alertInfo = {0};
    (void)memset_s(cmdData->result, sizeof(cmdData->result), 0, sizeof(cmdData->result));

    ResList *sslList = GetSslList();
    sslId = atoi(cmdData->paras[0]);
    void *ssl = GetTlsResFromId(sslList, sslId);
    if (ssl != NULL) {
        ALERT_GetInfo(ssl, &alertInfo);
    }

    ret = sprintf_s(cmdData->result, sizeof(cmdData->result), "%s|%s|%d",
        cmdData->id, cmdData->funcId, alertInfo.flag);
    ASSERT_RETURN(ret > 0);
    return SUCCESS;
}

int RpcTlsGetAlertLevel(CmdData *cmdData)
{
    int ret, sslId;
    ALERT_Info alertInfo = {0};
    (void)memset_s(cmdData->result, sizeof(cmdData->result), 0, sizeof(cmdData->result));

    ResList *sslList = GetSslList();
    sslId = atoi(cmdData->paras[0]);
    void *ssl = GetTlsResFromId(sslList, sslId);
    if (ssl != NULL) {
        ALERT_GetInfo(ssl, &alertInfo);
    }

    ret = sprintf_s(cmdData->result, sizeof(cmdData->result), "%s|%s|%d",
        cmdData->id, cmdData->funcId, alertInfo.level);
    ASSERT_RETURN(ret > 0);
    return SUCCESS;
}

int RpcTlsGetAlertDescription(CmdData *cmdData)
{
    int ret, sslId;
    ALERT_Info alertInfo = {0};
    (void)memset_s(cmdData->result, sizeof(cmdData->result), 0, sizeof(cmdData->result));

    ResList *sslList = GetSslList();
    sslId = atoi(cmdData->paras[0]);
    void *ssl = GetTlsResFromId(sslList, sslId);
    if (ssl != NULL) {
        ALERT_GetInfo(ssl, &alertInfo);
    }

    ret = sprintf_s(cmdData->result, sizeof(cmdData->result), "%s|%s|%d",
        cmdData->id, cmdData->funcId, alertInfo.description);
    ASSERT_RETURN(ret > 0);
    return SUCCESS;
}

int RpcTlsClose(CmdData *cmdData)
{
    int ret, sslId;
    void *ssl = NULL;
    char *endPtr = NULL;

    ASSERT_RETURN(memset_s(cmdData->result, sizeof(cmdData->result), 0, sizeof(cmdData->result)) == EOK);
    ResList *sslList = GetSslList();
    sslId = (int)strtol(cmdData->paras[0], &endPtr, 0);
    ssl = GetTlsResFromId(sslList, sslId);
    ASSERT_RETURN(ssl != NULL);

    ret = HLT_TlsClose(ssl);

    // Return the result
    ret = sprintf_s(cmdData->result, sizeof(cmdData->result), "%s|%s|%d", cmdData->id, cmdData->funcId, ret);
    ASSERT_RETURN(ret > 0);
    return SUCCESS;
}

int RpcFreeResFormSsl(CmdData *cmdData)
{
    int ret, sslId;
    void *ssl = NULL;
    char *endPtr = NULL;

    ASSERT_RETURN(memset_s(cmdData->result, sizeof(cmdData->result), 0, sizeof(cmdData->result)) == EOK);
    ResList *sslList = GetSslList();
    sslId = (int)strtol(cmdData->paras[0], &endPtr, 0);
    ssl = GetTlsResFromId(sslList, sslId);
    ASSERT_RETURN(ssl != NULL);

    ret = HLT_FreeResFromSsl(ssl);

    // Return the result
    ret = sprintf_s(cmdData->result, sizeof(cmdData->result), "%s|%s|%d", cmdData->id, cmdData->funcId, ret);
    ASSERT_RETURN(ret > 0);
    return SUCCESS;
}

int RpcSctpClose(CmdData *cmdData)
{
    int ret, fd;
    char *endPtr = NULL;

    ASSERT_RETURN(memset_s(cmdData->result, sizeof(cmdData->result), 0, sizeof(cmdData->result)) == EOK);
    fd = (int)strtol(cmdData->paras[0], &endPtr, 0);

    SctpClose(fd);

    // Return the result
    ret = sprintf_s(cmdData->result, sizeof(cmdData->result), "%s|%s|%d", cmdData->id, cmdData->funcId, SUCCESS);
    ASSERT_RETURN(ret > 0);
    return SUCCESS;
}

int RpcCloseFd(CmdData *cmdData)
{
    int ret, fd, linkType;
    char *endPtr = NULL;
    (void)memset_s(cmdData->result, sizeof(cmdData->result), 0, sizeof(cmdData->result));

    fd = (int)strtol(cmdData->paras[0], &endPtr, 0);
    linkType = (int)strtol(cmdData->paras[1], &endPtr, 0);

    ret = SUCCESS;
    HLT_CloseFd(fd, linkType);

    ret = sprintf_s(cmdData->result, sizeof(cmdData->result), "%s|%s|%d", cmdData->id, cmdData->funcId, ret);
    ASSERT_RETURN(ret > 0);
    return SUCCESS;
}

int RpcTlsSetMtu(CmdData *cmdData)
{
    int ret, sslId;
    uint16_t mtu;
    void *ssl = NULL;
    char *endPtr = NULL;
    (void)memset_s(cmdData->result, sizeof(cmdData->result), 0, sizeof(cmdData->result));

    ResList *sslList = GetSslList();
    sslId = (int)strtol(cmdData->paras[0], &endPtr, 0);
    mtu = (int)strtol(cmdData->paras[1], &endPtr, 0);
    ssl = GetTlsResFromId(sslList, sslId);
    ASSERT_RETURN(ssl != NULL);

    ret = HLT_TlsSetMtu(ssl, mtu);

    ret = sprintf_s(cmdData->result, sizeof(cmdData->result), "%s|%s|%d", cmdData->id, cmdData->funcId, ret);
    ASSERT_RETURN(ret > 0);
    return SUCCESS;
}

int RpcTlsGetErrorCode(CmdData *cmdData)
{
    int sslId;
    int errorCode;
    void *ssl = NULL;
    char *endPtr = NULL;
    (void)memset_s(cmdData->result, sizeof(cmdData->result), 0, sizeof(cmdData->result));

    ResList *sslList = GetSslList();
    sslId = (int)strtol(cmdData->paras[0], &endPtr, 0);
    ssl = GetTlsResFromId(sslList, sslId);
    ASSERT_RETURN(ssl != NULL);

    errorCode = HLT_TlsGetErrorCode(ssl);

    int ret = sprintf_s(cmdData->result, sizeof(cmdData->result), "%s|%s|%d", cmdData->id, cmdData->funcId, errorCode);
    ASSERT_RETURN(ret > 0);
    return SUCCESS;
}