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

/**
 * @defgroup crypt_types
 * @ingroup crypt
 * @brief types of crypto
 */

#ifndef CRYPT_TYPES_H
#define CRYPT_TYPES_H

#include <stdint.h>
#include <stddef.h>
#include "crypt_algid.h"

#ifdef __cplusplus
extern "C" {
#endif // __cplusplus

/**
 * @ingroup crypt_types
 *
 * Data structure
 */
typedef struct {
    uint8_t *data;   /**< Data content */
    uint32_t len;    /**< Data length */
} CRYPT_Data;

/**
 * @ingroup crypt_types
 *
 * Data range
 */
typedef struct {
uint32_t min;  /**< Minimum value */
uint32_t max;  /**< Maximum value */
} CRYPT_Range;

/**
 * @ingroup crypt_types
 *
 * Pkcsv15 padding mode, when RSA is used for signature.
 */
typedef struct {
    CRYPT_MD_AlgId mdId; /**< ID of the hash algorithm during pkcsv15 padding */
} CRYPT_RSA_PkcsV15Para;

/**
 * @ingroup crypt_types
 *
 * PSS padding mode, when RSA is used for signature.
 */
typedef struct {
    int32_t saltLen;      /**< pss salt length. the value -1 indicates hashLen, and the value -2 indicates MaxLen. */
    CRYPT_MD_AlgId mdId;  /**< mdid when pss padding. */
    CRYPT_MD_AlgId mgfId; /**< mgfid when pss padding. */
} CRYPT_RSA_PssPara;

typedef struct {
    CRYPT_MD_AlgId mdId;  /**< mdid when oaep padding */
    CRYPT_MD_AlgId mgfId; /**< mgfid when oaep padding */
} CRYPT_RSA_OaepPara;

typedef enum {
    CRYPT_RSA_BLINDING = 0x00000001,            /**< Enable the RSA blinding function for signature. */
    CRYPT_RSA_MAXFLAG
} CRYPT_RSA_Flag;

/**
 * @ingroup crypt_types
 *
 * RSA private key parameter structure
 */
typedef struct {
    uint8_t *d;  /**< RSA private key parameter marked as d. */
    uint8_t *n;  /**< RSA private key parameter marked as n. */
    uint8_t *p;  /**< RSA private key parameter marked as p. */
    uint8_t *q;  /**< RSA private key parameter marked as q. */
    uint8_t *dP; /**< RSA private key parameter marked as dP. */
    uint8_t *dQ; /**< RSA private key parameter marked as dQ. */
    uint8_t *qInv; /**< RSA private key parameter marked as qInv. */
    uint8_t *e;    /**< RSA public key parameter marked as e. */
    uint32_t dLen; /**< Length of the RSA private key parameter marked as d. */
    uint32_t nLen; /**< Length of the RSA private key parameter marked as n. */
    uint32_t pLen; /**< Length of the RSA private key parameter marked as p. */
    uint32_t qLen; /**< Length of the RSA private key parameter marked as q. */
    uint32_t dPLen; /**< Length of the RSA private key parameter marked as dPLen. */
    uint32_t dQLen; /**< Length of the RSA private key parameter marked as dQLen. */
    uint32_t qInvLen; /**< Length of the RSA private key parameter marked as qInvLen. */
    uint32_t eLen; /**< Length of the RSA public key parameter marked as eLen. */
} CRYPT_RsaPrv;

/**
 * @ingroup crypt_types
 *
 * Elliptic curve parameter information
 */
typedef struct {
    uint8_t *p;
    uint8_t *a;
    uint8_t *b;
    uint8_t *n;
    uint8_t *h;
    uint8_t *x;
    uint8_t *y;
    uint32_t pLen;
    uint32_t aLen;
    uint32_t bLen;
    uint32_t nLen;
    uint32_t hLen;
    uint32_t xLen;
    uint32_t yLen;
} CRYPT_EccPara;

/**
 * @ingroup crypt_types
 *
 * Paillier private key parameter structure
 */
typedef struct {
    uint8_t *n;      /**< Paillier private key parameter marked as n */
    uint8_t *lambda; /**< Paillier private key parameter marked as lambda */
    uint8_t *mu;     /**< Paillier private key parameter marked as mu */
    uint8_t *n2;     /**< Paillier private key parameter marked as n2 */
    uint32_t nLen;   /**< Length of the Paillier private key parameter marked as n */
    uint32_t lambdaLen; /**< Length of the Paillier private key parameter marked as lambda */
    uint32_t muLen; /**< Length of the Paillier private key parameter marked as mu */
    uint32_t n2Len; /**< Length of the Paillier private key parameter marked as n2 */
} CRYPT_PaillierPrv;

/**
 * @ingroup crypt_types
 *
 * DSA private key parameter structure
 */
typedef CRYPT_Data CRYPT_DsaPrv;

/**
 * @ingroup crypt_types
 *
 * ECC private key parameter structure.
 */
typedef CRYPT_Data CRYPT_EccPrv;

/**
 * @ingroup crypt_types
 *
 * ECDSA private key parameter structure.
 */
typedef CRYPT_Data CRYPT_EcdsaPrv;

/**
 * @ingroup crypt_types
 *
 * SM2 private key parameter structure
 */
typedef CRYPT_Data CRYPT_Sm2Prv;

/**
 * @ingroup crypt_types
 *
 * DH private key parameter structure
 */
typedef CRYPT_Data CRYPT_DhPrv;

/**
 * @ingroup crypt_types
 *
 * ECDH private key parameter structure
 */
typedef CRYPT_Data CRYPT_EcdhPrv;

/**
 * @ingroup crypt_types
 *
 * ed25519/x25519 private key parameter structure
 */
typedef CRYPT_Data CRYPT_Curve25519Prv;

/**
 * @ingroup crypt_types
 *
 * RSA public key parameter structure
 */
typedef struct {
    uint8_t *e;  /**< RSA public key parameter marked as e */
    uint8_t *n;  /**< RSA public key parameter marked as n */
    uint32_t eLen; /**< Length of the RSA public key parameter marked as e*/
    uint32_t nLen; /**< Length of the RSA public key parameter marked as e*/
} CRYPT_RsaPub;

/**
 * @ingroup crypt_types
 *
 * Paillier public key parameter structure
 */
typedef struct {
    uint8_t *n;  /**< Paillier public key parameter marked as n */
    uint8_t *g;  /**< Paillier public key parameter marked as g */
    uint8_t *n2; /**< Paillier public key parameter marked as n2 */
    uint32_t nLen; /**< Length of the Paillier public key parameter marked as n */
    uint32_t gLen; /**< Length of the Paillier public key parameter marked as g */
    uint32_t n2Len; /**< Length of the Paillier public key parameter marked as n2 */
} CRYPT_PaillierPub;

/**
 * @ingroup crypt_types
 *
 * DSA public key parameter structure
 */
typedef CRYPT_Data CRYPT_DsaPub;

/**
 * @ingroup crypt_types
 *
 * ECC public key parameter structure
 */
typedef CRYPT_Data CRYPT_EccPub;

/**
 * @ingroup crypt_types
 *
 * ECDSA public key parameter structure.
 */
typedef CRYPT_Data CRYPT_EcdsaPub;

/**
 * @ingroup crypt_types
 *
 * SM2 public key parameter structure
 */
typedef CRYPT_Data CRYPT_Sm2Pub;

/**
 * @ingroup crypt_types
 *
 * DH public key parameter structure
 */
typedef CRYPT_Data CRYPT_DhPub;

/**
 * @ingroup crypt_types
 *
 * ECDH public key parameter structure
 */
typedef CRYPT_Data CRYPT_EcdhPub;

/**
 * @ingroup crypt_types
 *
 * ed25519/x25519 public key parameter structure
 */
typedef CRYPT_Data CRYPT_Curve25519Pub;

/**
 * @ingroup crypt_types
 *
 * Para structure of the RSA algorithm
 */
typedef struct {   /**< This parameter cannot be NULL and is determined by the underlying structure. */
    uint8_t *e;    /**< Para Parameter e */
    uint32_t eLen; /**< Length of para e*/
    uint32_t bits; /**< Bits of para */
} CRYPT_RsaPara;

/**
 * @ingroup crypt_types
 *
 * Para structure of the DSA algorithm. This parameter cannot be null, and it is determined by the underlying structure.
 */
typedef struct {
    uint8_t *p;  /**< Parameter p */
    uint8_t *q;  /**< Parameter q */
    uint8_t *g;  /**< Parameter g */
    uint32_t pLen; /**< Length of parameter p*/
    uint32_t qLen; /**< Length of parameter q*/
    uint32_t gLen; /**< Length of parameter g*/
} CRYPT_DsaPara;

/**
 * @ingroup crypt_types
 *
 * Para structure of the DH algorithm
 */
typedef struct {
    uint8_t *p;  /**< Parameter p. */
    uint8_t *q;  /**< Parameter q, the parameter can be NULL. */
    uint8_t *g;  /**< Parameter g. */
    uint32_t pLen; /**< Length of parameter p. */
    uint32_t qLen; /**< Length of parameter q. */
    uint32_t gLen; /**< Length of parameter g. */
} CRYPT_DhPara;

/**
 * @ingroup crypt_types
 *
 * Para structure of the Paillier algorithm
 */
typedef struct {
    uint8_t *p; /**< Parameter p. */
    uint8_t *q; /**< Parameter q. */
    uint32_t pLen; /**< Length of parameter p. */
    uint32_t qLen; /**< Length of parameter q. */
    uint32_t bits; /**< Bits of para. */
} CRYPT_PaillierPara;

/**
 * @ingroup crypt_types
 *
     * Obtain the entropy source. If the default entropy source provided by HiTLS is not used,
     * the API must be registered. the output data must meet requirements such as the length.
     * The HiTLS does not check the entropy source. The data must be provided by the entropy source.
     *
     * @param ctx      [IN] Context used by the caller.
     * @param entropy  [OUT] Indicates the obtained entropy source data. The length of the entropy source data
     *                 must meet the following requirements: lenRange->min <= len <= lenRange->max.
     * @param strength  [IN] Entropy source strength.
     * @param lenRange  [IN] Entropy source length range.
     * @retval 0 indicates success, and other values indicate failure.
     */
typedef int32_t (*CRYPT_RAL_GetEntropyCb)(void *ctx, CRYPT_Data *entropy, uint32_t strength, CRYPT_Range *lenRange);

    /**
     * @ingroup crypt_types
     * @brief The entropy source memory is cleared, this API is optional.
     * @param ctx     [IN] Context used by the caller
     * @param entropy [OUT] Entropy source data
     * @retval  void
     */
typedef void (*CRYPT_RAL_CleanEntropyCb)(void *ctx, CRYPT_Data *entropy);

    /**
     * @ingroup crypt_types
     * @brief Obtain the random number. This API is not need to registered.
     *        For registration, the output data must meet requirements such as the length.
     *        The HiTLS does not check the entropy source, but will implement if provide the function.
     *
     * @param ctx      [IN] Context used by the caller
     * @param nonce    [OUT] Obtained random number.
     * The length of the random number must be lenRange->min <= len <= lenRange->max.
     * @param strength [IN]: Random number strength
     * @param lenRange [IN] Random number length range.
     * @retval 0 indicates success, and other values indicate failure.
     */
typedef int32_t (*CRYPT_RAL_GetNonceCb)(void *ctx, CRYPT_Data *nonce, uint32_t strength, CRYPT_Range *lenRange);

    /**
    * @ingroup crypt_types
    * @brief Random number memory clearance. this API is optional.
    * @param ctx [IN] Context used by the caller
    * @param nonce [OUT] random number
    * @retval void
    */
typedef void (*CRYPT_RAL_CleanNonceCb)(void *ctx, CRYPT_Data *nonce);

/**
 * @ingroup crypt_types
 *
 *     Metohd structure of the RAND registration interface, including the entropy source obtaining and clearing
 * interface and random number obtaining and clearing interface.
 *     For details about how to use the default entropy source of the HiTLS, see CRYPT_EAL_RandInit().
 *     If the default mode is not used, the entropy source obtaining interface cannot be null, interface for
 * obtaining random numbers can be null.
 */
typedef struct {
    CRYPT_RAL_GetEntropyCb getEntropy;
    CRYPT_RAL_CleanEntropyCb cleanEntropy;
    CRYPT_RAL_GetNonceCb getNonce;
    CRYPT_RAL_CleanNonceCb cleanNonce;
} CRYPT_RandSeedMethod;

/**
 * @ingroup crypt_ctrl_param
 *
 * Set and obtain internal mode parameters.
 */
typedef enum {
    CRYPT_CTRL_GET_IV,            /**< Obtains the IV data, the data type is uint8_t type. */
    CRYPT_CTRL_GET_BLOCKSIZE,     /**< Obtain the block size, the data type is uint8_t type. */
    CRYPT_CTRL_SET_AAD,           /**< Set the ADD information in AEAD encryption and decryption mode. */
    CRYPT_CTRL_GET_TAG,           /**< Obtain the tag at the end in AEAD encryption or decryption. */
    CRYPT_CTRL_SET_TAGLEN,        /**< Set the tag length before the encryption/decryption starts in AEAD
                                       encryption/decryption. the setting type is uint32_t. */
    CRYPT_CTRL_SET_MSGLEN,        /**< In CMM mode, the length of the encrypted message needs to be used as the
                                       input for calculation. the length must be set before SET_AAD. The input data
                                       type is int64_t. */
    CRYPT_CTRL_SET_FEEDBACKSIZE,  /**< Setting the ciphertext feedback length in CFB mode. */
    CRYPT_CTRL_GET_FEEDBACKSIZE,  /**< Obtaining the ciphertext feedback length in CFB mode. */
    CRYPT_CTRL_DES_NOKEYCHECK,    /**< DES does not verify the key. */
    CRYPT_CTRL_SET_SM4_CONSTTIME, /**< SM4 selects the side channel security implementation, which reduces
                                       the performance. Valid only when ARM assembly implementation is enabled. */
    CRYPT_CTRL_MAX
} CRYPT_CipherCtrl;

/**
 * @ingroup crypt_ctrl_param
 *
 * Set and obtain internal parameters of pkey.
 */
typedef enum {
    CRYPT_CTRL_SET_RSA_EMSA_PKCSV15,    /**< RSA set the signature padding mode to EMSA_PKCSV15. */
    CRYPT_CTRL_SET_RSA_EMSA_PSS,        /**< RSA set the signature padding mode to EMSA_PSS. */
    CRYPT_CTRL_SET_RSA_SALT,            /**< When the RSA algorithm is used for PSS signature, the salt data is
                                             specified. During signature, the user data address is directly saved
                                             to the key. And the user data is used for the next signature, the caller
                                             must ensure that the next signature is called within the life cycle
                                             of the salt data. This option is not recommended and is used only for
                                             KAT and self-verification. */
    CRYPT_CTRL_SET_ECC_POINT_FORMAT,    /**< ECC PKEY set the point format. For the point format,
                                             see CRYPT_PKEY_PointFormat. */
    CRYPT_CTRL_GET_RSA_SALT,            /**< Obtain the salt length of the RSA algorithm. */
    CRYPT_CTRL_GET_RSA_PADDING,         /**< Obtain the padding mode of the RSA algorithm. */
    CRYPT_CTRL_SET_RSA_PADDING,         /**< Set the padding mode of the RSA algorithm. */
    CRYPT_CTRL_GET_RSA_MD,              /**< Obtain the MD algorithm of the RSA algorithm. */
    CRYPT_CTRL_GET_RSA_MGF,             /**< Obtain the mgf algorithm when the RSA algorithm padding mode
                                             is PSS. */
    CRYPT_CTRL_SET_ECC_USE_COFACTOR_MODE, /**< Indicates whether to use the cofactor mode to prevent
                                               man-in-the-middle from tampering with the public key.
                                               Set this parameter to 1 when used or 0 when not used. */
    CRYPT_CTRL_SET_RSA_RSAES_OAEP,      /**< RSA set the padding mode to RSAES_OAEP. */
    CRYPT_CTRL_SET_RSA_OAEP_LABEL,      /**< RSA oaep padding and setting labels, used to generate hash values. */
    CRYPT_CTRL_SET_RSA_FLAG,            /**< RSA set the flag. */
    CRYPT_CTRL_CLR_RSA_FLAG,            /**< RSA clear the flag. */
    CRYPT_CTRL_SET_RSA_RSAES_PKCSV15,   /**< RSA Set the encryption/decryption padding mode to RSAES_PKCSV15. */
    CRYPT_CTRL_SET_RSA_RSAES_PKCSV15_TLS, /**< RSA Set the encryption/decryption padding mode to RSAES_PKCSV15_TLS. */
    CRYPT_CTRL_SET_SM2_USER_ID,
    CRYPT_CTRL_SET_SM2_SERVER,          /* SM2 set the user status. */
    CRYPT_CTRL_GENE_SM2_R,              /* SM2 obtain the R value. */
    CRYPT_CTRL_SET_SM2_R,               /* SM2 set the R value. */
    CRYPT_CTRL_SET_SM2_RANDOM,          /* SM2 set the r value. */
    CRYPT_CTRL_SET_SM2_PKG,             /* SM2 uses the PKG process. */
    CRYPT_CTRL_SM2_GET_SEND_CHECK,      /* SM2 obtain the check value sent from the local end to the peer end. */
    CRYPT_CTRL_SM2_DO_CHECK,            /* SM2 check the shared key. */

    CRYPT_CTRL_UP_REFERENCES,           /**< The reference count value increases automatically.
                                             It is applicable to asymmetric algorithms such as 25519, RSA, and ECC. */
    CRYPT_CTRL_GEN_ECC_PUBLICKEY,       /**< Use prikey genarate pubkey. */
} CRYPT_PkeyCtrl;

/**
 * @ingroup crypt_ctrl_param
 *
 * Set and obtain internal parameters of mac.
 */
typedef enum {
    CRYPT_CTRL_GET_MACLEN             /* Mac get maxlen . */
} CRYPT_MacCtrl;

/**
 * @ingroup crypt_padding_type
 *
 * Padding mode enumerated type
 */
typedef enum {
    CRYPT_PADDING_NONE = 0,         /**< Never pad (full blocks only).   */
    CRYPT_PADDING_ZEROS,            /**< Zero padding (not reversible).  */
    CRYPT_PADDING_ISO7816,          /**< ISO/IEC 7816-4 padding.         */
    CRYPT_PADDING_X923,             /**< ANSI X.923 padding.            */
    CRYPT_PADDING_PKCS5,            /**< PKCS5 padding.                  */
    CRYPT_PADDING_PKCS7,            /**< PKCS7 padding.                  */
    CRYPT_PADDING_MAX_COUNT
} CRYPT_PaddingType;

typedef enum {
    CRYPT_PKEY_EMSA_PKCSV15 = 1, /**< PKCS1-v1_5 according to RFC8017. */
    CRYPT_PKEY_EMSA_PSS,         /**< PSS according to RFC8017. */
    CRYPT_PKEY_RSAES_OAEP,       /**< OAEP according to RFC8017. */
    CRYPT_PKEY_RSAES_PKCSV15,    /**< RSAES_PKCSV15 according to RFC8017. */
    CRYPT_PKEY_RSA_NO_PAD,
    CRYPT_RSAES_PKCSV15_TLS, /* Specific RSA pkcs1.5 padding verification process to
                                prevent possible Bleichenbacher attacks */
    CRYPT_PKEY_RSA_PADDINGMAX,
} CRYPT_RsaPadType;

/**
 * @ingroup  crypt_types
 *
 * Operation type
 */
typedef enum {
    CRYPT_EVENT_ENC,          /**< Encryption. */
    CRYPT_EVENT_DEC,          /**< Decryption. */
    CRYPT_EVENT_GEN,          /**< Generate the key. */
    CRYPT_EVENT_SIGN,         /**< Signature. */
    CRYPT_EVENT_VERIFY,       /**< Verify the signature. */
    CRYPT_EVENT_MD,           /**< Hash. */
    CRYPT_EVENT_MAC,          /**< MAC. */
    CRYPT_EVENT_KDF,          /**< KDF. */
    CRYPT_EVENT_KEYAGGREMENT, /**< Key negotiation. */
    CRYPT_EVENT_KEYDERIVE,    /**< Derived key. */
    CRYPT_EVENT_RANDGEN,      /**< Generating a random number. */
    CRYPT_EVENT_ZERO,         /**< sensitive information to zero. */
    CRYPT_EVENT_ERR,          /**< An error occurred. */
    CRYPT_EVENT_SETSSP,       /**< Adding and Modifying Password Data and SSP. */
    CRYPT_EVENT_GETSSP,       /**< Access password data and SSP. */
    CRYPT_EVENT_MAX
} CRYPT_EVENT_TYPE;

/**
 * @ingroup  crypt_types
 *
 * Algorithm type
 */
typedef enum {
    CRYPT_ALGO_CIPHER = 0,
    CRYPT_ALGO_PKEY,
    CRYPT_ALGO_MD,
    CRYPT_ALGO_MAC,
    CRYPT_ALGO_KDF,
    CRYPT_ALGO_RAND
} CRYPT_ALGO_TYPE;

/**
 * @ingroup crypt_types
 * @brief   event report.
 *
 * @param   oper [IN] Operation type.
 * @param   type [IN] Algorithm type.
 * @param   id [IN] Algorithm ID.
 * @param   err [IN] CRYPT_SUCCESS, if successful.
 *                   For other error codes, see crypt_errno.h.
 *
 * @retval None
 */
typedef void (*EventReport)(CRYPT_EVENT_TYPE oper, CRYPT_ALGO_TYPE type, int32_t id, int32_t err);

/**
 * @ingroup crypt_types
 *
 *     Event reporting callback registration interface, the EAL reports an event when the service is executed
 * and an error is reported.
 *     If the CMVP feature is enabled, the default implementation is provided and registration is not allowed.
 *     Note that Multi-threading is not supported.
 *
 * @param   func     [IN] Event reporting and processing callback
 *
 * @retval  NONE
 */
void CRYPT_EAL_RegEventReport(EventReport func);

/**
 * @ingroup crypt_getInfo_type
 *
 * Obtain the algorithm attribute type.
 */
typedef enum {
    CRYPT_INFO_IS_AEAD = 0,          /**< Whether the AEAD algorithm is used. */
    CRYPT_INFO_IS_STREAM,            /**< Stream encryption or not. */
    CRYPT_INFO_IV_LEN,               /**< Algorithm IV length. */
    CRYPT_INFO_KEY_LEN,              /**< Algorithm key length. */
    CRYPT_INFO_BLOCK_LEN,            /**< Algorithm block length. */
    CRYPT_INFO_MAX
} CRYPT_INFO_TYPE;


typedef enum {
    CRYPT_KDF_HKDF_MODE_FULL = 0,
    CRYPT_KDF_HKDF_MODE_EXTRACT,
    CRYPT_KDF_HKDF_MODE_EXPAND,
} CRYPT_HKDF_MODE;

#define DEFAULT_PROVIDER_PARAM_TYPE 0

typedef struct {
    int32_t type;
    void *param;
    uint32_t paramLen;
} CRYPT_Param;

typedef struct {
    CRYPT_RandSeedMethod *seedMeth;
    void *seedCtx;
    const uint8_t *pers;
    uint32_t persLen;
    const uint8_t *adin;
    uint32_t adinLen;
    bool predictionResistant;
} CRYPT_RndParam;

#ifdef __cplusplus
}
#endif // __cplusplus

#endif // CRYPT_TYPES_H
