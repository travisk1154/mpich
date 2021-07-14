/*
 * Copyright (C) by Argonne National Laboratory
 *     See COPYRIGHT in top-level directory
 */

#include "mpidimpl.h"
#include "mpidch4r.h"
#include "ch4r_callbacks.h"

static int handle_unexp_cmpl(MPIR_Request * rreq);
static int recv_target_cmpl_cb(MPIR_Request * rreq);

int MPIDIG_do_cts(MPIR_Request * rreq)
{
    int mpi_errno = MPI_SUCCESS;

    MPIDIG_send_cts_msg_t am_hdr;
    am_hdr.sreq_ptr = (MPIDIG_REQUEST(rreq, req->rreq.peer_req_ptr));
    am_hdr.rreq_ptr = rreq;
    MPIR_Assert((void *) am_hdr.sreq_ptr != NULL);

    MPL_DBG_MSG_FMT(MPIDI_CH4_DBG_GENERAL, VERBOSE,
                    (MPL_DBG_FDEST, "do cts req %p handle=0x%x", rreq, rreq->handle));

#ifdef MPIDI_CH4_DIRECT_NETMOD
    mpi_errno = MPIDI_NM_am_send_hdr_reply(rreq->comm,
                                           MPIDIG_REQUEST(rreq, rank), MPIDIG_SEND_CTS, &am_hdr,
                                           (MPI_Aint) sizeof(am_hdr));
#else
    if (MPIDI_REQUEST(rreq, is_local)) {
        mpi_errno = MPIDI_SHM_am_send_hdr_reply(rreq->comm,
                                                MPIDIG_REQUEST(rreq, rank),
                                                MPIDIG_SEND_CTS, &am_hdr, sizeof(am_hdr));
    } else {
        mpi_errno = MPIDI_NM_am_send_hdr_reply(rreq->comm,
                                               MPIDIG_REQUEST(rreq, rank),
                                               MPIDIG_SEND_CTS, &am_hdr, (MPI_Aint) sizeof(am_hdr));
    }
#endif
    MPIR_ERR_CHECK(mpi_errno);

  fn_exit:
    return mpi_errno;
  fn_fail:
    goto fn_exit;
}

/* Checks to make sure that the specified request is the next one expected to finish. If it isn't
 * supposed to finish next, it is appended to a list of requests to be retrieved later. */
int MPIDIG_check_cmpl_order(MPIR_Request * req)
{
    int ret = 0;

    MPIR_FUNC_VERBOSE_STATE_DECL(MPID_STATE_MPIDIG_CHECK_CMPL_ORDER);
    MPIR_FUNC_VERBOSE_ENTER(MPID_STATE_MPIDIG_CHECK_CMPL_ORDER);

    if (MPIDIG_REQUEST(req, req->seq_no) == MPL_atomic_load_uint64(&MPIDI_global.exp_seq_no)) {
        MPL_atomic_fetch_add_uint64(&MPIDI_global.exp_seq_no, 1);
        ret = 1;
        goto fn_exit;
    }

    MPIDIG_REQUEST(req, req->request) = req;
    /* MPIDI_CS_ENTER(); */
    DL_APPEND(MPIDI_global.cmpl_list, req->dev.ch4.am.req);
    /* MPIDI_CS_EXIT(); */

  fn_exit:
    MPIR_FUNC_VERBOSE_EXIT(MPID_STATE_MPIDIG_CHECK_CMPL_ORDER);
    return ret;
}

void MPIDIG_progress_compl_list(void)
{
    MPIR_Request *req;
    MPIDIG_req_ext_t *curr, *tmp;

    MPIR_FUNC_VERBOSE_STATE_DECL(MPID_STATE_MPIDIG_PROGRESS_COMPL_LIST);
    MPIR_FUNC_VERBOSE_ENTER(MPID_STATE_MPIDIG_PROGRESS_COMPL_LIST);

    /* MPIDI_CS_ENTER(); */
  do_check_again:
    DL_FOREACH_SAFE(MPIDI_global.cmpl_list, curr, tmp) {
        if (curr->seq_no == MPL_atomic_load_uint64(&MPIDI_global.exp_seq_no)) {
            DL_DELETE(MPIDI_global.cmpl_list, curr);
            req = curr->request;
            MPIDIG_REQUEST(req, req->target_cmpl_cb) (req);
            goto do_check_again;
        }
    }
    /* MPIDI_CS_EXIT(); */
    MPIR_FUNC_VERBOSE_EXIT(MPID_STATE_MPIDIG_PROGRESS_COMPL_LIST);
}

