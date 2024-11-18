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

#include "hitls_x509_local.h"
#include "securec.h"
#include "bsl_obj.h"
#include "bsl_sal.h"
#include "bsl_log_internal.h"
#include "bsl_binlog_id.h"
#include "bsl_log.h"
#include "hitls_pki_errno.h"
#include "crypt_errno.h"
#include "crypt_eal_pkey.h"
#include "bsl_obj_internal.h"
#include "bsl_err_internal.h"
#include "bsl_pem_internal.h"
#include "crypt_encode.h"

int32_t HITLS_X509_ParseTbsRawData(uint8_t *encode, uint32_t encodeLen, uint8_t **tbsRawData, uint32_t *tbsRawDataLen)
{
    uint8_t *temp = encode;
    uint32_t tempLen = encodeLen;
    uint32_t valen;
    // x509
    int32_t ret = BSL_ASN1_DecodeTagLen(BSL_ASN1_TAG_CONSTRUCTED | BSL_ASN1_TAG_SEQUENCE, &temp, &tempLen, &valen);
    if (ret != BSL_SUCCESS) {
        BSL_ERR_PUSH_ERROR(ret);
        return ret;
    }
    uint32_t len = tempLen;
    *tbsRawData = temp;
    // tbs
    ret = BSL_ASN1_DecodeTagLen(BSL_ASN1_TAG_CONSTRUCTED | BSL_ASN1_TAG_SEQUENCE, &temp, &tempLen, &valen);
    if (ret != BSL_SUCCESS) {
        BSL_ERR_PUSH_ERROR(ret);
        return ret;
    }

    *tbsRawDataLen = len - tempLen + valen;
    return ret;
}

int32_t HITLS_X509_ParseSignAlgInfo(BSL_ASN1_Buffer *algId, BSL_ASN1_Buffer *param, HITLS_X509_Asn1AlgId *x509Alg)
{
    int32_t ret;
    BslOidString oidStr = {algId->len, (char *)algId->buff, 0};
    BslCid cid = BSL_OBJ_GetCIDFromOid(&oidStr);
    if (cid == BSL_CID_UNKNOWN) {
        BSL_ERR_PUSH_ERROR(HITLS_X509_ERR_ALG_OID);
        return HITLS_X509_ERR_ALG_OID;
    }
    if (cid == BSL_CID_RSASSAPSS) {
        ret = CRYPT_EAL_ParseRsaPssAlgParam(param, &x509Alg->rsaPssParam);
        if (ret != BSL_SUCCESS) {
            BSL_ERR_PUSH_ERROR(ret);
            return ret;
        }
    }
    x509Alg->algId = cid;
    return HITLS_X509_SUCCESS;
}

static int32_t HITLS_X509_ParseNameNode(BSL_ASN1_Buffer *asn, HITLS_X509_NameNode *node)
{
    uint8_t *temp = asn->buff;
    uint32_t tempLen = asn->len;
    // parse oid
    if (*temp != BSL_ASN1_TAG_OBJECT_ID) {
        BSL_ERR_PUSH_ERROR(HITLS_X509_ERR_NAME_OID);
        return HITLS_X509_ERR_NAME_OID;
    }

    int32_t ret = BSL_ASN1_DecodeItem(&temp, &tempLen, &node->nameType);
    if (ret != BSL_SUCCESS) {
        BSL_ERR_PUSH_ERROR(ret);
        return ret;
    }

    // parse string
    if (*temp != BSL_ASN1_TAG_UTF8STRING && *temp != BSL_ASN1_TAG_PRINTABLESTRING &&
        *temp != BSL_ASN1_TAG_IA5STRING) {
        BSL_ERR_PUSH_ERROR(HITLS_X509_ERR_PARSE_STR);
        return HITLS_X509_ERR_PARSE_STR;
    }

    ret = BSL_ASN1_DecodeItem(&temp, &tempLen, &node->nameValue);
    if (ret != BSL_SUCCESS) {
        BSL_ERR_PUSH_ERROR(ret);
        return ret;
    }
    return ret;
}

int32_t HITLS_X509_ParseListAsnItem(uint32_t layer, BSL_ASN1_Buffer *asn, void *cbParam, BSL_ASN1_List *list)
{
    (void) cbParam;
    int32_t ret = HITLS_X509_SUCCESS;
    HITLS_X509_NameNode *node = BSL_SAL_Calloc(1, sizeof(HITLS_X509_NameNode));
    if (node == NULL) {
        BSL_ERR_PUSH_ERROR(BSL_MALLOC_FAIL);
        return BSL_MALLOC_FAIL;
    }

    if (layer == 1) {
        node->layer = 1;
    } else { // layer == 2
        node->layer = 2;
        ret = HITLS_X509_ParseNameNode(asn, node);
        if (ret != HITLS_X509_SUCCESS) {
            goto ERR;
        }
    }

    ret = BSL_LIST_AddElement(list, node, BSL_LIST_POS_AFTER);
    if (ret != BSL_SUCCESS) {
        goto ERR;
    }
    return ret;
ERR:
    BSL_SAL_Free(node);
    return ret;
}

int32_t HITLS_X509_ParseNameList(BSL_ASN1_Buffer *name, BSL_ASN1_List *list)
{
    uint8_t expTag[] = {BSL_ASN1_TAG_CONSTRUCTED | BSL_ASN1_TAG_SET,
        BSL_ASN1_TAG_CONSTRUCTED | BSL_ASN1_TAG_SEQUENCE};
    BSL_ASN1_DecodeListParam listParam = {2, expTag};
    int32_t ret = BSL_ASN1_DecodeListItem(&listParam, name, &HITLS_X509_ParseListAsnItem, NULL, list);
    if (ret != BSL_SUCCESS) {
        BSL_LIST_DeleteAll(list, NULL);
    }
    return ret;
}

