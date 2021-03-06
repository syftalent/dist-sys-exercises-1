#include "hcoll.h"
#include "hcoll_dtypes.h"

#undef FUNCNAME
#define FUNCNAME hcoll_Barrier
#undef FCNAME
#define FCNAME MPIU_QUOTE(FUNCNAME)
int hcoll_Barrier(MPID_Comm * comm_ptr, int *err)
{
    int rc;
    MPI_Comm comm = comm_ptr->handle;
    MPIU_DBG_MSG(CH3_OTHER, VERBOSE, "RUNNING HCOL BARRIER.");
    rc = hcoll_collectives.coll_barrier(comm_ptr->hcoll_priv.hcoll_context);
    if (HCOLL_SUCCESS != rc) {
        MPIU_DBG_MSG(CH3_OTHER, VERBOSE, "RUNNING FALLBACK BARRIER.");
        void *ptr = comm_ptr->coll_fns->Barrier;
        comm_ptr->coll_fns->Barrier =
            (NULL != comm_ptr->hcoll_priv.hcoll_origin_coll_fns) ?
            comm_ptr->hcoll_priv.hcoll_origin_coll_fns->Barrier : NULL;
        rc = MPI_Barrier(comm);
        comm_ptr->coll_fns->Barrier = ptr;
        MPIU_DBG_MSG(CH3_OTHER, VERBOSE, "RUNNING FALLBACK BARRIER - done.");
    }
    return rc;
}

#undef FUNCNAME
#define FUNCNAME hcoll_Bcast
#undef FCNAME
#define FCNAME MPIU_QUOTE(FUNCNAME)
int hcoll_Bcast(void *buffer, int count, MPI_Datatype datatype, int root,
                MPID_Comm * comm_ptr, int *err)
{
    dte_data_representation_t dtype;
    int rc;
    MPIU_DBG_MSG(CH3_OTHER, VERBOSE, "RUNNING HCOLL BCAST.");
    dtype = mpi_dtype_2_dte_dtype(datatype);
    int is_homogeneous = 1, use_fallback = 0;
    MPI_Comm comm = comm_ptr->handle;
#ifdef MPID_HAS_HETERO
    if (comm_ptr->is_hetero)
        is_homogeneous = 0;
#endif
    if (HCOL_DTE_IS_COMPLEX(dtype) || HCOL_DTE_IS_ZERO(dtype) || (0 == is_homogeneous)) {
        /*If we are here then datatype is not simple predefined datatype */
        /*In future we need to add more complex mapping to the dte_data_representation_t */
        /* Now use fallback */
        MPIU_DBG_MSG(CH3_OTHER, VERBOSE, "unsupported data layout, calling fallback bcast.");
        use_fallback = 1;
    }
    else {
        rc = hcoll_collectives.coll_bcast(buffer, count, dtype, root,
                                          comm_ptr->hcoll_priv.hcoll_context);
        if (HCOLL_SUCCESS != rc) {
            use_fallback = 1;
        }
    }
    if (1 == use_fallback) {
        MPIU_DBG_MSG(CH3_OTHER, VERBOSE, "RUNNING FALLBACK BCAST - done.");
        void *ptr = comm_ptr->coll_fns->Bcast;
        comm_ptr->coll_fns->Bcast =
            (NULL != comm_ptr->hcoll_priv.hcoll_origin_coll_fns) ?
            comm_ptr->hcoll_priv.hcoll_origin_coll_fns->Bcast : NULL;
        rc = MPI_Bcast(buffer, count, datatype, root, comm);
        comm_ptr->coll_fns->Bcast = ptr;
        MPIU_DBG_MSG(CH3_OTHER, VERBOSE, "RUNNING FALLBACK BCAST - done.");
    }
    return rc;
}

