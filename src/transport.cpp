/******************************************************************************
 * Copyright (c) 2019 Advanced Micro Devices, Inc. All rights reserved.
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

#include "transport.hpp"
#include "ro_net_internal.hpp"

#ifdef MPI_TRANSPORT
#define NET_CHECK(cmd) \
{\
    if (cmd != MPI_SUCCESS) {\
        fprintf(stderr, "Unrecoverable error: MPI Failure\n");\
        exit(-1);\
    }\
}

MPITransport::MPITransport()
    : Transport(), hostBarrierDone(0), transport_up(false), handle(nullptr)
{
    int provided;
    indicies = new int[INDICIES_SIZE];
    NET_CHECK(MPI_Init_thread(0, 0, MPI_THREAD_MULTIPLE, &provided));
    if (provided != MPI_THREAD_MULTIPLE) {
        fprintf(stderr, "Warning requested multi-thread level is not "
                        "supported \n");
    }
    NET_CHECK(MPI_Comm_size(MPI_COMM_WORLD, &num_pes));
    NET_CHECK(MPI_Comm_rank(MPI_COMM_WORLD, &my_pe));
}

MPITransport::~MPITransport()
{
    delete [] indicies;
    MPI_Finalize();
}

void
MPITransport::threadProgressEngine()
{
    transport_up = true;
    while (!handle->done_flag) {
        submitRequestsToMPI();
        progress();
    }
    transport_up = false;
}

void
MPITransport::insertRequest(const queue_element_t * element, int queue_id)
{
    std::unique_lock<std::mutex> mlock(queue_mutex);
    q.push(element);
    q_wgid.push(queue_id);
}

void
MPITransport::submitRequestsToMPI()
{
    if (q.empty())
        return;

    std::unique_lock<std::mutex> mlock(queue_mutex);

    const queue_element_t *next_element = q.front();
    int queue_idx = q_wgid.front();
    q.pop();
    q_wgid.pop();
    mlock.unlock();

    queue_desc_t *queue_desc;
    if (queue_idx != -1)
        queue_desc = &handle->queue_descs[queue_idx];
    else
        queue_desc = &handle->queue_descs[0];

    switch (next_element->type) {
        case RO_NET_PUT: //put
            queue_desc->host_stats.numPut++;
            putMem(next_element->dst, next_element->src,
                                next_element->size, next_element->PE,
                                queue_idx, next_element->threadId, true);

            DPRINTF(("Received PUT dst %p src %p size %d "
                    "pe %d\n",
                    next_element->dst, next_element->src,
                    next_element->size, next_element->PE));

            break;
        case RO_NET_GET: //get
            queue_desc->host_stats.numGet++;
            getMem(next_element->dst, next_element->src,
                                next_element->size, next_element->PE,
                                queue_idx, next_element->threadId, true);

            DPRINTF(("Received GET dst %p src %p size %d pe %d\n",
                    next_element->dst, next_element->src,
                    next_element->size, next_element->PE));

            break;
        case RO_NET_PUT_NBI: //put_nbi
            queue_desc->host_stats.numPutNbi++;
            putMem(next_element->dst, next_element->src,
                                next_element->size, next_element->PE,
                                queue_idx, next_element->threadId, false);
            DPRINTF(("Received PUT NBI dst %p src %p size %d "
                    "pe %d\n",
                    next_element->dst, next_element->src,
                    next_element->size, next_element->PE));

            break;
        case RO_NET_GET_NBI: //get_nbi;
            queue_desc->host_stats.numGetNbi++;
            getMem(next_element->dst, next_element->src,
                                next_element->size, next_element->PE,
                                queue_idx, next_element->threadId, false);

            DPRINTF(("Received GET NBI dst %p src %p size %d "
                        "pe %d\n", next_element->dst, next_element->src,
                        next_element->size, next_element->PE));

            break;
        case RO_NET_FLOAT_SUM_TO_ALL: //float_sum_to_all;
            printf("doin reduction\n");
            reduction(next_element->dst, next_element->src,
                                    next_element->size,
                                    next_element->PE, queue_idx,
                                    next_element->PE,
                                    next_element->logPE_stride,
                                    next_element->PE_size, next_element->pWrk,
                                    next_element->pSync, RO_NET_SUM,
                                    next_element->threadId, true);

            DPRINTF(("Received FLOAT_SUM_TO_ALL dst %p src %p size %d "
                    "PE_start %d, logPE_stride %d, PE_size %d, pWrk %p, "
                    "pSync %p\n", next_element->dst, next_element->src,
                    next_element->size, next_element->PE,
                    next_element->logPE_stride, next_element->PE_size,
                    next_element->pWrk, next_element->pSync));

            break;
            case RO_NET_BARRIER_ALL: //Barrier_all;
            barrier(queue_idx, next_element->threadId, true);

            DPRINTF(("Received Barrier_all\n"));

            break;

        case RO_NET_FENCE: //fence
        case RO_NET_QUIET: //quiet
            queue_desc->host_stats.numQuiet++;
            quiet(queue_idx, next_element->threadId);
            DPRINTF(("Received FENCE/QUIET\n"));

            break;
        case RO_NET_FINALIZE: //finalize
            queue_desc->host_stats.numFinalize++;
            quiet(queue_idx, next_element->threadId);
            DPRINTF(("Received Finalize\n"));

            break;
        default:
            fprintf(stderr,
                    "Invalid GPU Packet received, exiting....\n");
            exit(-1);
            break;
    }
}

ro_net_status_t
MPITransport::initTransport(int num_queues,
                            ro_net_handle *ro_net_gpu_handle)
{
    waiting_quiet.resize(num_queues, std::vector<int>());
    outstanding.resize(num_queues, 0);
    transport_up = false;
    handle = ro_net_gpu_handle;
    progress_thread = new std::thread(&MPITransport::threadProgressEngine, this);
    while (!transport_up);
    return RO_NET_SUCCESS;
}

ro_net_status_t
MPITransport::finalizeTransport()
{
    progress_thread->join();
    delete progress_thread;
    return RO_NET_SUCCESS;
}

ro_net_status_t
MPITransport::allocateMemory(void **ptr, size_t size)
{
    #ifdef GPU_HEAP
    ro_net_device_uc_malloc(ptr, size);
    #else
    hipHostMalloc_assert(ptr, size);
    #endif

    MPI_Win window;
    NET_CHECK(MPI_Win_create(*ptr, size, 1, MPI_INFO_NULL, MPI_COMM_WORLD,
                             &window));

    NET_CHECK(MPI_Win_lock_all(0, window));
    NET_CHECK(MPI_Win_flush_all(window));

    window_vec.emplace_back(window, *ptr, size);
    DPRINTF(("Creating MPI Window ptr %p size %zu num_windows %zu\n", *ptr,
            size, window_vec.size()));
    return RO_NET_SUCCESS;
}

ro_net_status_t
MPITransport::deallocateMemory(void *ptr)
{
    if (ptr != NULL) {
        int idx = findWinIdx(ptr);
        MPI_Win window = window_vec[idx].getWindow();
        NET_CHECK(MPI_Win_unlock_all(window));
        NET_CHECK(MPI_Win_free(&window));
        #ifdef GPU_HEAP
        hipFree_assert(ptr);
        #else
        hipHostFree_assert(ptr);
        #endif
        window_vec.erase(window_vec.begin() + idx);
    }
    return RO_NET_SUCCESS;
}

MPI_Comm
MPITransport::createComm(int start, int logPstride, int size)
{
    // Check if communicator is cached
    CommKey key(start, logPstride, size);
    MPI_Comm comm;
    auto it = comm_map.find(key);
    if (it != comm_map.end()) {
        DPRINTF(("Using cached communicator\n"));
        return it->second;
    }

    int world_size;
    NET_CHECK(MPI_Comm_size(MPI_COMM_WORLD, &world_size));

    if (start == 0 && logPstride == 0 && size == world_size) {
        NET_CHECK(MPI_Comm_dup(MPI_COMM_WORLD, &comm));
    } else {

        MPI_Group world_group;
        NET_CHECK(MPI_Comm_group(MPI_COMM_WORLD, &world_group));

        int group_ranks[size];
        int stride = 2^(logPstride);
        group_ranks[0] =start;
        for (int i = 1 ; i < size; i++)
            group_ranks[i]= group_ranks[i-1]+stride;

        MPI_Group new_group;
        NET_CHECK(MPI_Group_incl(world_group, size, group_ranks, &new_group));
        NET_CHECK(MPI_Comm_create_group(MPI_COMM_WORLD, new_group, 0, &comm));
    }

    comm_map.insert(std::pair<CommKey, MPI_Comm>(key, comm));
    DPRINTF(("Creating new communicator\n"));

    return comm;
}

int
MPITransport::findWinIdx(void *dst) const
{
    for (int i = 0; i < window_vec.size(); i++) {
        if (dst >= window_vec[i].getStart() && dst < window_vec[i].getEnd())
            return i;
    }
    fprintf(stderr, "Unrecoverable error: Unknown window for %p\n", dst);
    exit(-1);
}

ro_net_status_t
MPITransport::barrier(int wg_id, int threadId, bool blocking)
{
    MPI_Request request;
    NET_CHECK(MPI_Ibarrier(MPI_COMM_WORLD, &request));

    req_prop_vec.emplace_back(threadId, wg_id, blocking);
    req_vec.push_back(request);
    outstanding[wg_id]++;
    return RO_NET_SUCCESS;
}

ro_net_status_t
MPITransport::reduction(void *dst, void *src, int size, int pe, int wg_id,
                        int start, int logPstride, int sizePE,
                        void *pWrk, long *pSync, RO_NET_Op op, int threadId,
                        bool blocking)
{
    MPI_Request request;
    // TODO: convert RO_NET_Op to MPI_OP
    assert(op == RO_NET_SUM);
    MPI_Op mpi_op = MPI_SUM;
    MPI_Comm comm = createComm(start, logPstride, sizePE);

    if (dst == src) {
        NET_CHECK(MPI_Iallreduce(MPI_IN_PLACE, dst, size, MPI_FLOAT, mpi_op,
                                 comm, &request));
    } else {
        NET_CHECK(MPI_Iallreduce(src, dst, size, MPI_FLOAT, mpi_op, comm,
                                 &request));
    }

    req_prop_vec.emplace_back(threadId, wg_id, blocking);
    req_vec.push_back(request);
    outstanding[wg_id]++;
    return RO_NET_SUCCESS;
}

ro_net_status_t
MPITransport::putMem(void *dst, void *src, int size, int pe, int wg_id,
                     int threadId, bool blocking)
{
    int idx = findWinIdx(dst);

    MPI_Request request;

    #if !defined(GPU_QUEUE) && defined(GPU_HEAP)
    // Need to flush HDP read cache so that the NIC can see data to push
    // out to the network.  If we have the network buffers allocated
    // on the host or we've already flushed for the command queue on the
    // GPU then we can ignore this step.
    hdp_read_inv(handle->hdp_regs);
    #endif

    NET_CHECK(MPI_Rput(src, size, MPI_CHAR, pe, window_vec[idx].getOffset(dst),
                       size, MPI_CHAR, window_vec[idx].getWindow(), &request));

    // Since MPI makes puts as complete as soon as the local buffer is free,
    // we need a flush to satisfy quiet.  Put it here as a hack for now even
    // though it should be in the progress loop.
    NET_CHECK(MPI_Win_flush_all(window_vec[idx].getWindow()));

    req_prop_vec.emplace_back(threadId, wg_id, blocking);
    req_vec.push_back(request);
    outstanding[wg_id]++;
    return RO_NET_SUCCESS;
}

ro_net_status_t
MPITransport::getMem(void *dst, void *src, int size, int pe, int wg_id,
                     int threadId, bool blocking)
{
    int idx = findWinIdx(src);

    MPI_Request request;

    outstanding[wg_id]++;

    NET_CHECK(MPI_Rget(dst, size, MPI_CHAR, pe, window_vec[idx].getOffset(src),
                    size, MPI_CHAR, window_vec[idx].getWindow(), &request));

   req_prop_vec.emplace_back(threadId, wg_id, blocking);
   req_vec.push_back(request);

    return RO_NET_SUCCESS;
}

ro_net_status_t
MPITransport::progress()
{
    MPI_Status status;
    int flag = 0;

    DPRINTF(("Entering progress engine\n"));
    std::queue<int> finished_wgs;

    // Enter the progress engine if there aren't any pending requests
    if (req_vec.size() == 0) {
        DPRINTF(("Probing MPI\n"));

        NET_CHECK(MPI_Iprobe(handle->num_pes - 1, 1000,
                            MPI_COMM_WORLD, &flag, &status));

    } else {
        DPRINTF(("Testing all outstanding requests (%zu)\n",
                req_vec.size()));

        // Check completion of any oustanding requests.  We check on either
        // the first 64 requests or the size of the request vector
        int incount = (req_vec.size() < INDICIES_SIZE) ?
            req_vec.size() : INDICIES_SIZE;
        int outcount;
        NET_CHECK(MPI_Testsome(incount, req_vec.data(), &outcount,
                               indicies, MPI_STATUSES_IGNORE));
        // If any request has completed remove it from the outstanding request
        // vector
        for (int i = 0; i < outcount; i++) {
            int indx = indicies[i];
            int wg_id = req_prop_vec[indx].wgId;
            int threadId = req_prop_vec[indx].threadId;

            if (wg_id != -1) {
                outstanding[wg_id]--;
                DPRINTF(("Finished op for wg_id %d at threadId %d (%d requests outstanding)\n",
                        wg_id, threadId, outstanding[wg_id]));
            } else {
                DPRINTF(("Finished host barrier\n"));
                hostBarrierDone = 1;
            }

            if (req_prop_vec[indx].blocking) {
                if (wg_id != -1)
                    handle->queue_descs[wg_id].status[threadId] = 1;
                #ifdef GPU_QUEUE
                SFENCE();
                hdp_flush(handle->hdp_regs);
                #endif
            }

            // If the GPU has requested a quiet, notify it of completion when all
            // outstanding requests are complete.
            if (!outstanding[wg_id] && !waiting_quiet[wg_id].empty()) {
                for (const auto threadId : waiting_quiet[wg_id]) {
                    DPRINTF(("Finished Quiet for wg_id %d at threadId %d\n",
                            wg_id, threadId));
                    handle->queue_descs[wg_id].status[threadId] = 1;
                }

                waiting_quiet[wg_id].clear();

                #ifdef GPU_QUEUE
                SFENCE();
                hdp_flush(handle->hdp_regs);
                #endif
            }
        }

        // Remove the MPI Request and the RequestProperty tracking entry
        sort(indicies, indicies + outcount, std::greater<int>());
        for (int i = 0; i < outcount; i++) {
            int indx = indicies[i];
            req_vec.erase(req_vec.begin() + indx);
            req_prop_vec.erase(req_prop_vec.begin() + indx);
        }
    }

    return RO_NET_SUCCESS;
}

ro_net_status_t
MPITransport::quiet(int wg_id, int threadId)
{
    if (!outstanding[wg_id]) {
        DPRINTF(("Finished Quiet immediately for wg_id %d at threadId %d\n",
                wg_id, threadId));
        handle->queue_descs[wg_id].status[threadId] = 1;
    } else {
        waiting_quiet[wg_id].emplace_back(threadId);
    }
    return RO_NET_SUCCESS;
}

int
MPITransport::numOutstandingRequests()
{
    return req_vec.size() + q.size();
}
#endif

#ifdef OPENSHMEM_TRANSPORT
#define NET_CHECK(cmd) \
{\
    if (cmd != 0) {\
        fprintf(stderr, "Unrecoverable error: SHMEM Failure\n");\
        exit(-1);\
    }\
}

OpenSHMEMTransport::OpenSHMEMTransport()
    : Transport()
{
    // TODO: Provide context support
    int provided;
    shmem_init_thread(SHMEM_THREAD_MULTIPLE, &provided);
    if (provided != SHMEM_THREAD_MULTIPLE) {
        fprintf(stderr, "Warning requested multi-thread level is not "
                        "supported \n");
    }
    num_pes = shmem_n_pes();
    my_pe = shmem_my_pe();
}

ro_net_status_t
OpenSHMEMTransport::initTransport(int num_queues)
{
    // setup a context per queue
    ctx_vec.resize(num_queues);
    for (int i = 0; i < ctx_vec.size(); i++) {
        NET_CHECK(shmem_ctx_create(SHMEM_CTX_SERIALIZED,
                                   ctx_vec.data() + i));
    }

    return RO_NET_SUCCESS;
}

ro_net_status_t
OpenSHMEMTransport::finalizeTransport()
{
    shmem_finalize();
    return RO_NET_SUCCESS;
}

ro_net_status_t
OpenSHMEMTransport::allocateMemory(void **ptr, size_t size)
{
    // TODO: only works for host memory
    if ((*ptr = shmem_malloc(size)) == nullptr)
        return RO_NET_OOM_ERROR;
    hipHostRegister_assert(*ptr, size, 0);
    return RO_NET_SUCCESS;
}

ro_net_status_t
OpenSHMEMTransport::deallocateMemory(void *ptr)
{
    hipHostUnregister_assert(ptr);
    shmem_free(ptr);
    return RO_NET_SUCCESS;
}

ro_net_status_t
OpenSHMEMTransport::barrier(int wg_id)
{
    shmem_barrier_all();
    return RO_NET_SUCCESS;
}

ro_net_status_t
OpenSHMEMTransport::reduction(void *dst, void *src, int size, int pe,
                              int wg_id, int start, int logPstride, int sizePE,
                              void *pWrk, long *pSync, RO_NET_Op op)
{
    assert(op == RO_NET_SUM);
    shmem_float_sum_to_all((float *) dst, (float *) src, size, pe, logPstride,
                           sizePE, (float *) pWrk, pSync);
    return RO_NET_SUCCESS;
}

ro_net_status_t
OpenSHMEMTransport::putMem(void *dst, void *src, int size, int pe, int wg_id)
{
    assert(wg_id < ctx_vec.size());
    shmem_ctx_putmem_nbi(ctx_vec[wg_id], dst, src, size, pe);
    return RO_NET_SUCCESS;
}

ro_net_status_t
OpenSHMEMTransport::getMem(void *dst, void *src, int size, int pe, int wg_id)
{
    assert(wg_id < ctx_vec.size());
    shmem_ctx_getmem_nbi(ctx_vec[wg_id], dst, src, size, pe);
    return RO_NET_SUCCESS;
}

ro_net_status_t
OpenSHMEMTransport::progress(int wg_id,
                             struct ro_net_handle *ronet_gpu_handle)
{
    // TODO: Might want to delay a quiet for a while to make sure we get
    // messages from other contexts injected before we block the service
    // thread.
    if (ronet_gpu_handle->needs_quiet[wg_id] ||
        ronet_gpu_handle->needs_blocking[wg_id]) {
        assert(wg_id < ctx_vec.size());
        shmem_ctx_quiet(ctx_vec[wg_id]);
        ronet_gpu_handle->needs_quiet[wg_id] = false;
        ronet_gpu_handle->needs_blocking[wg_id] = false;
        ronet_gpu_handle->queue_descs[wg_id].status = 1;

        #ifdef GPU_QUEUE
        SFENCE();
        hdp_flush(ro_net_gpu_handle->hdp_regs);
        #endif
    }

    return RO_NET_SUCCESS;
}

ro_net_status_t
OpenSHMEMTransport::quiet(int wg_id)
{
    return RO_NET_SUCCESS;
}

int
OpenSHMEMTransport::numOutstandingRequests()
{
    for (auto ctx : ctx_vec)
        shmem_ctx_quiet(ctx);
    return 0;
}

#endif