int32_t HITLS_X509_AddListItemDefault(void *item, uint32_t len, BSL_ASN1_List *list)
{
    void *node = BSL_SAL_Malloc(len);
    if (node == NULL) {
        BSL_ERR_PUSH_ERROR(BSL_MALLOC_FAIL);
        return BSL_MALLOC_FAIL;
    }
    (void)memcpy_s(node, len, item, len);
    int32_t ret = BSL_LIST_AddElement(list, node, BSL_LIST_POS_AFTER);
    if (ret != BSL_SUCCESS) {
        BSL_ERR_PUSH_ERROR(ret);
        BSL_SAL_Free(node);
    }
    return ret;
}

int32_t HITLS_X509_ParseTime(BSL_ASN1_Buffer *before, BSL_ASN1_Buffer *after, HITLS_X509_ValidTime *time)
{
    int32_t ret = BSL_ASN1_DecodePrimitiveItem(before, &time->start);
    if (ret != BSL_SUCCESS) {
        BSL_ERR_PUSH_ERROR(ret);
        return ret;
    }
    time->flag |= BSL_TIME_BEFORE_SET;
    if (before->tag == BSL_ASN1_TAG_UTCTIME) {
        time->flag |= BSL_TIME_BEFORE_IS_UTC;
    }
    // crl after time is optional
    if (after->tag != 0) {
        ret = BSL_ASN1_DecodePrimitiveItem(after, &time->end);
        if (ret != BSL_SUCCESS) {
            BSL_ERR_PUSH_ERROR(ret);
            return ret;
        }
        time->flag |= BSL_TIME_AFTER_SET;
        if (after->tag == BSL_ASN1_TAG_UTCTIME) {
            time->flag |= BSL_TIME_AFTER_IS_UTC;
        }
    }
    return ret;
}

static bool X509_CheckIsRsa(uint32_t algId)
{
    switch (algId) {
        case BSL_CID_RSA:
        case BSL_CID_MD5WITHRSA:
        case BSL_CID_SHA1WITHRSA:
        case BSL_CID_SHA224WITHRSAENCRYPTION:
        case BSL_CID_SHA256WITHRSAENCRYPTION:
        case BSL_CID_SHA384WITHRSAENCRYPTION:
        case BSL_CID_SHA512WITHRSAENCRYPTION:
        case BSL_CID_SM3WITHRSAENCRYPTION:
            return true;
        default:
            return false;
    }
}

int32_t HITLS_X509_EncodeSignAlgInfo(HITLS_X509_Asn1AlgId *x509Alg, BSL_ASN1_Buffer *asn)
{
    int32_t ret;
    BslOidString *oidStr = BSL_OBJ_GetOidFromCID(x509Alg->algId);
    if (oidStr == NULL) {
        BSL_ERR_PUSH_ERROR(HITLS_X509_ERR_ALG_OID);
        return HITLS_X509_ERR_ALG_OID;
    }
    BSL_ASN1_Buffer asnArr[2] = {0};
    asnArr[0].buff = (uint8_t *)oidStr->octs;
    asnArr[0].len = oidStr->octetLen;
    asnArr[0].tag = BSL_ASN1_TAG_OBJECT_ID;
    if (x509Alg->algId == BSL_CID_RSASSAPSS) {
        ret = CRYPT_EAL_EncodeRsaPssAlgParam(&(x509Alg->rsaPssParam), &asnArr[1].buff, &asnArr[1].len);
        if (ret != BSL_SUCCESS) {
            BSL_ERR_PUSH_ERROR(ret);
            return ret;
        }
        asnArr[1].tag = BSL_ASN1_TAG_SEQUENCE | BSL_ASN1_TAG_CONSTRUCTED;
    } else if (X509_CheckIsRsa(x509Alg->algId)) {
        asnArr[1].buff = NULL;
        asnArr[1].len = 0;
        asnArr[1].tag = BSL_ASN1_TAG_NULL;
    } else {
        /**
         * RFC5758 sec 3.2
         * When the ecdsa-with-SHA224, ecdsa-with-SHA256, ecdsa-with-SHA384, or
         * ecdsa-with-SHA512 algorithm identifier appears in the algorithm field
         * as an AlgorithmIdentifier, the encoding MUST omit the parameters
         * field.
         */
        asnArr[1].buff = NULL;
        asnArr[1].len = 0;
        asnArr[1].tag = BSL_ASN1_TAG_ANY;
    }
    BSL_ASN1_TemplateItem algTempl[] = {
        {BSL_ASN1_TAG_OBJECT_ID, 0, 0},
        {BSL_ASN1_TAG_ANY, BSL_ASN1_FLAG_OPTIONAL | BSL_ASN1_FLAG_HEADERONLY, 0},
    };
    BSL_ASN1_Template templ = {algTempl, sizeof(algTempl) / sizeof(algTempl[0])};
    // 2: alg + param
    ret = BSL_ASN1_EncodeTemplate(&templ, asnArr, 2, &(asn->buff), &(asn->len));
    BSL_SAL_Free(asnArr[1].buff);
    if (ret != HITLS_X509_SUCCESS) {
        BSL_ERR_PUSH_ERROR(ret);
        return ret;
    }
    asn->tag = BSL_ASN1_TAG_SEQUENCE | BSL_ASN1_TAG_CONSTRUCTED;
    return HITLS_X509_SUCCESS;
}