static int handle_unexp_cmpl(MPIR_Request * rreq)
{
    int mpi_errno = MPI_SUCCESS, in_use;
    MPIR_Request *match_req = NULL;
    size_t nbytes;
    int dt_contig;
    MPI_Aint dt_true_lb;
    MPIR_Datatype *dt_ptr;
    size_t dt_sz;

    MPIR_FUNC_VERBOSE_STATE_DECL(MPID_STATE_HANDLE_UNEXP_CMPL);
    MPIR_FUNC_VERBOSE_ENTER(MPID_STATE_HANDLE_UNEXP_CMPL);

    /* Check if this message has already been claimed by mprobe. */
    /* MPIDI_CS_ENTER(); */
    if (MPIDIG_REQUEST(rreq, req->status) & MPIDIG_REQ_UNEXP_DQUED) {
        /* This request has been claimed by mprobe */
        if (MPIDIG_REQUEST(rreq, req->status) & MPIDIG_REQ_UNEXP_CLAIMED) {
            /* mrecv has been already called */
            MPIDIG_handle_unexp_mrecv(rreq);
        } else {
            /* mrecv has not been called yet -- just take out the busy flag so that
             * mrecv in future knows this request is ready */
            MPIDIG_REQUEST(rreq, req->status) &= ~MPIDIG_REQ_BUSY;
        }
        /* MPIDI_CS_EXIT(); */
        goto fn_exit;
    }
    /* MPIDI_CS_EXIT(); */

    /* If this request was previously matched, but not handled */
    if (MPIDIG_REQUEST(rreq, req->status) & MPIDIG_REQ_MATCHED) {
        match_req = (MPIR_Request *) MPIDIG_REQUEST(rreq, req->rreq.match_req);

#ifndef MPIDI_CH4_DIRECT_NETMOD
        int is_cancelled;
        mpi_errno = MPIDI_anysrc_try_cancel_partner(match_req, &is_cancelled);
        MPIR_ERR_CHECK(mpi_errno);
        /* `is_cancelled` is assumed to be always true.
         * In typical config, anysrc partners won't occur if matching unexpected
         * message already exist.
         * In workq setup, since we will always progress shm first, when unexpected
         * message match, the NM partner wouldn't have progressed yet, so the cancel
         * should always succeed.
         */
        MPIR_Assert(is_cancelled);
#endif /* MPIDI_CH4_DIRECT_NETMOD */
    }

    /* If we didn't match the request, unmark the busy bit and skip the data movement below. */
    if (!match_req) {
        MPIDIG_REQUEST(rreq, req->status) &= ~MPIDIG_REQ_BUSY;
        goto fn_exit;
    }

    match_req->status.MPI_SOURCE = MPIDIG_REQUEST(rreq, rank);
    match_req->status.MPI_TAG = MPIDIG_REQUEST(rreq, tag);

    /* Figure out how much data needs to be moved. */
    MPIDI_Datatype_get_info(MPIDIG_REQUEST(match_req, count),
                            MPIDIG_REQUEST(match_req, datatype),
                            dt_contig, dt_sz, dt_ptr, dt_true_lb);
    MPIR_Datatype_get_size_macro(MPIDIG_REQUEST(match_req, datatype), dt_sz);

    /* Make sure this request has the right amount of data in it. */
    if (MPIDIG_REQUEST(rreq, count) > dt_sz * MPIDIG_REQUEST(match_req, count)) {
        rreq->status.MPI_ERROR = MPI_ERR_TRUNCATE;
        nbytes = dt_sz * MPIDIG_REQUEST(match_req, count);
    } else {
        rreq->status.MPI_ERROR = MPI_SUCCESS;
        nbytes = MPIDIG_REQUEST(rreq, count);   /* incoming message is always count of bytes. */
    }

    MPIR_STATUS_SET_COUNT(match_req->status, nbytes);
    MPIDIG_REQUEST(rreq, count) = dt_sz > 0 ? nbytes / dt_sz : 0;

    /* Perform the data copy (using the datatype engine if necessary for non-contig transfers) */
    if (!dt_contig) {
        MPI_Aint actual_unpack_bytes;
        mpi_errno = MPIR_Typerep_unpack(MPIDIG_REQUEST(rreq, buffer), nbytes,
                                        MPIDIG_REQUEST(match_req, buffer),
                                        MPIDIG_REQUEST(match_req, count),
                                        MPIDIG_REQUEST(match_req, datatype), 0,
                                        &actual_unpack_bytes);
        MPIR_ERR_CHECK(mpi_errno);

        if (actual_unpack_bytes != (MPI_Aint) nbytes) {
            mpi_errno = MPIR_Err_create_code(MPI_SUCCESS, MPIR_ERR_RECOVERABLE,
                                             __FUNCTION__, __LINE__,
                                             MPI_ERR_TYPE, "**dtypemismatch", 0);
            match_req->status.MPI_ERROR = mpi_errno;
        }
    } else {
        MPIR_Typerep_copy((char *) MPIDIG_REQUEST(match_req, buffer) + dt_true_lb,
                          MPIDIG_REQUEST(rreq, buffer), nbytes);
    }

    /* Now that the unexpected message has been completed, unset the status bit. */
    MPIDIG_REQUEST(rreq, req->status) &= ~MPIDIG_REQ_UNEXPECTED;

    /* If this is a synchronous send, send the reply back to the sender to unlock them. */
    if (MPIDIG_REQUEST(rreq, req->status) & MPIDIG_REQ_PEER_SSEND) {
        mpi_errno = MPIDIG_reply_ssend(rreq);
        MPIR_ERR_CHECK(mpi_errno);
    }
#ifndef MPIDI_CH4_DIRECT_NETMOD
    MPIDI_anysrc_free_partner(match_req);
#endif

    MPIR_Datatype_release_if_not_builtin(MPIDIG_REQUEST(match_req, datatype));
    if (MPIDIG_REQUEST(rreq, buffer)) {
        /* unexp pack buf is MPI_BYTE type, count == data size */
        MPIDU_genq_private_pool_free_cell(MPIDI_global.unexp_pack_buf_pool,
                                          MPIDIG_REQUEST(rreq, buffer));
    }
    MPIR_Object_release_ref(rreq, &in_use);
    MPID_Request_complete(rreq);
    MPID_Request_complete(match_req);
  fn_exit:
    MPIR_FUNC_VERBOSE_EXIT(MPID_STATE_HANDLE_UNEXP_CMPL);
    return mpi_errno;
  fn_fail:
    goto fn_exit;
}

