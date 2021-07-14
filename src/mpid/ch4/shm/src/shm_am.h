/*
 * Copyright (C) by Argonne National Laboratory
 *     See COPYRIGHT in top-level directory
 */

#ifndef SHM_AM_H_INCLUDED
#define SHM_AM_H_INCLUDED

#include <shm.h>
#include "../posix/shm_inline.h"
#include "../ipc/src/shm_inline.h"

MPL_STATIC_INLINE_PREFIX int MPIDI_SHM_am_send_hdr(int rank, MPIR_Comm * comm,
                                                   int handler_id, const void *am_hdr,
                                                   MPI_Aint am_hdr_sz)
{
    int ret;
    MPIR_FUNC_VERBOSE_STATE_DECL(MPID_STATE_MPIDI_SHM_AM_SEND_HDR);
    MPIR_FUNC_VERBOSE_ENTER(MPID_STATE_MPIDI_SHM_AM_SEND_HDR);

    ret = MPIDI_POSIX_am_send_hdr(rank, comm, MPIDI_POSIX_AM_HDR_CH4,
                                  handler_id, am_hdr, am_hdr_sz);

    MPIR_FUNC_VERBOSE_EXIT(MPID_STATE_MPIDI_SHM_AM_SEND_HDR);
    return ret;
}

MPL_STATIC_INLINE_PREFIX int MPIDI_SHM_am_isend(int rank, MPIR_Comm * comm, int handler_id,
                                                const void *am_hdr, MPI_Aint am_hdr_sz,
                                                const void *data, MPI_Aint count,
                                                MPI_Datatype datatype, MPIR_Request * sreq)
{
    int ret;
    bool dt_contig;
    MPI_Aint true_lb;
    void *vaddr;
    MPI_Aint data_sz;
    MPIDI_IPCI_ipc_attr_t ipc_attr;

    MPIR_FUNC_VERBOSE_STATE_DECL(MPID_STATE_MPIDI_SHM_AM_ISEND);
    MPIR_FUNC_VERBOSE_ENTER(MPID_STATE_MPIDI_SHM_AM_ISEND);

    //Switch based on request type choice
    switch(MPIDI_POSIX_AMREQUEST(sreq, am_type_choice)){

        //No choice was set, so we need to determine which path to chose
        case MPIDI_SHM_AMTYPE_NONE:
            //Get dt_contig & true_lb
            MPIDI_Datatype_check_size(datatype, count, data_sz);
            MPIDI_Datatype_check_contig_size_lb(datatype, count, dt_contig, data_sz, true_lb);

            //Get the address using the true_lb offset
            vaddr = (char *) data + true_lb;

            //Get GPU attribute
            MPIR_GPU_query_pointer_attr(vaddr, &ipc_attr.gpu_attr);

            //If message is too small use posix. Otherwise check
            //if the message is big enough for ipc. Worst case default to pipeline
            if(am_hdr_sz + data_sz) <= MPIDI_POSIX_am_eager_limit() {
                ret = MPIDI_POSIX_am_isend(rank, comm, MPIDI_POSIX_AM_HDR_CH4, handler_id, am_hdr,
                                           am_hdr_sz, data, count, datatype, sreq);
            } else {
                if(ipc_attr.gpu_attr.type == MPL_GPU_POINTER_DEV) {
                    if(data_sz >= ipc_attr.threshold.send_lmt_sz) {
                        ret = MPIDI_IPC_am_isend();
                    }   
                } else {
                    ret = MPIDI_POSIX_am_isend(rank, comm, MPIDI_POSIX_AM_HDR_CH4, handler_id, am_hdr,
                                               am_hdr_sz, data, count, datatype, sreq);
                }
            }

            break;

        case MPIDI_SHM_AMTYPE_SHORT:
        case MPIDI_SHM_AMTYPE_PIPELINE:
            ret = MPIDI_POSIX_am_isend(rank, comm, MPIDI_POSIX_AM_HDR_CH4, handler_id, am_hdr,
                                        am_hdr_sz, data, count, datatype, sreq);
            MPIDI_POSIX_AMREQUEST(sreq, am_type_choice) = MPIDI_SHM_AMTYPE_NONE;
            break;

        case MPIDI_SHM_AMTYPE_GPU_IPC:
            ret = MPIDI_IPC_am_isend();
            MPIDI_POSIX_AMREQUEST(sreq, am_type_choice) = MPIDI_SHM_AMTYPE_NONE;
            break;

        case MPIDI_SHM_AMTYPE_XPMEM_IPC:
            //Need to add xpmem code
            break;
    }       


    MPIR_FUNC_VERBOSE_EXIT(MPID_STATE_MPIDI_SHM_AM_ISEND);
    return ret;
}

MPL_STATIC_INLINE_PREFIX int MPIDI_SHM_am_send_hdr_reply(MPIR_Comm * comm,
                                                         int src_rank, int handler_id,
                                                         const void *am_hdr, MPI_Aint am_hdr_sz)
{
    int ret;

    MPIR_FUNC_VERBOSE_STATE_DECL(MPID_STATE_MPIDI_SHM_AM_SEND_HDR_REPLY);
    MPIR_FUNC_VERBOSE_ENTER(MPID_STATE_MPIDI_SHM_AM_SEND_HDR_REPLY);

    ret = MPIDI_POSIX_am_send_hdr_reply(comm, src_rank, MPIDI_POSIX_AM_HDR_CH4,
                                        handler_id, am_hdr, am_hdr_sz);

    MPIR_FUNC_VERBOSE_EXIT(MPID_STATE_MPIDI_SHM_AM_SEND_HDR_REPLY);
    return ret;
}

