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

#ifndef SAL_MEMIMPL_H
#define SAL_MEMIMPL_H

#include "hitls_build.h"
#ifdef HITLS_BSL_SAL_MEM

#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

/**
 * @ingroup bsl_sal
 *
 * Registrable function structure for memory allocation/release.
 */
typedef struct MemCallback {
    /**
     * @ingroup bsl_sal
     * @brief Allocate a memory block.
     *
     * Allocate a memory block.
     *
     * @param size [IN] Size of the allocated memory.
     * @retval: Not NULL, The start address of the allocated memory when memory is allocated successfully.
     * @retval  NULL, Memory allocation failure.
     */
    void *(*pfMalloc)(uint32_t size);

    /**
     * @ingroup bsl_sal
     * @brief Reclaim a memory block allocated by pfMalloc.
     *
     * Reclaim a block of memory allocated by pfMalloc.
     *
     * @param addr [IN] Start address of the memory allocated by pfMalloc.
     */
    void (*pfFree)(void *addr);
} BSL_SAL_MemCallback;

int32_t BSL_SAL_RegMemCallback(BSL_SAL_CB_FUNC_TYPE type, void *funcCb);

#ifdef HITLS_BSL_SAL_LINUX
void *SAL_MallocImpl(uint32_t size);

void SAL_FreeImpl(void *value);
#endif

#ifdef __cplusplus
}
#endif

#endif /* HITLS_BSL_SAL_MEM */

#endif // SAL_MEMIMPL_H