/* This function is called when a receive has completed on the receiver side. The input is the
 * request that has been completed. */
static int recv_target_cmpl_cb(MPIR_Request * rreq)
{
    int mpi_errno = MPI_SUCCESS;

    MPIR_FUNC_VERBOSE_STATE_DECL(MPID_STATE_RECV_TARGET_CMPL_CB);
    MPIR_FUNC_VERBOSE_ENTER(MPID_STATE_RECV_TARGET_CMPL_CB);

    MPL_DBG_MSG_FMT(MPIDI_CH4_DBG_GENERAL, VERBOSE,
                    (MPL_DBG_FDEST, "req %p handle=0x%x", rreq, rreq->handle));

    /* Check if this request is supposed to complete next or if it should be delayed. */
    if (!MPIDIG_check_cmpl_order(rreq))
        return mpi_errno;

    MPIDIG_recv_finish(rreq);

    if (MPIDIG_REQUEST(rreq, req->status) & MPIDIG_REQ_UNEXPECTED) {
        mpi_errno = handle_unexp_cmpl(rreq);
        MPIR_ERR_CHECK(mpi_errno);
        goto fn_exit;
    }

    rreq->status.MPI_SOURCE = MPIDIG_REQUEST(rreq, rank);
    rreq->status.MPI_TAG = MPIDIG_REQUEST(rreq, tag);

    if (MPIDIG_REQUEST(rreq, req->status) & MPIDIG_REQ_PEER_SSEND) {
        mpi_errno = MPIDIG_reply_ssend(rreq);
        MPIR_ERR_CHECK(mpi_errno);
    }
#ifndef MPIDI_CH4_DIRECT_NETMOD
    MPIDI_anysrc_free_partner(rreq);
#endif

    MPIR_Datatype_release_if_not_builtin(MPIDIG_REQUEST(rreq, datatype));
    if ((MPIDIG_REQUEST(rreq, req->status) & MPIDIG_REQ_RTS) &&
        MPIDIG_REQUEST(rreq, req->rreq.match_req) != NULL) {
        /* This block is executed only when the receive is enqueued (handoff) &&
         * receive was matched with an unexpected long RTS message.
         * `rreq` is the unexpected message received and `sigreq` is the message
         * that came from CH4 (e.g. MPIDI_recv_safe) */
        MPIR_Request *sigreq = MPIDIG_REQUEST(rreq, req->rreq.match_req);
        sigreq->status = rreq->status;
        MPIR_Request_add_ref(sigreq);
        MPID_Request_complete(sigreq);
        /* Free the unexpected request on behalf of the user */
        MPIDI_CH4_REQUEST_FREE(rreq);
    }
    MPID_Request_complete(rreq);
  fn_exit:
    MPIDIG_progress_compl_list();
    MPIR_FUNC_VERBOSE_EXIT(MPID_STATE_RECV_TARGET_CMPL_CB);
    return mpi_errno;
  fn_fail:
    goto fn_exit;
}

