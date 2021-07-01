/*
 * Copyright (C) by Argonne National Laboratory
 *     See COPYRIGHT in top-level directory
 */

#include "mpidimpl.h"
#include "ipc_pre.h"
#include "ipc_types.h"
#include "ipc_recv.h"
#include "ipc_control.h"

int MPIDI_IPCI_send_lmt_fin_cb(MPIDI_SHMI_ctrl_hdr_t * ctrl_hdr)
{
    int mpi_errno = MPI_SUCCESS;
    MPIR_Request *sreq = ctrl_hdr->ipc_slmt_fin.req_ptr;

    MPIR_FUNC_VERBOSE_STATE_DECL(MPID_STATE_MPIDI_IPCI_SEND_LMT_FIN_CB);
    MPIR_FUNC_VERBOSE_ENTER(MPID_STATE_MPIDI_IPCI_SEND_LMT_FIN_CB);

    IPC_TRACE("send_contig_lmt_fin_cb: complete sreq %p\n", sreq);

    MPIR_Datatype_release_if_not_builtin(MPIDIG_REQUEST(sreq, datatype));
    MPID_Request_complete(sreq);

  fn_exit:
    MPIR_FUNC_VERBOSE_EXIT(MPID_STATE_MPIDI_IPCI_SEND_LMT_FIN_CB);
    return mpi_errno;
}

int MPIDI_IPCI_send_lmt_rts_cb(MPIDI_SHMI_ctrl_hdr_t * ctrl_hdr)
{
    int mpi_errno = MPI_SUCCESS;
    MPIDI_IPC_ctrl_send_lmt_rts_t *slmt_rts_hdr = &ctrl_hdr->ipc_slmt_rts;
    MPIR_Request *rreq = NULL;
    MPIR_Comm *root_comm;
    MPIR_FUNC_VERBOSE_STATE_DECL(MPID_STATE_MPIDI_IPCI_SEND_LMT_RTS_CB);
    MPIR_FUNC_VERBOSE_ENTER(MPID_STATE_MPIDI_IPCI_SEND_LMT_RTS_CB);

    IPC_TRACE("send_contig_lmt_rts_cb: received data_sz 0x%" PRIu64 ", sreq_ptr 0x%p, "
              "src_lrank %d, match info[src_rank %d, tag %d, context_id 0x%x]\n",
              slmt_rts_hdr->data_sz, slmt_rts_hdr->sreq_ptr,
              slmt_rts_hdr->src_lrank, slmt_rts_hdr->src_rank, slmt_rts_hdr->tag,
              slmt_rts_hdr->context_id);

    /* Try to match a posted receive request. */
    while (TRUE) {
        rreq = MPIDIG_rreq_dequeue(slmt_rts_hdr->src_rank, slmt_rts_hdr->tag,
                                   slmt_rts_hdr->context_id, &MPIDI_global.posted_list,
                                   MPIDIG_PT2PT_POSTED);
#ifndef MPIDI_CH4_DIRECT_NETMOD
        if (rreq) {
            int is_cancelled;
            MPIDI_anysrc_try_cancel_partner(rreq, &is_cancelled);
            if (!is_cancelled) {
                MPIR_Datatype_release_if_not_builtin(MPIDIG_REQUEST(rreq, datatype));
                continue;
            }
            /* NOTE: NM partner is freed at MPIDI_anysrc_try_cancel_partner,
             * no need to call MPIDI_anysrc_free_partner at completions
             */
        }
#endif
        break;
    }

    if (rreq) {
        void *flattened_type = NULL;

        /* Matching receive was posted */
        MPIDIG_REQUEST(rreq, rank) = slmt_rts_hdr->src_rank;
        MPIDIG_REQUEST(rreq, tag) = slmt_rts_hdr->tag;
        MPIDIG_REQUEST(rreq, context_id) = slmt_rts_hdr->context_id;

        if (slmt_rts_hdr->flattened_type_size)
            flattened_type = slmt_rts_hdr->flattened_type;

        /* Complete IPC receive */
        mpi_errno = MPIDI_IPCI_handle_lmt_recv(slmt_rts_hdr->ipc_type,
                                               slmt_rts_hdr->ipc_handle,
                                               slmt_rts_hdr->data_sz, slmt_rts_hdr->sreq_ptr,
                                               flattened_type, rreq);
        MPIR_ERR_CHECK(mpi_errno);
    } else {
        /* Enqueue unexpected receive request */
        rreq = MPIDIG_request_create(MPIR_REQUEST_KIND__RECV, 2);
        MPIR_ERR_CHKANDSTMT(rreq == NULL, mpi_errno, MPIX_ERR_NOREQ, goto fn_fail, "**nomemreq");

        /* store CH4 am rreq info */
        MPIDIG_REQUEST(rreq, buffer) = NULL;
        MPIDIG_REQUEST(rreq, datatype) = MPI_BYTE;
        MPIDIG_REQUEST(rreq, count) = slmt_rts_hdr->data_sz;
        MPIDIG_REQUEST(rreq, rank) = slmt_rts_hdr->src_rank;
        MPIDIG_REQUEST(rreq, tag) = slmt_rts_hdr->tag;
        MPIDIG_REQUEST(rreq, context_id) = slmt_rts_hdr->context_id;
        MPIDI_REQUEST(rreq, is_local) = 1;

        /* store IPC internal info */
        MPIDI_SHM_REQUEST(rreq, status) |= MPIDI_SHM_REQ_IPC_SEND_LMT;
        MPIDI_IPCI_REQUEST(rreq, ipc_type) = slmt_rts_hdr->ipc_type;
        MPIDI_IPCI_REQUEST(rreq, unexp_rreq).ipc_handle = slmt_rts_hdr->ipc_handle;
        MPIDI_IPCI_REQUEST(rreq, unexp_rreq).data_sz = slmt_rts_hdr->data_sz;
        MPIDI_IPCI_REQUEST(rreq, unexp_rreq).src_lrank = slmt_rts_hdr->src_lrank;
        MPIDI_IPCI_REQUEST(rreq, unexp_rreq).sreq_ptr = slmt_rts_hdr->sreq_ptr;
        if (slmt_rts_hdr->flattened_type_size) {
            MPIDI_IPCI_REQUEST(rreq, unexp_rreq).flattened_type =
                MPL_malloc(slmt_rts_hdr->flattened_type_size, MPL_MEM_OTHER);
            memcpy(MPIDI_IPCI_REQUEST(rreq, unexp_rreq).flattened_type,
                   slmt_rts_hdr->flattened_type, slmt_rts_hdr->flattened_type_size);
        } else {
            MPIDI_IPCI_REQUEST(rreq, unexp_rreq).flattened_type = NULL;
        }

        MPIDIG_enqueue_request(rreq, &MPIDI_global.unexp_list, MPIDIG_PT2PT_UNEXP);

        IPC_TRACE("send_contig_lmt_rts_cb: enqueue unexpected, rreq=%p\n", rreq);
    }

  fn_exit:
    MPIR_FUNC_VERBOSE_EXIT(MPID_STATE_MPIDI_IPCI_SEND_LMT_RTS_CB);
    return mpi_errno;
  fn_fail:
    goto fn_exit;
}
