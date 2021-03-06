/*
 * Copyright (C) by Argonne National Laboratory
 *     See COPYRIGHT in top-level directory
 */

#include "mpiimpl.h"
#include "algo_common.h"
#include "treealgo.h"

/* Routine to schedule linear algorithm for neighbor_alltoallw */
int MPIR_TSP_Ineighbor_alltoallw_sched_allcomm_linear(const void *sendbuf,
                                                      const MPI_Aint sendcounts[],
                                                      const MPI_Aint sdispls[],
                                                      const MPI_Datatype sendtypes[], void *recvbuf,
                                                      const MPI_Aint recvcounts[],
                                                      const MPI_Aint rdispls[],
                                                      const MPI_Datatype recvtypes[],
                                                      MPIR_Comm * comm_ptr, MPIR_TSP_sched_t sched)
{
    int mpi_errno = MPI_SUCCESS;
    int indegree, outdegree, weighted;
    int k, l;
    int *srcs, *dsts;
    int tag;

    MPIR_FUNC_VERBOSE_STATE_DECL(MPID_STATE_MPIR_TSP_INEIGHBOR_ALLTOALLW_SCHED_ALLCOMM_LINEAR);
    MPIR_FUNC_VERBOSE_ENTER(MPID_STATE_MPIR_TSP_INEIGHBOR_ALLTOALLW_SCHED_ALLCOMM_LINEAR);

    MPIR_CHKLMEM_DECL(2);

    mpi_errno = MPIR_Topo_canon_nhb_count(comm_ptr, &indegree, &outdegree, &weighted);
    MPIR_ERR_CHECK(mpi_errno);
    MPIR_CHKLMEM_MALLOC(srcs, int *, indegree * sizeof(int), mpi_errno, "srcs", MPL_MEM_COMM);
    MPIR_CHKLMEM_MALLOC(dsts, int *, outdegree * sizeof(int), mpi_errno, "dsts", MPL_MEM_COMM);
    mpi_errno = MPIR_Topo_canon_nhb(comm_ptr,
                                    indegree, srcs, MPI_UNWEIGHTED,
                                    outdegree, dsts, MPI_UNWEIGHTED);
    MPIR_ERR_CHECK(mpi_errno);

    /* For correctness, transport based collectives need to get the
     * tag from the same pool as schedule based collectives */
    mpi_errno = MPIR_Sched_next_tag(comm_ptr, &tag);
    MPIR_ERR_CHECK(mpi_errno);

    for (k = 0; k < outdegree; ++k) {
        char *sb;

        sb = ((char *) sendbuf) + sdispls[k];
        MPIR_TSP_sched_isend(sb, sendcounts[k], sendtypes[k], dsts[k], tag, comm_ptr, sched, 0,
                             NULL);
    }

    for (l = 0; l < indegree; ++l) {
        char *rb;

        rb = ((char *) recvbuf) + rdispls[l];
        MPIR_TSP_sched_irecv(rb, recvcounts[l], recvtypes[l], srcs[l], tag, comm_ptr, sched, 0,
                             NULL);
    }

  fn_exit:
    MPIR_CHKLMEM_FREEALL();
    MPIR_FUNC_VERBOSE_EXIT(MPID_STATE_MPIR_TSP_INEIGHBOR_ALLTOALLW_SCHED_ALLCOMM_LINEAR);
    return mpi_errno;
  fn_fail:
    goto fn_exit;
}