int MPIDIG_send_origin_cb(MPIR_Request * sreq)
{
    int mpi_errno = MPI_SUCCESS;
    MPIR_FUNC_VERBOSE_STATE_DECL(MPID_STATE_MPIDIG_SEND_ORIGIN_CB);
    MPIR_FUNC_VERBOSE_ENTER(MPID_STATE_MPIDIG_SEND_ORIGIN_CB);
    MPID_Request_complete(sreq);
    MPIR_FUNC_VERBOSE_EXIT(MPID_STATE_MPIDIG_SEND_ORIGIN_CB);
    return mpi_errno;
}

int MPIDIG_send_data_origin_cb(MPIR_Request * sreq)
{
    int mpi_errno = MPI_SUCCESS;
    MPIR_FUNC_VERBOSE_STATE_DECL(MPID_STATE_MPIDIG_SEND_DATA_ORIGIN_CB);
    MPIR_FUNC_VERBOSE_ENTER(MPID_STATE_MPIDIG_SEND_DATA_ORIGIN_CB);
    MPIR_Datatype_release_if_not_builtin(MPIDIG_REQUEST(sreq, req->sreq).datatype);
    MPID_Request_complete(sreq);
    MPIR_FUNC_VERBOSE_EXIT(MPID_STATE_MPIDIG_SEND_DATA_ORIGIN_CB);
    return mpi_errno;
}

