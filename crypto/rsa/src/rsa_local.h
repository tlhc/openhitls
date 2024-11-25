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

#ifndef RSA_LOCAL_H
#define RSA_LOCAL_H

#include "hitls_build.h"
#ifdef HITLS_CRYPTO_RSA

#include "crypt_rsa.h"
#include "crypt_bn.h"
#include "crypt_local_types.h"
#include "crypt_types.h"
#include "sal_atomic.h"

#ifdef __cplusplus
extern "C" {
#endif /* __cpluscplus */

#define HASH_MAX_MDSIZE  (64)

typedef struct RSA_BlindSt {
    BN_BigNum *a;
    BN_BigNum *ai;
} RSA_Blind;

typedef struct {
    BN_BigNum *n;     // pub key n needed for no padding
    BN_BigNum *d;     // private key d needed for asn encoding
    BN_BigNum *p;     // prime factor p
    BN_BigNum *q;     // prime factor q
    BN_BigNum *dP;    // exponent dP for CRT
    BN_BigNum *dQ;    // exponent dQ for CRT
    BN_BigNum *qInv;  // CRT coefficient qInv
    BN_BigNum *e;     // public key e
} CRYPT_RSA_PrvKey;

typedef struct {
    BN_BigNum *n;  // modulo Value - converted.Not in char
    BN_BigNum *e;  // Exponent Value -converted.Not in char

    // Montgomery pre-calculation cache
    BN_Mont *mont;
} CRYPT_RSA_PubKey;

struct RSA_Para {
    BN_BigNum *e;  // Exponent Value -converted.Not in char
    uint32_t bits;   // length in bits of modulus
    BN_BigNum *p;     // prime factor p
    BN_BigNum *q;     // prime factor q
};

/**
 * @ingroup crypt_eal_pkey
 *
 * (For internal use)Set the padding mode of the RSA. The value 0 indicates that the padding mode is not set.
 */
typedef enum {
    EMSA_PKCSV15 = 1, /**< PKCS1-v1_5 complies with RFC8017 */
    EMSA_PSS,          /**< PSS complies with RFC8017 */
    RSAES_OAEP,          /**< OAEP complies with RFC8017 */
    RSAES_PKCSV15,       /**< RSAES_PKCSV15 complies with RFC8017 */
    RSA_NO_PAD,
    RSAES_PKCSV15_TLS, /* Specific RSA pkcs1.5 padding verification process
                          to prevent possible Bleichenbacher attacks */
} RSA_PadType;

/**
 * @ingroup crypt_types
 *
 * Pkcsv15 padding mode, when RSA is used for signature.
 */
typedef struct {
    CRYPT_MD_AlgId mdId; /**< ID of the hash algorithm during pkcsv15 padding */
} RSA_PkcsV15Para;

typedef struct {
    RSA_PadType type; /**< padding id */
    union {
        RSA_PkcsV15Para pkcsv15; /**< pkcsv15 padding mode */
        RSA_PadingPara pss;         /**< pss padding mode */
        RSA_PadingPara oaep; /**< oaep padding mode */
    } para;                            /**< padding mode combination, including pss and pkcsv15 */
    CRYPT_Data salt; // Used for the KAT test.
} RSAPad;

struct RSA_Ctx {
    CRYPT_RSA_PrvKey *prvKey;
    CRYPT_RSA_PubKey *pubKey;
    CRYPT_RSA_Para *para;
    RSA_Blind *blind;
    RSAPad pad;
    uint32_t flags;
    CRYPT_Data label; // Used for oaep padding
    BSL_SAL_RefCount references;
};

CRYPT_RSA_PrvKey *RSA_NewPrvKey(uint32_t bits);
CRYPT_RSA_PubKey *RSA_NewPubKey(uint32_t bits);
void RSA_FreePrvKey(CRYPT_RSA_PrvKey *prvKey);
void RSA_FreePubKey(CRYPT_RSA_PubKey *pubKey);
int32_t RSA_CalcPrvKey(CRYPT_RSA_Ctx *ctx, BN_Optimizer *optimizer);
int32_t GenPssSalt(CRYPT_Data *salt, const EAL_MdMethod *mdMethod, int32_t saltLen, uint32_t padBuffLen);
void ShallowCopyCtx(CRYPT_RSA_Ctx *ctx, CRYPT_RSA_Ctx *newCtx);
CRYPT_RSA_Para *CRYPT_RSA_DupPara(const CRYPT_RSA_Para *para);

/**
 * @ingroup rsa
 * @brief   Create a blinding handle.
 *
 * @retval  Return the blinding handle.
 */
RSA_Blind *RSA_BlindNewCtx(void);

/**
 * @ingroup rsa
 * @brief   Release the blinding handle.
 *
 * @param   b [IN] blinding Handle. b is set NULL by the invoker.
 *
 * @retval  none
 */
void RSA_BlindFreeCtx(RSA_Blind *b);
/**
 * @ingroup rsa
 * @brief Multiply n by blinding factor A
 *
 * @param b [IN] Blinding Handle
 * @param data [IN] Input data
 * @param n [IN] n in the public key (n, e)
 * @param opt [IN] BigNum optimizer
 *
 * @retval Return the error code.
 */
int32_t RSA_BlindCovert(RSA_Blind *b, BN_BigNum *data, BN_BigNum *n, BN_Optimizer *opt);

/**
 * @ingroup rsa
 * @brief Multiply n by the reverse blinding factor Ai
 *
 * @param b [IN] Blinding Handle
 * @param data [IN] Input data
 * @param n [IN] n in the public key (n, e)
 * @param opt [IN] BigNum optimizer
 *
 * @retval Return the error code.
 */
int32_t RSA_BlindInvert(RSA_Blind *b, BN_BigNum *data, BN_BigNum *n, BN_Optimizer *opt);
/**
 * @ingroup rsa
 * @brief Create a new Blind parameter with the parameters e and m,
 * e in the public key (n, e), n in the public key (n, e)
 *
 * @param b [IN] Blinding Handle
 * @param e [IN] e in the public key (n, e)
 * @param n [IN] n in the public key (n, e)
 *
 * @retval Return the error code.
 */
int32_t RSA_BlindCreateParam(RSA_Blind *b, BN_BigNum *e, BN_BigNum *n, BN_Optimizer *opt);

#define RSA_FREE_PRV_KEY(prvKey_)               \
do {                                            \
        RSA_FreePrvKey((prvKey_));              \
        (prvKey_) = NULL;                       \
    } while (0)

#define RSA_FREE_PUB_KEY(pubKey_)               \
    do {                                        \
        RSA_FreePubKey((pubKey_));              \
        (pubKey_) = NULL;                       \
    } while (0)

#define RSA_FREE_PARA(para_)                    \
    do {                                        \
        CRYPT_RSA_FreePara((para_));            \
        (para_) = NULL;                         \
    } while (0)

#ifdef __cplusplus
}
#endif

#endif // HITLS_CRYPTO_RSA

#endif // RSA_LOCAL_H
