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

#ifndef ECP_NISTP224_H
#define ECP_NISTP224_H

#include "hitls_build.h"
#if defined(HITLS_CRYPTO_CURVE_NISTP224) && defined(HITLS_CRYPTO_NIST_USE_ACCEL)

#include "ecc_local.h"

#ifdef __cplusplus
extern "C" {
#endif

/**
 * @brief   Convert the point information pt to the affine coordinate system and refresh the data to r.
 *
 * @param   para [IN] Curve parameters
 * @param   r [OUT] Output point information
 * @param   pt [IN] Input point information
 *
 * @retval CRYPT_SUCCESS    succeeded
 * @retval For details about other errors, see crypt_errno.h
 */
int32_t ECP224_Point2Affine(const ECC_Para *para, ECC_Point *r, const ECC_Point *pt);

/**
 * @brief   Calculate r = k1 * G + k2 * pt
 *
 * @param   para [IN] Curve parameters
 * @param   r [OUT] Output point information
 * @param   k1 [IN] Scalar 1, with a maximum of 224 bits
 * @param   k2 [IN] Scalar 2, with a maximum of 224 bits
 * @param   pt [IN] Point data
 *
 * @retval CRYPT_SUCCESS    succeeded
 * @retval For details about other errors, see crypt_errno.h
 */
int32_t ECP224_PointMulAdd(
    ECC_Para *para, ECC_Point *r, const BN_BigNum *k1, const BN_BigNum *k2, const ECC_Point *pt);

/**
 * @brief   If pt != NULL, calculate r = k * pt; otherwise, calculate r = k * G
 *
 * @param   para [IN] Curve parameter information
 * @param   r [OUT] Output point information
 * @param   k [IN] scalar with a maximum of 224 bits.
 * @param   pt [IN] Point data, which can be set to NULL.
 *
 * @retval CRYPT_SUCCESS    set successfully
 * @retval For details about other errors, see crypt_errno.h
 */
int32_t ECP224_PointMul(ECC_Para *para, ECC_Point *r, const BN_BigNum *k, const ECC_Point *pt);

#ifdef __cplusplus
}
#endif

#endif /* defined(HITLS_CRYPTO_CURVE_NISTP224) && defined(HITLS_CRYPTO_NIST_USE_ACCEL) */

#endif