int MPIDIG_send_target_msg_cb(int handler_id, void *am_hdr, void *data, MPI_Aint in_data_sz,
                              int is_local, int is_async, MPIR_Request ** req)
{
    int mpi_errno = MPI_SUCCESS;
    MPIR_Request *rreq = NULL;
    MPIDIG_hdr_t *hdr = (MPIDIG_hdr_t *) am_hdr;
    void *pack_buf = NULL;
    bool do_cts = false;

    MPIR_FUNC_VERBOSE_STATE_DECL(MPID_STATE_MPIDIG_SEND_TARGET_MSG_CB);
    MPIR_FUNC_VERBOSE_ENTER(MPID_STATE_MPIDIG_SEND_TARGET_MSG_CB);
    MPL_DBG_MSG_FMT(MPIDI_CH4_DBG_GENERAL, VERBOSE,
                    (MPL_DBG_FDEST, "HDR: data_sz=%ld, flags=0x%X", hdr->data_sz, hdr->flags));
    /* MPIDI_CS_ENTER(); */
    while (TRUE) {
        rreq =
            MPIDIG_rreq_dequeue(hdr->src_rank, hdr->tag, hdr->context_id,
                                &MPIDI_global.posted_list, MPIDIG_PT2PT_POSTED);
#ifndef MPIDI_CH4_DIRECT_NETMOD
        if (rreq) {
            int is_cancelled;
            mpi_errno = MPIDI_anysrc_try_cancel_partner(rreq, &is_cancelled);
            MPIR_ERR_CHECK(mpi_errno);
            if (!is_cancelled) {
                MPIR_Datatype_release_if_not_builtin(MPIDIG_REQUEST(rreq, datatype));
                continue;
            }
        }
#endif /* MPIDI_CH4_DIRECT_NETMOD */
        break;
    }
    /* MPIDI_CS_EXIT(); */

    if (rreq == NULL) {
        rreq = MPIDIG_request_create(MPIR_REQUEST_KIND__RECV, 2);
        MPIR_ERR_CHKANDSTMT(rreq == NULL, mpi_errno, MPIX_ERR_NOREQ, goto fn_fail, "**nomemreq");
        /* for unexpected message, always recv as MPI_BYTE into unexpected buffer. They will be
         * set to the recv side datatype and count when it is matched */
        MPIDIG_REQUEST(rreq, datatype) = MPI_BYTE;
        MPIDIG_REQUEST(rreq, count) = hdr->data_sz;
        if (hdr->data_sz) {
            if (in_data_sz) {
                /* If there is inline data, we allocate unexpected buffer */
                MPIR_Assert(in_data_sz <= MPIR_CVAR_CH4_AM_PACK_BUFFER_SIZE);
                mpi_errno =
                    MPIDU_genq_private_pool_alloc_cell(MPIDI_global.unexp_pack_buf_pool, &pack_buf);
                MPIR_Assert(pack_buf);
                MPIDIG_REQUEST(rreq, buffer) = pack_buf;
            } else {
                /* if the SEND expect to contain data, but there is no inline data, mark the recv
                 * request as not ready. */
                MPIDIG_REQUEST(rreq, buffer) = NULL;
                MPIDIG_REQUEST(rreq, recv_ready) = false;
            }
        } else {
            /* The SEND is a zero byte messeage */
            MPIDIG_REQUEST(rreq, buffer) = NULL;
        }
        MPIDIG_REQUEST(rreq, rank) = hdr->src_rank;
        MPIDIG_REQUEST(rreq, tag) = hdr->tag;
        MPIDIG_REQUEST(rreq, context_id) = hdr->context_id;

        MPIDIG_REQUEST(rreq, req->status) |= MPIDIG_REQ_UNEXPECTED;
        rreq->status.MPI_ERROR = hdr->error_bits;
        if (hdr->flags & MPIDIG_AM_SEND_FLAGS_RTS) {
            /* this is unexpected RNDV */
            MPIDIG_REQUEST(rreq, req->rreq.peer_req_ptr) = hdr->sreq_ptr;
            MPIDIG_REQUEST(rreq, req->status) |= MPIDIG_REQ_RTS;
            MPIDIG_REQUEST(rreq, req->rreq.match_req) = NULL;
        } else {
            if (MPIDIG_REQUEST(rreq, recv_ready)) {
                MPIDIG_REQUEST(rreq, req->status) |= MPIDIG_REQ_BUSY;
            }
        }
#ifndef MPIDI_CH4_DIRECT_NETMOD
        MPIDI_REQUEST(rreq, is_local) = is_local;
#endif
        MPID_THREAD_CS_ENTER(VCI, MPIDIU_THREAD_MPIDIG_GLOBAL_MUTEX);
        MPIDIG_enqueue_request(rreq, &MPIDI_global.unexp_list, MPIDIG_PT2PT_UNEXP);
        MPID_THREAD_CS_EXIT(VCI, MPIDIU_THREAD_MPIDIG_GLOBAL_MUTEX);

        /* at this point, we have created and enqueued the unexpected request. If the request is
         * ready for recv, we increase the seq_no and init the recv */
        if (MPIDIG_REQUEST(rreq, recv_ready)) {
            MPIDIG_REQUEST(rreq, req->seq_no) =
                MPL_atomic_fetch_add_uint64(&MPIDI_global.nxt_seq_no, 1);
            MPL_DBG_MSG_FMT(MPIDI_CH4_DBG_GENERAL, VERBOSE,
                            (MPL_DBG_FDEST, "seq_no: me=%" PRIu64 " exp=%" PRIu64,
                             MPIDIG_REQUEST(rreq, req->seq_no),
                             MPL_atomic_load_uint64(&MPIDI_global.exp_seq_no)));
            MPIDIG_recv_type_init(hdr->data_sz, rreq);
        }
    } else {
        MPL_DBG_MSG_FMT(MPIDI_CH4_DBG_GENERAL, VERBOSE,
                        (MPL_DBG_FDEST, "posted req %p handle=0x%x", rreq, rreq->handle));

        MPIDIG_REQUEST(rreq, rank) = hdr->src_rank;
        MPIDIG_REQUEST(rreq, tag) = hdr->tag;
        MPIDIG_REQUEST(rreq, context_id) = hdr->context_id;
#ifndef MPIDI_CH4_DIRECT_NETMOD
        MPIDI_REQUEST(rreq, is_local) = is_local;
#endif

        rreq->status.MPI_ERROR = hdr->error_bits;
        if (hdr->flags & MPIDIG_AM_SEND_FLAGS_RTS) {
            /* this is expected RNDV, init a special recv into unexp buffer */
            MPIDIG_REQUEST(rreq, req->rreq.peer_req_ptr) = hdr->sreq_ptr;
            MPIDIG_REQUEST(rreq, req->status) |= MPIDIG_REQ_RTS;
            MPIDIG_REQUEST(rreq, req->rreq.match_req) = NULL;
            MPIDIG_recv_type_init(hdr->data_sz, rreq);
            do_cts = true;
        } else {
            MPIDIG_REQUEST(rreq, req->seq_no) =
                MPL_atomic_fetch_add_uint64(&MPIDI_global.nxt_seq_no, 1);
            MPL_DBG_MSG_FMT(MPIDI_CH4_DBG_GENERAL, VERBOSE,
                            (MPL_DBG_FDEST, "seq_no: me=%" PRIu64 " exp=%" PRIu64,
                             MPIDIG_REQUEST(rreq, req->seq_no),
                             MPL_atomic_load_uint64(&MPIDI_global.exp_seq_no)));
            MPIDIG_recv_type_init(hdr->data_sz, rreq);
        }
    }

    if (hdr->flags & MPIDIG_AM_SEND_FLAGS_SYNC) {
        MPIDIG_REQUEST(rreq, req->rreq.peer_req_ptr) = hdr->sreq_ptr;
        MPIDIG_REQUEST(rreq, req->status) |= MPIDIG_REQ_PEER_SSEND;
    }

    MPIDIG_REQUEST(rreq, req->status) |= MPIDIG_REQ_IN_PROGRESS;

    MPIDIG_REQUEST(rreq, req->target_cmpl_cb) = recv_target_cmpl_cb;

    if (is_async) {
        if (hdr->flags & MPIDIG_AM_SEND_FLAGS_RTS) {
            *req = NULL;
        } else {
            *req = rreq;
        }
    } else {
        if (!(hdr->flags & MPIDIG_AM_SEND_FLAGS_RTS)) {
            MPIDIG_recv_copy(data, rreq);
            MPIDIG_REQUEST(rreq, req->target_cmpl_cb) (rreq);
        }
    }

    if (do_cts) {
        MPIDIG_do_cts(rreq);
    }

  fn_exit:
    MPIR_FUNC_VERBOSE_EXIT(MPID_STATE_MPIDIG_SEND_TARGET_MSG_CB);
    return mpi_errno;
  fn_fail:
    goto fn_exit;
}