#undef FUNCNAME
#define FUNCNAME hcoll_Allreduce
#undef FCNAME
#define FCNAME MPIU_QUOTE(FUNCNAME)
int hcoll_Allreduce(const void *sendbuf, void *recvbuf, int count, MPI_Datatype datatype,
                    MPI_Op op, MPID_Comm * comm_ptr, int *err)
{
    dte_data_representation_t Dtype;
    hcoll_dte_op_t *Op;
    int rc;
    int is_homogeneous = 1, use_fallback = 0;
    MPI_Comm comm = comm_ptr->handle;
#ifdef MPID_HAS_HETERO
    if (comm_ptr->is_hetero)
        is_homogeneous = 0;
#endif

    MPIU_DBG_MSG(CH3_OTHER, VERBOSE, "RUNNING HCOL ALLREDUCE.");
    Dtype = mpi_dtype_2_dte_dtype(datatype);
    Op = mpi_op_2_dte_op(op);
    if (MPI_IN_PLACE == sendbuf) {
        sendbuf = HCOLL_IN_PLACE;
    }
    if (HCOL_DTE_IS_COMPLEX(Dtype) || HCOL_DTE_IS_ZERO(Dtype) || (0 == is_homogeneous) ||
        (HCOL_DTE_OP_NULL == Op->id)) {
        MPIU_DBG_MSG(CH3_OTHER, VERBOSE, "unsupported data layout, calling fallback allreduce.");
        use_fallback = 1;
    }
    else {
        rc = hcoll_collectives.coll_allreduce(sendbuf, recvbuf, count, Dtype, Op,
                                              comm_ptr->hcoll_priv.hcoll_context);
        if (HCOLL_SUCCESS != rc) {
            use_fallback = 1;
        }
    }
    if (1 == use_fallback) {
        if (HCOLL_IN_PLACE == sendbuf) {
            sendbuf = MPI_IN_PLACE;
        }
        MPIU_DBG_MSG(CH3_OTHER, VERBOSE, "RUNNING FALLBACK ALLREDUCE.");
        void *ptr = comm_ptr->coll_fns->Allreduce;
        comm_ptr->coll_fns->Allreduce =
            (NULL != comm_ptr->hcoll_priv.hcoll_origin_coll_fns) ?
            comm_ptr->hcoll_priv.hcoll_origin_coll_fns->Allreduce : NULL;
        rc = MPI_Allreduce(sendbuf, recvbuf, count, datatype, op, comm);
        comm_ptr->coll_fns->Allreduce = ptr;
        MPIU_DBG_MSG(CH3_OTHER, VERBOSE, "RUNNING FALLBACK ALLREDUCE done.");
    }
    return rc;
}

#undef FUNCNAME
#define FUNCNAME hcoll_Allgather
#undef FCNAME
#define FCNAME MPIU_QUOTE(FUNCNAME)
int hcoll_Allgather(const void *sbuf, int scount, MPI_Datatype sdtype,
                    void *rbuf, int rcount, MPI_Datatype rdtype, MPID_Comm * comm_ptr, int *err)
{
    int is_homogeneous = 1, use_fallback = 0;
    MPI_Comm comm = comm_ptr->handle;
    dte_data_representation_t stype;
    dte_data_representation_t rtype;
    int rc;
    is_homogeneous = 1;
#ifdef MPID_HAS_HETERO
    if (comm_ptr->is_hetero)
        is_homogeneous = 0;
#endif

    MPIU_DBG_MSG(CH3_OTHER, VERBOSE, "RUNNING HCOLL ALLGATHER.");
    stype = mpi_dtype_2_dte_dtype(sdtype);
    rtype = mpi_dtype_2_dte_dtype(rdtype);
    if (MPI_IN_PLACE == sbuf) {
        sbuf = HCOLL_IN_PLACE;
    }
    if (HCOL_DTE_IS_COMPLEX(stype) || HCOL_DTE_IS_ZERO(stype) || HCOL_DTE_IS_ZERO(rtype) ||
        HCOL_DTE_IS_COMPLEX(rtype) || is_homogeneous == 0) {
        MPIU_DBG_MSG(CH3_OTHER, VERBOSE, "unsupported data layout; calling fallback allgather.");
        use_fallback = 1;
    }
    else {
        rc = hcoll_collectives.coll_allgather(sbuf, scount, stype, rbuf, rcount, rtype,
                                              comm_ptr->hcoll_priv.hcoll_context);
        if (HCOLL_SUCCESS != rc) {
            use_fallback = 1;
        }
    }
    if (1 == use_fallback) {
        if (HCOLL_IN_PLACE == sbuf) {
            sbuf = MPI_IN_PLACE;
        }
        MPIU_DBG_MSG(CH3_OTHER, VERBOSE, "RUNNING FALLBACK ALLGATHER.");
        void *ptr = comm_ptr->coll_fns->Allgather;
        comm_ptr->coll_fns->Allgather =
            (NULL != comm_ptr->hcoll_priv.hcoll_origin_coll_fns) ?
            comm_ptr->hcoll_priv.hcoll_origin_coll_fns->Allgather : NULL;
        rc = MPI_Allgather(sbuf, scount, sdtype, rbuf, rcount, rdtype, comm);
        comm_ptr->coll_fns->Allgather = ptr;
        MPIU_DBG_MSG(CH3_OTHER, VERBOSE, "RUNNING FALLBACK ALLGATHER - done.");
    }
    return rc;
}

