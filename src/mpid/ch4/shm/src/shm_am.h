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

    MPIR_FUNC_VERBOSE_STATE_DECL(MPID_STATE_MPIDI_SHM_AM_ISEND);
    MPIR_FUNC_VERBOSE_ENTER(MPID_STATE_MPIDI_SHM_AM_ISEND);

    ret = MPIDI_POSIX_am_isend(rank, comm, MPIDI_POSIX_AM_HDR_CH4, handler_id, am_hdr,
                               am_hdr_sz, data, count, datatype, sreq);

    MPIR_FUNC_VERBOSE_EXIT(MPID_STATE_MPIDI_SHM_AM_ISEND);
    return ret;
}

MPL_STATIC_INLINE_PREFIX int MPIDI_SHM_am_isendv(int rank, MPIR_Comm * comm, int handler_id,
                                                 struct iovec *am_hdrs, size_t iov_len,
                                                 const void *data, MPI_Aint count,
                                                 MPI_Datatype datatype, MPIR_Request * sreq)
{
    int ret;

    MPIR_FUNC_VERBOSE_STATE_DECL(MPID_STATE_MPIDI_SHM_AM_ISENDV);
    MPIR_FUNC_VERBOSE_ENTER(MPID_STATE_MPIDI_SHM_AM_ISENDV);

    ret = MPIDI_POSIX_am_isendv(rank, comm, MPIDI_POSIX_AM_HDR_CH4, handler_id, am_hdrs,
                                iov_len, data, count, datatype, sreq);

    MPIR_FUNC_VERBOSE_EXIT(MPID_STATE_MPIDI_SHM_AM_ISENDV);
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
    MPIDIG_IPC_hdr_t ipc_rdma_ack;
    MPI_Aint ipc_hdr_sz;
    void *ipc_hdr;
    int count;

    MPIR_FUNC_VERBOSE_STATE_DECL(MPID_STATE_MPIDI_SHM_AM_RECV);
    MPIR_FUNC_VERBOSE_ENTER(MPID_STATE_MPIDI_SHM_AM_RECV);

    /* TODO: handle IPC receive here */
    ipc_hdr = MPIDIG_REQUEST(rreq, buffer);
    MPIDIG_REQUEST(rreq, buffer) = MPIDI_SHM_REQUEST(rreq, ipc).ipc_buf;
    MPIDIG_REQ1UEST(rreq, count) = MPIDI_SHM_REQUEST(rreq, ipc).ipc_count;
    MPIDIG_REQUEST(rreq, datatype) = MPIDI_SHM_REQUEST(rreq, ipc).ipc_datatype;
    MPIDI_Datatype_check_size(MPIDIG_REQUEST(rreq, datatype), count, ipc_hdr_sz);

    //MPIDI_IPC_am_recv(MPIDI_POSIX_AMREQUEST_HDR(rreq, buf), rreq);
    MPIDI_IPC_am_recv(ipc_hdr, ipc_hdr_sz, rreq);

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
    vaddr = (char *) buf + true_lb;


    //If message is too small use posix eager
    if(am_hdr_sz + data_sz) <= MPIDI_POSIX_am_eager_limit() {
        return true;
    } else {
        //Get GPU attribute
        MPIR_GPU_query_pointer_attr(vaddr, &ipc_attr.gpu_attr);
        if (ipc_attr.gpu_attr.type == MPL_GPU_POINTER_DEV) {
            if(data_sz >= ipc_attr.threshold.send_lmt_sz) {
                return true;
            }   
        } else {
            return false;
        }
    }
}

MPL_STATIC_INLINE_PREFIX int MPIDI_SHM_am_recv_rdma_read(void *lmt_msg, size_t recv_data_sz,
                                                         MPIR_Request * rreq)
{
    int mpi_errno = MPI_SUCCESS;
    MPIR_FUNC_VERBOSE_STATE_DECL(MPID_STATE_MPIDI_SHM_AM_RECV_RDMA);
    MPIR_FUNC_VERBOSE_ENTER(MPID_STATE_MPIDI_SHM_AM_RECV_RDMA);

    mpi_errno = MPIDI_IPC_am_recv_rdma_read(lmt_msg, recv_data_sz, rreq);

    MPIR_FUNC_VERBOSE_EXIT(MPID_STATE_MPIDI_SHM_AM_RECV_RDMA);
    return mpi_errno;
}
#endif /* SHM_AM_H_INCLUDED */