int MPIDIG_send_data_target_msg_cb(int handler_id, void *am_hdr, void *data, MPI_Aint in_data_sz,
                                   int is_local, int is_async, MPIR_Request ** req)
{
    int mpi_errno = MPI_SUCCESS;
    MPIR_Request *rreq;
    MPIDIG_send_data_msg_t *seg_hdr = (MPIDIG_send_data_msg_t *) am_hdr;

    MPIR_FUNC_VERBOSE_STATE_DECL(MPID_STATE_MPIDIG_SEND_DATA_TARGET_MSG_CB);
    MPIR_FUNC_VERBOSE_ENTER(MPID_STATE_MPIDIG_SEND_DATA_TARGET_MSG_CB);

    rreq = (MPIR_Request *) seg_hdr->rreq_ptr;
    MPIR_Assert(rreq);

    MPIDIG_REQUEST(rreq, req->seq_no) = MPL_atomic_fetch_add_uint64(&MPIDI_global.nxt_seq_no, 1);
    MPL_DBG_MSG_FMT(MPIDI_CH4_DBG_GENERAL, VERBOSE,
                    (MPL_DBG_FDEST, "seq_no: me=%" PRIu64 " exp=%" PRIu64,
                     MPIDIG_REQUEST(rreq, req->seq_no),
                     MPL_atomic_load_uint64(&MPIDI_global.exp_seq_no)));

    if (is_async) {
        *req = rreq;
    } else {
        MPIDIG_recv_copy(data, rreq);
        MPIDIG_REQUEST(rreq, req->target_cmpl_cb) (rreq);
    }

    MPIR_FUNC_VERBOSE_EXIT(MPID_STATE_MPIDIG_SEND_DATA_TARGET_MSG_CB);
    return mpi_errno;
}

