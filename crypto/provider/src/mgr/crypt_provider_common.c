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

#ifdef HITLS_CRYPTO_PROVIDER

#include "crypt_provider.h"
#include "bsl_list.h"
#include "crypt_provider_local.h"
#include "crypt_errno.h"
#include "string.h"
#include "bsl_err_internal.h"
#include "crypt_entropy.h"

static CRYPT_EAL_LibCtx *g_libCtx = NULL;

int32_t CRYPT_EAL_ProviderGetFuncsFrom(CRYPT_EAL_LibCtx *libCtx, int32_t operaId, int32_t algId,
    const char *attribute, const CRYPT_EAL_Func **funcs, void **provCtx)
{
    if (funcs == NULL) {
        BSL_ERR_PUSH_ERROR(CRYPT_NULL_INPUT);
        return CRYPT_NULL_INPUT;
    }
    CRYPT_EAL_LibCtx *localCtx = libCtx;
    if (localCtx == NULL) {
        localCtx = g_libCtx;
    }

    if (localCtx == NULL) {
        BSL_ERR_PUSH_ERROR(CRYPT_NULL_INPUT);
        return CRYPT_NULL_INPUT;
    }
    if (attribute != NULL) {
        if (strlen(attribute) > (INT32_MAX/2)) {
            BSL_ERR_PUSH_ERROR(CRYPT_NULL_INPUT);
            return CRYPT_NULL_INPUT;
        }
    }

    return CRYPT_EAL_CompareAlgAndAttr(localCtx, operaId, algId, attribute, funcs, provCtx);
}

int32_t CRYPT_EAL_ProvMgrCtrl(CRYPT_EAL_ProvMgrCtx *ctx, int32_t cmd, void *val, uint32_t valLen)
{
    (void) valLen;
    if (ctx == NULL || val == NULL) {
        BSL_ERR_PUSH_ERROR(CRYPT_INVALID_ARG);
        return CRYPT_INVALID_ARG;
    }
    switch (cmd) {
        case CRYPT_EAL_MGR_GETSEEDCTX:
            *(void **) val = ctx->seedCtx;
            return CRYPT_SUCCESS;
        case CRYPT_EAL_MGR_GETLIBCTX:
            *(void **) val = ctx->libCtx;
            return CRYPT_SUCCESS;
        default:
            BSL_ERR_PUSH_ERROR(CRYPT_PROVIDER_NOT_FOUND);
            return CRYPT_PROVIDER_NOT_FOUND;
    }
}

// Function to get provider methods
int32_t CRYPT_EAL_InitProviderMethod(CRYPT_EAL_ProvMgrCtx *ctx, BSL_Param *param,
    CRYPT_EAL_ImplProviderInit providerInit)
{
    int32_t ret;
    CRYPT_RandSeedMethod meth = {0};
    void *seedCtx = NULL;
    // The implementer of provider may not use the default entropy source
    (void)EAL_SetDefaultEntropyMeth(&meth, &seedCtx);
    ctx->seedCtx = seedCtx;
    // Construct input method structure array
    CRYPT_EAL_Func capFuncs[] = {
        {CRYPT_EAL_CAP_GETENTROPY, meth.getEntropy},
        {CRYPT_EAL_CAP_CLEANENTROPY, meth.cleanEntropy},
        {CRYPT_EAL_CAP_GETNONCE, meth.getNonce},
        {CRYPT_EAL_CAP_CLEANNONCE, meth.cleanNonce},
        {CRYPT_EAL_CAP_MGRCTXCTRL, CRYPT_EAL_ProvMgrCtrl},
        CRYPT_EAL_FUNC_END  // End marker
    };

    CRYPT_EAL_Func *outFuncs = NULL;
    // Call CRYPT_EAL_ImplProviderInit to get methods
    ret = providerInit(ctx, param, capFuncs, &outFuncs, &ctx->provCtx);
    if (ret != CRYPT_SUCCESS) {
        BSL_ERR_PUSH_ERROR(ret);
        return ret;
    }
    if (outFuncs == NULL) {
        BSL_ERR_PUSH_ERROR(CRYPT_PROVIDER_ERR_UNEXPECTED_IMPL);
        return CRYPT_PROVIDER_ERR_UNEXPECTED_IMPL;
    }
    // Mount function addresses to corresponding positions in mgr according to method definition
    for (int i = 0; outFuncs[i].id != 0; i++) {
        switch (outFuncs[i].id) {
            case CRYPT_EAL_PROVCB_FREE:
                ctx->provFreeCb = (CRYPT_EAL_ProvFreeCb)outFuncs[i].func;
                break;
            case CRYPT_EAL_PROVCB_QUERY:
                ctx->provQueryCb = (CRYPT_EAL_ProvQueryCb)outFuncs[i].func;
                break;
            case CRYPT_EAL_PROVCB_CTRL:
                ctx->provCtrlCb = (CRYPT_EAL_ProvCtrlCb)outFuncs[i].func;
                break;
            default:
                break;
        }
    }
    if (ctx->provQueryCb == NULL) {
        if (ctx->provFreeCb != NULL) {
            ctx->provFreeCb(ctx->provCtx);
            ctx->provCtx = NULL;
        }
        BSL_ERR_PUSH_ERROR(CRYPT_PROVIDER_ERR_IMPL_NULL);
        return CRYPT_PROVIDER_ERR_IMPL_NULL;
    }

    return CRYPT_SUCCESS;
}

