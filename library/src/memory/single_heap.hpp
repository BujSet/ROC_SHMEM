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

#ifndef ROCSHMEM_LIBRARY_SRC_SINGLE_HEAP_HPP
#define ROCSHMEM_LIBRARY_SRC_SINGLE_HEAP_HPP

#include "address_record.hpp"
#include "heap_memory.hpp"
#include "hip_allocator.hpp"
#include "pow2_bins.hpp"

/**
 * @file single_heap.hpp
 *
 * @brief Contains a single heap
 *
 * The single heap implements local processing element allocations. The
 * symmetric heap delegates allocations to this class.
 */

namespace rocshmem {

class SingleHeap {
    /**
     * @brief Helper type for heap memory
     */
#ifdef USE_CACHED
    using HEAP_T = HeapMemory<HIPAllocator>;
#else
    using HEAP_T = HeapMemory<HIPAllocatorFinegrained>;
#endif

    /**
     * @brief Helper type for address records
     */
    using AR_T = AddressRecord;

    /**
     * @brief Helper type for allocation strategy
     */
    using STRAT_T = Pow2Bins<AR_T, HEAP_T>;

  public:
    /**
     * @brief Primary constructor
     */
    SingleHeap();

    /**
     * @brief Allocates memory from the heap
     *
     * @param[in,out] A pointer to memory handle
     * @param[in] Size in bytes of memory allocation
     */
    void
    malloc(void** ptr,
           size_t size);

    /**
     * @brief Frees memory from the heap
     *
     * @param[in] Raw pointer to heap memory
     */
    void
    free(void* ptr);

    /**
     * @brief
     *
     * @param[in]
     * @param[in]
     *
     * @return
     */
    void*
    realloc(void* ptr, size_t size);

    /**
     * @brief
     *
     * @param[in]
     * @param[in]
     *
     * @return
     */
    void*
    malign(size_t alignment,
           size_t size);

    /**
     * @brief Accessor for heap base ptr
     *
     * @return Pointer to base of my heap
     */
    char*
    get_base_ptr();

    /**
     * @brief Accessor for heap size
     *
     * @return Amount of bytes in heap
     */
    size_t
    get_size();

    /**
     * @brief Accessor for heap usage
     *
     * @return Amount of used bytes in heap
     */
    size_t
    get_used();

    /**
     * @brief Accessor for heap available
     *
     * @return Amount of available bytes in heap
     */
    size_t
    get_avail();

  private:
    /**
     * @brief Heap memory object
     */
    HEAP_T heap_mem_ {};

    /**
     * @brief Allocation strategy object
     */
    STRAT_T strat_ {&heap_mem_};
};

} // namespace rocshmem

#endif  // ROCSHMEM_LIBRARY_SRC_SINGLE_HEAP_HPP