#undef FUNCNAME
#define FUNCNAME hcoll_Ibarrier_req
#undef FCNAME
#define FCNAME MPIU_QUOTE(FUNCNAME)
int hcoll_Ibarrier_req(MPID_Comm * comm_ptr, MPID_Request ** request)
{
    int rc;
    void **rt_handle;
    MPI_Comm comm;
    MPI_Request req;
    comm = comm_ptr->handle;
    MPIU_DBG_MSG(CH3_OTHER, VERBOSE, "RUNNING HCOL IBARRIER.");
    rt_handle = (void **) request;
    rc = hcoll_collectives.coll_ibarrier(comm_ptr->hcoll_priv.hcoll_context, rt_handle);
    if (HCOLL_SUCCESS != rc) {
        MPIU_DBG_MSG(CH3_OTHER, VERBOSE, "RUNNING FALLBACK IBARRIER.");
        void *ptr = comm_ptr->coll_fns->Ibarrier_req;
        comm_ptr->coll_fns->Ibarrier_req =
            (comm_ptr->hcoll_priv.hcoll_origin_coll_fns !=
             NULL) ? comm_ptr->hcoll_priv.hcoll_origin_coll_fns->Ibarrier_req : NULL;
        rc = MPI_Ibarrier(comm, &req);
        MPID_Request_get_ptr(req, *request);
        comm_ptr->coll_fns->Ibarrier_req = ptr;
        MPIU_DBG_MSG(CH3_OTHER, VERBOSE, "RUNNING FALLBACK IBARRIER - done.");
    }
    return rc;
}

#undef FUNCNAME
#define FUNCNAME hcoll_Ibcast_req
#undef FCNAME
#define FCNAME MPIU_QUOTE(FUNCNAME)
int hcoll_Ibcast_req(void *buffer, int count, MPI_Datatype datatype, int root,
                     MPID_Comm * comm_ptr, MPID_Request ** request)
{
    int rc;
    void **rt_handle;
    dte_data_representation_t dtype;
    MPIU_DBG_MSG(CH3_OTHER, VERBOSE, "RUNNING HCOLL IBCAST.");
    dtype = mpi_dtype_2_dte_dtype(datatype);
    int is_homogeneous = 1, use_fallback = 0;
    MPI_Comm comm = comm_ptr->handle;
    MPI_Request req;
    rt_handle = (void **) request;
#ifdef MPID_HAS_HETERO
    if (comm_ptr->is_hetero)
        is_homogeneous = 0;
#endif
    if (HCOL_DTE_IS_COMPLEX(dtype) || HCOL_DTE_IS_ZERO(dtype) || (0 == is_homogeneous)) {
        /*If we are here then datatype is not simple predefined datatype */
        /*In future we need to add more complex mapping to the dte_data_representation_t */
        /* Now use fallback */
        MPIU_DBG_MSG(CH3_OTHER, VERBOSE, "unsupported data layout, calling fallback ibcast.");
        use_fallback = 1;
    }
    else {
        rc = hcoll_collectives.coll_ibcast(buffer, count, dtype, root, rt_handle,
                                           comm_ptr->hcoll_priv.hcoll_context);
        if (HCOLL_SUCCESS != rc) {
            use_fallback = 1;
        }
    }
    if (1 == use_fallback) {
        MPIU_DBG_MSG(CH3_OTHER, VERBOSE, "RUNNING FALLBACK IBCAST - done.");
        void *ptr = comm_ptr->coll_fns->Ibcast_req;
        comm_ptr->coll_fns->Ibcast_req =
            (comm_ptr->hcoll_priv.hcoll_origin_coll_fns !=
             NULL) ? comm_ptr->hcoll_priv.hcoll_origin_coll_fns->Ibcast_req : NULL;
        rc = MPI_Ibcast(buffer, count, datatype, root, comm, &req);
        MPID_Request_get_ptr(req, *request);
        comm_ptr->coll_fns->Ibcast_req = ptr;
        MPIU_DBG_MSG(CH3_OTHER, VERBOSE, "RUNNING FALLBACK IBCAST - done.");
    }
    return rc;
}