CRYPT_EAL_LibCtx *CRYPT_EAL_LibCtxNewInternal()
{
    CRYPT_EAL_LibCtx *libCtx = (CRYPT_EAL_LibCtx *)BSL_SAL_Calloc(1, sizeof(CRYPT_EAL_LibCtx));
    if (libCtx == NULL) {
        BSL_ERR_PUSH_ERROR(BSL_MALLOC_FAIL);
        return NULL;
    }

    // Initialize providers list
    libCtx->providers = BSL_LIST_New(sizeof(struct EAL_ProviderMgrCtx *));
    if (libCtx->providers == NULL) {
        goto ERR;
    }

    // Initialize thread lock
    int32_t ret = BSL_SAL_ThreadLockNew(&libCtx->lock);
    if (ret != BSL_SUCCESS) {
        BSL_LIST_FREE(libCtx->providers, NULL);
        goto ERR;
    }

    return libCtx;
ERR:
    BSL_SAL_Free(libCtx);
    libCtx = NULL;
    return NULL;
}

void EalFreeProviderMgrCtx(void *data)
{
    if (data == NULL) {
        return;
    }
    CRYPT_EAL_ProvMgrCtx *mgrCtx = (CRYPT_EAL_ProvMgrCtx *)data;
    if (mgrCtx->provFreeCb != NULL) {
        mgrCtx->provFreeCb(mgrCtx->provCtx);
    }

    BSL_SAL_ReferencesFree(&mgrCtx->ref);
    BSL_SAL_Free(mgrCtx);
}

int32_t CRYPT_EAL_LoadPreDefinedProvider(CRYPT_EAL_LibCtx *libCtx)
{
    CRYPT_EAL_ProvMgrCtx *mgrCtx = (CRYPT_EAL_ProvMgrCtx *)BSL_SAL_Calloc(1, sizeof(CRYPT_EAL_ProvMgrCtx));
    if (mgrCtx == NULL) {
        BSL_ERR_PUSH_ERROR(CRYPT_MEM_ALLOC_FAIL);
        return CRYPT_MEM_ALLOC_FAIL;
    }
    int32_t ret = BSL_SAL_ReferencesInit(&mgrCtx->ref);
    if (ret != BSL_SUCCESS) {
        BSL_SAL_Free(mgrCtx);
        BSL_ERR_PUSH_ERROR(ret);
        return ret;
    }

    ret = BSL_LIST_AddElement(libCtx->providers, mgrCtx, BSL_LIST_POS_END);
    if (ret != BSL_SUCCESS) {
        BSL_SAL_ReferencesFree(&mgrCtx->ref);
        BSL_SAL_Free(mgrCtx);
        return ret;
    }
    mgrCtx->libCtx = libCtx;
    ret = CRYPT_EAL_InitProviderMethod(mgrCtx, NULL, CRYPT_EAL_DefaultProvInit);
    if (ret != BSL_SUCCESS) {
        BSL_ERR_PUSH_ERROR(ret);
        BSL_LIST_DeleteAll(libCtx->providers, EalFreeProviderMgrCtx);
    }

    return ret;
}

int32_t CRYPT_EAL_InitPreDefinedProviders()
{
    int32_t ret;
    CRYPT_EAL_LibCtx *libCtx = CRYPT_EAL_LibCtxNewInternal();
    if (libCtx == NULL) {
        BSL_ERR_PUSH_ERROR(BSL_MALLOC_FAIL);
        return BSL_MALLOC_FAIL;
    }
    ret = CRYPT_EAL_LoadPreDefinedProvider(libCtx);
    if (ret != CRYPT_SUCCESS) {
        BSL_LIST_FREE(libCtx->providers, NULL);
        BSL_SAL_ThreadLockFree(libCtx->lock);
        BSL_SAL_FREE(libCtx);
        return ret;
    }
    g_libCtx = libCtx;
    return ret;
}

void CRYPT_EAL_FreePreDefinedProviders()
{
    CRYPT_EAL_LibCtx *libCtx = g_libCtx;
    if (libCtx == NULL) {
        return;
    }
    // Free the providers list and each EAL_ProviderMgrCtx in it
    if (libCtx->providers != NULL) {
        BSL_LIST_FREE(libCtx->providers, EalFreeProviderMgrCtx);
    }

    // Free thread lock
    if (libCtx->lock != NULL) {
        BSL_SAL_ThreadLockFree(libCtx->lock);
    }

    // Free the libctx structure itself
    BSL_SAL_Free(libCtx);
    g_libCtx = NULL;
}

#endif /* HITLS_CRYPTO_PROVIDER */