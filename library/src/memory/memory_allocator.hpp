/******************************************************************************
 * Copyright (c) 2022 Advanced Micro Devices, Inc. All rights reserved.
 *
 * Permission is hereby granted, free of charge, to any person obtaining a copy
 * of this software and associated documentation files (the "Software"), to
 * deal in the Software without restriction, including without limitation the
 * rights to use, copy, modify, merge, publish, distribute, sublicense, and/or
 * sell copies of the Software, and to permit persons to whom the Software is
 * furnished to do so, subject to the following conditions:
 *
 * The above copyright notice and this permission notice shall be included in
 * all copies or substantial portions of the Software.
 *
 * THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR
 * IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY,
 * FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT.  IN NO EVENT SHALL THE
 * AUTHORS OR COPYRIGHT HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER
 * LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING
 * FROM, OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS
 * IN THE SOFTWARE.
 *****************************************************************************/

#ifndef ROCSHMEM_LIBRARY_SRC_MEMORY_ALLOCATOR_HPP
#define ROCSHMEM_LIBRARY_SRC_MEMORY_ALLOCATOR_HPP

/**
 * @file memory_allocator.hpp
 *
 * @brief Wraps allocators in a uniform interface
 */

#include <cstdlib>
#include <functional>

#include <hip/hip_runtime_api.h>

namespace rocshmem {

class MemoryAllocator
{
  public:
    /**
     * @brief Required for default construction of other objects
     *
     * @note Not intended for direct usage.
     */
    MemoryAllocator() = default;

    /**
     * @brief Primary constructor
     *
     * @param[in] hip library allocation function
     * @param[in] hip library free function
     * @param[in] hip flags
     *
     * @note Finegrained flag is assumed by default
     */
    MemoryAllocator(hipError_t (*hip_alloc_fn)(void**, size_t, unsigned),
                    hipError_t (*hip_free_fn)(void*));

    /**
     * @brief Primary constructor
     *
     * @param[in] hip library allocation function
     * @param[in] hip library free function
     */
    MemoryAllocator(hipError_t (*hip_alloc_fn)(void**, size_t),
                    hipError_t (*hip_free_fn)(void*));

    /**
     * @brief Primary constructor
     *
     * @param[in] an allocation function
     * @param[in] a free function
     */
    MemoryAllocator(std::function<void*(size_t)> alloc_fn,
                    std::function<void(void*)> free_fn);

    /**
     * @brief Allocates memory
     *
     * @param[in, out] Address of raw pointer (&pointer_to_char)
     * @param[in] Size in bytes of memory allocation
     */
    void
    allocate(void** ptr, size_t size);

    /**
     * @brief Deallocates memory
     *
     * @param[in] Address of raw pointer (&pointer_to_char)
     */
    void
    deallocate(void* ptr);

  private:
    /**
     * @brief a standard allocator function
     */
    std::function<void*(size_t)> _alloc {nullptr};

    /**
     * @brief a standard free function
     */
    std::function<void(void*)> _free {nullptr};

    /**
     * @brief a hip-specific allocator function
     */
    std::function<hipError_t(void**, size_t)> _hip_alloc {nullptr};

    /**
     * @brief a hip-specific allocator function
     */
    std::function<hipError_t(void**, size_t, int)> _hip_alloc_finegrained {nullptr};

    /**
     * @brief a hip-specific free function
     */
    std::function<hipError_t(void*)> _hip_free {nullptr};

    /**
     * @brief a hip-specific return code
     */
    hipError_t _hip_return_value {hipSuccess};
};

}

#endif  // ROCSHMEM_LIBRARY_SRC_MEMORY_ALLOCATOR_HPP