static int32_t X509_EncodeRdName(BSL_ASN1_List *list, BSL_ASN1_Buffer *asnBuf)
{
    uint32_t maxCount = (BSL_LIST_COUNT(list) - 1) * 2; // 2: layer 1 and layer 2
    BSL_ASN1_Buffer *tmpBuf = BSL_SAL_Calloc(maxCount, sizeof(BSL_ASN1_Buffer));
    if (tmpBuf == NULL) {
        BSL_ERR_PUSH_ERROR(BSL_MALLOC_FAIL);
        return BSL_MALLOC_FAIL;
    }
    uint32_t iter = 0;
    HITLS_X509_NameNode *node = BSL_LIST_GET_NEXT(list);
    while (node != NULL && node->layer != 1) {
        tmpBuf[iter++] = node->nameType;
        tmpBuf[iter++] = node->nameValue;
        node = BSL_LIST_GET_NEXT(list);
    }

    BSL_ASN1_TemplateItem x509RdName[] = {
        {BSL_ASN1_TAG_CONSTRUCTED | BSL_ASN1_TAG_SEQUENCE, 0, 0},
            {BSL_ASN1_TAG_OBJECT_ID, 0, 1},
            {BSL_ASN1_TAG_ANY, 0, 1}
    };
    BSL_ASN1_Template templ = {x509RdName, sizeof(x509RdName) / sizeof(x509RdName[0])};
    int32_t ret = BSL_ASN1_EncodeListItem(BSL_ASN1_TAG_SET, iter / 2, &templ, tmpBuf, iter, asnBuf);
    BSL_SAL_Free(tmpBuf);
    return ret;
}

int32_t HITLS_X509_EncodeNameList(BSL_ASN1_List *list, BSL_ASN1_Buffer *name)
{
    int32_t ret;
    int32_t maxCount = (BSL_LIST_COUNT(list) + 1) / 2; // (count + 1) / 2 : round up, the maximum number of set
    BSL_ASN1_Buffer *asnBuf = BSL_SAL_Calloc(maxCount, sizeof(BSL_ASN1_Buffer));
    if (asnBuf == NULL) {
        BSL_ERR_PUSH_ERROR(BSL_MALLOC_FAIL);
        return BSL_MALLOC_FAIL;
    }
    HITLS_X509_NameNode *node = BSL_LIST_GET_FIRST(list);
    uint32_t iter = 0;
    while (node != NULL) {
        ret = X509_EncodeRdName(list, &asnBuf[iter]);
        if (ret != HITLS_X509_SUCCESS) {
            goto ERR;
        }
        iter++;
        node = BSL_LIST_Curr(list);
    }

    BSL_ASN1_TemplateItem x509Name[] = {
        {BSL_ASN1_TAG_CONSTRUCTED | BSL_ASN1_TAG_SET, 0, 0}
    };
    BSL_ASN1_Template templ = {x509Name, 1};
    ret = BSL_ASN1_EncodeListItem(BSL_ASN1_TAG_SEQUENCE, iter, &templ, asnBuf, iter, name);
ERR:
    for (uint32_t index = 0; index < iter; index++) {
        BSL_SAL_Free(asnBuf[index].buff);
    }
    BSL_SAL_Free(asnBuf);
    return ret;
}

static void X509_GetPemSymbol(bool isCert, BSL_PEM_Symbol *symbol)
{
    if (isCert) {
        symbol->head = BSL_PEM_CERT_BEGIN_STR;
        symbol->tail = BSL_PEM_CERT_END_STR;
    } else {
        symbol->head = BSL_PEM_CRL_BEGIN_STR;
        symbol->tail = BSL_PEM_CRL_END_STR;
    }
}

static int32_t X509_ParseAndAddRes(BSL_Buffer *asn1Buf, X509_ParseFuncCbk *parsefun, HITLS_X509_List *list)
{
    void *res = parsefun->x509New();
    if (res == NULL) {
        BSL_ERR_PUSH_ERROR(BSL_MALLOC_FAIL);
        return BSL_MALLOC_FAIL;
    }
    int32_t ret = parsefun->asn1Parse(&(asn1Buf->data), &(asn1Buf->dataLen), res);
    if (ret != HITLS_X509_SUCCESS) {
        parsefun->x509Free(res);
        BSL_ERR_PUSH_ERROR(ret);
        return ret;
    }
    ret = BSL_LIST_AddElement(list, res, BSL_LIST_POS_AFTER);
    if (ret != BSL_SUCCESS) {
        parsefun->x509Free(res);
        BSL_ERR_PUSH_ERROR(ret);
        return ret;
    }
    return HITLS_X509_SUCCESS;
}

int32_t HITLS_X509_ParseAsn1(BSL_Buffer *encode, X509_ParseFuncCbk *parsefun, HITLS_X509_List *list)
{
    uint8_t *data = encode->data;
    uint32_t dataLen = encode->dataLen;
    while (dataLen > 0) {
        uint32_t elemLen = dataLen;
        int32_t ret = BSL_ASN1_GetCompleteLen(data, &elemLen);
        if (ret != HITLS_X509_SUCCESS) {
            return ret;
        }
        BSL_Buffer asn1Buf = {data, elemLen};
        asn1Buf.data = BSL_SAL_Dump(data, elemLen);
        if (asn1Buf.data == NULL) {
            return BSL_DUMP_FAIL;
        }
        ret = X509_ParseAndAddRes(&asn1Buf, parsefun, list);
        if (ret != HITLS_X509_SUCCESS) {
            BSL_SAL_Free(asn1Buf.data);
            return ret;
        }
        data += elemLen;
        dataLen -= elemLen;
    }
    return HITLS_X509_SUCCESS;
}

