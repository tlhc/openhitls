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

#ifndef HITLS_PRINT_LOCAL_H
#define HITLS_PRINT_LOCAL_H

#include <stdint.h>
#include "bsl_uio.h"

#ifdef __cplusplus
extern "C" {
#endif

/**
 * Print format
 */
#define HITLS_X509_PRINT_DN_ONELINE 0
#define HITLS_X509_PRINT_DN_MULTILINE 1
#define HITLS_X509_PRINT_DN_RFC2253 2

typedef enum {
    HITLS_X509_SET_PRINT_FLAG,
    HITLS_X509_PRINT_DN,
} HITLS_X509_PrintCmd;

int32_t HITLS_X509_PrintCtrl(int32_t cmd, void *val, uint32_t valLen, BSL_UIO *uio);

#ifdef __cplusplus
}
#endif
#endif