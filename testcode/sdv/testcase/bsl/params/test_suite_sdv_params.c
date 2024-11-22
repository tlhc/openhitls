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

/* BEGIN_HEADER */

#include <unistd.h>
#include <stdlib.h>
#include <pthread.h>
#include <string.h>
#include "bsl_sal.h"
#include "bsl_params.h"
#include "bsl_err.h"
#include "bsl_log.h"
/* END_HEADER */


/* BEGIN_CASE */
void SDV_BSL_BSL_PARAM_InitValue_API_TC001()
{
    BSL_Param param = {0};
    int32_t val = 1;
    ASSERT_EQ(BSL_PARAM_InitValue(&param, 0, BSL_PARAM_TYPE_UINT32,
        &val, sizeof(val)), BSL_PARAMS_INVALID_KEY);
    ASSERT_EQ(BSL_PARAM_InitValue(NULL, 1, BSL_PARAM_TYPE_UINT32, &val, sizeof(val)), BSL_INVALID_ARG);
    ASSERT_EQ(BSL_PARAM_InitValue(&param, 1, BSL_PARAM_TYPE_UINT32, NULL, sizeof(val)), BSL_INVALID_ARG);
    ASSERT_EQ(BSL_PARAM_InitValue(&param, 1, 100, &val, sizeof(val)), BSL_PARAMS_INVALID_TYPE);
    ASSERT_EQ(BSL_PARAM_InitValue(&param, 1, BSL_PARAM_TYPE_UINT32, &val, sizeof(val)), BSL_SUCCESS);
exit:
    return;
}
/* END_CASE */

/* BEGIN_CASE */
void SDV_BSL_BSL_PARAM_SetValue_API_TC001()
{
    BSL_Param param = {0};
    int32_t val = 1;
    ASSERT_EQ(BSL_PARAM_InitValue(&param, 1, BSL_PARAM_TYPE_UINT32, &val, sizeof(val)), BSL_SUCCESS);
    ASSERT_EQ(BSL_PARAM_SetValue(&param, 0, BSL_PARAM_TYPE_UINT32, &val, sizeof(val)), BSL_PARAMS_INVALID_KEY);
    ASSERT_EQ(BSL_PARAM_SetValue(NULL, 1, BSL_PARAM_TYPE_UINT32, &val, sizeof(val)), BSL_INVALID_ARG);
    ASSERT_EQ(BSL_PARAM_SetValue(&param, 2, BSL_PARAM_TYPE_UINT32, &val, sizeof(val)), BSL_PARAMS_MISMATCH);
    ASSERT_EQ(BSL_PARAM_SetValue(&param, 1, BSL_PARAM_TYPE_UINT32, &val, 5), BSL_INVALID_ARG);
    ASSERT_EQ(BSL_PARAM_SetValue(&param, 1, BSL_PARAM_TYPE_UINT32, NULL, sizeof(val)), BSL_INVALID_ARG);
    val = 4;
    ASSERT_EQ(BSL_PARAM_SetValue(&param, 1, BSL_PARAM_TYPE_OCTETS_PTR, &val, sizeof(val)), BSL_PARAMS_MISMATCH);
    ASSERT_EQ(BSL_PARAM_SetValue(&param, 1, BSL_PARAM_TYPE_UINT32, &val, sizeof(val)), BSL_SUCCESS);
    int32_t retVal = 0;
    uint32_t retValLen = sizeof(retVal);
    ASSERT_EQ(BSL_PARAM_GetValue(&param, 1, BSL_PARAM_TYPE_UINT32, &retVal, &retValLen), BSL_SUCCESS);
    ASSERT_EQ(retVal, val);
exit:
    return;
}
/* END_CASE */