int32_t HITLS_X509_ParsePem(BSL_Buffer *encode, bool isCert, X509_ParseFuncCbk *parsefun, HITLS_X509_List *list)
{
    char *nextEncode = (char *)(encode->data);
    uint32_t nextEncodeLen = encode->dataLen;
    BSL_PEM_Symbol symbol = {0};
    X509_GetPemSymbol(isCert, &symbol);
    while (nextEncodeLen > 0) {
        BSL_Buffer asn1Buf = {0};
        int32_t ret = BSL_PEM_ParsePem2Asn1(&nextEncode, &nextEncodeLen, &symbol, &(asn1Buf.data),
            &(asn1Buf.dataLen));
        if (ret != HITLS_X509_SUCCESS) {
            break;
        }
        ret = X509_ParseAndAddRes(&asn1Buf, parsefun, list);
        if (ret != HITLS_X509_SUCCESS) {
            BSL_SAL_Free(asn1Buf.data);
            return ret;
        }
    }
    if (BSL_LIST_COUNT(list) == 0) {
        BSL_ERR_PUSH_ERROR(HITLS_X509_ERR_PARSE_NO_ELEMENT);
        return HITLS_X509_ERR_PARSE_NO_ELEMENT;
    }
    return HITLS_X509_SUCCESS;
}

int32_t HITLS_X509_ParseUnknown(BSL_Buffer *encode, bool isCert, X509_ParseFuncCbk *parsefun,
    HITLS_X509_List *list)
{
    bool isPem = BSL_PEM_IsPemFormat((char *)(encode->data), encode->dataLen);
    if (isPem) {
        return HITLS_X509_ParsePem(encode, isCert, parsefun, list);
    } else {
        return HITLS_X509_ParseAsn1(encode, parsefun, list);
    }
}

int32_t HITLS_X509_ParseX509(int32_t format, BSL_Buffer *encode, bool isCert, X509_ParseFuncCbk *parsefun,
    HITLS_X509_List *list)
{
    switch (format) {
        case BSL_FORMAT_ASN1:
            return HITLS_X509_ParseAsn1(encode, parsefun, list);
        case BSL_FORMAT_PEM:
            return HITLS_X509_ParsePem(encode, isCert, parsefun, list);
        case BSL_FORMAT_UNKNOWN:
            return HITLS_X509_ParseUnknown(encode, isCert, parsefun, list);
        default:
            BSL_ERR_PUSH_ERROR(HITLS_X509_ERR_NOT_SUPPORT_FORMAT);
            return HITLS_X509_ERR_NOT_SUPPORT_FORMAT;
    }
}

static int32_t X509_NodeNameCompare(BSL_ASN1_Buffer *src, BSL_ASN1_Buffer *dest)
{
    if (src->tag != dest->tag) {
        return 1;
    }
    if (src->len != dest->len) {
        return 1;
    }
    return memcmp(src->buff, dest->buff, dest->len);
}

static int32_t X509_NodeNameCaseCompare(BSL_ASN1_Buffer *src, BSL_ASN1_Buffer *dest)
{
    if ((src->tag == BSL_ASN1_TAG_UTF8STRING || src->tag == BSL_ASN1_TAG_PRINTABLESTRING) &&
        (dest->tag == BSL_ASN1_TAG_UTF8STRING || dest->tag == BSL_ASN1_TAG_PRINTABLESTRING)) {
        if (src->len != dest->len) {
            return 1;
        }
        for (uint32_t i = 0; i < src->len; i++) {
            if (src->buff[i] == dest->buff[i]) {
                continue;
            }
            if ('a' <= src->buff[i] && src->buff[i] <= 'z' && src->buff[i] - dest->buff[i] == 32) {
                continue;
            }
            if ('a' <= dest->buff[i] && dest->buff[i] <= 'z' && dest->buff[i] - src->buff[i] == 32) {
                continue;
            }
            return 1;
        }
        return 0;
    }
    return 1;
}

static int32_t X509_NodeNameValueCompare(BSL_ASN1_Buffer *src, BSL_ASN1_Buffer *dest)
{
    // quick comparison
    if (X509_NodeNameCompare(src, dest) == 0) {
        return 0;
    }
    return X509_NodeNameCaseCompare(src, dest);
}


static int32_t X509_NodeCompare(BSL_ASN1_Buffer *buffOri, BSL_ASN1_Buffer *buff)
{
    if (buffOri->tag != buff->tag) {
        return 1;
    }
    if (buffOri->len != buff->len) {
        return 1;
    }
    return memcmp(buffOri->buff, buff->buff, buff->len);
}

int32_t HITLS_X509_CmpNameNode(BSL_ASN1_List *nameOri, BSL_ASN1_List *name)
{
    HITLS_X509_NameNode *nodeOri = BSL_LIST_GET_FIRST(nameOri);
    HITLS_X509_NameNode *node = BSL_LIST_GET_FIRST(name);
    while (nodeOri != NULL || node != NULL) {
        if (nodeOri == NULL || node == NULL) {
            return 1;
        }
        if (X509_NodeCompare(&nodeOri->nameType, &node->nameType) != 0) {
            return 1;
        }
        if (nodeOri->layer != node->layer) {
            return 1;
        }
        if (X509_NodeNameValueCompare(&nodeOri->nameValue, &node->nameValue) != 0) {
            return 1;
        }
        nodeOri = (HITLS_X509_NameNode *)BSL_LIST_GET_NEXT(nameOri);
        node = (HITLS_X509_NameNode *)BSL_LIST_GET_NEXT(name);
    }
    return 0;
}