#undef FUNCNAME
#define FUNCNAME hcoll_Iallgather_req
#undef FCNAME
#define FCNAME MPIU_QUOTE(FUNCNAME)
int hcoll_Iallgather_req(const void *sendbuf, int sendcount, MPI_Datatype sendtype, void *recvbuf,
                         int recvcount, MPI_Datatype recvtype, MPID_Comm * comm_ptr,
                         MPID_Request ** request)
{
    int is_homogeneous = 1, use_fallback = 0;
    MPI_Comm comm = comm_ptr->handle;
    dte_data_representation_t stype;
    dte_data_representation_t rtype;
    int rc;
    void **rt_handle;
    MPI_Request req;
    rt_handle = (void **) request;

    is_homogeneous = 1;
#ifdef MPID_HAS_HETERO
    if (comm_ptr->is_hetero)
        is_homogeneous = 0;
#endif

    MPIU_DBG_MSG(CH3_OTHER, VERBOSE, "RUNNING HCOLL IALLGATHER.");
    stype = mpi_dtype_2_dte_dtype(sendtype);
    rtype = mpi_dtype_2_dte_dtype(recvtype);
    if (MPI_IN_PLACE == sendbuf) {
        sendbuf = HCOLL_IN_PLACE;
    }
    if (HCOL_DTE_IS_COMPLEX(stype) || HCOL_DTE_IS_ZERO(stype) || HCOL_DTE_IS_ZERO(rtype) ||
        HCOL_DTE_IS_COMPLEX(rtype) || is_homogeneous == 0) {
        MPIU_DBG_MSG(CH3_OTHER, VERBOSE, "unsupported data layout; calling fallback iallgather.");
        use_fallback = 1;
    }
    else {
        rc = hcoll_collectives.coll_iallgather(sendbuf, sendcount, stype, recvbuf, recvcount, rtype,
                                               comm_ptr->hcoll_priv.hcoll_context, rt_handle);
        if (HCOLL_SUCCESS != rc) {
            use_fallback = 1;
        }
    }
    if (1 == use_fallback) {
        if (HCOLL_IN_PLACE == sendbuf) {
            sendbuf = MPI_IN_PLACE;
        }
        MPIU_DBG_MSG(CH3_OTHER, VERBOSE, "RUNNING FALLBACK IALLGATHER.");
        void *ptr = comm_ptr->coll_fns->Iallgather_req;
        comm_ptr->coll_fns->Iallgather_req =
            (comm_ptr->hcoll_priv.hcoll_origin_coll_fns !=
             NULL) ? comm_ptr->hcoll_priv.hcoll_origin_coll_fns->Iallgather_req : NULL;
        rc = MPI_Iallgather(sendbuf, sendcount, sendtype, recvbuf, recvcount, recvtype, comm, &req);
        MPID_Request_get_ptr(req, *request);
        comm_ptr->coll_fns->Iallgather_req = ptr;
        MPIU_DBG_MSG(CH3_OTHER, VERBOSE, "RUNNING FALLBACK IALLGATHER - done.");
    }
    return rc;
}

#undef FUNCNAME
#define FUNCNAME hcoll_Iallreduce_req
#undef FCNAME
#define FCNAME MPIU_QUOTE(FUNCNAME)
int hcoll_Iallreduce_req(const void *sendbuf, void *recvbuf, int count, MPI_Datatype datatype,
                         MPI_Op op, MPID_Comm * comm_ptr, MPID_Request ** request)
{
    dte_data_representation_t Dtype;
    hcoll_dte_op_t *Op;
    int rc;
    void **rt_handle;
    MPI_Request req;
    int is_homogeneous = 1, use_fallback = 0;
    MPI_Comm comm = comm_ptr->handle;
    rt_handle = (void **) request;
#ifdef MPID_HAS_HETERO
    if (comm_ptr->is_hetero)
        is_homogeneous = 0;
#endif

    MPIU_DBG_MSG(CH3_OTHER, VERBOSE, "RUNNING HCOL IALLREDUCE.");
    Dtype = mpi_dtype_2_dte_dtype(datatype);
    Op = mpi_op_2_dte_op(op);
    if (MPI_IN_PLACE == sendbuf) {
        sendbuf = HCOLL_IN_PLACE;
    }
    if (HCOL_DTE_IS_COMPLEX(Dtype) || HCOL_DTE_IS_ZERO(Dtype) || (0 == is_homogeneous) ||
        (HCOL_DTE_OP_NULL == Op->id)) {
        MPIU_DBG_MSG(CH3_OTHER, VERBOSE, "unsupported data layout, calling fallback iallreduce.");
        use_fallback = 1;
    }
    else {
        rc = hcoll_collectives.coll_iallreduce(sendbuf, recvbuf, count, Dtype, Op,
                                               comm_ptr->hcoll_priv.hcoll_context, rt_handle);
        if (HCOLL_SUCCESS != rc) {
            use_fallback = 1;
        }
    }
    if (1 == use_fallback) {
        if (HCOLL_IN_PLACE == sendbuf) {
            sendbuf = MPI_IN_PLACE;
        }
        MPIU_DBG_MSG(CH3_OTHER, VERBOSE, "RUNNING FALLBACK IALLREDUCE.");
        void *ptr = comm_ptr->coll_fns->Iallreduce_req;
        comm_ptr->coll_fns->Iallreduce_req =
            (comm_ptr->hcoll_priv.hcoll_origin_coll_fns !=
             NULL) ? comm_ptr->hcoll_priv.hcoll_origin_coll_fns->Iallreduce_req : NULL;
        rc = MPI_Iallreduce(sendbuf, recvbuf, count, datatype, op, comm, &req);
        MPID_Request_get_ptr(req, *request);
        comm_ptr->coll_fns->Iallreduce_req = ptr;
        MPIU_DBG_MSG(CH3_OTHER, VERBOSE, "RUNNING FALLBACK IALLREDUCE done.");
    }
    return rc;
}