int MPIDIG_ssend_ack_target_msg_cb(int handler_id, void *am_hdr, void *data, MPI_Aint in_data_sz,
                                   int is_local, int is_async, MPIR_Request ** req)
{
    int mpi_errno = MPI_SUCCESS;
    MPIR_Request *sreq;
    MPIDIG_ssend_ack_msg_t *msg_hdr = (MPIDIG_ssend_ack_msg_t *) am_hdr;
    MPIR_FUNC_VERBOSE_STATE_DECL(MPID_STATE_MPIDIG_SSEND_ACK_TARGET_MSG_CB);
    MPIR_FUNC_VERBOSE_ENTER(MPID_STATE_MPIDIG_SSEND_ACK_TARGET_MSG_CB);

    sreq = (MPIR_Request *) msg_hdr->sreq_ptr;
    MPID_Request_complete(sreq);

    if (is_async)
        *req = NULL;

    MPIR_FUNC_VERBOSE_EXIT(MPID_STATE_MPIDIG_SSEND_ACK_TARGET_MSG_CB);
    return mpi_errno;
}

int MPIDIG_send_cts_target_msg_cb(int handler_id, void *am_hdr, void *data, MPI_Aint in_data_sz,
                                  int is_local, int is_async, MPIR_Request ** req)
{
    int mpi_errno = MPI_SUCCESS;
    MPIR_Request *sreq;
    MPIDIG_send_cts_msg_t *msg_hdr = (MPIDIG_send_cts_msg_t *) am_hdr;
    MPIDIG_send_data_msg_t send_hdr;
    MPIR_FUNC_VERBOSE_STATE_DECL(MPID_STATE_MPIDIG_SEND_CTS_TARGET_MSG_CB);
    MPIR_FUNC_VERBOSE_ENTER(MPID_STATE_MPIDIG_SEND_CTS_TARGET_MSG_CB);

    sreq = (MPIR_Request *) msg_hdr->sreq_ptr;
    MPIR_Assert(sreq != NULL);

    MPL_DBG_MSG_FMT(MPIDI_CH4_DBG_GENERAL, VERBOSE,
                    (MPL_DBG_FDEST, "got cts req handle=0x%x", sreq->handle));

    /* Start the main data transfer */
    send_hdr.rreq_ptr = msg_hdr->rreq_ptr;
#ifndef MPIDI_CH4_DIRECT_NETMOD
    if (MPIDI_REQUEST(sreq, is_local))
        mpi_errno =
            MPIDI_SHM_am_isend_reply(sreq->comm,
                                     MPIDIG_REQUEST(sreq, rank), MPIDIG_SEND_DATA,
                                     &send_hdr, (MPI_Aint) sizeof(send_hdr),
                                     MPIDIG_REQUEST(sreq, req->sreq).src_buf,
                                     MPIDIG_REQUEST(sreq, req->sreq).count,
                                     MPIDIG_REQUEST(sreq, req->sreq).datatype, sreq);
    else
#endif
    {
        mpi_errno =
            MPIDI_NM_am_isend_reply(sreq->comm,
                                    MPIDIG_REQUEST(sreq, rank), MPIDIG_SEND_DATA,
                                    &send_hdr, (MPI_Aint) sizeof(send_hdr),
                                    MPIDIG_REQUEST(sreq, req->sreq).src_buf,
                                    MPIDIG_REQUEST(sreq, req->sreq).count,
                                    MPIDIG_REQUEST(sreq, req->sreq).datatype, sreq);
    }

    MPIR_ERR_CHECK(mpi_errno);

    MPIR_FUNC_VERBOSE_EXIT(MPID_STATE_MPIDIG_SEND_CTS_TARGET_MSG_CB);

  fn_exit:
    return mpi_errno;
  fn_fail:
    goto fn_exit;
}