/**
 * RFC 4055 section 3.3
 *
 * The key is identified by the id-RSASSA-PSS signature algorithm identifier, but the parameters field is
 * absent.  In this case no parameter validation is needed.
 * The key is identified by the id-RSASSA-PSS signature algorithm identifier and the parameters are present.
 * In this case all parameters in the signature structure algorithm identifier MUST match the parameters
 * in the key structure algorithm identifier except the saltLength field.  The saltLength field in the
 * signature parameters MUST be greater or equal to that in the key parameters field.
 */
static int32_t X509_CheckPssParam(CRYPT_EAL_PkeyCtx *key, int32_t algId, const CRYPT_RSA_PssPara *rsaPssParam)
{
    if (algId != BSL_CID_RSASSAPSS) {
        BSL_ERR_PUSH_ERROR(HITLS_X509_ERR_MD_NOT_MATCH);
        return HITLS_X509_ERR_MD_NOT_MATCH;
    }
    uint32_t mdId;
    int32_t ret = CRYPT_EAL_PkeyCtrl(key, CRYPT_CTRL_GET_RSA_MD, &mdId, sizeof(uint32_t));
    if (ret != CRYPT_SUCCESS) {
        BSL_ERR_PUSH_ERROR(ret);
        return ret;
    }
    if (mdId == BSL_CID_UNKNOWN) {
        /* If the hash algorithm is unknown, no pss parameter is specified in key. */
        return HITLS_X509_SUCCESS;
    }
    if (mdId != rsaPssParam->mdId) {
        BSL_ERR_PUSH_ERROR(HITLS_X509_ERR_MD_NOT_MATCH);
        return HITLS_X509_ERR_MD_NOT_MATCH;
    }
    uint32_t mgfId;
    ret = CRYPT_EAL_PkeyCtrl(key, CRYPT_CTRL_GET_RSA_MGF, &mgfId, sizeof(uint32_t));
    if (ret != CRYPT_SUCCESS) {
        BSL_ERR_PUSH_ERROR(ret);
        return ret;
    }
    if (mgfId != rsaPssParam->mgfId) {
        BSL_ERR_PUSH_ERROR(HITLS_X509_ERR_MGF_NOT_MATCH);
        return HITLS_X509_ERR_MGF_NOT_MATCH;
    }
    int32_t saltLen;
    ret = CRYPT_EAL_PkeyCtrl(key, CRYPT_CTRL_GET_RSA_SALT, &saltLen, sizeof(int32_t));
    if (ret != CRYPT_SUCCESS) {
        BSL_ERR_PUSH_ERROR(ret);
        return ret;
    }
    if (rsaPssParam->saltLen < saltLen) {
        BSL_ERR_PUSH_ERROR(HITLS_X509_ERR_PSS_SALTLEN);
        return HITLS_X509_ERR_PSS_SALTLEN;
    }
    return HITLS_X509_SUCCESS;
}

int32_t HITLS_X509_CheckAlg(CRYPT_EAL_PkeyCtx *pubkey, HITLS_X509_Asn1AlgId *subAlg)
{
    uint32_t pubKeyId = CRYPT_EAL_PkeyGetId(pubkey);
    if (pubKeyId == BSL_CID_UNKNOWN) {
        BSL_ERR_PUSH_ERROR(HITLS_X509_ERR_VFY_GET_SIGNID);
        return HITLS_X509_ERR_VFY_GET_SIGNID;
    }
    if (pubKeyId == CRYPT_PKEY_RSA) {
        CRYPT_RsaPadType pad;
        int32_t ret = CRYPT_EAL_PkeyCtrl(pubkey, CRYPT_CTRL_GET_RSA_PADDING, &pad, sizeof(pad));
        if (ret != CRYPT_SUCCESS) {
            BSL_ERR_PUSH_ERROR(ret);
            return ret;
        }
        if (pad == CRYPT_PKEY_EMSA_PSS) {
            return X509_CheckPssParam(pubkey, subAlg->algId, &subAlg->rsaPssParam);
        }
    }
    BslCid subSignAlg = BSL_OBJ_GetAsymIdFromSignId(subAlg->algId);
    if (subSignAlg == BSL_CID_UNKNOWN) {
        BSL_ERR_PUSH_ERROR(HITLS_X509_ERR_VFY_GET_SIGNID);
        return HITLS_X509_ERR_VFY_GET_SIGNID;
    }
    if (pubKeyId != subSignAlg) {
        BSL_ERR_PUSH_ERROR(HITLS_X509_ERR_VFY_SIGNALG_NOT_MATCH);
        return HITLS_X509_ERR_VFY_SIGNALG_NOT_MATCH;
    }
    return HITLS_X509_SUCCESS;
}