MPL_STATIC_INLINE_PREFIX int MPIDI_SHM_am_isend_reply(MPIR_Comm * comm,
                                                      int src_rank, int handler_id,
                                                      const void *am_hdr, MPI_Aint am_hdr_sz,
                                                      const void *data, MPI_Aint count,
                                                      MPI_Datatype datatype, MPIR_Request * sreq)
{
    int ret;

    MPIR_FUNC_VERBOSE_STATE_DECL(MPID_STATE_MPIDI_SHM_AM_ISEND_REPLY);
    MPIR_FUNC_VERBOSE_ENTER(MPID_STATE_MPIDI_SHM_AM_ISEND_REPLY);

    ret = MPIDI_POSIX_am_isend_reply(comm, src_rank, MPIDI_POSIX_AM_HDR_CH4,
                                     handler_id, am_hdr, am_hdr_sz, data, count, datatype, sreq);

    MPIR_FUNC_VERBOSE_EXIT(MPID_STATE_MPIDI_SHM_AM_ISEND_REPLY);
    return ret;
}

MPL_STATIC_INLINE_PREFIX int MPIDI_SHM_am_recv(MPIR_Request * rreq)
{
    int ret = MPI_SUCCESS;

    MPIR_FUNC_VERBOSE_STATE_DECL(MPID_STATE_MPIDI_SHM_AM_RECV);
    MPIR_FUNC_VERBOSE_ENTER(MPID_STATE_MPIDI_SHM_AM_RECV);

    /* TODO: handle IPC receive here */

    MPIR_FUNC_VERBOSE_EXIT(MPID_STATE_MPIDI_SHM_AM_RECV);
    return ret;
}

MPL_STATIC_INLINE_PREFIX MPI_Aint MPIDI_SHM_am_hdr_max_sz(void)
{
    return MPIDI_POSIX_am_hdr_max_sz();
}

MPL_STATIC_INLINE_PREFIX MPI_Aint MPIDI_SHM_am_eager_limit(void)
{
    return MPIDI_POSIX_am_eager_limit();
}

MPL_STATIC_INLINE_PREFIX MPI_Aint MPIDI_SHM_am_eager_buf_limit(void)
{
    return MPIDI_POSIX_am_eager_buf_limit();
}

MPL_STATIC_INLINE_PREFIX void MPIDI_SHM_am_request_init(MPIR_Request * req)
{
    MPIR_FUNC_VERBOSE_STATE_DECL(MPID_STATE_MPIDI_SHM_AM_REQUEST_INIT);
    MPIR_FUNC_VERBOSE_ENTER(MPID_STATE_MPIDI_SHM_AM_REQUEST_INIT);

    MPIDI_SHM_REQUEST(req, status) = 0;
    MPIDI_POSIX_am_request_init(req);

    MPIR_FUNC_VERBOSE_EXIT(MPID_STATE_MPIDI_SHM_AM_REQUEST_INIT);
}

MPL_STATIC_INLINE_PREFIX void MPIDI_SHM_am_request_finalize(MPIR_Request * req)
{
    MPIR_FUNC_VERBOSE_STATE_DECL(MPID_STATE_MPIDI_SHM_AM_REQUEST_FINALIZE);
    MPIR_FUNC_VERBOSE_ENTER(MPID_STATE_MPIDI_SHM_AM_REQUEST_FINALIZE);

    MPIDI_POSIX_am_request_finalize(req);

    MPIR_FUNC_VERBOSE_EXIT(MPID_STATE_MPIDI_SHM_AM_REQUEST_FINALIZE);
}

MPL_STATIC_INLINE_PREFIX bool MPIDI_SHM_am_check_eager(MPI_Aint am_hdr_sz, MPI_Aint data_sz,
                                                       const void *data, MPI_Aint count,
                                                       MPI_Datatype datatype, MPIR_Request * sreq)
{
    bool dt_contig;
    MPI_Aint true_lb;
    void *vaddr;
    MPIDI_IPCI_ipc_attr_t ipc_attr;

    //Get dt_contig & true_lb
    MPIDI_Datatype_check_contig_size_lb(datatype, count, dt_contig, data_sz, true_lb);

    //Get the address using the true_lb offset
    vaddr = (char *) data + true_lb;

    //Get GPU attribute
    MPIR_GPU_query_pointer_attr(vaddr, &ipc_attr.gpu_attr);

    //If message is too small use posix eager. Otherwise check
    //if the message is big enough for ipc. We save the decisions
    //in am_type_choice for later use.

    if(am_hdr_sz + data_sz) <= MPIDI_POSIX_am_eager_limit() {
        MPIDI_POSIX_AMREQUEST(sreq, am_type_choice) = MPIDI_SHM_AMTYPE_SHORT;
        return true;
    } else {
        if(ipc_attr.gpu_attr.type == MPL_GPU_POINTER_DEV) {
            if(data_sz >= ipc_attr.threshold.send_lmt_sz) {
                MPIDI_POSIX_AMREQUEST(sreq, am_type_choice) = MPIDI_SHM_AMTYPE_GPU_IPC;
                return true;
            }   
        } else {
            MPIDI_POSIX_AMREQUEST(sreq, am_type_choice) = MPIDI_SHM_AMTYPE_PIPELINE;
            return false;
        }
    } //Choice for XPMEM needs to be added
}

#endif /* SHM_AM_H_INCLUDED */