int MPIDIG_send_ipc_datatype_req_target_msg_cb(int handler_id, void *am_hdr, void *data,
                                               MPI_Aint in_data_sz, int is_local, int is_async,
                                               MPIR_Request ** req)
{
    MPIR_Request *rreq = *req;
    int mpi_errno = MPI_SUCCESS;
    MPIDIG_IPC_hdr_t *ipc_hdr = (MPIDIG_IPC_hdr_t *) am_hdr;

    MPIR_FUNC_VERBOSE_STATE_DECL(MPID_STATE_MPIDIG_SEND_IPC_DATATYPE_REQ_TARGET_MSG_CB);
    MPIR_FUNC_VERBOSE_ENTER(MPID_STATE_MPIDIG_SEND_IPC_DATATYPE_REQ_TARGET_MSG_CB);

    MPIDI_SHM_REQUEST(rreq, ipc).ipc_buf = MPIDIG_REQUEST(rreq, buffer);
    MPIDI_SHM_REQUEST(rreq, ipc).ipc_count = MPIDIG_REQUEST(rreq, count);
    MPIDI_SHM_REQUEST(rreq, ipc).ipc_datatype = MPIDIG_REQUEST(rreq, datatype);

    MPIDIG_REQUEST(rreq, buffer) = MPL_malloc(in_data_sz, MPL_MEM_OTHER);
    MPIDIG_REQUEST(rreq, count) = in_data_sz;
    MPIDIG_REQUEST(rreq, datatype) = MPI_BYTE;

    MPIDIG_REQUEST(rreq, req->target_cmpl_cb) = recv_target_ipc_rdma_cb;
    MPIDI_SHM_REQUEST(rreq, ipc).sreq_ptr = ipc_hdr->sreq;

  fn_exit:
    MPIR_FUNC_VERBOSE_EXIT(MPID_STATE_MPIDIG_SEND_IPC_DATATYPE_REQ_TARGET_MSG_CB);
    return mpi_errno;
  fn_fail:
    goto fn_exit;
}

int MPIDIG_send_ipc_datatype_ack_origin_cb(int handler_id, void *am_hdr, void *data,
                                           MPI_Aint in_data_sz, int is_local, int is_async,
                                           MPIR_Request ** req)
{
    MPIR_Request *rreq;
    MPIR_FUNC_VERBOSE_STATE_DECL(MPID_STATE_MPIDIG_SEND_IPC_DATATYPE_ACK_ORIGIN_CB);
    MPIR_FUNC_VERBOSE_ENTER(MPID_STATE_MPIDIG_SEND_IPC_DATATYPE_ACK_ORIGIN_CB);

    rreq = *req;
    MPIR_Datatype_release_if_not_builtin(MPIDIG_REQUEST(rreq, datatype));
    MPID_Request_complete(rreq);

    MPIR_FUNC_VERBOSE_EXIT(MPID_STATE_MPIDIG_SEND_IPC_DATATYPE_ACK_ORIGIN_CB);
    return MPI_SUCCESS;
}

int recv_target_ipc_rdma_cb(MPIR_Request * rreq)
{
    int mpi_errno = MPI_SUCCESS;
    MPIDIG_IPC_hdr_t ipc_rdma_ack;

    MPIR_FUNC_VERBOSE_STATE_DECL(MPID_STATE_RECV_TARGET_IPC_RDMA_CB);
    MPIR_FUNC_VERBOSE_ENTER(MPID_STATE_RECV_TARGET_IPC_RDMA_CB);

    mpi_errno = MPIDI_SHM_am_recv(rreq);
    MPIR_ERR_CHECK(mpi_errno);

    ipc_rdma_ack.sreq = MPIDI_SHM_REQUEST(rreq, ipc).sreq;
    int rank = MPIDIG_REQUEST(rreq, rank);
    MPIR_Comm *comm = MPIDIG_context_id_to_comm(MPIDIG_REQUEST(rreq, context_id));

    mpi_errno = MPIDI_SHM_am_send_hdr(rank, comm, MPIDIG_IPC_DATATYPE_ACK, &ipc_rdma_ack,
                                      sizeof(MPIDIG_IPC_hdr_t));

  fn_exit:
    MPIR_FUNC_VERBOSE_EXIT(MPID_STATE_RECV_TARGET_IPC_RDMA_CB);
    return mpi_errno;
  fn_fail:
    goto fn_exit;
}