int32_t HITLS_X509_SignAsn1Data(CRYPT_EAL_PkeyCtx *priv, CRYPT_MD_AlgId mdId,
    BSL_ASN1_Buffer *asn1Buff, BSL_Buffer *rawSignBuff, BSL_ASN1_BitString *sign)
{
    BSL_ASN1_TemplateItem templItem = {BSL_ASN1_TAG_CONSTRUCTED | BSL_ASN1_TAG_SEQUENCE, 0, 0};
    BSL_ASN1_Template templ = {&templItem, 1};

    int32_t ret = BSL_ASN1_EncodeTemplate(&templ, asn1Buff, 1, &rawSignBuff->data, &rawSignBuff->dataLen);
    if (ret != HITLS_X509_SUCCESS) {
        BSL_ERR_PUSH_ERROR(ret);
        return ret;
    }

    sign->len = CRYPT_EAL_PkeyGetSignLen(priv);
    sign->buff = (uint8_t *)BSL_SAL_Malloc(sign->len);
    if (sign->buff == NULL) {
        BSL_SAL_FREE(rawSignBuff->data);
        rawSignBuff->dataLen = 0;
        BSL_ERR_PUSH_ERROR(BSL_MALLOC_FAIL);
        return BSL_MALLOC_FAIL;
    }
    ret = CRYPT_EAL_PkeySign(priv, mdId, rawSignBuff->data, rawSignBuff->dataLen, sign->buff, &sign->len);
    if (ret != CRYPT_SUCCESS) {
        BSL_ERR_PUSH_ERROR(ret);
        BSL_SAL_FREE(sign->buff);
        BSL_SAL_FREE(rawSignBuff->data);
        rawSignBuff->dataLen = 0;
    }

    return ret;
}

static uint32_t X509_GetHashId(HITLS_X509_Asn1AlgId *alg)
{
    uint32_t hashId = BSL_OBJ_GetHashIdFromSignId(alg->algId);
    if (hashId != BSL_CID_UNKNOWN) {
        return hashId;
    }
    if (alg->algId == BSL_CID_RSASSAPSS) {
        return alg->rsaPssParam.mdId;
    }
    return BSL_CID_UNKNOWN;
}

static int32_t X509_CtrlAlgInfo(const CRYPT_EAL_PkeyCtx *pubKey, uint32_t hashId, HITLS_X509_Asn1AlgId *alg)
{
    switch (alg->algId) {
        case BSL_CID_MD5WITHRSA:
        case BSL_CID_SHA1WITHRSA:
        case BSL_CID_SHA224WITHRSAENCRYPTION:
        case BSL_CID_SHA256WITHRSAENCRYPTION:
        case BSL_CID_SHA384WITHRSAENCRYPTION:
        case BSL_CID_SHA512WITHRSAENCRYPTION:
        case BSL_CID_SM3WITHRSAENCRYPTION:
            {
                CRYPT_RSA_PkcsV15Para pkcs15Para = {hashId};
                return CRYPT_EAL_PkeyCtrl((CRYPT_EAL_PkeyCtx *)(uintptr_t)pubKey, CRYPT_CTRL_SET_RSA_EMSA_PKCSV15,
                    &pkcs15Para, sizeof(CRYPT_RSA_PkcsV15Para));
            }
        case BSL_CID_RSASSAPSS:
            return CRYPT_EAL_PkeyCtrl((CRYPT_EAL_PkeyCtx *)(uintptr_t)pubKey, CRYPT_CTRL_SET_RSA_EMSA_PSS,
                                      &alg->rsaPssParam, sizeof(CRYPT_RSA_PssPara));
        default:
            return HITLS_X509_SUCCESS;
        }
}

int32_t HITLS_X509_CheckSignature(const CRYPT_EAL_PkeyCtx *pubKey, uint8_t *rawData, uint32_t rawDataLen,
    HITLS_X509_Asn1AlgId *alg, BSL_ASN1_BitString *signature)
{
    uint32_t hashId = X509_GetHashId(alg);
    if (hashId == BSL_CID_UNKNOWN) {
        BSL_ERR_PUSH_ERROR(HITLS_X509_ERR_VFY_GET_HASHID);
        return HITLS_X509_ERR_VFY_GET_HASHID;
    }
    CRYPT_EAL_PkeyCtx *verifyPubKey = CRYPT_EAL_PkeyDupCtx(pubKey);
    if (verifyPubKey == NULL) {
        BSL_ERR_PUSH_ERROR(HITLS_X509_ERR_VFY_DUP_PUBKEY);
        return HITLS_X509_ERR_VFY_DUP_PUBKEY;
    }
    int32_t ret = X509_CtrlAlgInfo(verifyPubKey, hashId, alg);
    if (ret != HITLS_X509_SUCCESS) {
        CRYPT_EAL_PkeyFreeCtx(verifyPubKey);
        BSL_ERR_PUSH_ERROR(ret);
        return ret;
    }
    ret = CRYPT_EAL_PkeyVerify(verifyPubKey, hashId, rawData, rawDataLen, signature->buff, signature->len);
    CRYPT_EAL_PkeyFreeCtx(verifyPubKey);
    if (ret != CRYPT_SUCCESS) {
        BSL_ERR_PUSH_ERROR(ret);
    }
    return ret;
}

int32_t HITLS_X509_CheckAki(HITLS_X509_Ext *issueExt, HITLS_X509_Ext *subjectExt, BSL_ASN1_List *subName,
    BSL_ASN1_Buffer *serialNum)
{
    HITLS_X509_ExtAki aki = {0};
    HITLS_X509_ExtSki ski = {0};
    int32_t ret = X509_ExtCtrl(issueExt, HITLS_X509_EXT_GET_SKI, (void *)&ski, sizeof(HITLS_X509_ExtSki));
    if (ret != HITLS_X509_SUCCESS && ret != HITLS_X509_ERR_EXT_NOT_FOUND) {
        BSL_ERR_PUSH_ERROR(ret);
        return ret;
    }
    if (ret == HITLS_X509_ERR_EXT_NOT_FOUND) {
        return HITLS_X509_SUCCESS;
    }
    ret = X509_ExtCtrl(subjectExt, HITLS_X509_EXT_GET_AKI, (void *)&aki, sizeof(HITLS_X509_ExtAki));
    if (ret != HITLS_X509_SUCCESS && ret != HITLS_X509_ERR_EXT_NOT_FOUND) {
        BSL_ERR_PUSH_ERROR(ret);
        return ret;
    }
    if (ret == HITLS_X509_ERR_EXT_NOT_FOUND) {
        return HITLS_X509_SUCCESS;
    }
    if (ski.kid.dataLen != aki.kid.dataLen || memcmp(ski.kid.data, aki.kid.data, ski.kid.dataLen) != 0) {
        HITLS_X509_ClearAuthorityKeyId(&aki);
        return HITLS_X509_ERR_VFY_AKI_SKI_NOT_MATCH;
    }
    if (aki.issuerName != NULL) {
        ret = HITLS_X509_CmpNameNode(aki.issuerName, subName);
        HITLS_X509_ClearAuthorityKeyId(&aki);
        if (ret != 0) {
            return HITLS_X509_ERR_VFY_AKI_SKI_NOT_MATCH;
        }
    }
    if (aki.serialNum.dataLen != 0 && aki.serialNum.data != NULL) {
        if (aki.serialNum.dataLen != serialNum->len ||
            memcmp(aki.serialNum.data, serialNum->buff, aki.serialNum.dataLen) != 0) {
            return HITLS_X509_ERR_VFY_AKI_SKI_NOT_MATCH;
        }
    }

    return HITLS_X509_SUCCESS;
}

static int32_t X509_SetRsaPssDefaultParam(CRYPT_EAL_PkeyCtx *prvKey, int32_t mdId, HITLS_X509_Asn1AlgId *signAlgId)
{
    int32_t currentHash;
    int32_t ret = CRYPT_EAL_PkeyCtrl(prvKey, CRYPT_CTRL_GET_RSA_MD, &currentHash, sizeof(int32_t));
    if (ret != CRYPT_SUCCESS) {
        BSL_ERR_PUSH_ERROR(ret);
        return ret;
    }
    if (currentHash == BSL_CID_UNKNOWN) {
        signAlgId->algId = BSL_CID_RSASSAPSS;
        signAlgId->rsaPssParam.mdId = mdId;
        signAlgId->rsaPssParam.mgfId = mdId;
        signAlgId->rsaPssParam.saltLen = 20; // 20: default saltLen
        return CRYPT_EAL_PkeyCtrl(prvKey, CRYPT_CTRL_SET_RSA_EMSA_PSS, (void *)&signAlgId->rsaPssParam,
            sizeof(CRYPT_RSA_PssPara));
    }

    if (currentHash != mdId) {
        BSL_ERR_PUSH_ERROR(HITLS_X509_ERR_MD_NOT_MATCH);
        return HITLS_X509_ERR_MD_NOT_MATCH;
    }
    int32_t currentMgfId;
    ret = CRYPT_EAL_PkeyCtrl(prvKey, CRYPT_CTRL_GET_RSA_MGF, &currentMgfId, sizeof(int32_t));
    if (ret != CRYPT_SUCCESS) {
        BSL_ERR_PUSH_ERROR(ret);
        return ret;
    }
    int32_t saltLen;
    ret = CRYPT_EAL_PkeyCtrl(prvKey, CRYPT_CTRL_GET_RSA_SALT, &saltLen, sizeof(int32_t));
    if (ret != CRYPT_SUCCESS) {
        BSL_ERR_PUSH_ERROR(ret);
        return ret;
    }
    signAlgId->algId = BSL_CID_RSASSAPSS;
    signAlgId->rsaPssParam.mdId = currentHash;
    signAlgId->rsaPssParam.mgfId = currentMgfId;
    signAlgId->rsaPssParam.saltLen = saltLen;
    return ret;
}

static int32_t X509_SetRsaPssParam(CRYPT_EAL_PkeyCtx *prvKey, int32_t mdId, const HITLS_X509_SignAlgParam *algParam,
    bool checkKeyParam, HITLS_X509_Asn1AlgId *signAlgId)
{
    if ((int32_t)algParam->rsaPss.mdId != mdId) {
        BSL_ERR_PUSH_ERROR(HITLS_X509_ERR_MD_NOT_MATCH);
        return HITLS_X509_ERR_MD_NOT_MATCH;
    }

    if (checkKeyParam) {
        int32_t ret = X509_CheckPssParam(prvKey, algParam->algId, &algParam->rsaPss);
        if (ret != HITLS_X509_SUCCESS) {
            return ret;
        }
    }

    signAlgId->algId = BSL_CID_RSASSAPSS;
    signAlgId->rsaPssParam = algParam->rsaPss;
    return CRYPT_EAL_PkeyCtrl(prvKey, CRYPT_CTRL_SET_RSA_EMSA_PSS, (void *)&algParam->rsaPss,
        sizeof(CRYPT_RSA_PssPara));
}

static int32_t X509_SetRsaPkcsParam(CRYPT_EAL_PkeyCtx *prvKey, int32_t mdId, bool setPadding)
{
    if (setPadding) {
        CRYPT_RsaPadType pad = CRYPT_PKEY_EMSA_PKCSV15;
        int32_t ret = CRYPT_EAL_PkeyCtrl(prvKey, CRYPT_CTRL_SET_RSA_PADDING, &pad, sizeof(CRYPT_RsaPadType));
        if (ret != CRYPT_SUCCESS) {
            BSL_ERR_PUSH_ERROR(ret);
            return ret;
        }
    }
    CRYPT_RSA_PkcsV15Para pkcs15Param = {mdId};
    return CRYPT_EAL_PkeyCtrl(prvKey, CRYPT_CTRL_SET_RSA_EMSA_PKCSV15, &pkcs15Param, sizeof(CRYPT_RSA_PkcsV15Para));
}

int32_t X509_SetRsaSignParam(CRYPT_EAL_PkeyCtx *prvKey, int32_t mdId, const HITLS_X509_SignAlgParam *algParam,
    HITLS_X509_Asn1AlgId *signAlgId)
{
    CRYPT_RsaPadType pad;
    int32_t ret = CRYPT_EAL_PkeyCtrl(prvKey, CRYPT_CTRL_GET_RSA_PADDING, &pad, sizeof(CRYPT_RsaPadType));
    if (ret != CRYPT_SUCCESS) {
        BSL_ERR_PUSH_ERROR(ret);
        return ret;
    }

    switch (pad) {
        case CRYPT_PKEY_EMSA_PSS:
            if (algParam != NULL) {
                return X509_SetRsaPssParam(prvKey, mdId, algParam, true, signAlgId);
            } else {
                return X509_SetRsaPssDefaultParam(prvKey, mdId, signAlgId);
            }
        case CRYPT_PKEY_EMSA_PKCSV15:
            if (algParam != NULL) {
                BSL_ERR_PUSH_ERROR(HITLS_X509_ERR_SIGN_PARAM);
                return HITLS_X509_ERR_SIGN_PARAM;
            }
            ret = X509_SetRsaPkcsParam(prvKey, mdId, false);
            break;
        default:
            if (algParam != NULL) {
                return X509_SetRsaPssParam(prvKey, mdId, algParam, false, signAlgId);
            } else {
                ret = X509_SetRsaPkcsParam(prvKey, mdId, true);
            }
            break;
    }
    if (ret != HITLS_X509_SUCCESS) {
        BSL_ERR_PUSH_ERROR(ret);
        return ret;
    }
    signAlgId->algId = BSL_OBJ_GetSignIdFromHashAndAsymId(BSL_CID_RSA, mdId);
    if (signAlgId->algId == BSL_CID_UNKNOWN) {
        BSL_ERR_PUSH_ERROR(HITLS_X509_ERR_ENCODE_SIGNID);
        return HITLS_X509_ERR_ENCODE_SIGNID;
    }
    return HITLS_X509_SUCCESS;
}

int32_t HITLS_X509_Sign(uint32_t mdId, const CRYPT_EAL_PkeyCtx *prvKey, const HITLS_X509_SignAlgParam *algParam,
    void *obj, HITLS_X509_SignCb signCb)
{
    if (!X509_IsValidHashAlg(mdId)) {
        BSL_ERR_PUSH_ERROR(HITLS_X509_ERR_HASHID);
        return HITLS_X509_ERR_HASHID;
    }

    CRYPT_PKEY_AlgId keyAlgId = CRYPT_EAL_PkeyGetId(prvKey);
    if (keyAlgId == CRYPT_PKEY_MAX) {
        BSL_ERR_PUSH_ERROR(HITLS_X509_ERR_CERT_SIGN_ALG);
        return HITLS_X509_ERR_CERT_SIGN_ALG;
    }
    int32_t ret;
    CRYPT_EAL_PkeyCtx *signKey = (CRYPT_EAL_PkeyCtx *)(uintptr_t)prvKey;
    HITLS_X509_Asn1AlgId signAlgId = {0};
    switch (keyAlgId) {
        case CRYPT_PKEY_RSA:
            signKey = CRYPT_EAL_PkeyDupCtx(prvKey);
            if (signKey == NULL) {
                BSL_ERR_PUSH_ERROR(HITLS_X509_ERR_VFY_DUP_PUBKEY);
                return HITLS_X509_ERR_VFY_DUP_PUBKEY;
            }
            ret = X509_SetRsaSignParam(signKey, mdId, algParam, &signAlgId);
            if (ret != HITLS_X509_SUCCESS) {
                CRYPT_EAL_PkeyFreeCtx(signKey);
                BSL_ERR_PUSH_ERROR(ret);
                return ret;
            }
            break;
        case CRYPT_PKEY_ECDSA:
        case CRYPT_PKEY_SM2:
            signAlgId.algId = BSL_OBJ_GetSignIdFromHashAndAsymId((BslCid)keyAlgId, (BslCid)mdId);
            if (signAlgId.algId == BSL_CID_UNKNOWN) {
                BSL_ERR_PUSH_ERROR(HITLS_X509_ERR_ENCODE_SIGNID);
                return HITLS_X509_ERR_ENCODE_SIGNID;
            }
            break;
        default:
            BSL_ERR_PUSH_ERROR(HITLS_X509_ERR_CERT_SIGN_ALG);
            return HITLS_X509_ERR_CERT_SIGN_ALG;
    }

    ret = signCb(mdId, signKey, &signAlgId, obj);
    if (keyAlgId == CRYPT_PKEY_RSA) {
        CRYPT_EAL_PkeyFreeCtx(signKey);
    }
    return ret;
}