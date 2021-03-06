/* -*- Mode: C; c-basic-offset:4 ; indent-tabs-mode:nil ; -*- */
/*
 *  (C) 2012 NEC Corporation
 *      Author: Masamichi Takagi
 *      See COPYRIGHT in top-level directory.
 */

#include "ib_impl.h"
#include "mpidrma.h"

//#define MPID_NEM_IB_DEBUG_POLL
#ifdef dprintf  /* avoid redefinition with src/mpid/ch3/include/mpidimpl.h */
#undef dprintf
#endif
#ifdef MPID_NEM_IB_DEBUG_POLL
#define dprintf printf
#else
#define dprintf(...)
#endif

static int entered_drain_scq = 0;

#if 0
#define MPID_NEM_IB_SEND_PROGRESS_POLLINGSET MPID_nem_ib_send_progress(vc);
#else
#define MPID_NEM_IB_SEND_PROGRESS_POLLINGSET {     \
        do {                                                        \
            int n;                                                      \
            for (n = 0; n < MPID_NEM_IB_NRINGBUF; n++) {                \
                if (((MPID_nem_ib_ringbuf_allocated[n / 64] >> (n & 63)) & 1) == 0) { \
                    continue;                                           \
                }                                                       \
                mpi_errno = MPID_nem_ib_poll_eager(&MPID_nem_ib_ringbuf[n]); /*FIXME: perform send_progress for all sendqs */ \
                MPIU_ERR_CHKANDJUMP(mpi_errno, mpi_errno, MPI_ERR_OTHER, "**MPID_nem_ib_poll_eager"); \
            }                                                           \
        } while (0);                                                    \
}
#if 0
   int n;                                         \
   for(n = 0; n < MPID_nem_ib_npollingset; n++) {  \
       MPIDI_VC_t *vc_n = MPID_nem_ib_pollingset[n];  \
       /*MPID_nem_ib_debug_current_vc_ib = vc_ib;*/   \
       MPID_nem_ib_send_progress(vc_n);               \
   }                                                  \

#endif
#endif
#if 1
#define MPID_NEM_IB_CHECK_AND_SEND_PROGRESS \
    do {                                                                \
        if (!MPID_nem_ib_sendq_empty(vc_ib->sendq) && MPID_nem_ib_sendq_ready_to_send_head(vc_ib)) { \
            MPID_nem_ib_send_progress(vc);                              \
        }                                                               \
    } while (0)
#else
#define MPID_NEM_IB_CHECK_AND_SEND_PROGRESS \
    do { \
        MPID_NEM_IB_SEND_PROGRESS_POLLINGSET; \
    } while (0)
#endif

#undef FUNCNAME
#define FUNCNAME MPID_nem_ib_drain_scq
#undef FCNAME
#define FCNAME MPIDI_QUOTE(FUNCNAME)
int MPID_nem_ib_drain_scq(int dont_call_progress)
{

    int mpi_errno = MPI_SUCCESS;
    int result;
    int i;
    struct ibv_wc cqe[MPID_NEM_IB_COM_MAX_CQ_HEIGHT_DRAIN];

    MPIDI_STATE_DECL(MPID_STATE_MPID_NEM_IB_DRAIN_SCQ);
    MPIDI_FUNC_ENTER(MPID_STATE_MPID_NEM_IB_DRAIN_SCQ);

    /* prevent a call path drain_scq -> send_progress -> drain_scq */
    if (entered_drain_scq) {
        dprintf("drain_scq,re-enter\n");
        goto fn_exit;
    }
    entered_drain_scq = 1;

#ifdef MPID_NEM_IB_ONDEMAND
    /* drain_scq is called after poll_eager calls vc_terminate
     * or nobody created QP */
    if (!MPID_nem_ib_rc_shared_scq) {
        dprintf("drain_scq,CQ is null\n");
        goto fn_exit;
    }
#endif

#if 0   /*def HAVE_LIBDCFA */
    result = ibv_poll_cq(MPID_nem_ib_rc_shared_scq, 1, &cqe[0]);
#else
    result =
        ibv_poll_cq(MPID_nem_ib_rc_shared_scq, /*3 */ MPID_NEM_IB_COM_MAX_CQ_HEIGHT_DRAIN, &cqe[0]);
#endif

    MPIU_ERR_CHKANDJUMP(result < 0, mpi_errno, MPI_ERR_OTHER, "**netmod,ib,ibv_poll_cq");

    if (result > 0) {
        dprintf("drain_scq,result=%d\n", result);
    }
    for (i = 0; i < result; i++) {
        dprintf("drain_scq,i=%d\n", i);

        MPID_Request *req;
        MPID_Request_kind_t kind;
        int req_type, msg_type;

        /* Obtain sreq */
        //req = (MPID_Request *) cqe[i].wr_id;
        MPID_nem_ib_rc_send_request *req_wrap = (MPID_nem_ib_rc_send_request *) cqe[i].wr_id;
        req = (MPID_Request *) req_wrap->wr_id;

        /* decrement reference counter of mr_cache_entry registered by ib_com_isend or ib_com_lrecv */
        struct MPID_nem_ib_com_reg_mr_cache_entry_t *mr_cache =
            (struct MPID_nem_ib_com_reg_mr_cache_entry_t *) req_wrap->mr_cache;
        if (mr_cache) {
            MPID_nem_ib_com_reg_mr_release(mr_cache);
        }

        kind = req->kind;
        req_type = MPIDI_Request_get_type(req);
        msg_type = MPIDI_Request_get_msg_type(req);

        dprintf("drain_scq,req=%p,req->ref_count=%d,cc_ptr=%d\n", req, req->ref_count,
                *req->cc_ptr);
        if (req->ref_count <= 0) {
            printf("%d\n", *(int *) 0);
        }

#ifdef HAVE_LIBDCFA
        if (cqe[i].status != IBV_WC_SUCCESS) {
            printf("drain_scq,kind=%d,req_type=%d,msg_type=%d,cqe.status=%08x\n", kind, req_type,
                   msg_type, cqe[i].status);
        }
#else
        if (cqe[i].status != IBV_WC_SUCCESS) {
            printf
                ("drain_scq,kind=%d,req_type=%d,msg_type=%d,comm=%p,cqe.status=%08x,%s,sseq_num=%d\n",
                 kind, req_type, msg_type, req->comm, cqe[i].status,
                 ibv_wc_status_str(cqe[i].status), VC_FIELD(req->ch.vc, ibcom->sseq_num));
        }
#endif
        MPID_NEM_IB_ERR_FATAL(cqe[i].status != IBV_WC_SUCCESS, mpi_errno, MPI_ERR_OTHER,
                              "**MPID_nem_ib_drain_scq");

        /*
         * packets generated by MPIDI_CH3_iStartMsgv has req_type of RECV
         * lmt_initiate_lmt, lmt_send_put_done
         */
        if (
               //req_type == MPIDI_REQUEST_TYPE_SEND
               (req_type == MPIDI_REQUEST_TYPE_SEND || req_type == MPIDI_REQUEST_TYPE_RSEND ||
                req_type == MPIDI_REQUEST_TYPE_RECV || req_type == MPIDI_REQUEST_TYPE_SSEND)
               && msg_type == MPIDI_REQUEST_EAGER_MSG) {
            dprintf("drain_scq,send/recv,eager,req_type=%d,,comm=%p,opcode=%d\n", req_type,
                    req->comm, cqe[i].opcode);

            MPID_nem_ib_vc_area *vc_ib = VC_IB(req->ch.vc);
            dprintf("drain_scq,MPIDI_REQUEST_EAGER_MSG,%d->%d,sendq_empty=%d,ncom=%d,ncqe=%d,rdmabuf_occ=%d\n", MPID_nem_ib_myrank, req->ch.vc->pg_rank, MPID_nem_ib_sendq_empty(vc_ib->sendq), vc_ib->ibcom->ncom, MPID_nem_ib_ncqe, MPID_nem_ib_diff16(vc_ib->ibcom->sseq_num, vc_ib->ibcom->lsr_seq_num_tail));      /* moved before MPID_Request_release because this references req->ch.vc */

            /* free temporal buffer for eager-send non-contiguous data.
             * MPIDI_Request_create_sreq (in mpid_isend.c) sets req->dev.datatype
             * control message has a req_type of MPIDI_REQUEST_TYPE_RECV and
             * msg_type of MPIDI_REQUEST_EAGER_MSG because
             * control message send follows
             * MPIDI_CH3_iStartMsg/v-->MPID_nem_ib_iStartContigMsg-->MPID_nem_ib_iSendContig
             * and MPID_nem_ib_iSendContig set req->dev.state to zero.
             * see MPID_Request_create (in src/mpid/ch3/src/ch3u_request.c)
             * eager-short message has req->comm of zero
             */
            if (req_type == MPIDI_REQUEST_TYPE_SEND && req->comm) {
                /* exclude control messages by requiring MPIDI_REQUEST_TYPE_SEND
                 * exclude eager-short by requiring req->comm != 0 */
                int is_contig;
                MPID_Datatype_is_contig(req->dev.datatype, &is_contig);
                if (!is_contig && REQ_FIELD(req, lmt_pack_buf)) {
                    dprintf("drain_scq,eager-send,non-contiguous,free lmt_pack_buf=%p\n",
                            REQ_FIELD(req, lmt_pack_buf));
                    MPIU_Free(REQ_FIELD(req, lmt_pack_buf));
                }
            }

            /* As for request by PKT_PUT, both req->type and req->comm are not set.
             * If receiver's data type is derived-type, req->dev.datatype_ptr is set.
             */
            if ((*req->cc_ptr == 1) && (req_type == 0) && !req->comm) {
                if (req->dev.datatype_ptr && (req->dev.segment_size > 0) &&
                    REQ_FIELD(req, lmt_pack_buf)) {
                    MPIU_Free(REQ_FIELD(req, lmt_pack_buf));
                }
            }

            /* decrement the number of entries in IB command queue */
            vc_ib->ibcom->ncom -= 1;
            MPID_nem_ib_ncqe -= 1;
            MPID_nem_ib_rdmawr_from_free(REQ_FIELD(req, buf_from), REQ_FIELD(req, buf_from_sz));
            dprintf("drain_scq,afree=%p,sz=%d\n", REQ_FIELD(req, buf_from),
                    REQ_FIELD(req, buf_from_sz));

            dprintf("drain_scq,eager-send,ncqe=%d\n", MPID_nem_ib_ncqe);
            MPIU_Assert(req->ref_count >= 1 && req->ref_count <= 3);

            /* ref_count is decremented in drain_scq and wait */
            if (*req->cc_ptr > 0) {
                dprintf("drain_scq,MPID_nem_ib_ncqe_nces=%d,cc_ptr=%d,pending_sends=%d\n",
                        MPID_nem_ib_ncqe_nces, *req->cc_ptr, VC_FIELD(req->ch.vc, pending_sends));
                MPID_nem_ib_ncqe_nces -= 1;

                int (*reqFn) (MPIDI_VC_t *, MPID_Request *, int *);

                (VC_FIELD(req->ch.vc, pending_sends)) -= 1;

                /* as in the template */
                reqFn = req->dev.OnDataAvail;
                if (!reqFn) {
                    MPIDI_CH3U_Request_complete(req);
                    dprintf("drain_scq,complete,req=%p\n", req);
                    MPIU_DBG_MSG(CH3_CHANNEL, VERBOSE, ".... complete");
                    //dprintf("drain_scq,complete,req=%p,pcc incremented to %d\n", req,
                    //MPIDI_CH3I_progress_completion_count.v);
                }
                else {
                    dprintf("drain_scq,reqFn isn't zero\n");
                    MPIDI_VC_t *vc = req->ch.vc;
                    int complete = 0;
                    mpi_errno = reqFn(vc, req, &complete);
                    if (mpi_errno)
                        MPIU_ERR_POP(mpi_errno);
                    /* not-completed case is not implemented */
                    MPIU_Assert(complete == TRUE);
                }
            }
            else {
                MPID_Request_release(req);
                dprintf("drain_scq,relese,req=%p\n", req);
            }
            /* try to send from sendq */
            //dprintf("ib_poll,SCQ,!lmt,send_progress\n");
            if (!MPID_nem_ib_sendq_empty(vc_ib->sendq)) {
                dprintf("drain_scq,eager-send,ncom=%d,ncqe=%d,diff=%d\n",
                        vc_ib->ibcom->ncom < MPID_NEM_IB_COM_MAX_SQ_CAPACITY,
                        MPID_nem_ib_ncqe < MPID_NEM_IB_COM_MAX_CQ_CAPACITY,
                        MPID_nem_ib_diff16(vc_ib->ibcom->sseq_num,
                                           vc_ib->ibcom->lsr_seq_num_tail) <
                        vc_ib->ibcom->local_ringbuf_nslot);

                MPID_Request *sreq = MPID_nem_ib_sendq_head(vc_ib->sendq);
                int msg_type_sreq = MPIDI_Request_get_msg_type(sreq);

                if (sreq->kind == MPID_REQUEST_SEND && msg_type_sreq == MPIDI_REQUEST_EAGER_MSG) {
                    dprintf("drain_scq,eager-send,head is eager-send\n");
                }
                else if (sreq->kind == MPID_REQUEST_RECV && msg_type_sreq == MPIDI_REQUEST_RNDV_MSG) {
                    dprintf("drain_scq,eager-send,head is lmt RDMA-read\n");
                }
                else if (sreq->kind == MPID_REQUEST_SEND && msg_type_sreq == MPIDI_REQUEST_RNDV_MSG) {
                    dprintf("drain_scq,eager-send,head is lmt RDMA-write\n");
                }
            }
            /*  call MPID_nem_ib_send_progress for all VCs in polling-set
             * instead of VC which releases CQ, command
             * when releasing them
             * because commands for VC-A are blocked by the command
             * for VC-B and waiting in the sendq
             */
            dprintf("drain_scq,eager-send,send_progress\n");
            //MPID_NEM_IB_SEND_PROGRESS_POLLINGSET;

            dprintf("drain_scq,eager-send,next\n");

            MPIU_Free(req_wrap);
        }
        else if (req_type == MPIDI_REQUEST_TYPE_GET_RESP && msg_type == MPIDI_REQUEST_EAGER_MSG) {
            dprintf("drain_scq,GET_RESP,eager,req_type=%d,,comm=%p,opcode=%d\n", req_type,
                    req->comm, cqe[i].opcode);

            MPID_nem_ib_vc_area *vc_ib = VC_IB(req->ch.vc);
            dprintf("drain_scq,MPIDI_REQUEST_EAGER_MSG,%d->%d,sendq_empty=%d,ncom=%d,ncqe=%d,rdmabuf_occ=%d\n", MPID_nem_ib_myrank, req->ch.vc->pg_rank, MPID_nem_ib_sendq_empty(vc_ib->sendq), vc_ib->ibcom->ncom, MPID_nem_ib_ncqe, MPID_nem_ib_diff16(vc_ib->ibcom->sseq_num, vc_ib->ibcom->lsr_seq_num_tail));      /* moved before MPID_Request_release because this references req->ch.vc */

            /* decrement the number of entries in IB command queue */
            vc_ib->ibcom->ncom -= 1;
            MPID_nem_ib_ncqe -= 1;
            MPID_nem_ib_rdmawr_from_free(REQ_FIELD(req, buf_from), REQ_FIELD(req, buf_from_sz));

            /* this request may be from Noncontig */
            if ((*req->cc_ptr == 1) && req->dev.datatype_ptr && (req->dev.segment_size > 0) &&
                REQ_FIELD(req, lmt_pack_buf)) {
                MPIU_Free(REQ_FIELD(req, lmt_pack_buf));
            }

            dprintf("drain_scq,GET_RESP,ncqe=%d\n", MPID_nem_ib_ncqe);
            MPIU_Assert(req->ref_count == 1 || req->ref_count == 2);

            /* ref_count is decremented in drain_scq and wait */
            dprintf("drain_scq,MPID_nem_ib_ncqe_nces=%d,cc_ptr=%d,pending_sends=%d\n",
                    MPID_nem_ib_ncqe_nces, *req->cc_ptr, VC_FIELD(req->ch.vc, pending_sends));
            MPID_nem_ib_ncqe_nces -= 1;

            int (*reqFn) (MPIDI_VC_t *, MPID_Request *, int *);

            (VC_FIELD(req->ch.vc, pending_sends)) -= 1;

            /* as in the template */
            reqFn = req->dev.OnDataAvail;
            if (!reqFn) {
                MPIDI_CH3U_Request_complete(req);
                dprintf("drain_scq,complete,req=%p\n", req);
                MPIU_DBG_MSG(CH3_CHANNEL, VERBOSE, ".... complete");
                //dprintf("drain_scq,complete,req=%p,pcc incremented to %d\n", req,
                //MPIDI_CH3I_progress_completion_count.v);
            }
            else {
                dprintf("drain_scq,reqFn isn't zero\n");
                dprintf("drain_scq,GET_RESP,before dev.OnDataAvail,ref_count=%d\n", req->ref_count);
                MPIDI_VC_t *vc = req->ch.vc;
                int complete = 0;
                mpi_errno = reqFn(vc, req, &complete);
                if (mpi_errno)
                    MPIU_ERR_POP(mpi_errno);
                /* not-completed case is not implemented */
                MPIU_Assert(complete == TRUE);
            }

            //MPID_NEM_IB_SEND_PROGRESS_POLLINGSET;

            dprintf("drain_scq,GET_RESP,next\n");

            MPIU_Free(req_wrap);
        }
        else if (req_type == MPIDI_REQUEST_TYPE_RECV && msg_type == MPIDI_REQUEST_RNDV_MSG &&
                 cqe[i].opcode == IBV_WC_RDMA_READ) {
            /* lmt get */
            /* the case for lmt-put-done or lmt-put where
             * (1) sender finds end-flag won't change (2) sender sends RTS to receiver
             * (3) receiver gets (4) here
             * is distinguished by cqe[i].opcode
             */
            dprintf("drain_scq,recv,rndv,rdma-read,kind=%d,opcode=%d\n", kind, cqe[i].opcode);


            MPID_nem_ib_vc_area *vc_ib = VC_IB(req->ch.vc);
#if defined(MPID_NEM_IB_LMT_GET_CQE)

            /* end of packet */
            if (req_wrap->mf == MPID_NEM_IB_LMT_LAST_PKT) {
                /* unpack non-contiguous dt */
                int is_contig;
                MPID_Datatype_is_contig(req->dev.datatype, &is_contig);
                if (!is_contig) {
                    dprintf("drain_scq,lmt,GET_CQE,unpack noncontiguous data to user buffer\n");

                    /* see MPIDI_CH3U_Request_unpack_uebuf (in /src/mpid/ch3/src/ch3u_request.c) */
                    /* or MPIDI_CH3U_Receive_data_found (in src/mpid/ch3/src/ch3u_handle_recv_pkt.c) */
                    MPIDI_msg_sz_t unpack_sz = req->ch.lmt_data_sz;
                    MPID_Segment seg;
                    MPI_Aint last;

                    MPID_Segment_init(req->dev.user_buf, req->dev.user_count, req->dev.datatype,
                                      &seg, 0);
                    last = unpack_sz;
                    MPID_Segment_unpack(&seg, 0, &last, REQ_FIELD(req, lmt_pack_buf));
                    if (last != unpack_sz) {
                        /* --BEGIN ERROR HANDLING-- */
                        /* received data was not entirely consumed by unpack()
                         * because too few bytes remained to fill the next basic
                         * datatype */
                        MPIR_STATUS_SET_COUNT(req->status, last);
                        req->status.MPI_ERROR =
                            MPIR_Err_create_code(MPI_SUCCESS, MPIR_ERR_RECOVERABLE, FCNAME,
                                                 __LINE__, MPI_ERR_TYPE, "**MPID_nem_ib_poll", 0);
                        /* --END ERROR HANDLING-- */
                    }
                    dprintf("drain_scq,lmt,GET_CQE,ref_count=%d,lmt_pack_buf=%p\n", req->ref_count,
                            REQ_FIELD(req, lmt_pack_buf));
                    MPID_nem_ib_stfree(REQ_FIELD(req, lmt_pack_buf), (size_t) req->ch.lmt_data_sz);
                }
                dprintf("drain_scq,lmt,GET_CQE,lmt_send_GET_DONE,rsr_seq_num_tail=%d\n",
                        vc_ib->ibcom->rsr_seq_num_tail);

                /* send done to sender. vc is stashed in MPID_nem_ib_lmt_start_recv (in ib_lmt.c) */
                MPID_nem_ib_lmt_send_GET_DONE(req->ch.vc, req);
            }
            else if (req_wrap->mf == MPID_NEM_IB_LMT_SEGMENT_LAST) {
                MPID_nem_ib_lmt_send_GET_DONE(req->ch.vc, req);
            }
#endif
            /* unmark "lmt is going on" */

            //dprintf("ib_poll,SCQ,lmt,%d->%d,sendq_empty=%d,ncom=%d,ncqe=%d,rdmabuf_occ=%d\n", MPID_nem_ib_myrank, req->ch.vc->pg_rank, MPID_nem_ib_sendq_empty(vc_ib->sendq), vc_ib->ibcom->ncom, MPID_nem_ib_ncqe, MPID_nem_ib_diff16(vc_ib->ibcom->sseq_num, vc_ib->ibcom->lsr_seq_num_tail)); /* moved before MPID_Request_release because this references req->ch.vc */

            /* decrement the number of entries in IB command queue */
            vc_ib->ibcom->ncom -= 1;
            MPID_nem_ib_ncqe -= 1;
            dprintf("drain_scq,rdma-read,ncqe=%d\n", MPID_nem_ib_ncqe);

#ifdef MPID_NEM_IB_LMT_GET_CQE
            if (req_wrap->mf == MPID_NEM_IB_LMT_LAST_PKT) {
                dprintf("drain_scq,GET_CQE,Request_complete\n");
                /* mark completion on rreq */
                MPIDI_CH3U_Request_complete(req);
                dprintf("drain_scq,complete,req=%p\n", req);
            }
#else /* GET, and !GET_CQE */

            int is_contig;
            MPID_Datatype_is_contig(req->dev.datatype, &is_contig);
            if (!is_contig) {
                //if (req->ref_count == 1) {
                dprintf("drain_scq,GET&&!GET_CQE,ref_count=%d,lmt_pack_buf=%p\n", req->ref_count,
                        REQ_FIELD(req, lmt_pack_buf));
                /* debug, polling waits forever when freeing here. */
                //free(REQ_FIELD(req, lmt_pack_buf));
                //MPID_nem_ib_stfree(REQ_FIELD(req, lmt_pack_buf), (size_t)req->ch.lmt_data_sz);
                //dprintf("drain_scq,lmt,insert to free-list=%p\n", MPID_nem_ib_fl);
                //} else {
                //dprintf("drain_scq,GET&&!GET_CQE,ref_count=%d,lmt_pack_buf=%p\n", req->ref_count, REQ_FIELD(req, lmt_pack_buf));
                //}
            }

            /* lmt_start_recv increments ref_count
             * drain_scq and ib_poll is not ordered, so both can decrement ref_count */
            MPID_Request_release(req);
            dprintf("drain_scq,relese,req=%p\n", req);
#endif
            MPIU_Free(req_wrap);

            /* try to send from sendq */
            if (!MPID_nem_ib_sendq_empty(vc_ib->sendq)) {
                dprintf("drain_scq,GET,ncom=%d,ncqe=%d,diff=%d\n",
                        vc_ib->ibcom->ncom < MPID_NEM_IB_COM_MAX_SQ_CAPACITY,
                        MPID_nem_ib_ncqe < MPID_NEM_IB_COM_MAX_CQ_CAPACITY,
                        MPID_nem_ib_diff16(vc_ib->ibcom->sseq_num,
                                           vc_ib->ibcom->lsr_seq_num_tail) <
                        vc_ib->ibcom->local_ringbuf_nslot);
                MPID_Request *sreq = MPID_nem_ib_sendq_head(vc_ib->sendq);
                int msg_type_sreq = MPIDI_Request_get_msg_type(sreq);

                if (sreq->kind == MPID_REQUEST_SEND && msg_type_sreq == MPIDI_REQUEST_EAGER_MSG) {
                    dprintf("drain_scq,eager-send,head is eager-send\n");
                }
                else if (sreq->kind == MPID_REQUEST_RECV && msg_type_sreq == MPIDI_REQUEST_RNDV_MSG) {
                    dprintf("drain_scq,eager-send,head is lmt\n");
                }
            }
            //if (!MPID_nem_ib_sendq_empty(vc_ib->sendq) && MPID_nem_ib_sendq_ready_to_send_head(vc_ib)) {
            //MPID_NEM_IB_SEND_PROGRESS_POLLINGSET
            //}
        }
        else if (req_type == 13 && cqe[i].opcode == IBV_WC_RDMA_READ) {
            MPID_nem_ib_vc_area *vc_ib = VC_IB(req->ch.vc);

            /* end of packet */
            if (req_wrap->mf == MPID_NEM_IB_LMT_LAST_PKT) {
                MPIDI_msg_sz_t data_len = req->ch.lmt_data_sz;
                MPI_Aint type_size;

                MPID_Datatype_get_size_macro(req->dev.datatype, type_size);
                req->dev.recv_data_sz = type_size * req->dev.user_count;

                int complete = 0;
                int (*reqFn) (MPIDI_VC_t *, MPID_Request *, int *);
                mpi_errno =
                    MPIDI_CH3U_Receive_data_found(req, REQ_FIELD(req, lmt_pack_buf), &data_len,
                                                  &complete);

                /* Data receive must be completed */
                MPIU_Assert(complete == TRUE);

                MPIU_Free(REQ_FIELD(req, lmt_pack_buf));

                MPID_nem_ib_lmt_send_PKT_LMT_DONE(req->ch.vc, req);
                reqFn = req->dev.OnFinal;
                if (reqFn) {
                    reqFn(req->ch.vc, req, &complete);
                } else {
                    MPIDI_CH3U_Request_complete(req);
                }
            }

            /* decrement the number of entries in IB command queue */
            vc_ib->ibcom->ncom -= 1;
            MPID_nem_ib_ncqe -= 1;

            MPIU_Free(req_wrap);
        }
        else if (req_type == MPIDI_REQUEST_TYPE_PUT_RESP && cqe[i].opcode == IBV_WC_RDMA_READ) {
            MPID_nem_ib_vc_area *vc_ib = VC_IB(req->ch.vc);

            /* end of packet */
            if (req_wrap->mf == MPID_NEM_IB_LMT_LAST_PKT) {
                MPIDI_msg_sz_t data_len = req->ch.lmt_data_sz;
                int complete = 0;
                mpi_errno =
                    MPIDI_CH3U_Receive_data_found(req, REQ_FIELD(req, lmt_pack_buf), &data_len,
                                                  &complete);

                /* Data receive must be completed */
                MPIU_Assert(complete == TRUE);

                MPIU_Free(REQ_FIELD(req, lmt_pack_buf));

                complete = 0;
                mpi_errno = MPIDI_CH3_ReqHandler_PutRecvComplete(req->ch.vc, req, &complete);      // call MPIDI_CH3U_Request_complete()
                if (mpi_errno)
                    MPIU_ERR_POP(mpi_errno);
                MPIU_Assert(complete == TRUE);

                MPID_nem_ib_lmt_send_PKT_LMT_DONE(req->ch.vc, req);
                MPIDI_CH3U_Request_complete(req);
            }

            /* decrement the number of entries in IB command queue */
            vc_ib->ibcom->ncom -= 1;
            MPID_nem_ib_ncqe -= 1;

            MPIU_Free(req_wrap);
        }
        else if (req_type == MPIDI_REQUEST_TYPE_PUT_RESP_DERIVED_DT &&
                 cqe[i].opcode == IBV_WC_RDMA_READ) {
            MPID_nem_ib_vc_area *vc_ib = VC_IB(req->ch.vc);
            /* end of packet */
            if (req_wrap->mf == MPID_NEM_IB_LMT_LAST_PKT) {
                MPIDI_msg_sz_t buflen = req->ch.lmt_data_sz;
                char *buf = (char *) REQ_FIELD(req, lmt_pack_buf);
                int complete = 0;
                int dataloop_size = *(int *) req->dev.dtype_info;       /* copy from temp store area */

                /* copy all of dtype_info and dataloop */
                MPIU_Memcpy(req->dev.dtype_info, buf, sizeof(MPIDI_RMA_dtype_info));
                MPIU_Memcpy(req->dev.dataloop, buf + sizeof(MPIDI_RMA_dtype_info), dataloop_size);


                /* All dtype data has been received, call req handler */
                mpi_errno =
                    MPIDI_CH3_ReqHandler_PutDerivedDTRecvComplete(req->ch.vc, req, &complete);
                MPIU_ERR_CHKANDJUMP1(mpi_errno, mpi_errno, MPI_ERR_OTHER, "**ch3|postrecv",
                                     "**ch3|postrecv %s", "MPIDI_CH3_PKT_PUT");
                /* return 'complete == FALSE' */

                buflen -= (sizeof(MPIDI_RMA_dtype_info) + dataloop_size);
                buf += (sizeof(MPIDI_RMA_dtype_info) + dataloop_size);

                mpi_errno = MPID_nem_ib_handle_pkt_bh(req->ch.vc, req, buf, buflen);
                MPIU_ERR_CHKANDJUMP(mpi_errno, mpi_errno, MPI_ERR_OTHER,
                                    "**MPID_nem_ib_handle_pkt_bh");

                MPID_nem_ib_lmt_send_PKT_LMT_DONE(req->ch.vc, req);

                MPIU_Free(REQ_FIELD(req, lmt_pack_buf));
                MPIDI_CH3U_Request_complete(req);
            }

            /* decrement the number of entries in IB command queue */
            vc_ib->ibcom->ncom -= 1;
            MPID_nem_ib_ncqe -= 1;

            MPIU_Free(req_wrap);
        }
        else if (req_type == MPIDI_REQUEST_TYPE_ACCUM_RESP && cqe[i].opcode == IBV_WC_RDMA_READ) {
            MPID_nem_ib_vc_area *vc_ib = VC_IB(req->ch.vc);

            /* end of packet */
            if (req_wrap->mf == MPID_NEM_IB_LMT_LAST_PKT) {
                MPIDI_msg_sz_t data_len = req->ch.lmt_data_sz;
                int complete = 0;
                mpi_errno =
                    MPIDI_CH3U_Receive_data_found(req, REQ_FIELD(req, lmt_pack_buf), &data_len,
                                                  &complete);

                /* Data receive must be completed */
                MPIU_Assert(complete == TRUE);

                MPIU_Free(REQ_FIELD(req, lmt_pack_buf));

                complete = 0;
                mpi_errno = MPIDI_CH3_ReqHandler_AccumRecvComplete(req->ch.vc, req, &complete);      // call MPIDI_CH3U_Request_complete()
                if (mpi_errno)
                    MPIU_ERR_POP(mpi_errno);
                MPIU_Assert(complete == TRUE);

                MPID_nem_ib_lmt_send_PKT_LMT_DONE(req->ch.vc, req);
                MPIDI_CH3U_Request_complete(req);
            }

            /* decrement the number of entries in IB command queue */
            vc_ib->ibcom->ncom -= 1;
            MPID_nem_ib_ncqe -= 1;

            MPIU_Free(req_wrap);
        }
        else if (req_type == MPIDI_REQUEST_TYPE_ACCUM_RESP_DERIVED_DT &&
                 cqe[i].opcode == IBV_WC_RDMA_READ) {
            MPID_nem_ib_vc_area *vc_ib = VC_IB(req->ch.vc);
            /* end of packet */
            if (req_wrap->mf == MPID_NEM_IB_LMT_LAST_PKT) {
                MPIDI_msg_sz_t buflen = req->ch.lmt_data_sz;
                char *buf = (char *) REQ_FIELD(req, lmt_pack_buf);
                int complete = 0;
                int dataloop_size = *(int *) req->dev.dtype_info;       /* copy from temp store area */

                /* copy all of dtype_info and dataloop */
                MPIU_Memcpy(req->dev.dtype_info, buf, sizeof(MPIDI_RMA_dtype_info));
                MPIU_Memcpy(req->dev.dataloop, buf + sizeof(MPIDI_RMA_dtype_info), dataloop_size);


                /* All dtype data has been received, call req handler */
                mpi_errno =
                    MPIDI_CH3_ReqHandler_AccumDerivedDTRecvComplete(req->ch.vc, req, &complete);
                MPIU_ERR_CHKANDJUMP1(mpi_errno, mpi_errno, MPI_ERR_OTHER, "**ch3|postrecv",
                                     "**ch3|postrecv %s", "MPIDI_CH3_ACCUMULATE");
                /* return 'complete == FALSE' */

                buflen -= (sizeof(MPIDI_RMA_dtype_info) + dataloop_size);
                buf += (sizeof(MPIDI_RMA_dtype_info) + dataloop_size);

                mpi_errno = MPID_nem_ib_handle_pkt_bh(req->ch.vc, req, buf, buflen);
                MPIU_ERR_CHKANDJUMP(mpi_errno, mpi_errno, MPI_ERR_OTHER,
                                    "**MPID_nem_ib_handle_pkt_bh");

                MPID_nem_ib_lmt_send_PKT_LMT_DONE(req->ch.vc, req);

                MPIU_Free(REQ_FIELD(req, lmt_pack_buf));
                MPIDI_CH3U_Request_complete(req);
            }

            /* decrement the number of entries in IB command queue */
            vc_ib->ibcom->ncom -= 1;
            MPID_nem_ib_ncqe -= 1;

            MPIU_Free(req_wrap);
        }
        else {
            printf("drain_scq,unknown kind=%d,req_type=%d,msg_type=%d\n", kind, req_type, msg_type);
            assert(0);
#if 1   // lazy consulting of completion queue
            MPIU_ERR_CHKANDJUMP(1, mpi_errno, MPI_ERR_OTHER, "**MPID_nem_ib_drain_scq");
#else
            //printf("kind=%d\n", kind);
#endif
            MPIU_Free(req_wrap);
        }
    }
    if (!dont_call_progress) {
        MPID_NEM_IB_SEND_PROGRESS_POLLINGSET;
    }
  fn_exit:
    entered_drain_scq = 0;
    MPIDI_FUNC_EXIT(MPID_STATE_MPID_NEM_IB_DRAIN_SCQ);
    return mpi_errno;
  fn_fail:
    goto fn_exit;
}

/* bottom part of MPID_nem_handle_pkt() */
#undef FUNCNAME
#define FUNCNAME MPID_nem_ib_handle_pkt_bh
#undef FCNAME
#define FCNAME MPIDI_QUOTE(FUNCNAME)
int MPID_nem_ib_handle_pkt_bh(MPIDI_VC_t * vc, MPID_Request * req, char *buf, MPIDI_msg_sz_t buflen)
{
    int mpi_errno = MPI_SUCCESS;
    int complete = 0;

    MPIDI_STATE_DECL(MPID_STATE_MPID_NEM_IB_HANDLE_PKT_BH);
    MPIDI_FUNC_ENTER(MPID_STATE_MPID_NEM_IB_HANDLE_PKT_BH);

    while (buflen && !complete) {
        MPID_IOV *iov;
        int n_iov;
        iov = &req->dev.iov[req->dev.iov_offset];
        n_iov = req->dev.iov_count;

        while (n_iov && buflen >= iov->MPID_IOV_LEN) {
            size_t iov_len = iov->MPID_IOV_LEN;
            MPIU_Memcpy(iov->MPID_IOV_BUF, buf, iov_len);

            buflen -= iov_len;
            buf += iov_len;
            --n_iov;
            ++iov;
        }

        if (n_iov) {
            if (buflen > 0) {
                MPIU_Memcpy(iov->MPID_IOV_BUF, buf, buflen);
                iov->MPID_IOV_BUF = (void *) ((char *) iov->MPID_IOV_BUF + buflen);
                iov->MPID_IOV_LEN -= buflen;
                buflen = 0;
            }

            req->dev.iov_offset = iov - req->dev.iov;
            req->dev.iov_count = n_iov;
        }
        else {
            int (*reqFn) (MPIDI_VC_t *, MPID_Request *, int *);

            reqFn = req->dev.OnDataAvail;
            if (!reqFn) {
                MPIDI_CH3U_Request_complete(req);
                complete = TRUE;
            }
            else {
                mpi_errno = reqFn(vc, req, &complete);
                if (mpi_errno)
                    MPIU_ERR_POP(mpi_errno);
            }

            if (!complete) {
                req->dev.iov_offset = 0;
                MPIU_Assert(req->dev.iov_count > 0 &&
                            req->dev.iov[req->dev.iov_offset].MPID_IOV_LEN > 0);
            }
        }
    }
  fn_exit:
    MPIDI_FUNC_EXIT(MPID_STATE_MPID_NEM_IB_HANDLE_PKT_BH);
    return mpi_errno;
  fn_fail:
    goto fn_exit;
}

#undef FUNCNAME
#define FUNCNAME MPID_nem_ib_drain_scq_scratch_pad
#undef FCNAME
#define FCNAME MPIDI_QUOTE(FUNCNAME)
int MPID_nem_ib_drain_scq_scratch_pad()
{

    int mpi_errno = MPI_SUCCESS;
    int result;
    int i;
    struct ibv_wc cqe[MPID_NEM_IB_COM_MAX_CQ_HEIGHT_DRAIN];

    MPIDI_STATE_DECL(MPID_STATE_MPID_NEM_IB_DRAIN_SCQ_SCRATCH_PAD);
    MPIDI_FUNC_ENTER(MPID_STATE_MPID_NEM_IB_DRAIN_SCQ_SCRATCH_PAD);

    /* drain_scq_scratch_pad is called after poll_eager calls vc_terminate */
    if (!MPID_nem_ib_rc_shared_scq_scratch_pad) {
        dprintf("drain_scq_scratch_pad,CQ is null\n");
        goto fn_exit;
    }

#if 0   /*def HAVE_LIBDCFA */
    result = ibv_poll_cq(MPID_nem_ib_rc_shared_scq_scratch_pad, 1, &cqe[0]);
#else
    result =
        ibv_poll_cq(MPID_nem_ib_rc_shared_scq_scratch_pad, MPID_NEM_IB_COM_MAX_CQ_HEIGHT_DRAIN,
                    &cqe[0]);
#endif
    MPIU_ERR_CHKANDJUMP(result < 0, mpi_errno, MPI_ERR_OTHER, "**netmod,ib,ibv_poll_cq");

    if (result > 0) {
        dprintf("drain_scq_scratch_pad,found,result=%d\n", result);
    }
    for (i = 0; i < result; i++) {

#ifdef HAVE_LIBDCFA
        if (cqe[i].status != IBV_WC_SUCCESS) {
            dprintf("drain_scq_scratch_pad,status=%08x\n", cqe[i].status);
        }
#else
        if (cqe[i].status != IBV_WC_SUCCESS) {
            dprintf("drain_scq_scratch_pad,status=%08x,%s\n", cqe[i].status,
                    ibv_wc_status_str(cqe[i].status));
        }
#endif
        MPIU_ERR_CHKANDJUMP(cqe[i].status != IBV_WC_SUCCESS, mpi_errno, MPI_ERR_OTHER,
                            "**MPID_nem_ib_drain_scq_scratch_pad");

        MPID_nem_ib_com_t *ibcom_scratch_pad = (MPID_nem_ib_com_t *) cqe[i].wr_id;
        dprintf("drain_scq_scratch_pad,ibcom_scratch_pad=%p\n", ibcom_scratch_pad);
        ibcom_scratch_pad->ncom_scratch_pad -= 1;
        MPID_nem_ib_ncqe_scratch_pad -= 1;
    }

  fn_exit:
    MPIDI_FUNC_EXIT(MPID_STATE_MPID_NEM_IB_DRAIN_SCQ_SCRATCH_PAD);
    return mpi_errno;
  fn_fail:
    goto fn_exit;
}

#undef FUNCNAME
#define FUNCNAME MPID_nem_ib_poll_eager
#undef FCNAME
#define FCNAME MPIDI_QUOTE(FUNCNAME)
int MPID_nem_ib_poll_eager(MPID_nem_ib_ringbuf_t * ringbuf)
{

    int mpi_errno = MPI_SUCCESS;
    int ibcom_errno;
    struct MPIDI_VC *vc;
    MPID_nem_ib_vc_area *vc_ib;
    //int result;
    //struct ibv_wc cqe[MPID_NEM_IB_COM_MAX_CQ_HEIGHT_DRAIN];
    //uint64_t tscs, tsce;

    MPIDI_STATE_DECL(MPID_STATE_MPID_NEM_IB_POLL_EAGER);
    MPIDI_FUNC_ENTER(MPID_STATE_MPID_NEM_IB_POLL_EAGER);

    //MPID_nem_ib_tsc_poll = MPID_nem_ib_rdtsc();

    uint16_t *remote_poll;
    switch (ringbuf->type) {
    case MPID_NEM_IB_RINGBUF_EXCLUSIVE:
        remote_poll = &VC_FIELD(ringbuf->vc, ibcom->rsr_seq_num_poll);
        break;
    case MPID_NEM_IB_RINGBUF_SHARED:
        remote_poll = &MPID_nem_ib_remote_poll_shared;
        break;
    default:
        printf("unknown ringbuf->type\n");
    }

    void *buf =
        (uint8_t *) ringbuf->start +
        MPID_NEM_IB_COM_RDMABUF_SZSEG * ((uint16_t) (*remote_poll % ringbuf->nslot));
    volatile uint64_t *head_flag = MPID_NEM_IB_NETMOD_HDR_HEAD_FLAG_PTR(buf);
    if (*head_flag == 0) {
        goto fn_exit;
    }
    dprintf("ib_poll_eager,remote_poll=%d,buf=%p,sz=%d\n", *remote_poll, buf,
            MPID_NEM_IB_NETMOD_HDR_SZ_GET(buf));

#if 0
    ibcom_errno = MPID_nem_ib_com_poll_cq(MPID_NEM_IB_COM_RC_SHARED_RCQ, &cqe, &result);
    MPIU_ERR_CHKANDJUMP(ibcom_errno, mpi_errno, MPI_ERR_OTHER, "**MPID_nem_ib_com_poll_cq");
#endif
    dprintf("ib_poll_eager,eager-send,found\n");
    fflush(stdout);

    //MPIU_ERR_CHKANDJUMP1(cqe.status != IBV_WC_SUCCESS, mpi_errno, MPI_ERR_OTHER, "**MPID_nem_ib_com_poll_cq", "**MPID_nem_ib_com_poll_cq %s", MPID_nem_ib_com_strerror(ibcom_errno));

    int off_pow2_aligned;
    MPID_NEM_IB_OFF_POW2_ALIGNED(MPID_NEM_IB_NETMOD_HDR_SZ_GET(buf));
    volatile MPID_nem_ib_netmod_trailer_t *netmod_trailer =
        (MPID_nem_ib_netmod_trailer_t *) ((uint8_t *) buf + off_pow2_aligned);
    dprintf("poll,off_pow2_aligned=%d,netmod_trailer=%p,sz=%d\n", off_pow2_aligned, netmod_trailer,
            MPID_NEM_IB_NETMOD_HDR_SZ_GET(buf));
    //int k = 0;
    //tsce = MPID_nem_ib_rdtsc(); printf("9,%ld\n", tsce - tscs); // 55 for 512-byte
    //tscs = MPID_nem_ib_rdtsc();
    //#define MPID_NEM_IB_TLBPREF_POLL 20
#ifdef MPID_NEM_IB_TLBPREF_POLL
    int tlb_pref_ahd = (uint64_t) tailmagic + 4096 * MPID_NEM_IB_TLBPREF_POLL - (uint64_t) buf;
#endif
    while (netmod_trailer->tail_flag != MPID_NEM_IB_COM_MAGIC) {
        //k++;
#if 0   /* pre-fetch next RDMA-write-buf slot to cover TLB miss latency */
        __asm__ __volatile__
            ("movq %0, %%rsi;"
             "movq 0(%%rsi), %%rsi;"::"r"(ringbuf->start + MPID_NEM_IB_COM_RDMABUF_SZSEG *
                                          ((uint16_t)
                                            ((*remote_poll + 1) % MPID_NEM_IB_COM_RDMABUF_NSEG)))
             :"%rsi");
#endif
#ifdef MPID_NEM_IB_TLBPREF_POLL
        __asm__ __volatile__
            ("movq %0, %%rsi;" "movq 0(%%rsi), %%rax;"::"r"(buf + tlb_pref_ahd):"%rsi", "%rax");
        tlb_pref_ahd = (tlb_pref_ahd + 4096 * 20) % MPID_NEM_IB_COM_RDMABUF_SZ;
#endif
    }
    //tsce = MPID_nem_ib_rdtsc(); printf("0,%ld\n", tsce - tscs); // 20-60 for 512-byte
    //tscs = MPID_nem_ib_rdtsc();
    //dprintf("magic wait=%d\n", k);


    /* this reduces memcpy in MPIDI_CH3U_Receive_data_found */
    /* MPIDI_CH3_PktHandler_EagerSend (in ch3u_eager.c)
     * MPIDI_CH3U_Receive_data_found (in ch3u_handle_recv_pkt.c)
     * MPIU_Memcpy((char*)(rreq->dev.user_buf) + dt_true_lb, buf, data_sz);
     * 600 cycle for 512B!!! --> 284 cycle with prefetch
     */

#if 1
    void *rsi;
    for (rsi = (void *) buf; rsi < (void *) ((uint8_t *) buf + MPID_NEM_IB_NETMOD_HDR_SZ_GET(buf));
         rsi = (uint8_t *) rsi + 64 * 4) {
#ifdef __MIC__
        __asm__ __volatile__
            ("movq %0, %%rsi;"
             "vprefetch0 0x00(%%rsi);"
             "vprefetch0 0x40(%%rsi);" "vprefetch0 0x80(%%rsi);" "vprefetch0 0xc0(%%rsi);"::"r"(rsi)
             :"%rsi");
#else
        __asm__ __volatile__
            ("movq %0, %%rsi;"
             "prefetchnta 0x00(%%rsi);"
             "prefetchnta 0x40(%%rsi);"
             "prefetchnta 0x80(%%rsi);" "prefetchnta 0xc0(%%rsi);"::"r"(rsi)
             :"%rsi");
#endif
    }
#endif

    /* Increment here because handle_pkt of CLOSE calls poll_eager recursively */
    (*remote_poll) += 1;
    dprintf("ib_poll,inc,remote_poll=%d\n", *remote_poll);

    /* VC is stored in the packet for shared ring buffer */
    switch (ringbuf->type) {
    case MPID_NEM_IB_RINGBUF_EXCLUSIVE:
        vc = ringbuf->vc;
        break;
    case MPID_NEM_IB_RINGBUF_SHARED:
        vc = MPID_NEM_IB_NETMOD_HDR_VC_GET(buf);
        break;
    default:
        printf("unknown ringbuf->type\n");
    }
    vc_ib = VC_IB(vc);
    dprintf("poll_eager,vc=%p\n", vc);

    /* Save it because handle_pkt frees buf when the packet is MPIDI_CH3_PKT_CLOSE */
    ssize_t sz_pkt = MPID_NEM_IB_NETMOD_HDR_SIZEOF_GET(buf);
    MPIDI_CH3_Pkt_eager_send_t *pkt = (MPIDI_CH3_Pkt_eager_send_t *) ((uint8_t *) buf + sz_pkt);
    dprintf("pkt=%p,sizeof=%ld\n", pkt, sz_pkt);
    MPIU_Assert(MPID_NEM_IB_NETMOD_HDR_SZ_GET(buf) >= sz_pkt + sizeof(MPIDI_CH3_Pkt_t));
    dprintf
        ("handle_pkt,before,%d<-%d,id=%d,pkt->type=%d,pcc=%d,MPIDI_CH3_PKT_END_ALL=%d,pkt=%p,subtype=%d\n",
         MPID_nem_ib_myrank, vc->pg_rank, *remote_poll, pkt->type,
         MPIDI_CH3I_progress_completion_count.v, MPIDI_CH3_PKT_END_ALL, pkt,
         ((MPID_nem_pkt_netmod_t *) pkt)->subtype);
    /* see MPIDI_CH3_PktHandler_EagerSend (in src/mpid/ch3/src/ch3u_eager.c) */
    mpi_errno =
        MPID_nem_handle_pkt(vc, (char *) ((uint8_t *) buf + sz_pkt),
                            (MPIDI_msg_sz_t) (MPID_NEM_IB_NETMOD_HDR_SZ_GET(buf) - sz_pkt));
    if (mpi_errno) {
        MPIU_ERR_POP(mpi_errno);
    }
    //tsce = MPID_nem_ib_rdtsc(); printf("0,%ld\n", tsce - tscs); // 512-byte, 900 cyc (1100 w/o prefetch)

    /* Update occupation status of remote SR (send request) queue */
    /* this includes local RDMA-wr-to buf occupation
     * because MPID_nem_handle_pkt releases RDMA-wr-to buf by copying data out */
    /* responder releases resource and then embed largest sequence number into MPI message bound to initiator */
#if 1
    if ((vc->state != MPIDI_VC_STATE_INACTIVE) ||
        (vc->state == MPIDI_VC_STATE_INACTIVE && vc_ib->vc_terminate_buf == buf)) {
        dprintf
            ("handle_pkt,after,%d<-%d,id=%d,pkt->type=%d,eagershort=%d,close=%d,rts=%d,piggy-backed-eagersend=%d\n",
             MPID_nem_ib_myrank, vc->pg_rank, *remote_poll, pkt->type,
             MPIDI_CH3_PKT_EAGERSHORT_SEND, MPIDI_CH3_PKT_CLOSE, MPIDI_NEM_PKT_LMT_RTS,
             MPIDI_NEM_IB_PKT_EAGER_SEND);
    }

    int notify_rate;
    if ((vc->state != MPIDI_VC_STATE_INACTIVE) ||
        (vc->state == MPIDI_VC_STATE_INACTIVE && vc_ib->vc_terminate_buf == buf)) {
        ibcom_errno =
            MPID_nem_ib_com_rdmabuf_occupancy_notify_rate_get(MPID_nem_ib_conns[vc->pg_rank].fd,
                                                              &notify_rate);
        dprintf("poll_eager,sendq=%d,ncom=%d,ncqe=%d,ldiff=%d(%d-%d),rate=%d\n",
                MPID_nem_ib_sendq_empty(vc_ib->sendq),
                vc_ib->ibcom->ncom < MPID_NEM_IB_COM_MAX_SQ_CAPACITY,
                MPID_nem_ib_ncqe < MPID_NEM_IB_COM_MAX_CQ_CAPACITY,
                MPID_nem_ib_diff16(vc_ib->ibcom->sseq_num, vc_ib->ibcom->lsr_seq_num_tail),
                vc_ib->ibcom->sseq_num, vc_ib->ibcom->lsr_seq_num_tail, notify_rate);
    }

    if (ringbuf->type == MPID_NEM_IB_RINGBUF_EXCLUSIVE) {
        dprintf("poll_eager,rdiff=%d(%d-%d)\n",
                MPID_nem_ib_diff16(vc_ib->ibcom->rsr_seq_num_tail,
                                   vc_ib->ibcom->rsr_seq_num_tail_last_sent),
                vc_ib->ibcom->rsr_seq_num_tail, vc_ib->ibcom->rsr_seq_num_tail_last_sent);
    }

    //dprintf("ib_poll,current pcc=%d\n", MPIDI_CH3I_progress_completion_count.v);

    /* Don't forget to put lmt-cookie types here!! */
    if (1) {
        /* lmt cookie messages or control message other than eager-short */

        /* eager-send with zero-length data is released here
         * because there is no way to trace the RDMA-write-to buffer addr
         * because rreq->dev.tmpbuf is set to zero in ch3_eager.c
         */
        if ((vc->state != MPIDI_VC_STATE_INACTIVE) ||
            (vc->state == MPIDI_VC_STATE_INACTIVE && vc_ib->vc_terminate_buf == buf)) {
            dprintf("poll_eager,released,type=%d,MPIDI_NEM_IB_PKT_REPLY_SEQ_NUM=%d\n", pkt->type,
                    MPIDI_NEM_IB_PKT_REPLY_SEQ_NUM);
            MPID_nem_ib_recv_buf_released(vc,
                                          (void *) ((uint8_t *) buf +
                                                    sz_pkt + sizeof(MPIDI_CH3_Pkt_t)));
        }
    }
    else {
        if (MPID_NEM_IB_NETMOD_HDR_SZ_GET(buf) == sz_pkt + sizeof(MPIDI_CH3_Pkt_t)) {
            if (pkt->type == MPIDI_CH3_PKT_EAGERSHORT_SEND
                //||                  pkt->type == MPIDI_CH3_PKT_GET
) {
            }
            else {
                printf("ib_poll,unknown pkt->type=%d\n", pkt->type);
                assert(0);
                MPIU_ERR_INTERNALANDJUMP(mpi_errno, "MPI header only but not released");
            }
        }
    }
#endif

    if ((vc->state != MPIDI_VC_STATE_INACTIVE) ||
        (vc->state == MPIDI_VC_STATE_INACTIVE && vc_ib->vc_terminate_buf == buf)) {
        dprintf("ib_poll,hdr_ringbuf_type=%d\n", MPID_NEM_IB_NETMOD_HDR_RINGBUF_TYPE_GET(buf));

        if (MPID_NEM_IB_NETMOD_HDR_RINGBUF_TYPE_GET(buf) & MPID_NEM_IB_RINGBUF_RELINDEX) {
            vc_ib->ibcom->lsr_seq_num_tail = MPID_NEM_IB_NETMOD_HDR_RELINDEX_GET(buf);
            dprintf("ib_poll,local_tail is updated to %d\n",
                    MPID_NEM_IB_NETMOD_HDR_RELINDEX_GET(buf));
        }
    }

    /* Clear flag */
    if ((vc->state != MPIDI_VC_STATE_INACTIVE) ||
        (vc->state == MPIDI_VC_STATE_INACTIVE && vc_ib->vc_terminate_buf == buf))
        MPID_NEM_IB_NETMOD_HDR_HEAD_FLAG_SET(buf, 0);

#if 1   /* We move this code from the end of vc_terminate. */
    if (vc->state == MPIDI_VC_STATE_INACTIVE && vc_ib->vc_terminate_buf == buf) {
        /* clear stored data */
        vc_ib->vc_terminate_buf = NULL;

        /* Destroy ring-buffer */
        ibcom_errno = MPID_nem_ib_ringbuf_free(vc);
        MPIU_ERR_CHKANDJUMP(ibcom_errno, mpi_errno, MPI_ERR_OTHER, "**MPID_nem_ib_ringbuf_free");

        /* Check connection status stored in VC when on-demand connection is used */
        dprintf("vc_terminate,%d->%d,close\n", MPID_nem_ib_myrank, vc->pg_rank);
        ibcom_errno = MPID_nem_ib_com_close(vc_ib->sc->fd);
        MPIU_ERR_CHKANDJUMP(ibcom_errno, mpi_errno, MPI_ERR_OTHER, "**MPID_nem_ib_com_close");

        /* Destroy array of scratch-pad QPs */
        MPIU_Assert(MPID_nem_ib_conns_ref_count > 0);
        if (--MPID_nem_ib_conns_ref_count == 0) {
            MPIU_Free(MPID_nem_ib_conns);
        }

        /* TODO don't create them for shared memory vc */

        /* Destroy scratch-pad */
        ibcom_errno = MPID_nem_ib_com_free(MPID_nem_ib_scratch_pad_fds[vc->pg_rank],
#ifdef MPID_NEM_IB_ONDEMAND
                                           MPID_NEM_IB_CM_OFF_CMD +
                                           MPID_NEM_IB_CM_NSEG * sizeof(MPID_nem_ib_cm_cmd_t) +
                                           sizeof(MPID_nem_ib_ringbuf_headtail_t)
#else
                                           MPID_nem_ib_nranks * sizeof(MPID_nem_ib_com_qp_state_t)
#endif
);

        MPIU_ERR_CHKANDJUMP(ibcom_errno, mpi_errno, MPI_ERR_OTHER, "**MPID_nem_ib_com_free");

        /* Destroy scratch-pad QP */
        ibcom_errno = MPID_nem_ib_com_close(MPID_nem_ib_scratch_pad_fds[vc->pg_rank]);
        MPIU_ERR_CHKANDJUMP(ibcom_errno, mpi_errno, MPI_ERR_OTHER, "**MPID_nem_ib_com_close");

        /* Destroy array of scratch-pad QPs */
        MPIU_Assert(MPID_nem_ib_scratch_pad_fds_ref_count > 0);
        if (--MPID_nem_ib_scratch_pad_fds_ref_count == 0) {
            MPIU_Free(MPID_nem_ib_scratch_pad_fds);
            MPIU_Free(MPID_nem_ib_scratch_pad_ibcoms);
        }
    }
#endif

  fn_exit:
    MPIDI_FUNC_EXIT(MPID_STATE_MPID_NEM_IB_POLL_EAGER);
    return mpi_errno;
  fn_fail:
    goto fn_exit;
}

#undef FUNCNAME
#define FUNCNAME MPID_nem_ib_poll
#undef FCNAME
#define FCNAME MPIDI_QUOTE(FUNCNAME)
int MPID_nem_ib_poll(int in_blocking_poll)
{

    int mpi_errno = MPI_SUCCESS;
    int ibcom_errno;
    uint32_t i;

    MPIDI_STATE_DECL(MPID_STATE_MPID_NEM_IB_POLL);
    MPIDI_FUNC_ENTER(MPID_STATE_MPID_NEM_IB_POLL);

#if 1
    unsigned int progress_completion_count_old = MPIDI_CH3I_progress_completion_count.v;
#endif

    /* poll lmt */
    /* when receiver side sends CTS to sender side
     * sender receives CTS and give up sending RTS
     * sender initiates RDMA-write,
     * sender sends RTS of the next epoch,
     * to detect the end of RDMA-write first and DP the entry for CTS,
     * you should perform lmt-poll first, next eager-poll
     */
    MPID_Request *rreq, *prev_rreq;
    rreq = MPID_nem_ib_lmtq_head(MPID_nem_ib_lmtq);
    if (rreq) {
#if defined (MPID_NEM_IB_TIMER_WAIT_IB_POLL)
        if (in_blocking_poll) {
            tsc[0] = MPI_rdtsc();
        }
#endif
        // dprintf("ib_poll,poll lmtq\n");
        prev_rreq = NULL;
        do {
            /* Obtain cookie. pkt_RTS_handler memcpy it (in mpid_nem_lmt.c) */
            /* MPID_IOV_BUF is macro, converted into iov_base (in src/include/mpiiov.h) */
            /* do not use s_cookie_buf because do_cts frees it */
            //MPID_nem_ib_lmt_cookie_t* s_cookie_buf = (MPID_nem_ib_lmt_cookie_t*)rreq->ch.lmt_tmp_cookie.iov_base;

            /* Wait for completion of DMA */
            /* do not use s_cookie_buf->sz because do_cts frees it */
            volatile void *write_to_buf;
            int is_contig;
            MPID_Datatype_is_contig(rreq->dev.datatype, &is_contig);
            if (is_contig) {
                write_to_buf =
                    (void *) ((char *) rreq->dev.user_buf /*+ REQ_FIELD(req, lmt_dt_true_lb) */);
            }
            else {
                write_to_buf = REQ_FIELD(rreq, lmt_pack_buf);
            }

            //assert(REQ_FIELD(rreq, lmt_dt_true_lb) == 0);
            volatile uint8_t *tailmagic =
                (uint8_t *) ((uint8_t *) write_to_buf /*+ REQ_FIELD(rreq, lmt_dt_true_lb) */  +
                             rreq->ch.lmt_data_sz - sizeof(uint8_t));

            if (*tailmagic != REQ_FIELD(rreq, lmt_tail)) {
                goto next;
            }
            dprintf("ib_poll,sz=%ld,old tail=%02x,new tail=%02x\n", rreq->ch.lmt_data_sz,
                    REQ_FIELD(rreq, lmt_tail), *tailmagic);

            dprintf
                ("ib_poll,lmt found,%d<-%d,req=%p,ref_count=%d,is_contig=%d,write_to_buf=%p,lmt_pack_buf=%p,user_buf=%p,tail=%p\n",
                 MPID_nem_ib_myrank, rreq->ch.vc->pg_rank, rreq, rreq->ref_count, is_contig,
                 write_to_buf, REQ_FIELD(rreq, lmt_pack_buf), rreq->dev.user_buf, tailmagic);

            /* unpack non-contiguous dt */
            if (!is_contig) {
                dprintf("ib_poll,copying noncontiguous data to user buffer\n");

                /* see MPIDI_CH3U_Request_unpack_uebuf (in /src/mpid/ch3/src/ch3u_request.c) */
                /* or MPIDI_CH3U_Receive_data_found (in src/mpid/ch3/src/ch3u_handle_recv_pkt.c) */
                MPIDI_msg_sz_t unpack_sz = rreq->ch.lmt_data_sz;
                MPID_Segment seg;
                MPI_Aint last;

                MPID_Segment_init(rreq->dev.user_buf, rreq->dev.user_count, rreq->dev.datatype,
                                  &seg, 0);
                last = unpack_sz;
                MPID_Segment_unpack(&seg, 0, &last, REQ_FIELD(rreq, lmt_pack_buf));
                if (last != unpack_sz) {
                    /* --BEGIN ERROR HANDLING-- */
                    /* received data was not entirely consumed by unpack()
                     * because too few bytes remained to fill the next basic
                     * datatype */
                    MPIR_STATUS_SET_COUNT(rreq->status, last);
                    rreq->status.MPI_ERROR =
                        MPIR_Err_create_code(MPI_SUCCESS, MPIR_ERR_RECOVERABLE, FCNAME, __LINE__,
                                             MPI_ERR_TYPE, "**MPID_nem_ib_poll", 0);
                    /* --END ERROR HANDLING-- */
                }
#if 1   /* debug, enable again later, polling waits forever when freeing it here. */
                //if (rreq->ref_count == 1) {
                dprintf("ib_poll,lmt,ref_count=%d,lmt_pack_buf=%p\n", rreq->ref_count,
                        REQ_FIELD(rreq, lmt_pack_buf));
                //MPIU_Free(REQ_FIELD(rreq, lmt_pack_buf));
                MPID_nem_ib_stfree(REQ_FIELD(rreq, lmt_pack_buf), (size_t) rreq->ch.lmt_data_sz);
                //} else {
                // dprintf("ib_poll,lmt,ref_count=%d,lmt_pack_buf=%p\n", rreq->ref_count, REQ_FIELD(rreq, lmt_pack_buf));
                //}
#endif
            }

            /* send done to sender. vc is stashed in MPID_nem_ib_lmt_start_recv (in ib_lmt.c) */
#ifdef MPID_NEM_IB_DEBUG_POLL
            MPID_nem_ib_vc_area *vc_ib = VC_IB(rreq->ch.vc);
#endif
            dprintf("ib_poll,GET,lmt_send_GET_DONE,rsr_seq_num_tail=%d\n",
                    vc_ib->ibcom->rsr_seq_num_tail);
            MPID_nem_ib_lmt_send_GET_DONE(rreq->ch.vc, rreq);
            dprintf("ib_poll,prev_rreq=%p,rreq->lmt_next=%p\n", prev_rreq,
                    MPID_nem_ib_lmtq_next(rreq));

            /* unlink rreq */
            if (prev_rreq != NULL) {
                MPID_nem_ib_lmtq_next(prev_rreq) = MPID_nem_ib_lmtq_next(rreq);
            }
            else {
                MPID_nem_ib_lmtq_head(MPID_nem_ib_lmtq) = MPID_nem_ib_lmtq_next(rreq);
            }
            if (MPID_nem_ib_lmtq_next(rreq) == NULL) {
                MPID_nem_ib_lmtq.tail = prev_rreq;
            }

            /* save rreq->dev.next (and rreq) because decrementing reference-counter might free rreq */
            MPID_Request *tmp_rreq = rreq;
            rreq = MPID_nem_ib_lmtq_next(rreq);

            /* decrement completion-counter */
            dprintf("ib_poll,%d<-%d,", MPID_nem_ib_myrank, tmp_rreq->ch.vc->pg_rank);
            int incomplete;
            MPIDI_CH3U_Request_decrement_cc(tmp_rreq, &incomplete);
            dprintf("lmt,complete,tmp_rreq=%p,rreq->ref_count=%d,comm=%p\n", tmp_rreq,
                    tmp_rreq->ref_count, tmp_rreq->comm);

            if (!incomplete) {
                MPIDI_CH3_Progress_signal_completion();
            }

            /* lmt_start_recv increments ref_count
             * drain_scq and ib_poll is not ordered, so both can decrement ref_count */
            /* ref_count is decremented
             * get-lmt: ib_poll, drain_scq, wait
             * put-lmt: ib_poll, wait */
            MPID_Request_release(tmp_rreq);
            dprintf("ib_poll,relese,req=%p\n", tmp_rreq);
            dprintf("ib_poll,lmt,after release,tmp_rreq=%p,rreq->ref_count=%d,comm=%p\n",
                    tmp_rreq, tmp_rreq->ref_count, tmp_rreq->comm);


            goto next_unlinked;
          next:
            prev_rreq = rreq;
            rreq = MPID_nem_ib_lmtq_next(rreq);
          next_unlinked:;
        } while (rreq);
#if defined (MPID_NEM_IB_TIMER_WAIT_IB_POLL)
        if (in_blocking_poll) {
            stsc[0] += MPI_rdtsc() - tsc[0];
        }
#endif
    }

#if defined (MPID_NEM_IB_TIMER_WAIT_IB_POLL)
    if (in_blocking_poll) {
        tsc[1] = MPI_rdtsc();
    }
#endif
    int ncom_almost_full = 0;

    /* [MPID_NEM_IB_NRINGBUF-1] stores shared ring buffer */
    for (i = 0; i < MPID_NEM_IB_NRINGBUF; i++) {
        if ((((MPID_nem_ib_ringbuf_allocated[i / 64] >> (i & 63)) & 1) == 0) ||
            !MPID_nem_ib_ringbuf) {
            //dprintf("poll,cont\n");
            continue;
        }
        //tscs = MPID_nem_ib_rdtsc();
        //dprintf("poll,kicking progress engine for %d\n", i);
        mpi_errno = MPID_nem_ib_poll_eager(&MPID_nem_ib_ringbuf[i]);
        MPIU_ERR_CHKANDJUMP(mpi_errno, mpi_errno, MPI_ERR_OTHER, "**MPID_nem_ib_poll_eager");

        /* MPID_nem_ib_ringbuf may be freed in poll_eager, when we received CLOSE-packet. */
        if (!MPID_nem_ib_ringbuf) {
            dprintf("MPID_nem_ib_ringbuf is freed\n");
            continue;
        }

        /* without this, command in sendq doesn't have a chance
         * to perform send_progress
         * when send and progress_send call drain_scq asking it
         * for not performing send_progress and make the CQ empty */
        if (MPID_nem_ib_ringbuf[i].type == MPID_NEM_IB_RINGBUF_EXCLUSIVE) {
            mpi_errno = MPID_nem_ib_send_progress(MPID_nem_ib_ringbuf[i].vc);
            MPIU_ERR_CHKANDJUMP(mpi_errno, mpi_errno, MPI_ERR_OTHER, "**MPID_nem_ib_send_progress");

            ncom_almost_full |=
                (VC_FIELD(MPID_nem_ib_ringbuf[i].vc, ibcom->ncom) >=
                 MPID_NEM_IB_COM_MAX_SQ_HEIGHT_DRAIN);
        }


#if 0
        /* aggressively perform drain_scq */
        ncom_almost_full |= !(MPID_nem_ib_sendq_empty(VC_FIELD(MPID_nem_ib_ringbuf[i].vc, sendq)));
#endif
    }
#if defined (MPID_NEM_IB_TIMER_WAIT_IB_POLL)
    if (in_blocking_poll) {
        stsc[1] += MPI_rdtsc() - tsc[1];
    }
#endif

    // lazy feching of completion queue entry because it causes cache-miss
#if defined (MPID_NEM_IB_LMT_GET_CQE)
    if (MPID_nem_ib_ncqe_to_drain > 0 || MPID_nem_ib_ncqe_nces > 0 ||
        MPID_nem_ib_ncqe >= MPID_NEM_IB_COM_MAX_CQ_HEIGHT_DRAIN || ncom_almost_full)
#endif
#if !defined (MPID_NEM_IB_LMT_GET_CQE)
        if (/*(in_blocking_poll && result == 0) || */ MPID_nem_ib_ncqe_nces >
            0 || MPID_nem_ib_ncqe >= MPID_NEM_IB_COM_MAX_CQ_HEIGHT_DRAIN || ncom_almost_full)
#endif
        {
#if defined (MPID_NEM_IB_TIMER_WAIT_IB_POLL)
            if (in_blocking_poll) {
                tsc[0] = MPI_rdtsc();
            }
#endif
            //dprintf("ib_poll,calling drain_scq\n");
            ibcom_errno = MPID_nem_ib_drain_scq(0);
            MPIU_ERR_CHKANDJUMP(ibcom_errno, mpi_errno, MPI_ERR_OTHER, "**MPID_nem_ib_drain_scq");
#if defined (MPID_NEM_IB_TIMER_WAIT_IB_POLL)
            if (in_blocking_poll) {
                stsc[0] += MPI_rdtsc() - tsc[0];
            }
#endif
        }
#if 1
    /* aggressively perform drain_scq */
    ibcom_errno = MPID_nem_ib_drain_scq(0);
    MPIU_ERR_CHKANDJUMP(ibcom_errno, mpi_errno, MPI_ERR_OTHER, "**MPID_nem_ib_drain_scq");
#endif
#ifdef MPID_NEM_IB_ONDEMAND
    /* process incoming connection request */
    MPID_nem_ib_cm_poll_syn();
    MPID_nem_ib_cm_poll();
    //dprintf("ib_poll,MPID_nem_ib_ncqe_scratch_pad_to_drain=%d\n",
    //MPID_nem_ib_ncqe_scratch_pad_to_drain);
    /* process outgoing conncetion request */
    if (MPID_nem_ib_ncqe_scratch_pad_to_drain > 0 ||
        MPID_nem_ib_ncqe_scratch_pad >= MPID_NEM_IB_COM_MAX_CQ_HEIGHT_DRAIN) {
        ibcom_errno = MPID_nem_ib_cm_drain_scq();
        MPIU_ERR_CHKANDJUMP(ibcom_errno, mpi_errno, MPI_ERR_OTHER, "**MPID_nem_ib_cm_drain_scq");
    }

    /* Kick progress engine because time elapsed and it'd fire a event in the send queue */
    MPID_nem_ib_cm_progress();
#endif
    MPID_nem_ib_ringbuf_progress();
    MPID_nem_ib_progress_engine_vt += 1;        /* Progress virtual time */
#if 1
    /* if polling on eager-send and lmt would repeat frequently, perform "pause" to yield instruction issue bandwitdh to other logical-core */
    if (in_blocking_poll && progress_completion_count_old == MPIDI_CH3I_progress_completion_count.v) {
        __asm__ __volatile__("pause;":::"memory");
    }
#endif
    //if (in_blocking_poll) { goto prev; }

  fn_exit:
    MPIDI_FUNC_EXIT(MPID_STATE_MPID_NEM_IB_POLL);
    return mpi_errno;
  fn_fail:
    goto fn_exit;
}

   /* new rreq is obtained in MPID_Irecv in mpid_irecv.c,
    * so we associate rreq with a receive request and ibv_post_recv it
    * so that we can obtain rreq by ibv_poll_cq
    */
#undef FUNCNAME
#define FUNCNAME MPID_nem_ib_recv_posted
#undef FCNAME
#define FCNAME MPIDI_QUOTE(FUNCNAME)
int MPID_nem_ib_recv_posted(struct MPIDI_VC *vc, struct MPID_Request *req)
{

    int mpi_errno = MPI_SUCCESS;
    MPID_nem_ib_vc_area *vc_ib = VC_IB(vc);
    MPIDI_STATE_DECL(MPID_STATE_MPID_NEM_IB_RECV_POSTED);
    MPIDI_FUNC_ENTER(MPID_STATE_MPID_NEM_IB_RECV_POSTED);
    dprintf("recv_posted,enter,%d->%d,req=%p\n", MPID_nem_ib_myrank, vc->pg_rank, req);
#ifdef MPID_NEM_IB_ONDEMAND
    if (vc_ib->connection_state != MPID_NEM_IB_CM_ESTABLISHED) {
        goto fn_exit;
    }
#endif

#if 0
    int ibcom_errno;
    ibcom_errno = MPID_nem_ib_com_irecv(vc_ib->sc->fd, (uint64_t) vc->pg_rank);
    MPIU_ERR_CHKANDJUMP(ibcom_errno, mpi_errno, MPI_ERR_OTHER, "**MPID_nem_ib_com_irecv");
#endif
#if 1   /*takagi */
    MPIDI_msg_sz_t data_sz;
    int dt_contig;
    MPI_Aint dt_true_lb;
    MPID_Datatype *dt_ptr;
    MPIDI_Datatype_get_info(req->dev.user_count, req->dev.datatype,
                            dt_contig, data_sz, dt_ptr, dt_true_lb);
    /* poll when rreq is for lmt */
    /* anticipating received message finds maching request in the posted-queue */
    if (data_sz + sizeof(MPIDI_CH3_Pkt_eager_send_t) > vc->eager_max_msg_sz) {
        //if (MPID_nem_ib_tsc_poll - MPID_nem_ib_rdtsc() > MPID_NEM_IB_POLL_PERIOD_RECV_POSTED) {
//#if 1
        if (VC_FIELD(vc, ibcom->remote_ringbuf)) {
            mpi_errno = MPID_nem_ib_poll_eager(VC_FIELD(vc, ibcom->remote_ringbuf));
//#else
//        mpi_errno = MPID_nem_ib_poll(0);
//#endif
            if (mpi_errno) {
                MPIU_ERR_POP(mpi_errno);
            }
        }
        //}
    }

    else {
#if 1
        /* anticipating received message finds maching request in the posted-queue */
        //if (MPID_nem_ib_tsc_poll - MPID_nem_ib_rdtsc() > MPID_NEM_IB_POLL_PERIOD_RECV_POSTED) {
        if (VC_FIELD(vc, ibcom->remote_ringbuf)) {
#if 1
            mpi_errno = MPID_nem_ib_poll_eager(VC_FIELD(vc, ibcom->remote_ringbuf));
#else
            mpi_errno = MPID_nem_ib_poll(0);
#endif
            if (mpi_errno) {
                MPIU_ERR_POP(mpi_errno);
            }
        }
        //}
#endif
    }
#endif

  fn_exit:
    MPIDI_FUNC_EXIT(MPID_STATE_MPID_NEM_IB_RECV_POSTED);
    return mpi_errno;
  fn_fail:
    goto fn_exit;
}

/* (1) packet-handler memcpy RDMA-write-to buf data to MPI user-buffer when matching request is found in posted-queue
   (2) MPI_Irecv memcpy RDMA-write-to buf data to MPI user-buffer when matching request is found in unexpected-queue
   the latter case can't be dealt with when call this after poll-found and packet-handler
   (packet-handler memcpy RDMA-write-to buf to another buffer when
   matching request is not found in posted-queue, so calling this after poll-found and packet-handler
   suffices in original MPICH implementation)
*/
#undef FUNCNAME
#define FUNCNAME MPID_nem_ib_recv_buf_released
#undef FCNAME
#define FCNAME MPIDI_QUOTE(FUNCNAME)
int MPID_nem_ib_recv_buf_released(struct MPIDI_VC *vc, void *user_data)
{
    int mpi_errno = MPI_SUCCESS;
    int ibcom_errno;
    MPID_nem_ib_vc_area *vc_ib = VC_IB(vc);
    MPIDI_STATE_DECL(MPID_STATE_MPID_NEM_IB_RECV_BUF_RELEASED);
    MPIDI_FUNC_ENTER(MPID_STATE_MPID_NEM_IB_RECV_BUF_RELEASED);
    dprintf("recv_buf_released,%d<-%d,user_data=%p\n", MPID_nem_ib_myrank, vc->pg_rank, user_data);
#if 1   /* moving from ib_poll */
    /* Clear all possible tail flag slots */
    /* tail flag is located at MPID_NEM_IB_COM_INLINE_DATA boundary and variable length entails multiple prospective locations for the future use */
    /* see MPIDI_CH3_PktHandler_EagerShortSend (in src/mpid/ch3/src/ch3u_eager.c */
    /* eager-send with zero-length data is released in poll
     * because there is no way to trace the RDMA-write-to buffer addr
     * because rreq->dev.tmpbuf is set to zero in ch3_eager.c
     */
    if (user_data == NULL) {
        goto fn_exit;
    }

    if ((void *) MPID_nem_ib_rdmawr_to_alloc_start > user_data &&
        user_data >= (void *) (MPID_nem_ib_rdmawr_to_alloc_start +
                               MPID_NEM_IB_COM_RDMABUF_SZ * MPID_NEM_IB_NRINGBUF)) {
        MPID_nem_ib_segv;
    }
    unsigned long mod =
        (unsigned long) ((uint8_t *) user_data -
                         (uint8_t *) vc_ib->ibcom->remote_ringbuf->start) &
        (MPID_NEM_IB_COM_RDMABUF_SZSEG - 1);
    void *buf = (void *) ((uint8_t *) user_data - mod);
    //dprintf("recv_buf_released,clearing,buf=%p\n", buf);
    int off_pow2_aligned;
    MPID_NEM_IB_OFF_POW2_ALIGNED(MPID_NEM_IB_NETMOD_HDR_SZ_GET(buf));
    //dprintf("recv_buf_released,sz=%d,pow2=%d\n", MPID_NEM_IB_NETMOD_HDR_SZ_GET(buf), off_pow2_aligned);
#if 1
    uint32_t offset;
    for (offset = 15;;
         offset =
         (((offset + 1) << 1) - 1) > MPID_NEM_IB_MAX_OFF_POW2_ALIGNED ?
         MPID_NEM_IB_MAX_OFF_POW2_ALIGNED : (((offset + 1) << 1) - 1)) {
        MPID_nem_ib_netmod_trailer_t *netmod_trailer =
            (MPID_nem_ib_netmod_trailer_t *) ((uint8_t *) buf + offset);
        if (MPID_nem_ib_rdmawr_to_alloc_start > (uint8_t *) netmod_trailer &&
            (uint8_t *) netmod_trailer >=
            MPID_nem_ib_rdmawr_to_alloc_start + MPID_NEM_IB_COM_RDMABUF_SZ * MPID_NEM_IB_NRINGBUF) {
            MPID_nem_ib_segv;
        }
        netmod_trailer->tail_flag = 0;
        if (offset == off_pow2_aligned) {
            break;
        }
    }
#endif
#endif

#if 1   /* moving from ib_poll */
    /* mark that one eager-send RDMA-write-to buffer has been released */
    uint16_t index_slot =
        (unsigned long) ((uint8_t *) user_data -
                         (uint8_t *) vc_ib->ibcom->remote_ringbuf->start) /
        MPID_NEM_IB_COM_RDMABUF_SZSEG;
    MPIU_Assert(index_slot < (uint16_t) (vc_ib->ibcom->remote_ringbuf->nslot));
    dprintf("released,user_data=%p,mem=%p,sub=%08lx,index_slot=%d\n",
            user_data, vc_ib->ibcom->remote_ringbuf->start,
            (unsigned long) user_data -
            (unsigned long) vc_ib->ibcom->remote_ringbuf->start, index_slot);
    dprintf("released,index_slot=%d,released=%016lx\n", index_slot,
            vc_ib->ibcom->remote_ringbuf->remote_released[index_slot / 64]);
    vc_ib->ibcom->remote_ringbuf->remote_released[index_slot / 64] |= (1ULL << (index_slot & 63));
    dprintf("released,after bitset,%016lx\n",
            vc_ib->ibcom->remote_ringbuf->remote_released[index_slot / 64]);
    //    int index_tail = (vc_ib->ibcom->rsr_seq_num_tail + 1) & (vc_ib->ibcom->local_ringbuf_nslot-1);
    MPID_nem_ib_ringbuf_headtail_t *headtail =
        (MPID_nem_ib_ringbuf_headtail_t *) ((uint8_t *) MPID_nem_ib_scratch_pad +
                                            MPID_NEM_IB_RINGBUF_OFF_HEAD);
    uint16_t index_tail =
        vc_ib->ibcom->remote_ringbuf->type ==
        MPID_NEM_IB_RINGBUF_EXCLUSIVE ? ((uint16_t) (vc_ib->ibcom->rsr_seq_num_tail + 1) %
                                         vc_ib->ibcom->
                                         remote_ringbuf->nslot) : ((uint16_t) (headtail->tail +
                                                                               1) %
                                                                   vc_ib->ibcom->remote_ringbuf->
                                                                   nslot);
    dprintf("released,index_tail=%d\n", index_tail);
    dprintf("released,%016lx\n", vc_ib->ibcom->remote_ringbuf->remote_released[index_tail / 64]);
    if (1 || (index_tail & 7) || MPID_nem_ib_diff16(index_slot, index_tail) >= vc_ib->ibcom->remote_ringbuf->nslot - 8) {       /* avoid wrap-around */
        while (1) {
            if (((vc_ib->ibcom->remote_ringbuf->
                  remote_released[index_tail / 64] >> (index_tail & 63)) & 1) == 1) {
                if (vc_ib->ibcom->remote_ringbuf->type == MPID_NEM_IB_RINGBUF_EXCLUSIVE) {
                    vc_ib->ibcom->rsr_seq_num_tail += 1;
                    dprintf("exclusive ringbuf,remote_tail,incremented to %d\n",
                            vc_ib->ibcom->rsr_seq_num_tail);
                }
                else {
                    headtail->tail += 1;
                    dprintf("shared ringbuf,tail,incremented to %d,head=%ld\n",
                            headtail->tail, headtail->head);
                }
                vc_ib->ibcom->remote_ringbuf->remote_released[index_tail / 64] &=
                    ~(1ULL << (index_tail & 63));
                index_tail = (uint16_t) (index_tail + 1) % vc_ib->ibcom->remote_ringbuf->nslot;
            }
            else {
                break;
            }
        }
    }
    else {
        if (((vc_ib->ibcom->remote_ringbuf->remote_released[index_tail /
                                                            64] >> (index_tail & 63)) & 0xff) ==
            0xff) {
            vc_ib->ibcom->rsr_seq_num_tail += 8;
            vc_ib->ibcom->remote_ringbuf->remote_released[index_tail / 64] &=
                ~(0xffULL << (index_tail & 63));
            //dprintf("released[index_tail/64]=%016lx\n", vc_ib->ibcom->remote_ringbuf->remote_released[index_tail / 64]);
        }
    }

    //dprintf("recv_buf_released,%d->%d,rsr_seq_num_tail=%d,rsr_seq_num_tail_last_sent=%d\n", MPID_nem_ib_myrank, vc->pg_rank, vc_ib->ibcom->rsr_seq_num_tail, vc_ib->ibcom->rsr_seq_num_tail_last_sent);

    int notify_rate;
    ibcom_errno =
        MPID_nem_ib_com_rdmabuf_occupancy_notify_rate_get(MPID_nem_ib_conns
                                                          [vc->pg_rank].fd, &notify_rate);
    MPIU_ERR_CHKANDJUMP(ibcom_errno, mpi_errno, MPI_ERR_OTHER,
                        "**MPID_nem_ib_com_rdmabuf_occupancy_notify_rate_get");
    /* if you missed the chance to make eager-send message piggy-back it */
    if (vc_ib->ibcom->remote_ringbuf->type ==
        MPID_NEM_IB_RINGBUF_EXCLUSIVE &&
        MPID_nem_ib_diff16(vc_ib->ibcom->rsr_seq_num_tail,
                           vc_ib->ibcom->rsr_seq_num_tail_last_sent) >
        MPID_NEM_IB_COM_RDMABUF_OCCUPANCY_NOTIFY_RATE_DELAY_MULTIPLIER(notify_rate)
        //|| MPID_nem_ib_diff16(lsr_seq_num_head, vc_ib->ibcom->lsr_seq_num_tail_last_sent) == vc_ib->ibcom->local_ringbuf_nslot
) {
        MPID_Request *sreq;
        sreq = MPID_nem_ib_sendq_head(vc_ib->sendq);
        if (sreq) {
            int msg_type = MPIDI_Request_get_msg_type(sreq);
            if (msg_type == MPIDI_REQUEST_EAGER_MSG &&  /* guard for the following pointer dereference */
                ((MPIDI_CH3_Pkt_t
                  *) sreq->dev.iov[0].MPID_IOV_BUF)->type ==
                MPIDI_NEM_PKT_NETMOD
                &&
                ((MPID_nem_pkt_netmod_t *) sreq->dev.iov[0].MPID_IOV_BUF)->subtype ==
                MPIDI_NEM_IB_PKT_REPLY_SEQ_NUM) {
                goto skip;
            }
        }
        //printf("recv_buf_released,sending reply_seq_num,diff=%d,rate=%d,id=%d\n", MPID_nem_ib_diff16(vc_ib->ibcom->rsr_seq_num_tail, vc_ib->ibcom->rsr_seq_num_tail_last_sent), notify_rate + (notify_rate>>1), vc_ib->ibcom->sseq_num);
        MPID_nem_ib_send_reply_seq_num(vc);
      skip:;
    }
#endif

  fn_exit:
    MPIDI_FUNC_EXIT(MPID_STATE_MPID_NEM_IB_RECV_BUF_RELEASED);
    return mpi_errno;
  fn_fail:
    goto fn_exit;
}

#if 0
/* packet handler for wrapper packet of MPIDI_NEM_PKT_LMT_DONE */
/* see pkt_DONE_handler (in src/mpid/ch3/channels/nemesis/src/mpid_nem_lmt.c) */
#undef FUNCNAME
#define FUNCNAME MPID_nem_ib_PktHandler_lmt_done
#undef FCNAME
#define FCNAME MPIDI_QUOTE(FUNCNAME)
int MPID_nem_ib_PktHandler_lmt_done(MPIDI_VC_t * vc,
                                    MPIDI_CH3_Pkt_t * pkt,
                                    MPIDI_msg_sz_t * buflen, MPID_Request ** rreqp)
{
    int mpi_errno = MPI_SUCCESS;
    int ibcom_errno;
    MPID_nem_ib_pkt_lmt_done_t *const done_pkt = (MPID_nem_ib_pkt_lmt_done_t *) pkt;
    MPID_Request *req;
    MPIDI_STATE_DECL(MPID_STATE_MPID_NEM_IB_PKTHANDLER_LMT_DONE);
    MPIDI_FUNC_ENTER(MPID_STATE_MPID_NEM_IB_PKTHANDLER_LMT_DONE);
    /* Check the assumption on sizeof(MPIDI_CH3_Pkt_t).
     * It is utilized in pkt_DONE_handler (in src/mpid/ch3/channels/nemesis/src/mpid_nem_lmt.c)
     * that must be larger than sizeof(MPID_nem_ib_pkt_lmt_done_t) */
    if (sizeof(MPID_nem_ib_pkt_lmt_done_t) > sizeof(MPIDI_CH3_Pkt_t)) {
        MPIU_ERR_SETFATALANDJUMP(mpi_errno, MPI_ERR_INTERN, "**sizeof(MPIDI_CH3_Pkt_t)");
    }

    /* fall back to the original handler */
    /* we don't need to worry about the difference caused by embedding seq_num
     * because the handler does not use it (e.g. applying sizeof operator to it) */
    MPID_nem_pkt_lmt_done_t *pkt_parent_class = (MPID_nem_pkt_lmt_done_t *) pkt;
    pkt_parent_class->type = MPIDI_NEM_PKT_LMT_DONE;
#if 0
    mpi_errno = MPID_nem_handle_pkt(vc, (char *) pkt_parent_class, *buflen);
#else
    MPIU_ERR_CHKANDJUMP(1, mpi_errno, MPI_ERR_OTHER, "**notimplemented");
    /* you need to modify mpid_nem_lmt.c to make pkt_DONE_handler visible to me */
    //mpi_errno = pkt_DONE_handler(vc, pkt, buflen, rreqp);
#endif
    if (mpi_errno) {
        MPIU_ERR_POP(mpi_errno);
    }

  fn_exit:
    MPIDI_FUNC_EXIT(MPID_STATE_MPID_NEM_IB_PKTHANDLER_LMT_DONE);
    return mpi_errno;
  fn_fail:
    goto fn_exit;
}
#endif

/* packet handler for wrapper packet of MPIDI_CH3_PKT_EAGER_SEND */
/* see MPIDI_CH3_PktHandler_EagerSend (in src/mpid/ch3/src/ch3u_eager.c) */
#undef FUNCNAME
#define FUNCNAME MPID_nem_ib_PktHandler_EagerSend
#undef FCNAME
#define FCNAME MPIDI_QUOTE(FUNCNAME)
int MPID_nem_ib_PktHandler_EagerSend(MPIDI_VC_t * vc,
                                     MPIDI_CH3_Pkt_t * pkt, MPIDI_msg_sz_t * buflen /* out */ ,
                                     MPID_Request ** rreqp /* out */)
{
    MPID_nem_ib_pkt_prefix_t *netmod_pkt = (MPID_nem_ib_pkt_prefix_t *) pkt;
    MPIDI_CH3_Pkt_eager_send_t *ch3_pkt =
        (MPIDI_CH3_Pkt_eager_send_t *) ((uint8_t *) pkt + sizeof(MPID_nem_ib_pkt_prefix_t));
    int mpi_errno = MPI_SUCCESS;
    MPIDI_STATE_DECL(MPID_STATE_MPID_NEM_IB_PKTHANDLER_EAGERSEND);
    MPIDI_FUNC_ENTER(MPID_STATE_MPID_NEM_IB_PKTHANDLER_EAGERSEND);
    dprintf("ib_pkthandler_eagersend,tag=%d\n", ch3_pkt->match.parts.tag);
    /* Check the assumption on sizeof(MPIDI_CH3_Pkt_t).
     * It is utilized to point the payload location in MPIDI_CH3_PktHandler_EagerSend
     * (src/mpid/ch3/src/ch3u_eager.c) that must be larger than sizeof(MPID_nem_ib_pkt_eager_send_t) */
    //if (sizeof(MPID_nem_ib_pkt_eager_send_t) > sizeof(MPIDI_CH3_Pkt_t)) {
    //MPIU_ERR_SETFATALANDJUMP(mpi_errno, MPI_ERR_INTERN, "**sizeof(MPIDI_CH3_Pkt_t)");
    //}
    /* Update occupation status of local SR (send request) queue */
    MPID_nem_ib_vc_area *vc_ib = VC_IB(vc);
    dprintf
        ("MPID_nem_ib_PktHandler_EagerSend,lsr_seq_num_tail=%d,netmod_pkt->seq_num_tail=%d\n",
         vc_ib->ibcom->lsr_seq_num_tail, netmod_pkt->seq_num_tail);
    vc_ib->ibcom->lsr_seq_num_tail = netmod_pkt->seq_num_tail;
    dprintf
        ("MPID_nem_ib_PktHandler_EagerSend,lsr_seq_num_tail updated to %d\n",
         vc_ib->ibcom->lsr_seq_num_tail);
#ifndef MPID_NEM_IB_DISABLE_VAR_OCC_NOTIFY_RATE
    /* change remote notification policy of RDMA-write-to buf */
    dprintf("pkthandler,eagersend,old rstate=%d\n", vc_ib->ibcom->rdmabuf_occupancy_notify_rstate);
    MPID_nem_ib_change_rdmabuf_occupancy_notify_policy_lw(vc_ib, lsr_seq_num_tail);
    dprintf("pkthandler,eagersend,new rstate=%d\n", vc_ib->ibcom->rdmabuf_occupancy_notify_rstate);
#endif
    dprintf
        ("pkthandler,eagersend,sendq_empty=%d,ncom=%d,rdmabuf_occ=%d\n",
         MPID_nem_ib_sendq_empty(vc_ib->sendq), vc_ib->ibcom->ncom,
         MPID_nem_ib_diff16(vc_ib->ibcom->sseq_num, vc_ib->ibcom->lsr_seq_num_tail));
    /* try to send from sendq because at least one RDMA-write-to buffer has been released */
    /* calling drain_scq from progress_send derpives of chance
     * for ib_poll to drain sendq using ncqe
     * however transfers events to
     * (not to reply_seq_num because it's regulated by the rate)
     * fire on ib_poll using nces (e.g. MPI_Put) so we need to perform
     * progress_send for all of VCs using nces in ib_poll. */
    dprintf("pkthandler,eagersend,send_progress\n");
    fflush(stdout);
    MPID_NEM_IB_CHECK_AND_SEND_PROGRESS;
    /* fall back to the original handler */
    /* we don't need to worry about the difference caused by embedding seq_num
     * because size of MPI-header of MPIDI_CH3_PKT_EAGER_SEND equals to sizeof(MPIDI_CH3_Pkt_t)
     * see MPID_nem_ib_iSendContig
     */
    //ch3_pkt->type = MPIDI_CH3_PKT_EAGER_SEND;
#if 0
        mpi_errno = MPID_nem_handle_pkt(vc, (char *) pkt_parent_class, *buflen);
#else
        dprintf("ib_poll.c,before PktHandler_EagerSend,buflen=%ld\n", *buflen);
    MPIDI_msg_sz_t ch3_buflen = *buflen - sizeof(MPID_nem_ib_pkt_prefix_t);
    mpi_errno = MPIDI_CH3_PktHandler_EagerSend(vc, (MPIDI_CH3_Pkt_t *) ch3_pkt, &ch3_buflen, rreqp);
    dprintf("ib_poll.c,after PktHandler_EagerSend,buflen=%ld\n", ch3_buflen);
    *buflen = ch3_buflen + sizeof(MPID_nem_ib_pkt_prefix_t);
    dprintf("ib_poll.c,after addition,buflen=%ld\n", *buflen);
#endif
    if (mpi_errno) {
        MPIU_ERR_POP(mpi_errno);
    }

  fn_exit:
    MPIDI_FUNC_EXIT(MPID_STATE_MPID_NEM_IB_PKTHANDLER_EAGERSEND);
    return mpi_errno;
  fn_fail:
    goto fn_exit;
}

#if 0   /* modification of mpid_nem_lmt.c is required */

/* Temporary fix because it's static */
int pkt_RTS_handler(MPIDI_VC_t * vc, MPIDI_CH3_Pkt_t * pkt,
                    MPIDI_msg_sz_t * buflen, MPID_Request ** rreqp);
/* packet handler for wrapper packet of MPIDI_NEM_PKT_LMT_RTS */
/* see pkt_RTS_handler (in src/mpid/ch3/channels/nemesis/src/mpid_nem_lmt.c) */
#undef FUNCNAME
#define FUNCNAME MPID_nem_ib_pkt_RTS_handler
#undef FCNAME
#define FCNAME MPIDI_QUOTE(FUNCNAME)
int MPID_nem_ib_pkt_RTS_handler(MPIDI_VC_t * vc,
                                MPIDI_CH3_Pkt_t * pkt, MPIDI_msg_sz_t * buflen /* out */ ,
                                MPID_Request ** rreqp /* out */)
{
    MPID_nem_ib_pkt_prefix_t *netmod_pkt = (MPID_nem_ib_pkt_prefix_t *) pkt;
    MPIDI_CH3_Pkt_t *ch3_pkt =
        (MPIDI_CH3_Pkt_t *) ((uint8_t *) pkt + sizeof(MPID_nem_ib_pkt_prefix_t));
    int mpi_errno = MPI_SUCCESS;
    int ibcom_errno;
    MPIDI_STATE_DECL(MPID_STATE_MPID_NEM_IB_PKT_RTS_HANDLER);
    MPIDI_FUNC_ENTER(MPID_STATE_MPID_NEM_IB_PKT_RTS_HANDLER);
    /* Update occupation status of local SR (send request) queue */
    MPID_nem_ib_vc_area *vc_ib = VC_IB(vc);
    dprintf
        ("MPID_nem_ib_pkt_RTS_handler,lsr_seq_num_tail=%d,netmod_pkt->seq_num_tail=%d\n",
         vc_ib->ibcom->lsr_seq_num_tail, netmod_pkt->seq_num_tail);
    vc_ib->ibcom->lsr_seq_num_tail = netmod_pkt->seq_num_tail;
    dprintf
        ("MPID_nem_ib_pkt_RTS_handler,lsr_seq_num_tail updated to %d\n",
         vc_ib->ibcom->lsr_seq_num_tail);
#ifndef MPID_NEM_IB_DISABLE_VAR_OCC_NOTIFY_RATE
    /* change remote notification policy of RDMA-write-to buf */
    dprintf("pkthandler,rts,old rstate=%d\n", vc_ib->ibcom->rdmabuf_occupancy_notify_rstate);
    MPID_nem_ib_change_rdmabuf_occupancy_notify_policy_lw(vc_ib, &vc_ib->ibcom->lsr_seq_num_tail);
    dprintf("pkthandler,rts,new rstate=%d\n", vc_ib->ibcom->rdmabuf_occupancy_notify_rstate);
#endif
    dprintf("pkthandler,rts,sendq_empty=%d,ncom=%d,rdmabuf_occ=%d\n",
            MPID_nem_ib_sendq_empty(vc_ib->sendq), vc_ib->ibcom->ncom,
            MPID_nem_ib_diff16(vc_ib->ibcom->sseq_num, vc_ib->ibcom->lsr_seq_num_tail));
    /* try to send from sendq because at least one RDMA-write-to buffer has been released */
    dprintf("pkthandler,eagersend,send_progress\n");
    fflush(stdout);
    MPID_NEM_IB_CHECK_AND_SEND_PROGRESS;
    /* fall back to the original handler */
    /* we don't need to worry about the difference caused by embedding seq_num
     * because size of MPI-header of MPIDI_CH3_PKT_EAGER_SEND equals to sizeof(MPIDI_CH3_Pkt_t)
     * see MPID_nem_ib_iSendContig
     */
    MPIDI_msg_sz_t ch3_buflen = *buflen - sizeof(MPID_nem_ib_pkt_prefix_t);
    mpi_errno = pkt_RTS_handler(vc, ch3_pkt, &ch3_buflen, rreqp);
    *buflen = ch3_buflen + sizeof(MPID_nem_ib_pkt_prefix_t);
    if (mpi_errno) {
        MPIU_ERR_POP(mpi_errno);
    }

  fn_exit:
    MPIDI_FUNC_EXIT(MPID_STATE_MPID_NEM_IB_PKT_RTS_HANDLER);
    return mpi_errno;
  fn_fail:
    goto fn_exit;
}
#endif

#if 1
/* packet handler for wrapper packet of MPIDI_CH3_PKT_PUT */
/* see MPIDI_CH3_PktHandler_EagerSend (in src/mpid/ch3/src/ch3u_rma_sync.c) */
#undef FUNCNAME
#define FUNCNAME MPID_nem_ib_PktHandler_Put
#undef FCNAME
#define FCNAME MPIDI_QUOTE(FUNCNAME)
int MPID_nem_ib_PktHandler_Put(MPIDI_VC_t * vc, MPIDI_CH3_Pkt_t * pkt,
                               MPIDI_msg_sz_t * buflen /* out */ ,
                               MPID_Request ** rreqp /* out */)
{
    MPID_nem_ib_vc_area *vc_ib = VC_IB(vc);
    int mpi_errno = MPI_SUCCESS;
    MPID_Request *req = NULL;
    MPIDI_CH3_Pkt_put_t *put_pkt =
        (MPIDI_CH3_Pkt_put_t *) ((uint8_t *) pkt + sizeof(MPIDI_CH3_Pkt_t));
    MPID_nem_ib_rma_lmt_cookie_t *s_cookie_buf =
        (MPID_nem_ib_rma_lmt_cookie_t *) ((uint8_t *) pkt + sizeof(MPIDI_CH3_Pkt_t) +
                                          sizeof(MPIDI_CH3_Pkt_t));

    /* ref. MPIDI_CH3_PktHandler_Put (= pktArray[MPIDI_CH3_PKT_PUT]) */
    MPI_Aint type_size;

    MPID_Win *win_ptr;

    MPIDI_STATE_DECL(MPID_STATE_MPID_NEM_IB_PKTHANDLER_PUT);
    MPIDI_FUNC_ENTER(MPID_STATE_MPID_NEM_IB_PKTHANDLER_PUT);

    MPIU_Assert(put_pkt->target_win_handle != MPI_WIN_NULL);
    MPID_Win_get_ptr(put_pkt->target_win_handle, win_ptr);

    req = MPID_Request_create();
    MPIU_Object_set_ref(req, 1);        /* decrement only in drain_scq ? */
    int incomplete;
    MPIDI_CH3U_Request_increment_cc(req, &incomplete);  // decrement in drain_scq

    req->dev.user_buf = put_pkt->addr;
    req->dev.user_count = put_pkt->count;
    req->dev.target_win_handle = put_pkt->target_win_handle;
    req->dev.source_win_handle = put_pkt->source_win_handle;
    req->dev.flags = put_pkt->flags;
    req->dev.OnFinal = MPIDI_CH3_ReqHandler_PutRecvComplete;

    if (MPIR_DATATYPE_IS_PREDEFINED(put_pkt->datatype)) {
        MPIDI_Request_set_type(req, MPIDI_REQUEST_TYPE_PUT_RESP);
        req->dev.datatype = put_pkt->datatype;

        MPID_Datatype_get_size_macro(put_pkt->datatype, type_size);
        req->dev.recv_data_sz = type_size * put_pkt->count;
        if (put_pkt->immed_len > 0) {
            /* See if we can receive some data from packet header. */
            MPIU_Memcpy(req->dev.user_buf, put_pkt->data, put_pkt->immed_len);
            req->dev.user_buf = (void*)((char*)req->dev.user_buf + put_pkt->immed_len);
            req->dev.recv_data_sz -= put_pkt->immed_len;
        }
    }
    else {
        /* derived datatype */
        MPIDI_Request_set_type(req, MPIDI_REQUEST_TYPE_PUT_RESP_DERIVED_DT);
        req->dev.datatype = MPI_DATATYPE_NULL;

        req->dev.dtype_info = (MPIDI_RMA_dtype_info *) MPIU_Malloc(sizeof(MPIDI_RMA_dtype_info));
        req->dev.dataloop = MPIU_Malloc(put_pkt->dataloop_size);

        /* We have to store the value of 'put_pkt->dataloop_size' which we use in drain_scq.
         * Temporarily, put it in req->dev.dtype_info.
         */
        *(int *) req->dev.dtype_info = put_pkt->dataloop_size;
    }

    /* ref. pkt_RTS_handler (= pktArray[MPIDI_NEM_PKT_LMT_RTS]) */

    void *write_to_buf;

    req->ch.lmt_data_sz = s_cookie_buf->len;
    req->ch.lmt_req_id = s_cookie_buf->sender_req_id;

    REQ_FIELD(req, lmt_pack_buf) = MPIU_Malloc((size_t) req->ch.lmt_data_sz);
    write_to_buf = REQ_FIELD(req, lmt_pack_buf);

    /* stash vc for ib_poll */
    req->ch.vc = vc;

    REQ_FIELD(req, lmt_tail) = s_cookie_buf->tail;

    /* try to issue RDMA-read command */
    int slack = 1;              /* slack for control packet bringing sequence number */
    if (MPID_nem_ib_sendq_empty(vc_ib->sendq) &&
        vc_ib->ibcom->ncom < MPID_NEM_IB_COM_MAX_SQ_CAPACITY - slack &&
        MPID_nem_ib_ncqe < MPID_NEM_IB_COM_MAX_CQ_CAPACITY - slack) {
        mpi_errno =
            MPID_nem_ib_lmt_start_recv_core(req, s_cookie_buf->addr, s_cookie_buf->rkey,
                                            s_cookie_buf->len, write_to_buf,
                                            s_cookie_buf->max_msg_sz, 1);
        if (mpi_errno) {
            MPIU_ERR_POP(mpi_errno);
        }
    }
    else {
        /* enqueue command into send_queue */
        dprintf("lmt_start_recv, enqueuing,sendq_empty=%d,ncom=%d,ncqe=%d\n",
                MPID_nem_ib_sendq_empty(vc_ib->sendq),
                vc_ib->ibcom->ncom < MPID_NEM_IB_COM_MAX_SQ_CAPACITY,
                MPID_nem_ib_ncqe < MPID_NEM_IB_COM_MAX_CQ_CAPACITY);

        /* make raddr, (sz is in rreq->ch.lmt_data_sz), rkey, (user_buf is in req->dev.user_buf) survive enqueue, free cookie, dequeue */
        REQ_FIELD(req, lmt_raddr) = s_cookie_buf->addr;
        REQ_FIELD(req, lmt_rkey) = s_cookie_buf->rkey;
        REQ_FIELD(req, lmt_write_to_buf) = write_to_buf;
        REQ_FIELD(req, lmt_szsend) = s_cookie_buf->len;
        REQ_FIELD(req, max_msg_sz) = s_cookie_buf->max_msg_sz;
        REQ_FIELD(req, last) = 1;       /* not support segmentation */

        /* set for send_progress */
        MPIDI_Request_set_msg_type(req, MPIDI_REQUEST_RNDV_MSG);
        req->kind = MPID_REQUEST_RECV;

        MPID_nem_ib_sendq_enqueue(&vc_ib->sendq, req);
    }

    /* prefix + header + data */
    *buflen =
        sizeof(MPIDI_CH3_Pkt_t) + sizeof(MPIDI_CH3_Pkt_t) + sizeof(MPID_nem_ib_rma_lmt_cookie_t);
    *rreqp = NULL;

  fn_exit:
    MPIDI_FUNC_EXIT(MPID_STATE_MPID_NEM_IB_PKTHANDLER_PUT);
    return mpi_errno;
  fn_fail:
    goto fn_exit;
}
#endif

/* packet handler for wrapper packet of MPIDI_CH3_PKT_ACCUMULATE */
/* see MPIDI_CH3_PktHandler_Accumulate (in src/mpid/ch3/src/ch3u_rma_sync.c) */
#undef FUNCNAME
#define FUNCNAME MPID_nem_ib_PktHandler_Accumulate
#undef FCNAME
#define FCNAME MPIDI_QUOTE(FUNCNAME)
int MPID_nem_ib_PktHandler_Accumulate(MPIDI_VC_t * vc,
                                      MPIDI_CH3_Pkt_t * pkt, MPIDI_msg_sz_t * buflen /* out */ ,
                                      MPID_Request ** rreqp /* out */)
{
    MPID_nem_ib_vc_area *vc_ib = VC_IB(vc);
    int mpi_errno = MPI_SUCCESS;
    MPID_Request *req = NULL;
    MPIDI_CH3_Pkt_accum_t *accum_pkt =
        (MPIDI_CH3_Pkt_accum_t *) ((uint8_t *) pkt + sizeof(MPIDI_CH3_Pkt_t));
    MPID_nem_ib_rma_lmt_cookie_t *s_cookie_buf =
        (MPID_nem_ib_rma_lmt_cookie_t *) ((uint8_t *) pkt + sizeof(MPIDI_CH3_Pkt_t) +
                                          sizeof(MPIDI_CH3_Pkt_t));

    /* ref. MPIDI_CH3_PktHandler_Accumulate */
    MPI_Aint true_lb, true_extent, extent;
    MPI_Aint type_size;
    MPID_Win *win_ptr;

    MPIDI_STATE_DECL(MPID_STATE_MPID_NEM_IB_PKTHANDLER_ACCUMULATE);
    MPIDI_FUNC_ENTER(MPID_STATE_MPID_NEM_IB_PKTHANDLER_ACCUMULATE);

    MPIU_Assert(accum_pkt->target_win_handle != MPI_WIN_NULL);
    MPID_Win_get_ptr(accum_pkt->target_win_handle, win_ptr);

    req = MPID_Request_create();
    MPIU_Object_set_ref(req, 1);

    int incomplete;
    MPIDI_CH3U_Request_increment_cc(req, &incomplete);  // decrement in drain_scq

    req->dev.user_count = accum_pkt->count;
    req->dev.op = accum_pkt->op;
    req->dev.real_user_buf = accum_pkt->addr;
    req->dev.target_win_handle = accum_pkt->target_win_handle;
    req->dev.source_win_handle = accum_pkt->source_win_handle;
    req->dev.flags = accum_pkt->flags;

    req->dev.resp_request_handle = MPI_REQUEST_NULL;
    req->dev.OnFinal = MPIDI_CH3_ReqHandler_AccumRecvComplete;

    if (MPIR_DATATYPE_IS_PREDEFINED(accum_pkt->datatype)) {
        MPIDI_Request_set_type(req, MPIDI_REQUEST_TYPE_ACCUM_RESP);
        req->dev.datatype = accum_pkt->datatype;

        MPIR_Type_get_true_extent_impl(accum_pkt->datatype, &true_lb, &true_extent);
        MPID_Datatype_get_extent_macro(accum_pkt->datatype, extent);

        /* Predefined types should always have zero lb */
        MPIU_Assert(true_lb == 0);

        req->dev.user_buf = MPIU_Malloc(accum_pkt->count * (MPIR_MAX(extent, true_extent)));
        req->dev.final_user_buf = req->dev.user_buf;

        MPID_Datatype_get_size_macro(accum_pkt->datatype, type_size);
        req->dev.recv_data_sz = type_size * accum_pkt->count;

        if (accum_pkt->immed_len > 0) {
            /* See if we can receive some data from packet header. */
            MPIU_Memcpy(req->dev.user_buf, accum_pkt->data, accum_pkt->immed_len);
            req->dev.user_buf = (void*)((char*)req->dev.user_buf + accum_pkt->immed_len);
            req->dev.recv_data_sz -= accum_pkt->immed_len;
        }

    }
    else {
        MPIDI_Request_set_type(req, MPIDI_REQUEST_TYPE_ACCUM_RESP_DERIVED_DT);
        req->dev.OnDataAvail = MPIDI_CH3_ReqHandler_AccumDerivedDTRecvComplete;
        req->dev.datatype = MPI_DATATYPE_NULL;

        req->dev.dtype_info = (MPIDI_RMA_dtype_info *) MPIU_Malloc(sizeof(MPIDI_RMA_dtype_info));
        req->dev.dataloop = MPIU_Malloc(accum_pkt->dataloop_size);

        /* We have to store the value of 'put_pkt->dataloop_size' which we use in drain_scq.
         * Temporarily, put it in req->dev.dtype_info.
         */
        *(int *) req->dev.dtype_info = accum_pkt->dataloop_size;
    }

    /* ref. pkt_RTS_handler (= pktArray[MPIDI_NEM_PKT_LMT_RTS]) */
    void *write_to_buf;

    req->ch.lmt_data_sz = s_cookie_buf->len;
    req->ch.lmt_req_id = s_cookie_buf->sender_req_id;

    REQ_FIELD(req, lmt_pack_buf) = MPIU_Malloc((size_t) req->ch.lmt_data_sz);
    write_to_buf = REQ_FIELD(req, lmt_pack_buf);

    /* stash vc for ib_poll */
    req->ch.vc = vc;

    REQ_FIELD(req, lmt_tail) = s_cookie_buf->tail;

    /* try to issue RDMA-read command */
    int slack = 1;              /* slack for control packet bringing sequence number */
    if (MPID_nem_ib_sendq_empty(vc_ib->sendq) &&
        vc_ib->ibcom->ncom < MPID_NEM_IB_COM_MAX_SQ_CAPACITY - slack &&
        MPID_nem_ib_ncqe < MPID_NEM_IB_COM_MAX_CQ_CAPACITY - slack) {
        mpi_errno =
            MPID_nem_ib_lmt_start_recv_core(req, s_cookie_buf->addr, s_cookie_buf->rkey,
                                            s_cookie_buf->len, write_to_buf,
                                            s_cookie_buf->max_msg_sz, 1);
        if (mpi_errno) {
            MPIU_ERR_POP(mpi_errno);
        }
    }
    else {
        /* enqueue command into send_queue */
        dprintf("lmt_start_recv, enqueuing,sendq_empty=%d,ncom=%d,ncqe=%d\n",
                MPID_nem_ib_sendq_empty(vc_ib->sendq),
                vc_ib->ibcom->ncom < MPID_NEM_IB_COM_MAX_SQ_CAPACITY,
                MPID_nem_ib_ncqe < MPID_NEM_IB_COM_MAX_CQ_CAPACITY);

        /* make raddr, (sz is in rreq->ch.lmt_data_sz), rkey, (user_buf is in req->dev.user_buf) survive enqueue, free cookie, dequeue */
        REQ_FIELD(req, lmt_raddr) = s_cookie_buf->addr;
        REQ_FIELD(req, lmt_rkey) = s_cookie_buf->rkey;
        REQ_FIELD(req, lmt_write_to_buf) = write_to_buf;
        REQ_FIELD(req, lmt_szsend) = s_cookie_buf->len;
        REQ_FIELD(req, max_msg_sz) = s_cookie_buf->max_msg_sz;
        REQ_FIELD(req, last) = 1;       /* not support segmentation */

        /* set for send_progress */
        MPIDI_Request_set_msg_type(req, MPIDI_REQUEST_RNDV_MSG);
        req->kind = MPID_REQUEST_RECV;

        MPID_nem_ib_sendq_enqueue(&vc_ib->sendq, req);
    }

    /* prefix + header + data */
    *buflen =
        sizeof(MPIDI_CH3_Pkt_t) + sizeof(MPIDI_CH3_Pkt_t) + sizeof(MPID_nem_ib_rma_lmt_cookie_t);
    *rreqp = NULL;

  fn_exit:
    MPIDI_FUNC_EXIT(MPID_STATE_MPID_NEM_IB_PKTHANDLER_ACCUMULATE);
    return mpi_errno;
  fn_fail:
    goto fn_exit;
}

/* packet handler for wrapper packet of MPIDI_CH3_PKT_GET */
/* see MPIDI_CH3_PktHandler_Get (in src/mpid/ch3/src/ch3u_rma_sync.c) */
#undef FUNCNAME
#define FUNCNAME MPID_nem_ib_PktHandler_Get
#undef FCNAME
#define FCNAME MPIDI_QUOTE(FUNCNAME)
int MPID_nem_ib_PktHandler_Get(MPIDI_VC_t * vc, MPIDI_CH3_Pkt_t * pkt,
                               MPIDI_msg_sz_t * buflen /* out */ ,
                               MPID_Request ** rreqp /* out */)
{
    MPID_nem_ib_pkt_prefix_t *netmod_pkt = (MPID_nem_ib_pkt_prefix_t *) pkt;
    MPIDI_CH3_Pkt_get_t *ch3_pkt =
        (MPIDI_CH3_Pkt_get_t *) ((uint8_t *) pkt + sizeof(MPID_nem_ib_pkt_prefix_t));
    int mpi_errno = MPI_SUCCESS;
    MPIDI_STATE_DECL(MPID_STATE_MPID_NEM_IB_PKTHANDLER_GET);
    MPIDI_FUNC_ENTER(MPID_STATE_MPID_NEM_IB_PKTHANDLER_GET);
    /* Update occupation status of local SR (send request) queue */
    MPID_nem_ib_vc_area *vc_ib = VC_IB(vc);
    dprintf
        ("MPID_nem_ib_Pkthandler_Get,lsr_seq_num_tail=%d,get_pkt->seq_num_tail=%d\n",
         vc_ib->ibcom->lsr_seq_num_tail, netmod_pkt->seq_num_tail);
    vc_ib->ibcom->lsr_seq_num_tail = netmod_pkt->seq_num_tail;
    dprintf("MPID_nem_ib_Pkthandler_Get,lsr_seq_num_tail updated to %d\n",
            vc_ib->ibcom->lsr_seq_num_tail);
#ifndef MPID_NEM_IB_DISABLE_VAR_OCC_NOTIFY_RATE
    /* change remote notification policy of RDMA-write-to buf */
    dprintf("pkthandler,put,old rstate=%d\n", vc_ib->ibcom->rdmabuf_occupancy_notify_rstate);
    MPID_nem_ib_change_rdmabuf_occupancy_notify_policy_lw(vc_ib, &vc_ib->ibcom->lsr_seq_num_tail);
    dprintf("pkthandler,put,new rstate=%d\n", vc_ib->ibcom->rdmabuf_occupancy_notify_rstate);
#endif
    dprintf("pkthandler,put,sendq_empty=%d,ncom=%d,rdmabuf_occ=%d\n",
            MPID_nem_ib_sendq_empty(vc_ib->sendq), vc_ib->ibcom->ncom,
            MPID_nem_ib_diff16(vc_ib->ibcom->sseq_num, vc_ib->ibcom->lsr_seq_num_tail));
    /* try to send from sendq because at least one RDMA-write-to buffer has been released */
    dprintf("pkthandler,get,send_progress\n");
    fflush(stdout);
    MPID_NEM_IB_SEND_PROGRESS_POLLINGSET
        /* fall back to the original handler */
        /* we don't need to worry about the difference caused by embedding seq_num
         * because size of MPI-header of MPIDI_CH3_PKT_PUT equals to sizeof(MPIDI_CH3_Pkt_t)
         * see MPID_nem_ib_iSendContig
         */
        MPIDI_msg_sz_t ch3_buflen = *buflen - sizeof(MPID_nem_ib_pkt_prefix_t);
    mpi_errno = MPIDI_CH3_PktHandler_Get(vc, (MPIDI_CH3_Pkt_t *) ch3_pkt, &ch3_buflen, rreqp);
    *buflen = ch3_buflen + sizeof(MPID_nem_ib_pkt_prefix_t);
    if (mpi_errno) {
        MPIU_ERR_POP(mpi_errno);
    }

  fn_exit:
    MPIDI_FUNC_EXIT(MPID_STATE_MPID_NEM_IB_PKTHANDLER_GET);
    return mpi_errno;
  fn_fail:
    goto fn_exit;
}

/* packet handler for wrapper packet of MPIDI_CH3_PKT_GET_RESP */
/* see MPIDI_CH3_PktHandler_GetResp (in src/mpid/ch3/src/ch3u_rma_sync.c) */
#undef FUNCNAME
#define FUNCNAME MPID_nem_ib_PktHandler_GetResp
#undef FCNAME
#define FCNAME MPIDI_QUOTE(FUNCNAME)
int MPID_nem_ib_PktHandler_GetResp(MPIDI_VC_t * vc,
                                   MPIDI_CH3_Pkt_t * pkt, MPIDI_msg_sz_t * buflen /* out */ ,
                                   MPID_Request ** rreqp /* out */)
{
    MPID_nem_ib_vc_area *vc_ib = VC_IB(vc);
    int mpi_errno = MPI_SUCCESS;
    MPID_Request *req = NULL;
    MPIDI_CH3_Pkt_get_resp_t *get_resp_pkt =
        (MPIDI_CH3_Pkt_get_resp_t *) ((uint8_t *) pkt + sizeof(MPIDI_CH3_Pkt_t));
    MPID_nem_ib_rma_lmt_cookie_t *s_cookie_buf =
        (MPID_nem_ib_rma_lmt_cookie_t *) ((uint8_t *) pkt + sizeof(MPIDI_CH3_Pkt_t) +
                                          sizeof(MPIDI_CH3_Pkt_t));

    MPIDI_STATE_DECL(MPID_STATE_MPID_NEM_IB_PKTHANDLER_GETRESP);
    MPIDI_FUNC_ENTER(MPID_STATE_MPID_NEM_IB_PKTHANDLER_GETRESP);

    MPID_Request_get_ptr(get_resp_pkt->request_handle, req);

    MPID_Win *win_ptr;
    int target_rank = get_resp_pkt->target_rank;

    MPID_Win_get_ptr(get_resp_pkt->source_win_handle, win_ptr);

    /* decrement ack_counter on target */
    if (get_resp_pkt->flags & MPIDI_CH3_PKT_FLAG_RMA_LOCK_GRANTED) {
        mpi_errno = set_lock_sync_counter(win_ptr, target_rank);
        if (mpi_errno) MPIU_ERR_POP(mpi_errno);
    }
    if (get_resp_pkt->flags & MPIDI_CH3_PKT_FLAG_RMA_FLUSH_ACK) {
        mpi_errno = MPIDI_CH3I_RMA_Handle_flush_ack(win_ptr, target_rank);
        if (mpi_errno) MPIU_ERR_POP(mpi_errno);
    }
    if (get_resp_pkt->flags & MPIDI_CH3_PKT_FLAG_RMA_UNLOCK_ACK) {
        mpi_errno = MPIDI_CH3I_RMA_Handle_flush_ack(win_ptr, target_rank);
        if (mpi_errno) MPIU_ERR_POP(mpi_errno);
    }

    void *write_to_buf;

    req->ch.lmt_data_sz = s_cookie_buf->len;
    req->ch.lmt_req_id = s_cookie_buf->sender_req_id;

    REQ_FIELD(req, lmt_pack_buf) = MPIU_Malloc((size_t) req->ch.lmt_data_sz);
    write_to_buf = REQ_FIELD(req, lmt_pack_buf);

    /* This is magic number to pick up request in drain_scq */
    MPIDI_Request_set_type(req, 13);    // currently Request-type is defined from 1 to 12.

    /* stash vc for ib_poll */
    req->ch.vc = vc;

    REQ_FIELD(req, lmt_tail) = s_cookie_buf->tail;

    /* try to issue RDMA-read command */
    int slack = 1;              /* slack for control packet bringing sequence number */
    if (MPID_nem_ib_sendq_empty(vc_ib->sendq) &&
        vc_ib->ibcom->ncom < MPID_NEM_IB_COM_MAX_SQ_CAPACITY - slack &&
        MPID_nem_ib_ncqe < MPID_NEM_IB_COM_MAX_CQ_CAPACITY - slack) {
        mpi_errno =
            MPID_nem_ib_lmt_start_recv_core(req, s_cookie_buf->addr, s_cookie_buf->rkey,
                                            s_cookie_buf->len, write_to_buf,
                                            s_cookie_buf->max_msg_sz, 1);
        if (mpi_errno) {
            MPIU_ERR_POP(mpi_errno);
        }
    }
    else {
        /* enqueue command into send_queue */
        dprintf("lmt_start_recv, enqueuing,sendq_empty=%d,ncom=%d,ncqe=%d\n",
                MPID_nem_ib_sendq_empty(vc_ib->sendq),
                vc_ib->ibcom->ncom < MPID_NEM_IB_COM_MAX_SQ_CAPACITY,
                MPID_nem_ib_ncqe < MPID_NEM_IB_COM_MAX_CQ_CAPACITY);

        /* make raddr, (sz is in rreq->ch.lmt_data_sz), rkey, (user_buf is in req->dev.user_buf) survive enqueue, free cookie, dequeue */
        REQ_FIELD(req, lmt_raddr) = s_cookie_buf->addr;
        REQ_FIELD(req, lmt_rkey) = s_cookie_buf->rkey;
        REQ_FIELD(req, lmt_write_to_buf) = write_to_buf;
        REQ_FIELD(req, lmt_szsend) = s_cookie_buf->len;
        REQ_FIELD(req, max_msg_sz) = s_cookie_buf->max_msg_sz;
        REQ_FIELD(req, last) = 1;       /* not support segmentation */

        MPID_nem_ib_sendq_enqueue(&vc_ib->sendq, req);
    }

    /* prefix + header + data */
    *buflen =
        sizeof(MPIDI_CH3_Pkt_t) + sizeof(MPIDI_CH3_Pkt_t) + sizeof(MPID_nem_ib_rma_lmt_cookie_t);
    *rreqp = NULL;

  fn_exit:
    MPIDI_FUNC_EXIT(MPID_STATE_MPID_NEM_IB_PKTHANDLER_GETRESP);
    return mpi_errno;
  fn_fail:
    goto fn_exit;
}

/* MPI_Isend set req-type to MPIDI_REQUEST_TYPE_RECV */
#undef FUNCNAME
#define FUNCNAME MPID_nem_ib_pkt_GET_DONE_handler
#undef FCNAME
#define FCNAME MPIDI_QUOTE(FUNCNAME)
int MPID_nem_ib_pkt_GET_DONE_handler(MPIDI_VC_t * vc,
                                     MPIDI_CH3_Pkt_t * pkt,
                                     MPIDI_msg_sz_t * buflen, MPID_Request ** rreqp)
{
    int mpi_errno = MPI_SUCCESS;
    MPID_nem_ib_pkt_lmt_get_done_t *const done_pkt = (MPID_nem_ib_pkt_lmt_get_done_t *) pkt;
    MPID_Request *req;
    MPID_nem_ib_vc_area *vc_ib = VC_IB(vc);
    MPIDI_STATE_DECL(MPID_STATE_MPID_NEM_IB_PKT_GET_DONE_HANDLER);
    MPIDI_FUNC_ENTER(MPID_STATE_MPID_NEM_IB_PKT_GET_DONE_HANDLER);
    dprintf("get_done_handler,enter\n");
    *buflen = sizeof(MPIDI_CH3_Pkt_t);
    MPID_Request_get_ptr(done_pkt->req_id, req);
    MPIU_THREAD_CS_ENTER(LMT,);
    switch (MPIDI_Request_get_type(req)) {
        /* MPIDI_Request_set_type is not performed when
         * MPID_Isend --> FDU_or_AEP --> recv_posted --> ib_poll --> PUTCTS packet-handler */
    case MPIDI_REQUEST_TYPE_RECV:
        MPIU_ERR_INTERNALANDJUMP(mpi_errno, "unexpected request type");
        break;
    case MPIDI_REQUEST_TYPE_SEND:
    case MPIDI_REQUEST_TYPE_RSEND:
    case MPIDI_REQUEST_TYPE_SSEND:
    case MPIDI_REQUEST_TYPE_BSEND:
#if 0   /* obsolete, it's in netmod header now */
        /* extract embeded RDMA-write-to buffer occupancy information */
        dprintf
            ("get_done_handler,old lsr_seq_num_tail=%d,done_pkt->seq_num_tail=%d\n",
             vc_ib->ibcom->lsr_seq_num_tail, done_pkt->seq_num_tail);
        vc_ib->ibcom->lsr_seq_num_tail = done_pkt->seq_num_tail;
        //dprintf("lmt_start_recv,new lsr_seq_num=%d\n", vc_ib->ibcom->lsr_seq_num_tail);
#ifndef MPID_NEM_IB_DISABLE_VAR_OCC_NOTIFY_RATE
        /* change remote notification policy of RDMA-write-to buf */
        //dprintf("lmt_start_recv,reply_seq_num,old rstate=%d\n", vc_ib->ibcom->rdmabuf_occupancy_notify_rstate);
        MPID_nem_ib_change_rdmabuf_occupancy_notify_policy_lw(vc_ib,
                                                              &vc_ib->ibcom->lsr_seq_num_tail);
        //dprintf("lmt_start_recv,reply_seq_num,new rstate=%d\n", vc_ib->ibcom->rdmabuf_occupancy_notify_rstate);
#endif
        //dprintf("lmt_start_recv,reply_seq_num,sendq_empty=%d,ncom=%d,ncqe=%d,rdmabuf_occ=%d\n", MPID_nem_ib_sendq_empty(vc_ib->sendq), vc_ib->ibcom->ncom, MPID_nem_ib_ncqe, MPID_nem_ib_diff16(vc_ib->ibcom->sseq_num, vc_ib->ibcom->lsr_seq_num_tail));
#endif

        /* decrement reference counter of mr_cache_entry */
        MPID_nem_ib_com_reg_mr_release(REQ_FIELD(req, lmt_mr_cache));

        /* try to send from sendq because at least one RDMA-write-to buffer has been released */
        //dprintf("lmt_start_recv,reply_seq_num,send_progress\n");
        if (!MPID_nem_ib_sendq_empty(vc_ib->sendq)) {
            dprintf("get_done_handler,ncom=%d,ncqe=%d,diff=%d(%d-%d)\n",
                    vc_ib->ibcom->ncom < MPID_NEM_IB_COM_MAX_SQ_CAPACITY,
                    MPID_nem_ib_ncqe < MPID_NEM_IB_COM_MAX_CQ_CAPACITY,
                    MPID_nem_ib_diff16(vc_ib->ibcom->sseq_num,
                                       vc_ib->ibcom->lsr_seq_num_tail) <
                    vc_ib->ibcom->local_ringbuf_nslot, vc_ib->ibcom->sseq_num,
                    vc_ib->ibcom->lsr_seq_num_tail);
        }
        dprintf("get_done_handler,send_progress\n");
        fflush(stdout);

        if (REQ_FIELD(req, seg_seq_num) == REQ_FIELD(req, seg_num)) {
            /* last packet of segments */
            MPID_NEM_IB_CHECK_AND_SEND_PROGRESS;
            mpi_errno = vc->ch.lmt_done_send(vc, req);
            if (mpi_errno)
                MPIU_ERR_POP(mpi_errno);
        }
        else {
            /* Send RTS for next segment */
            REQ_FIELD(req, seg_seq_num) += 1;   /* next segment number */
            int next_seg_seq_num = REQ_FIELD(req, seg_seq_num);

            uint32_t length;
            if (next_seg_seq_num == REQ_FIELD(req, seg_num))
                length = REQ_FIELD(req, data_sz) - (long) (next_seg_seq_num - 1) * REQ_FIELD(req, max_msg_sz);  //length of last segment
            else
                length = REQ_FIELD(req, max_msg_sz);

            void *addr =
                (void *) ((char *) REQ_FIELD(req, buf.from) +
                          (long) (next_seg_seq_num - 1) * REQ_FIELD(req, max_msg_sz));
            struct MPID_nem_ib_com_reg_mr_cache_entry_t *mr_cache =
                MPID_nem_ib_com_reg_mr_fetch(addr, length, 0, MPID_NEM_IB_COM_REG_MR_GLOBAL);
            MPIU_ERR_CHKANDJUMP(!mr_cache, mpi_errno, MPI_ERR_OTHER,
                                "**MPID_nem_ib_com_reg_mr_fetch");
            struct ibv_mr *mr = mr_cache->mr;
            /* store new cache entry */
            REQ_FIELD(req, lmt_mr_cache) = (void *) mr_cache;

#ifdef HAVE_LIBDCFA
            void *_addr = mr->host_addr;
#else
            void *_addr = addr;
#endif
            MPID_nem_ib_lmt_send_RTS(vc, done_pkt->receiver_req_id, _addr, mr->rkey,
                                     next_seg_seq_num);
        }
        break;
    default:
        MPIU_ERR_INTERNALANDJUMP(mpi_errno, "unexpected request type");
        break;
    }

    *rreqp = NULL;
  fn_exit:
    MPIU_THREAD_CS_EXIT(LMT,);
    MPIDI_FUNC_EXIT(MPID_STATE_MPID_NEM_IB_PKT_GET_DONE_HANDLER);
    return mpi_errno;
  fn_fail:
    goto fn_exit;
}

#undef FUNCNAME
#define FUNCNAME MPID_nem_ib_pkt_RTS_handler
#undef FCNAME
#define FCNAME MPIDI_QUOTE(FUNCNAME)
int MPID_nem_ib_pkt_RTS_handler(MPIDI_VC_t * vc,
                                MPIDI_CH3_Pkt_t * pkt,
                                MPIDI_msg_sz_t * buflen, MPID_Request ** rreqp)
{
    int mpi_errno = MPI_SUCCESS;
    MPID_nem_ib_pkt_lmt_rts_t *const rts_pkt = (MPID_nem_ib_pkt_lmt_rts_t *) pkt;
    MPID_Request *req;
    MPID_nem_ib_vc_area *vc_ib = VC_IB(vc);
    dprintf("ib_pkt_RTS_handler,enter\n");
    *buflen = sizeof(MPIDI_CH3_Pkt_t);
    MPID_Request_get_ptr(rts_pkt->req_id, req);
    MPIU_THREAD_CS_ENTER(LMT,);

    void *write_to_buf =
        (void *) ((char *) REQ_FIELD(req, buf.to) +
                  (long) (rts_pkt->seg_seq_num - 1) * REQ_FIELD(req, max_msg_sz));

    int last;
    long length;

    /* last segment */
    if (rts_pkt->seg_seq_num == REQ_FIELD(req, seg_num)) {
        last = 1;
        length =
            req->ch.lmt_data_sz - (long) (rts_pkt->seg_seq_num - 1) * REQ_FIELD(req, max_msg_sz);
    }
    else {
        last = 0;
        length = REQ_FIELD(req, max_msg_sz);
    }
    /* try to issue RDMA-read command */
    int slack = 1;              /* slack for control packet bringing sequence number */
    if (MPID_nem_ib_sendq_empty(vc_ib->sendq) &&
        vc_ib->ibcom->ncom < MPID_NEM_IB_COM_MAX_SQ_CAPACITY - slack &&
        MPID_nem_ib_ncqe < MPID_NEM_IB_COM_MAX_CQ_CAPACITY - slack) {
        mpi_errno =
            MPID_nem_ib_lmt_start_recv_core(req, rts_pkt->addr, rts_pkt->rkey, length,
                                            write_to_buf, REQ_FIELD(req, max_msg_sz),
                                            last);
        if (mpi_errno) {
            MPIU_ERR_POP(mpi_errno);
        }
    }
    else {
        /* enqueue command into send_queue */
        dprintf("ib_pkt_RTS_handler, enqueuing,sendq_empty=%d,ncom=%d,ncqe=%d\n",
                MPID_nem_ib_sendq_empty(vc_ib->sendq),
                vc_ib->ibcom->ncom < MPID_NEM_IB_COM_MAX_SQ_CAPACITY,
                MPID_nem_ib_ncqe < MPID_NEM_IB_COM_MAX_CQ_CAPACITY);

        /* make raddr, (sz is in rreq->ch.lmt_data_sz), rkey, (user_buf is in req->dev.user_buf) survive enqueue, free cookie, dequeue */
        REQ_FIELD(req, lmt_raddr) = rts_pkt->addr;
        REQ_FIELD(req, lmt_rkey) = rts_pkt->rkey;
        REQ_FIELD(req, lmt_write_to_buf) = write_to_buf;
        REQ_FIELD(req, lmt_szsend) = length;
        REQ_FIELD(req, last) = last;

        MPID_nem_ib_sendq_enqueue(&vc_ib->sendq, req);
    }

    *rreqp = NULL;
  fn_exit:
    MPIU_THREAD_CS_EXIT(LMT,);
    return mpi_errno;
  fn_fail:
    goto fn_exit;
}

#undef FUNCNAME
#define FUNCNAME MPID_nem_ib_PktHandler_req_seq_num
#undef FCNAME
#define FCNAME MPIDI_QUOTE(FUNCNAME)
int MPID_nem_ib_PktHandler_req_seq_num(MPIDI_VC_t * vc,
                                       MPIDI_CH3_Pkt_t * pkt,
                                       MPIDI_msg_sz_t * buflen, MPID_Request ** rreqp)
{
    int mpi_errno = MPI_SUCCESS;
    MPID_nem_ib_pkt_req_seq_num_t *const req_pkt = (MPID_nem_ib_pkt_req_seq_num_t *) pkt;
    MPIDI_STATE_DECL(MPID_STATE_MPID_NEM_IB_PKTHANDLER_REQ_SEQ_NUM);
    MPIDI_FUNC_ENTER(MPID_STATE_MPID_NEM_IB_PKTHANDLER_REQ_SEQ_NUM);
    /* mark as all of the message is read */
    *buflen = sizeof(MPIDI_CH3_Pkt_t);
    /* mark as I don't need continuation read request */
    *rreqp = NULL;
    /* update occupancy info of SR */
    /* request piggy-backs seq_num although it's requesting responder's seq_num */
    MPID_nem_ib_vc_area *vc_ib = VC_IB(vc);
    vc_ib->ibcom->lsr_seq_num_tail = req_pkt->seq_num_tail;
    dprintf
        ("PktHandler_req_seq_num,sendq=%d,ncom=%d,ncqe=%d,diff=%d(%d-%d)\n",
         MPID_nem_ib_sendq_empty(vc_ib->sendq),
         vc_ib->ibcom->ncom < MPID_NEM_IB_COM_MAX_SQ_CAPACITY,
         MPID_nem_ib_ncqe < MPID_NEM_IB_COM_MAX_CQ_CAPACITY,
         MPID_nem_ib_diff16(vc_ib->ibcom->sseq_num,
                            vc_ib->ibcom->lsr_seq_num_tail) <
         vc_ib->ibcom->local_ringbuf_nslot, vc_ib->ibcom->sseq_num, vc_ib->ibcom->lsr_seq_num_tail);
    /* send reply */
    dprintf("PktHandler_req_seq_num,sending reply_seq_num,id=%d\n", vc_ib->ibcom->sseq_num);
    MPID_nem_ib_send_reply_seq_num(vc);
  fn_exit:
    MPIDI_FUNC_EXIT(MPID_STATE_MPID_NEM_IB_PKTHANDLER_REQ_SEQ_NUM);
    return mpi_errno;
  fn_fail:
    goto fn_exit;
}

#undef FUNCNAME
#define FUNCNAME MPID_nem_ib_PktHandler_reply_seq_num
#undef FCNAME
#define FCNAME MPIDI_QUOTE(FUNCNAME)
int MPID_nem_ib_PktHandler_reply_seq_num(MPIDI_VC_t * vc,
                                         MPIDI_CH3_Pkt_t * pkt,
                                         MPIDI_msg_sz_t * buflen, MPID_Request ** rreqp)
{
    int mpi_errno = MPI_SUCCESS;
    MPID_nem_ib_pkt_reply_seq_num_t *const reply_pkt = (MPID_nem_ib_pkt_reply_seq_num_t *) pkt;
    MPIDI_STATE_DECL(MPID_STATE_MPID_NEM_IB_PKTHANDLER_REPLY_SEQ_NUM);
    MPIDI_FUNC_ENTER(MPID_STATE_MPID_NEM_IB_PKTHANDLER_REPLY_SEQ_NUM);
    /* mark as all of the message is consumed */
    *buflen = sizeof(MPIDI_CH3_Pkt_t);
    /* mark as I don't need continuation read request */
    *rreqp = NULL;
    /* update occupancy info of RDMA-write-buf */
    MPID_nem_ib_vc_area *vc_ib = VC_IB(vc);
    dprintf
        ("pkthandler,reply_seq_num,old lsr_seq_num=%d,reply_pkt->seq_num_tail=%d\n",
         vc_ib->ibcom->lsr_seq_num_tail, reply_pkt->seq_num_tail);
    vc_ib->ibcom->lsr_seq_num_tail = reply_pkt->seq_num_tail;
    //dprintf("pkthandler,reply_seq_num,new lsr_seq_num=%d\n", vc_ib->ibcom->lsr_seq_num_tail);
#ifndef MPID_NEM_IB_DISABLE_VAR_OCC_NOTIFY_RATE
    /* change remote notification policy of RDMA-write-to buf */
    //dprintf("pkthandler,reply_seq_num,old rstate=%d\n", vc_ib->ibcom->rdmabuf_occupancy_notify_rstate);
    MPID_nem_ib_change_rdmabuf_occupancy_notify_policy_lw(vc_ib, &(vc_ib->ibcom->lsr_seq_num_tail));
    //dprintf("pkthandler,reply_seq_num,new rstate=%d\n", vc_ib->ibcom->rdmabuf_occupancy_notify_rstate);
#endif
    //dprintf("pkthandler,reply_seq_num,sendq_empty=%d,ncom=%d,ncqe=%d,rdmabuf_occ=%d\n", MPID_nem_ib_sendq_empty(vc_ib->sendq), vc_ib->ibcom->ncom, MPID_nem_ib_ncqe, MPID_nem_ib_diff16(vc_ib->ibcom->sseq_num, vc_ib->ibcom->lsr_seq_num_tail));
    /* try to send from sendq because at least one RDMA-write-to buffer has been released */
    //dprintf("pkthandler,reply_seq_num,send_progress\n");
    dprintf("pkthandler,reply_seq_num,send_progress\n");
    MPID_NEM_IB_CHECK_AND_SEND_PROGRESS;

  fn_exit:
    MPIDI_FUNC_EXIT(MPID_STATE_MPID_NEM_IB_PKTHANDLER_REPLY_SEQ_NUM);
    return mpi_errno;
    //fn_fail:
    goto fn_exit;
}

#undef FUNCNAME
#define FUNCNAME MPID_nem_ib_PktHandler_change_rdmabuf_occupancy_notify_state
#undef FCNAME
#define FCNAME MPIDI_QUOTE(FUNCNAME)
int MPID_nem_ib_PktHandler_change_rdmabuf_occupancy_notify_state
    (MPIDI_VC_t * vc, MPIDI_CH3_Pkt_t * pkt, MPIDI_msg_sz_t * buflen, MPID_Request ** rreqp) {
    int mpi_errno = MPI_SUCCESS;
    int ibcom_errno;
    MPID_nem_ib_pkt_change_rdmabuf_occupancy_notify_state_t *const reply_pkt =
        (MPID_nem_ib_pkt_change_rdmabuf_occupancy_notify_state_t *) pkt;
    MPIDI_STATE_DECL(MPID_STATE_MPID_NEM_IB_PKTHANDLER_CHANGE_RDMABUF_OCCUPANCY_NOTIFY_STATE);
    MPIDI_FUNC_ENTER(MPID_STATE_MPID_NEM_IB_PKTHANDLER_CHANGE_RDMABUF_OCCUPANCY_NOTIFY_STATE);
    /* mark as all of the message is read */
    *buflen = sizeof(MPIDI_CH3_Pkt_t);
    /* mark as I don't need continuation read request */
    *rreqp = NULL;
    /* update occupancy info of SR */
    MPID_nem_ib_vc_area *vc_ib = VC_IB(vc);
    dprintf("pkthandler,change notify state,old lstate=%d,pkt->state=%d\n",
            vc_ib->ibcom->rdmabuf_occupancy_notify_lstate, reply_pkt->state);
    int *rdmabuf_occupancy_notify_lstate;
    ibcom_errno =
        MPID_nem_ib_com_rdmabuf_occupancy_notify_lstate_get(vc_ib->sc->fd,
                                                            &rdmabuf_occupancy_notify_lstate);
    MPIU_ERR_CHKANDJUMP(ibcom_errno, mpi_errno, MPI_ERR_OTHER,
                        "**MPID_nem_ib_com_rdmabuf_occupancy_notify_lstate_get");
    *rdmabuf_occupancy_notify_lstate = reply_pkt->state;
    dprintf("pkthandler,change notify state,new lstate=%d\n",
            vc_ib->ibcom->rdmabuf_occupancy_notify_lstate);
  fn_exit:
    MPIDI_FUNC_EXIT(MPID_STATE_MPID_NEM_IB_PKTHANDLER_CHANGE_RDMABUF_OCCUPANCY_NOTIFY_STATE);
    return mpi_errno;
  fn_fail:
    goto fn_exit;
}

#undef FUNCNAME
#define FUNCNAME MPID_nem_ib_pkt_rma_lmt_getdone
#undef FCNAME
#define FCNAME MPIDI_QUOTE(FUNCNAME)
int MPID_nem_ib_pkt_rma_lmt_getdone(MPIDI_VC_t * vc,
                                    MPIDI_CH3_Pkt_t * pkt,
                                    MPIDI_msg_sz_t * buflen, MPID_Request ** rreqp)
{
    int mpi_errno = MPI_SUCCESS;
    MPID_nem_ib_pkt_lmt_get_done_t *const done_pkt = (MPID_nem_ib_pkt_lmt_get_done_t *) pkt;
    MPID_Request *req;
    int req_type;

    *buflen = sizeof(MPIDI_CH3_Pkt_t);
    MPID_Request_get_ptr(done_pkt->req_id, req);

    MPIU_THREAD_CS_ENTER(LMT,);

    /* decrement reference counter of mr_cache_entry */
    MPID_nem_ib_com_reg_mr_release(REQ_FIELD(req, lmt_mr_cache));

    req_type = MPIDI_Request_get_type(req);
    /* free memory area for cookie */
    if (!req->ch.s_cookie) {
        dprintf("lmt_done_send,enter,req->ch.s_cookie is zero");
    }
    MPIU_Free(req->ch.s_cookie);

    if ((req_type == 0 && !req->comm) || (req_type == MPIDI_REQUEST_TYPE_GET_RESP)) {
        if ((*req->cc_ptr == 1) && req->dev.datatype_ptr && (req->dev.segment_size > 0) &&
            REQ_FIELD(req, lmt_pack_buf)) {
            MPIU_Free(REQ_FIELD(req, lmt_pack_buf));
        }
    }
    MPIDI_CH3U_Request_complete(req);

    *rreqp = NULL;
  fn_exit:
    MPIU_THREAD_CS_EXIT(LMT,);
    return mpi_errno;
  fn_fail:
    goto fn_exit;
}

#ifdef MPID_NEM_IB_ONDEMAND
#undef FUNCNAME
#define FUNCNAME MPID_nem_ib_cm_drain_scq
#undef FCNAME
#define FCNAME MPIDI_QUOTE(FUNCNAME)
int MPID_nem_ib_cm_drain_scq()
{

    int mpi_errno = MPI_SUCCESS;
    int result;
    int i;
    struct ibv_wc cqe[MPID_NEM_IB_COM_MAX_CQ_HEIGHT_DRAIN];
    MPID_nem_ib_cm_cmd_shadow_t *shadow_cm;
    MPID_nem_ib_ringbuf_cmd_shadow_t *shadow_ringbuf;
    MPIDI_STATE_DECL(MPID_STATE_MPID_NEM_IB_CM_DRAIN_SCQ);
    MPIDI_FUNC_ENTER(MPID_STATE_MPID_NEM_IB_CM_DRAIN_SCQ);
    //dprintf("cm_drain_scq,enter\n");
    /* cm_drain_scq is called after poll_eager calls vc_terminate */
    if (!MPID_nem_ib_rc_shared_scq_scratch_pad) {
        dprintf("cm_drain_scq,CQ is null\n");
        goto fn_exit;
    }

    result =
        ibv_poll_cq(MPID_nem_ib_rc_shared_scq_scratch_pad,
                    MPID_NEM_IB_COM_MAX_CQ_HEIGHT_DRAIN, &cqe[0]);
    MPIU_ERR_CHKANDJUMP(result < 0, mpi_errno, MPI_ERR_OTHER, "**netmod,ib,ibv_poll_cq");
    if (result > 0) {
        dprintf("cm_drain_scq,found,result=%d\n", result);
    }
    for (i = 0; i < result; i++) {

        dprintf("cm_drain_scq,wr_id=%p\n", (void *) cqe[i].wr_id);
#ifdef HAVE_LIBDCFA
        if (cqe[i].status != IBV_WC_SUCCESS) {
            dprintf("cm_drain_scq,status=%08x\n", cqe[i].status);
            MPID_nem_ib_segv;
        }
#else
        if (cqe[i].status != IBV_WC_SUCCESS) {
            dprintf("cm_drain_scq,status=%08x,%s\n", cqe[i].status,
                    ibv_wc_status_str(cqe[i].status));
            MPID_nem_ib_segv;
        }
#endif
        MPIU_ERR_CHKANDJUMP(cqe[i].status != IBV_WC_SUCCESS, mpi_errno,
                            MPI_ERR_OTHER, "**MPID_nem_ib_cm_drain_scq");
        MPID_nem_ib_cm_ringbuf_cmd_type_t *type =
            (MPID_nem_ib_cm_ringbuf_cmd_type_t *) cqe[i].wr_id;
        switch (*type) {
        case MPID_NEM_IB_CM_CAS:{
                shadow_cm = (MPID_nem_ib_cm_cmd_shadow_t *) cqe[i].wr_id;
                dprintf("cm_drain_scq,cm_cas,req=%p,responder_rank=%d\n",
                        shadow_cm->req, shadow_cm->req->responder_rank);
                /* Check if CAS have succeeded */
                uint64_t *cas_retval = (uint64_t *) shadow_cm->buf_from;
                if (*cas_retval == MPID_NEM_IB_CM_RELEASED) {
                    /* CAS succeeded, so write command */

                    dprintf("cm_drain_scq,cm_cas,succeeded\n");
                    if (is_conn_established(shadow_cm->req->responder_rank)) {
#if 1
                        /* Explicitly release CAS word because
                         * ConnectX-3 doesn't support safe CAS with PCI device and CPU */
                        MPID_nem_ib_cm_cas_release(MPID_nem_ib_conns
                                                   [shadow_cm->req->responder_rank].vc);

                        shadow_cm->req->ibcom->outstanding_connection_tx -= 1;
                        dprintf("cm_drain_scq,cm_cas,established is true,%d->%d,tx=%d\n",
                                MPID_nem_ib_myrank, shadow_cm->req->responder_rank,
                                shadow_cm->req->ibcom->outstanding_connection_tx);
                        /* Let the guard down to let the following connection request go. */
                        VC_FIELD(MPID_nem_ib_conns[shadow_cm->req->responder_rank].vc,
                                 connection_guard) = 0;
                        /* free memory : req->ref_count is 3, so call MPIU_Free() directly */
                        //MPID_nem_ib_cm_request_release(shadow_cm->req);
                        MPIU_Free(shadow_cm->req);
#else
                        /* Connection is already established.
                         * In this case, responder may already have performed vc_terminate.
                         * However, since initiator has to release responder's CAS word,
                         * initiator sends CM_CAS_RELEASE2. */
                        dprintf("cm_drain_scq,cm_cas,established,%d->%d\n",
                                MPID_nem_ib_myrank, shadow_cm->req->responder_rank);
                        shadow_cm->req->state = MPID_NEM_IB_CM_CAS_RELEASE2;
                        if (MPID_nem_ib_ncqe_scratch_pad < MPID_NEM_IB_COM_MAX_CQ_CAPACITY &&
                            shadow_cm->req->ibcom->ncom_scratch_pad <
                            MPID_NEM_IB_COM_MAX_SQ_CAPACITY) {

                            MPID_nem_ib_cm_cmd_syn_t *cmd =
                                (MPID_nem_ib_cm_cmd_syn_t *) shadow_cm->req->ibcom->
                                icom_mem[MPID_NEM_IB_COM_SCRATCH_PAD_FROM];
                            MPID_NEM_IB_CM_COMPOSE_CAS_RELEASE2(cmd, shadow_cm->req);
                            cmd->initiator_rank = MPID_nem_ib_myrank;
                            MPID_nem_ib_cm_cmd_shadow_t *shadow_syn =
                                (MPID_nem_ib_cm_cmd_shadow_t *)
                                MPIU_Malloc(sizeof(MPID_nem_ib_cm_cmd_shadow_t));
                            shadow_syn->type = shadow_cm->req->state;
                            shadow_syn->req = shadow_cm->req;
                            dprintf("shadow_syn=%p,shadow_syn->req=%p\n", shadow_syn,
                                    shadow_syn->req);
                            dprintf("cm_drain_scq,cm_cas,established,sending cas_release2,%d->%d\n",
                                    MPID_nem_ib_myrank, shadow_cm->req->responder_rank);
                            mpi_errno =
                                MPID_nem_ib_cm_cmd_core(shadow_cm->req->responder_rank, shadow_syn,
                                                        (void *) cmd,
                                                        sizeof(MPID_nem_ib_cm_cmd_syn_t),
                                                        1 /* syn:1 */ , 0);
                            MPIU_ERR_CHKANDJUMP(mpi_errno, mpi_errno, MPI_ERR_OTHER,
                                                "**MPID_nem_ib_cm_send_core");
                        }
                        else {
                            MPID_NEM_IB_CM_COMPOSE_CAS_RELEASE2((MPID_nem_ib_cm_cmd_syn_t *) &
                                                                (shadow_cm->req->cmd),
                                                                shadow_cm->req);
                            ((MPID_nem_ib_cm_cmd_syn_t *) & shadow_cm->req->cmd)->initiator_rank =
                                MPID_nem_ib_myrank;
                            MPID_nem_ib_cm_sendq_enqueue(&MPID_nem_ib_cm_sendq, shadow_cm->req);
                        }
#endif
                    }
                    else {
                        /* Increment receiving transaction counter. Initiator receives SYNACK and ACK2 */
                        shadow_cm->req->ibcom->incoming_connection_tx += 2;
                        dprintf("cm_drain_scq,cas succeeded,sending syn,%d->%d,connection_tx=%d\n",
                                MPID_nem_ib_myrank, shadow_cm->req->responder_rank,
                                shadow_cm->req->ibcom->outstanding_connection_tx);
                        shadow_cm->req->state = MPID_NEM_IB_CM_SYN;
                        if (MPID_nem_ib_ncqe_scratch_pad < MPID_NEM_IB_COM_MAX_CQ_CAPACITY &&
                            shadow_cm->req->ibcom->ncom_scratch_pad <
                            MPID_NEM_IB_COM_MAX_SQ_CAPACITY &&
                            MPID_nem_ib_diff16(MPID_nem_ib_cm_ringbuf_head,
                                               MPID_nem_ib_cm_ringbuf_tail) < MPID_NEM_IB_CM_NSEG) {

                            MPID_nem_ib_cm_cmd_syn_t *cmd =
                                (MPID_nem_ib_cm_cmd_syn_t *) shadow_cm->req->ibcom->
                                icom_mem[MPID_NEM_IB_COM_SCRATCH_PAD_FROM];
                            MPID_NEM_IB_CM_COMPOSE_SYN(cmd, shadow_cm->req);
                            cmd->responder_ringbuf_index =
                                shadow_cm->req->responder_ringbuf_index =
                                MPID_nem_ib_cm_ringbuf_head;
                            dprintf("cm_drain_scq,giving ringbuf_index=%d\n",
                                    cmd->responder_ringbuf_index);
                            MPID_nem_ib_cm_ringbuf_head++;
                            cmd->initiator_rank = MPID_nem_ib_myrank;
                            MPID_nem_ib_cm_cmd_shadow_t *shadow_syn =
                                (MPID_nem_ib_cm_cmd_shadow_t *)
                                MPIU_Malloc(sizeof(MPID_nem_ib_cm_cmd_shadow_t));
                            shadow_syn->type = shadow_cm->req->state;
                            shadow_syn->req = shadow_cm->req;
                            dprintf("shadow_syn=%p,shadow_syn->req=%p\n", shadow_syn,
                                    shadow_syn->req);
                            mpi_errno =
                                MPID_nem_ib_cm_cmd_core(shadow_cm->req->responder_rank, shadow_syn,
                                                        (void *) cmd,
                                                        sizeof(MPID_nem_ib_cm_cmd_syn_t),
                                                        1 /* syn:1 */ , 0);
                            MPIU_ERR_CHKANDJUMP(mpi_errno, mpi_errno, MPI_ERR_OTHER,
                                                "**MPID_nem_ib_cm_send_core");
                        }
                        else {
                            MPID_NEM_IB_CM_COMPOSE_SYN((MPID_nem_ib_cm_cmd_syn_t *) &
                                                       (shadow_cm->req->cmd), shadow_cm->req);
                            MPID_nem_ib_cm_sendq_enqueue(&MPID_nem_ib_cm_sendq, shadow_cm->req);
                            dprintf("cm_drain_scq,enqueue syn,%d->%d\n",
                                    MPID_nem_ib_myrank, shadow_cm->req->responder_rank);
                        }
                    }
                }
                else {
                    if (is_conn_established(shadow_cm->req->responder_rank)) {
                        /* CAS is failed, and connection is already established */

                        dprintf("cm_drain_scq,cm_cas,connection is already established\n");
                        MPID_nem_ib_ncqe_scratch_pad_to_drain -= 1;
                        shadow_cm->req->ibcom->ncom_scratch_pad -= 1;
                        shadow_cm->req->ibcom->outstanding_connection_tx -= 1;
                        dprintf("cm_drain_scq,cm_cas,cas failed,established is true,%d->%d,tx=%d\n",
                                MPID_nem_ib_myrank, shadow_cm->req->responder_rank,
                                shadow_cm->req->ibcom->outstanding_connection_tx);
                        MPID_nem_ib_rdmawr_from_free(shadow_cm->buf_from, shadow_cm->buf_from_sz);
                        /* Let the guard down to let the following connection request go. */
                        VC_FIELD(MPID_nem_ib_conns[shadow_cm->req->responder_rank].vc,
                                 connection_guard) = 0;
                        /* free memory : req->ref_count is 2, so call MPIU_Free() directly */
                        //MPID_nem_ib_cm_request_release(shadow_cm->req);
                        MPIU_Free(shadow_cm->req);
                        MPIU_Free(shadow_cm);
                        break;
                    }

                    shadow_cm->req->retry_backoff =
                        shadow_cm->req->retry_backoff ? (shadow_cm->req->retry_backoff << 1) : 1;
                    shadow_cm->req->retry_decided = MPID_nem_ib_progress_engine_vt;     /* Schedule retry */
                    MPID_nem_ib_cm_sendq_enqueue(&MPID_nem_ib_cm_sendq, shadow_cm->req);
                    dprintf
                        ("cm_drain_scq,cm_cas,cas failed,%d->%d,retval=%016lx,decided=%ld,backoff=%ld\n",
                         MPID_nem_ib_myrank, shadow_cm->req->responder_rank, *cas_retval,
                         shadow_cm->req->retry_decided, shadow_cm->req->retry_backoff);
                }
                MPID_nem_ib_ncqe_scratch_pad_to_drain -= 1;
                shadow_cm->req->ibcom->ncom_scratch_pad -= 1;
                MPID_nem_ib_rdmawr_from_free(shadow_cm->buf_from, shadow_cm->buf_from_sz);
                MPIU_Free(shadow_cm);
                break;
            }
        case MPID_NEM_IB_CM_CAS_RELEASE:{
                shadow_cm = (MPID_nem_ib_cm_cmd_shadow_t *) cqe[i].wr_id;
                dprintf("cm_drain_scq,cm_cas_release,req=%p,responder_rank=%d\n",
                        shadow_cm->req, shadow_cm->req->responder_rank);
                /* Check if CAS have succeeded */
                uint64_t *cas_retval = (uint64_t *) shadow_cm->buf_from;
                if (*cas_retval == MPID_nem_ib_myrank) {
                    /* CAS succeeded */
                    dprintf("cm_drain_scq,cm_cas_release,cas succeeded,%d->%d,retval=%016lx\n",
                            MPID_nem_ib_myrank, shadow_cm->req->responder_rank, *cas_retval);
                    shadow_cm->req->ibcom->outstanding_connection_tx -= 1;
                    MPID_nem_ib_cm_request_release(shadow_cm->req);
                }
                else {

                    shadow_cm->req->retry_backoff =
                        shadow_cm->req->retry_backoff ? (shadow_cm->req->retry_backoff << 1) : 1;
                    shadow_cm->req->retry_decided = MPID_nem_ib_progress_engine_vt;     /* Schedule retry */
                    MPID_nem_ib_cm_sendq_enqueue(&MPID_nem_ib_cm_sendq, shadow_cm->req);
                    dprintf
                        ("cm_drain_scq,cm_cas_release,cas failed,%d->%d,retval=%016lx,decided=%ld,backoff=%ld\n",
                         MPID_nem_ib_myrank, shadow_cm->req->responder_rank, *cas_retval,
                         shadow_cm->req->retry_decided, shadow_cm->req->retry_backoff);
                }

                shadow_cm->req->ibcom->ncom_scratch_pad -= 1;
                MPID_nem_ib_rdmawr_from_free(shadow_cm->buf_from, shadow_cm->buf_from_sz);
                MPIU_Free(shadow_cm);
                break;
            }
        case MPID_NEM_IB_CM_SYN:
            dprintf("cm_drain_scq,syn sent\n");
            shadow_cm = (MPID_nem_ib_cm_cmd_shadow_t *) cqe[i].wr_id;
            shadow_cm->req->ibcom->ncom_scratch_pad -= 1;
            dprintf("cm_drain_scq,syn sent,%d->%d,connection_tx=%d\n",
                    MPID_nem_ib_myrank, shadow_cm->req->responder_rank,
                    shadow_cm->req->ibcom->outstanding_connection_tx);
            dprintf("cm_drain_scq,syn,buf_from=%p,sz=%d\n", shadow_cm->buf_from,
                    shadow_cm->buf_from_sz);
            MPID_nem_ib_cm_request_release(shadow_cm->req);
            MPID_nem_ib_rdmawr_from_free(shadow_cm->buf_from, shadow_cm->buf_from_sz);
            MPIU_Free(shadow_cm);
            break;
        case MPID_NEM_IB_CM_CAS_RELEASE2:
            dprintf("cm_drain_scq,release2 sent\n");
            shadow_cm = (MPID_nem_ib_cm_cmd_shadow_t *) cqe[i].wr_id;
            shadow_cm->req->ibcom->ncom_scratch_pad -= 1;
            shadow_cm->req->ibcom->outstanding_connection_tx -= 1;
            dprintf("cm_drain_scq,cas_release2 sent,%d->%d,connection_tx=%d\n",
                    MPID_nem_ib_myrank, shadow_cm->req->responder_rank,
                    shadow_cm->req->ibcom->outstanding_connection_tx);
            dprintf("cm_drain_scq,syn,buf_from=%p,sz=%d\n", shadow_cm->buf_from,
                    shadow_cm->buf_from_sz);
            MPID_nem_ib_rdmawr_from_free(shadow_cm->buf_from, shadow_cm->buf_from_sz);
            /* free memory : req->ref_count is 2, so call MPIU_Free() directly */
            //MPID_nem_ib_cm_request_release(shadow_cm->req);
            MPIU_Free(shadow_cm->req);
            MPIU_Free(shadow_cm);
            break;
        case MPID_NEM_IB_CM_SYNACK:
            shadow_cm = (MPID_nem_ib_cm_cmd_shadow_t *) cqe[i].wr_id;
            dprintf("cm_drain_scq,synack sent,req=%p,initiator_rank=%d\n", shadow_cm->req,
                    shadow_cm->req->initiator_rank);
            shadow_cm->req->ibcom->ncom_scratch_pad -= 1;
            dprintf("cm_drain_scq,synack sent,%d->%d,tx=%d\n",
                    MPID_nem_ib_myrank, shadow_cm->req->initiator_rank,
                    shadow_cm->req->ibcom->outstanding_connection_tx);
            dprintf("cm_drain_scq,synack,buf_from=%p,sz=%d\n", shadow_cm->buf_from,
                    shadow_cm->buf_from_sz);
            MPID_nem_ib_rdmawr_from_free(shadow_cm->buf_from, shadow_cm->buf_from_sz);
            MPIU_Free(shadow_cm);
            break;
        case MPID_NEM_IB_CM_ACK1:
            dprintf("cm_drain_scq,ack1 sent\n");
            shadow_cm = (MPID_nem_ib_cm_cmd_shadow_t *) cqe[i].wr_id;
            shadow_cm->req->ibcom->ncom_scratch_pad -= 1;
            shadow_cm->req->ibcom->outstanding_connection_tx -= 1;
            dprintf("cm_drain_scq,ack1,%d->%d,connection_tx=%d\n",
                    MPID_nem_ib_myrank, shadow_cm->req->responder_rank,
                    shadow_cm->req->ibcom->outstanding_connection_tx);
            dprintf("cm_drain_scq,ack1,buf_from=%p,sz=%d\n", shadow_cm->buf_from,
                    shadow_cm->buf_from_sz);
            MPID_nem_ib_rdmawr_from_free(shadow_cm->buf_from, shadow_cm->buf_from_sz);
            /* Finalize protocol because there is no referer in cm_drain_scq and sendq.
             * Note that there might be one in cm_poll. */
            MPID_nem_ib_cm_request_release(shadow_cm->req);
            MPIU_Free(shadow_cm);
            break;
        case MPID_NEM_IB_CM_ACK2:
            shadow_cm = (MPID_nem_ib_cm_cmd_shadow_t *) cqe[i].wr_id;
            dprintf("cm_drain_scq,ack2 sent,req=%p,initiator_rank=%p=%d\n",
                    shadow_cm->req, &shadow_cm->req->initiator_rank,
                    shadow_cm->req->initiator_rank);
            shadow_cm->req->ibcom->ncom_scratch_pad -= 1;
            shadow_cm->req->ibcom->outstanding_connection_tx -= 1;
            dprintf("cm_drain_scq,ack2,%d->%d,tx=%d\n",
                    MPID_nem_ib_myrank, shadow_cm->req->initiator_rank,
                    shadow_cm->req->ibcom->outstanding_connection_tx);
            dprintf("cm_drain_scq,ack2,buf_from=%p,sz=%d\n", shadow_cm->buf_from,
                    shadow_cm->buf_from_sz);
            MPID_nem_ib_rdmawr_from_free(shadow_cm->buf_from, shadow_cm->buf_from_sz);
            /* Let the guard down to let the following connection request go. */
            VC_FIELD(MPID_nem_ib_conns[shadow_cm->req->initiator_rank].vc, connection_guard) = 0;
            /* Finalize protocol because there is no referer in cm_drain_scq, sendq
             * and cm_poll because cm_poll sent ACK2. */
            MPID_nem_ib_cm_request_release(shadow_cm->req);
            MPIU_Free(shadow_cm);
            break;
        case MPID_NEM_IB_CM_ALREADY_ESTABLISHED:
        case MPID_NEM_IB_CM_RESPONDER_IS_CONNECTING:
            /* These cases mean the end of CM-op, so we do the almost same operation as ack2 */
            shadow_cm = (MPID_nem_ib_cm_cmd_shadow_t *) cqe[i].wr_id;
            dprintf
                ("cm_drain_scq,established or connecting sent,req=%p,initiator_rank=%p=%d\n",
                 shadow_cm->req, &shadow_cm->req->initiator_rank, shadow_cm->req->initiator_rank);
            shadow_cm->req->ibcom->ncom_scratch_pad -= 1;
            shadow_cm->req->ibcom->outstanding_connection_tx -= 1;
            dprintf("cm_drain_scq,established or connecting sent,%d->%d,connection_tx=%d,type=%d\n",
                    MPID_nem_ib_myrank, shadow_cm->req->initiator_rank,
                    shadow_cm->req->ibcom->outstanding_connection_tx, *type);
            shadow_cm->req->ibcom->incoming_connection_tx -= 1;
            MPID_nem_ib_rdmawr_from_free(shadow_cm->buf_from, shadow_cm->buf_from_sz);
            /* Let the guard down to let the following connection request go. */
            VC_FIELD(MPID_nem_ib_conns[shadow_cm->req->initiator_rank].vc, connection_guard) = 0;
            /* Finalize protocol because there is no referer in cm_drain_scq, sendq
             * and cm_poll because cm_poll sent ACK2. */
            MPID_nem_ib_cm_request_release(shadow_cm->req);
            MPIU_Free(shadow_cm);
            break;
        case MPID_NEM_IB_RINGBUF_ASK_FETCH:
            shadow_ringbuf = (MPID_nem_ib_ringbuf_cmd_shadow_t *) cqe[i].wr_id;
            memcpy(&shadow_ringbuf->req->fetched,
                   shadow_ringbuf->buf_from, sizeof(MPID_nem_ib_ringbuf_headtail_t));
            dprintf("cm_drain_scq,ask_fetch sent,%d->%d,req=%p,fetched->head=%ld,tail=%d\n",
                    MPID_nem_ib_myrank, shadow_ringbuf->req->vc->pg_rank,
                    shadow_ringbuf->req, shadow_ringbuf->req->fetched.head,
                    shadow_ringbuf->req->fetched.tail);
            /* Proceed to cas */
            MPID_nem_ib_ringbuf_ask_cas(shadow_ringbuf->req->vc, shadow_ringbuf->req);
            MPID_nem_ib_ncqe_scratch_pad_to_drain -= 1;
            shadow_ringbuf->req->ibcom->ncom_scratch_pad -= 1;
            MPID_nem_ib_rdmawr_from_free(shadow_ringbuf->buf_from, shadow_ringbuf->buf_from_sz);
            MPIU_Free(shadow_ringbuf);
            break;
        case MPID_NEM_IB_RINGBUF_ASK_CAS:{
                shadow_ringbuf = (MPID_nem_ib_ringbuf_cmd_shadow_t *) cqe[i].wr_id;
                /* Check if CAS have succeeded */
                MPID_nem_ib_ringbuf_headtail_t *cas_retval =
                    (MPID_nem_ib_ringbuf_headtail_t *) shadow_ringbuf->buf_from;
                dprintf
                    ("cm_drain_scq,ask_cas sent,req=%p,fetched.head=%lx,retval=%lx\n",
                     shadow_ringbuf->req, shadow_ringbuf->req->fetched.head, cas_retval->head);
                if (cas_retval->head == shadow_ringbuf->req->fetched.head) {
                    /* CAS succeeded */
                    dprintf
                        ("cm_drain_scq,ask_cas,cas succeeded,%d->%d,local_head=%d,local_tail=%d,nslot=%d\n",
                         MPID_nem_ib_myrank, shadow_ringbuf->req->vc->pg_rank,
                         VC_FIELD(shadow_ringbuf->req->vc, ibcom->sseq_num),
                         VC_FIELD(shadow_ringbuf->req->vc, ibcom->lsr_seq_num_tail),
                         VC_FIELD(shadow_ringbuf->req->vc, ibcom->local_ringbuf_nslot));
                    if (MPID_nem_ib_diff16
                        (VC_FIELD(shadow_ringbuf->req->vc, ibcom->sseq_num),
                         VC_FIELD(shadow_ringbuf->req->vc,
                                  ibcom->lsr_seq_num_tail)) >=
                        VC_FIELD(shadow_ringbuf->req->vc, ibcom->local_ringbuf_nslot)) {
                        dprintf("cm_drain_scq,ask_cas,refill fast path\n");
                        /* Refill now when we don't have any slots */
                        VC_FIELD(shadow_ringbuf->req->vc, ibcom->sseq_num) =
                            (uint16_t) shadow_ringbuf->req->fetched.head;
                        /* Move tail pointer to indicate only one slot is available to us */
                        VC_FIELD(shadow_ringbuf->req->vc, ibcom->lsr_seq_num_tail) = (uint16_t)
                            (VC_FIELD(shadow_ringbuf->req->vc, ibcom->sseq_num) -
                             VC_FIELD(shadow_ringbuf->req->vc, ibcom->local_ringbuf_nslot) + 1);
                        dprintf
                            ("cm_drain_scq,ask_cas,after refill,local_head=%d,local_tail=%d,nslot=%d\n",
                             VC_FIELD(shadow_ringbuf->req->vc, ibcom->sseq_num),
                             VC_FIELD(shadow_ringbuf->req->vc, ibcom->lsr_seq_num_tail),
                             VC_FIELD(shadow_ringbuf->req->vc, ibcom->local_ringbuf_nslot));
                    }
                    else {
                        dprintf("cm_drain_scq,ask_cas,refill slow path\n");
                        /* Enqueue slots to avoid overwriting the slots when we have some slots.
                         * This happens when two or more asks succeeded before
                         * the first queued send is issued. */
                        MPID_nem_ib_ringbuf_sector_t *sector = (MPID_nem_ib_ringbuf_sector_t *)
                            MPIU_Malloc(sizeof(MPID_nem_ib_ringbuf_sector_t));
                        MPIU_ERR_CHKANDJUMP(!sector, mpi_errno, MPI_ERR_OTHER, "**malloc");
                        sector->type = MPID_NEM_IB_RINGBUF_SHARED;
                        sector->start =
                            VC_FIELD(shadow_ringbuf->req->vc, ibcom->local_ringbuf_start);
                        sector->nslot =
                            VC_FIELD(shadow_ringbuf->req->vc, ibcom->local_ringbuf_nslot);
                        sector->head = (uint16_t) shadow_ringbuf->req->fetched.head;
                        sector->tail =
                            sector->head - VC_FIELD(shadow_ringbuf->req->vc,
                                                    ibcom->local_ringbuf_nslot) + 1;
                        MPID_nem_ib_ringbuf_sectorq_enqueue(&VC_FIELD
                                                            (shadow_ringbuf->req->vc,
                                                             ibcom->sectorq), sector);
                    }
                    /* Let the guard down so that the following ask-fetch can be issued */
                    VC_FIELD(shadow_ringbuf->req->vc, ibcom->ask_guard) = 0;
                    /* Kick progress engine */
                    dprintf
                        ("cm_drain_scq,call send_progress for %d,ncom=%d,ncqe=%d,local_head=%d,local_tail=%d,nslot=%d\n",
                         shadow_ringbuf->req->vc->pg_rank, VC_FIELD(shadow_ringbuf->req->vc,
                                                                    ibcom->ncom),
                         MPID_nem_ib_ncqe, VC_FIELD(shadow_ringbuf->req->vc, ibcom->sseq_num),
                         VC_FIELD(shadow_ringbuf->req->vc, ibcom->lsr_seq_num_tail),
                         VC_FIELD(shadow_ringbuf->req->vc, ibcom->local_ringbuf_nslot)
);
                    MPID_nem_ib_send_progress(shadow_ringbuf->req->vc);
                    MPIU_Free(shadow_ringbuf->req);
                }
                else {
                    /* CAS failed */
                    dprintf("ask-cas,failed\n");
                    MPID_nem_ib_segv;
                    /* Let the guard down so that this ask-fetch can be issued in ringbuf_progress */
                    VC_FIELD(shadow_ringbuf->req->vc, ibcom->ask_guard) = 0;
                    /* Retry from fetch */
                    shadow_ringbuf->req->state = MPID_NEM_IB_RINGBUF_ASK_FETCH;
                    /* Schedule retry */
                    dprintf("cm_drain_scq,retval=%08lx,backoff=%ld\n",
                            cas_retval->head, shadow_ringbuf->req->retry_backoff);
                    MPID_NEM_IB_RINGBUF_UPDATE_BACKOFF(shadow_ringbuf->req->retry_backoff);
                    shadow_ringbuf->req->retry_decided = MPID_nem_ib_progress_engine_vt;
                    /* Make the ask-fetch in order */
                    MPID_nem_ib_ringbuf_sendq_enqueue_at_head(&MPID_nem_ib_ringbuf_sendq,
                                                              shadow_ringbuf->req);
                    dprintf("cm_drain_scq,ask_cas,cas failed,decided=%ld,backoff=%ld\n",
                            shadow_ringbuf->req->retry_decided, shadow_ringbuf->req->retry_backoff);
                }
                MPID_nem_ib_ncqe_scratch_pad_to_drain -= 1;
                shadow_ringbuf->req->ibcom->ncom_scratch_pad -= 1;
                MPID_nem_ib_rdmawr_from_free(shadow_ringbuf->buf_from, shadow_ringbuf->buf_from_sz);
                MPIU_Free(shadow_ringbuf);
                break;
            }
        case MPID_NEM_IB_NOTIFY_OUTSTANDING_TX_EMPTY:
            shadow_cm = (MPID_nem_ib_cm_cmd_shadow_t *) cqe[i].wr_id;
            shadow_cm->req->ibcom->notify_outstanding_tx_empty |= NOTIFY_OUTSTANDING_TX_SCQ;
            MPID_nem_ib_rdmawr_from_free(shadow_cm->buf_from, shadow_cm->buf_from_sz);
            MPIU_Free(shadow_cm->req);
            MPIU_Free(shadow_cm);
            break;
        default:
            printf("unknown type=%d\n", *type);
            MPIU_ERR_CHKANDJUMP(1, mpi_errno, MPI_ERR_OTHER, "**MPID_nem_ib_cm_drain_scq");
            break;
        }
        MPID_nem_ib_ncqe_scratch_pad -= 1;
    }
    /* The number of CQE is reduced or a slot of the ringbuf is released, so kick progress engine */
    if (result > 0) {
        MPID_nem_ib_cm_progress();
        MPID_nem_ib_ringbuf_progress();
    }

  fn_exit:
    MPIDI_FUNC_EXIT(MPID_STATE_MPID_NEM_IB_CM_DRAIN_SCQ);
    return mpi_errno;
  fn_fail:
    goto fn_exit;
}

int MPID_nem_ib_cm_drain_rcq(void)
{
    int mpi_errno = MPI_SUCCESS;
    int result;
    int i;
    struct ibv_wc cqe[MPID_NEM_IB_COM_MAX_CQ_HEIGHT_DRAIN];
    MPID_nem_ib_cm_notify_send_t *shadow_cm;

    if (!MPID_nem_ib_rc_shared_rcq_scratch_pad) {
        dprintf("cm_drain_rcq,CQ is null\n");
        goto fn_exit;
    }

    result =
        ibv_poll_cq(MPID_nem_ib_rc_shared_rcq_scratch_pad, MPID_NEM_IB_COM_MAX_CQ_HEIGHT_DRAIN,
                    &cqe[0]);
    MPIU_ERR_CHKANDJUMP(result < 0, mpi_errno, MPI_ERR_OTHER, "**netmod,ib,ibv_poll_cq");

    if (result > 0) {
        dprintf("cm_drain_rcq,found,result=%d\n", result);
    }
    for (i = 0; i < result; i++) {

        dprintf("cm_drain_rcq,wr_id=%p\n", (void *) cqe[i].wr_id);

#ifdef HAVE_LIBDCFA
        if (cqe[i].status != IBV_WC_SUCCESS) {
            dprintf("cm_drain_rcq,status=%08x\n", cqe[i].status);
            MPID_nem_ib_segv;
        }
#else
        if (cqe[i].status != IBV_WC_SUCCESS) {
            dprintf("cm_drain_rcq,status=%08x,%s\n", cqe[i].status,
                    ibv_wc_status_str(cqe[i].status));
            MPID_nem_ib_segv;
        }
#endif
        MPIU_ERR_CHKANDJUMP(cqe[i].status != IBV_WC_SUCCESS, mpi_errno, MPI_ERR_OTHER,
                            "**MPID_nem_ib_cm_drain_rcq");

        MPID_nem_ib_cm_cmd_type_t *type = (MPID_nem_ib_cm_cmd_type_t *) cqe[i].wr_id;
        switch (*type) {
        case MPID_NEM_IB_NOTIFY_OUTSTANDING_TX_EMPTY:{
                int initiator_rank;
                MPID_nem_ib_com_t *ibcom;

                dprintf("cm_drain_rcq,notify_outstanding_tx_empty\n");
                shadow_cm = (MPID_nem_ib_cm_notify_send_t *) cqe[i].wr_id;
                initiator_rank = shadow_cm->initiator_rank;

                MPID_nem_ib_rdmawr_from_free(shadow_cm, sizeof(MPID_nem_ib_cm_notify_send_t));

                MPID_nem_ib_com_obtain_pointer(MPID_nem_ib_scratch_pad_fds[initiator_rank], &ibcom);
                ibcom->notify_outstanding_tx_empty |= NOTIFY_OUTSTANDING_TX_RCQ;
                MPID_nem_ib_com_scratch_pad_recv(MPID_nem_ib_scratch_pad_fds[initiator_rank],
                                                 sizeof(MPID_nem_ib_cm_notify_send_t));
            }
            break;
        default:
            printf("unknown type=%d\n", *type);
            MPIU_ERR_CHKANDJUMP(1, mpi_errno, MPI_ERR_OTHER, "**MPID_nem_ib_cm_drain_rcq");
            break;
        }
    }

  fn_exit:
    return mpi_errno;
  fn_fail:
    goto fn_exit;
}

#undef FUNCNAME
#define FUNCNAME MPID_nem_ib_cm_poll_syn
#undef FCNAME
#define FCNAME MPIDI_QUOTE(FUNCNAME)
int MPID_nem_ib_cm_poll_syn()
{
    int mpi_errno = MPI_SUCCESS;
    int ibcom_errno;
    int ib_port = 1;
    MPIDI_STATE_DECL(MPID_STATE_MPID_NEM_IB_CM_POLL_SYN);
    MPIDI_FUNC_ENTER(MPID_STATE_MPID_NEM_IB_CM_POLL_SYN);
    /* scratch pad is freed after receiving CLOSE */
    if (!MPID_nem_ib_scratch_pad) {
        dprintf("cm_poll_syn,MPID_nem_ib_scratch_pad is zero\n");
        goto fn_exit;
    }

    /* Make the following store instruction onto the CAS word switch
     * the value from "acquired" to "released" by
     * waiting until modification on CAS word by a PCIe device
     * propagated to the cache tag. */
    volatile uint64_t *cas_word = (uint64_t *) (MPID_nem_ib_scratch_pad);
    if (*cas_word == MPID_NEM_IB_CM_RELEASED) {
        goto fn_exit;
    }

    /* Memory layout is (CAS-word:SYN#0:SYN#1:...:SYN#N:CMD#0:CMD#1:...CMD#M) */
    void *slot = (MPID_nem_ib_scratch_pad + MPID_NEM_IB_CM_OFF_SYN +
                  sizeof(MPID_nem_ib_cm_cmd_t) * (0 % MPID_NEM_IB_CM_NSEG));
    volatile uint8_t *head_flag = (uint8_t *) slot;
    if (*head_flag == MPID_NEM_IB_CM_HEAD_FLAG_ZERO) {
        goto fn_exit;
    }   /* Incoming message hasn't arrived */

    volatile MPID_nem_ib_cm_cmd_syn_t *syn_tail_flag = (MPID_nem_ib_cm_cmd_syn_t *) slot;

    switch (*head_flag) {
    case MPID_NEM_IB_CM_SYN:{
            int is_synack = 0;
            while (syn_tail_flag->tail_flag.tail_flag != MPID_NEM_IB_COM_MAGIC) {
                /* __asm__ __volatile__("pause;":::"memory"); */
            }

            MPID_nem_ib_cm_cmd_syn_t *syn = (MPID_nem_ib_cm_cmd_syn_t *) slot;
            dprintf("cm_poll_syn,syn detected!,%d->%d,ringbuf_index given=%d\n",
                    syn->initiator_rank, MPID_nem_ib_myrank, syn->responder_ringbuf_index);
            MPID_nem_ib_cm_req_t *req = MPIU_Malloc(sizeof(MPID_nem_ib_cm_req_t));
            MPIU_ERR_CHKANDJUMP(!req, mpi_errno, MPI_ERR_OTHER, "**malloc");
            req->ref_count = 1; /* Released when draining SCQ of ACK2 */
            req->ringbuf_index = syn->responder_ringbuf_index;
            req->initiator_rank = syn->initiator_rank;
            req->responder_rank = MPID_nem_ib_myrank;
            ibcom_errno =
                MPID_nem_ib_com_obtain_pointer(MPID_nem_ib_scratch_pad_fds
                                               [req->initiator_rank], &req->ibcom);
            MPIU_ERR_CHKANDJUMP(ibcom_errno, mpi_errno, MPI_ERR_OTHER,
                                "**MPID_nem_ib_com_obtain_pointer");
            if (is_conn_established(syn->initiator_rank)) {
                dprintf("cm_poll_syn,established is true,%d->%d,connection_tx=%d\n",
                        syn->initiator_rank, MPID_nem_ib_myrank,
                        req->ibcom->outstanding_connection_tx);
                req->state = MPID_NEM_IB_CM_ALREADY_ESTABLISHED;
            }
            else if ((MPID_nem_ib_myrank > syn->initiator_rank) &&
                     (req->ibcom->outstanding_connection_tx > 0)) {
                dprintf("cm_poll_syn,connection_tx>0,%d->%d,connection_tx=%d\n",
                        syn->initiator_rank, MPID_nem_ib_myrank,
                        req->ibcom->outstanding_connection_tx);
                req->state = MPID_NEM_IB_CM_RESPONDER_IS_CONNECTING;
            }
            else {
                /* Skip QP createion on race condition */
                if (!
                    (VC_FIELD
                     (MPID_nem_ib_conns[syn->initiator_rank].vc,
                      connection_state) & MPID_NEM_IB_CM_LOCAL_QP_RESET)) {
                    ibcom_errno =
                        MPID_nem_ib_com_open(ib_port, MPID_NEM_IB_COM_OPEN_RC,
                                             &MPID_nem_ib_conns[syn->initiator_rank].fd);
                    MPIU_ERR_CHKANDJUMP(ibcom_errno, mpi_errno, MPI_ERR_OTHER,
                                        "**MPID_nem_ib_com_open");
                    /* store pointer to MPID_nem_ib_com */
                    dprintf("cm_poll_syn,initiator fd=%d\n",
                            MPID_nem_ib_conns[syn->initiator_rank].fd);
                    ibcom_errno =
                        MPID_nem_ib_com_obtain_pointer(MPID_nem_ib_conns[syn->initiator_rank].fd,
                                                       &VC_FIELD(MPID_nem_ib_conns
                                                                 [syn->initiator_rank].vc, ibcom));
                    MPIU_ERR_CHKANDJUMP(ibcom_errno, mpi_errno, MPI_ERR_OTHER,
                                        "**MPID_nem_ib_com_obtain_pointer");
                    /* Allocate RDMA-write-to ring-buf for remote */
                    mpi_errno =
                        MPID_nem_ib_ringbuf_alloc(MPID_nem_ib_conns[syn->initiator_rank].vc);
                    MPIU_ERR_CHKANDJUMP(mpi_errno, mpi_errno, MPI_ERR_OTHER,
                                        "**MPID_nem_ib_ringbuf_alloc");
                    /* Record state transition for race condition detection */
                    VC_FIELD(MPID_nem_ib_conns[syn->initiator_rank].vc,
                             connection_state) |= MPID_NEM_IB_CM_LOCAL_QP_RESET;
                }

                req->state = MPID_NEM_IB_CM_SYNACK;
                is_synack = 1;
            }

            /* Increment transaction counter here because this path is executed only once */
            req->ibcom->outstanding_connection_tx += 1;
            /* Increment receiving transaction counter.
             * In the case of SYNACK, Responder receives ack1
             * In the case of ALREADY_ESTABLISHED or RESPONDER_IS_CONNECTING,
             * decrement in cm_drain_scq.
             */
            req->ibcom->incoming_connection_tx += 1;
            if (MPID_nem_ib_ncqe_scratch_pad < MPID_NEM_IB_COM_MAX_CQ_CAPACITY &&
                req->ibcom->ncom_scratch_pad < MPID_NEM_IB_COM_MAX_SQ_CAPACITY &&
                MPID_nem_ib_diff16(MPID_nem_ib_cm_ringbuf_head,
                                   MPID_nem_ib_cm_ringbuf_tail) < MPID_NEM_IB_CM_NSEG) {

                MPID_nem_ib_cm_cmd_synack_t *cmd =
                    (MPID_nem_ib_cm_cmd_synack_t *) req->ibcom->
                    icom_mem[MPID_NEM_IB_COM_SCRATCH_PAD_FROM];
                if (is_synack) {
                    dprintf("cm_poll_syn,sending synack,%d->%d[%d],connection_tx=%d\n",
                            MPID_nem_ib_myrank, syn->initiator_rank, req->ringbuf_index,
                            req->ibcom->outstanding_connection_tx);
                    MPID_NEM_IB_CM_COMPOSE_SYNACK(cmd, req, syn->initiator_req);
                    dprintf
                        ("cm_poll_syn,composing synack,responder_req=%p,cmd->rmem=%p,rkey=%08x,ringbuf_nslot=%d,remote_vc=%p\n",
                         cmd->responder_req, cmd->rmem, cmd->rkey, cmd->ringbuf_nslot,
                         cmd->remote_vc);
                    cmd->initiator_ringbuf_index = req->initiator_ringbuf_index =
                        MPID_nem_ib_cm_ringbuf_head;
                    dprintf("cm_poll_syn,giving ringbuf_index=%d\n", cmd->initiator_ringbuf_index);
                    MPID_nem_ib_cm_ringbuf_head++;
                }
                else {
                    dprintf
                        ("cm_poll_syn,sending established or connecting,%d->%d[%d],connection_tx=%d,state=%d\n",
                         MPID_nem_ib_myrank, syn->initiator_rank, req->ringbuf_index,
                         req->ibcom->outstanding_connection_tx, req->state);
                    MPID_NEM_IB_CM_COMPOSE_END_CM(cmd, req, syn->initiator_req, req->state);
                }
                MPID_nem_ib_cm_cmd_shadow_t *shadow = (MPID_nem_ib_cm_cmd_shadow_t *)
                    MPIU_Malloc(sizeof(MPID_nem_ib_cm_cmd_shadow_t));
                shadow->type = req->state;
                shadow->req = req;
                dprintf("cm_poll_syn,shadow=%p,shadow->req=%p\n", shadow, shadow->req);
                mpi_errno =
                    MPID_nem_ib_cm_cmd_core(req->initiator_rank, shadow, (void *) cmd,
                                            sizeof(MPID_nem_ib_cm_cmd_synack_t), 0,
                                            req->ringbuf_index);
                MPIU_ERR_CHKANDJUMP(mpi_errno, mpi_errno, MPI_ERR_OTHER,
                                    "**MPID_nem_ib_cm_send_core");
            }
            else {
                dprintf("cm_poll_syn,enqueue,ncqe=%d,ncom=%d,head=%d,tail=%d\n",
                        MPID_nem_ib_ncqe_scratch_pad, req->ibcom->ncom_scratch_pad,
                        MPID_nem_ib_cm_ringbuf_head, MPID_nem_ib_cm_ringbuf_tail);
                if (is_synack) {
                    dprintf("cm_poll_syn,queueing syn,%d->%d,connection_tx=%d\n",
                            MPID_nem_ib_myrank, syn->initiator_rank,
                            req->ibcom->outstanding_connection_tx);
                    MPID_NEM_IB_CM_COMPOSE_SYNACK((MPID_nem_ib_cm_cmd_synack_t *) &
                                                  (req->cmd), req, syn->initiator_req);
                }
                else {
                    dprintf
                        ("cm_poll_syn,queueing established or connecting,%d->%d,connection_tx=%d,state=%d\n",
                         MPID_nem_ib_myrank, syn->initiator_rank,
                         req->ibcom->outstanding_connection_tx, req->state);
                    MPID_NEM_IB_CM_COMPOSE_END_CM((MPID_nem_ib_cm_cmd_synack_t *) & (req->cmd), req,
                                                  syn->initiator_req, req->state);
                }
                MPID_nem_ib_cm_sendq_enqueue(&MPID_nem_ib_cm_sendq, req);
            }
        }
        goto common_tail;
        break;
    case MPID_NEM_IB_CM_CAS_RELEASE2:{
            MPID_nem_ib_segv;
            /* Initiator requests to release CAS word.
             * Because connection is already established.
             * In this case, responder may already have performed vc_terminate. */

            while (syn_tail_flag->tail_flag.tail_flag != MPID_NEM_IB_COM_MAGIC) {
                /* __asm__ __volatile__("pause;":::"memory"); */
            }

#ifdef MPID_NEM_IB_DEBUG_POLL
            MPID_nem_ib_cm_cmd_syn_t *syn = (MPID_nem_ib_cm_cmd_syn_t *) slot;
#endif
            dprintf("cm_poll_syn,release2 detected,%d->%d\n",
                    syn->initiator_rank, MPID_nem_ib_myrank);
        }

      common_tail:

        /* Clear head-flag */
        *head_flag = MPID_NEM_IB_CM_HEAD_FLAG_ZERO;

        /* Clear tail-flag */
        syn_tail_flag->tail_flag.tail_flag = 0;

        /* Release CAS word.
         * Note that the following store instruction switches the value from "acquired" to "released"
         * because the load instruction above made the cache tag for the CAS word
         * reflect the switch of the value from "released" to "acquired".
         * We want to prevent the case where the store instruction switches the value from
         * "released" to "released" then a write command from a PCI device arrives
         * and switches the value from "released" to "acquired")
         */
        //*cas_word = MPID_NEM_IB_CM_RELEASED;
        dprintf("cm_poll_syn,exit,%d,cas_word,%p,%lx\n", MPID_nem_ib_myrank, cas_word, *cas_word);
        break;
    default:
        printf("unknown connection command\n");
        MPIU_ERR_CHKANDJUMP(1, mpi_errno, MPI_ERR_OTHER, "**MPID_nem_ib_cm_poll");
        break;
    }

  fn_exit:
    MPIDI_FUNC_EXIT(MPID_STATE_MPID_NEM_IB_CM_POLL_SYN);
    return mpi_errno;
  fn_fail:
    goto fn_exit;
}


#undef FUNCNAME
#define FUNCNAME MPID_nem_ib_cm_release
#undef FCNAME
int MPID_nem_ib_cm_release(uint16_t index)
{
    int mpi_errno = MPI_SUCCESS;
    int old_ringbuf_tail = MPID_nem_ib_cm_ringbuf_tail;
    uint16_t index_slot = index % MPID_NEM_IB_CM_NSEG;
    MPIDI_STATE_DECL(MPID_STATE_MPID_NEM_IB_CM_RELEASE);
    MPIDI_FUNC_ENTER(MPID_STATE_MPID_NEM_IB_CM_RELEASE);
    //dprintf("user_data=%p,mem=%p,sub=%08lx,index=%d\n", user_data, vc_ib->ibcom->icom_mem[MPID_NEM_IB_COM_RDMAWR_TO], (unsigned long)user_data - (unsigned long)vc_ib->ibcom->icom_mem[MPID_NEM_IB_COM_RDMAWR_TO], index);
    //dprintf("index=%d,released=%016lx\n", index, vc_ib->ibcom->remote_ringbuf->remote_released[index / 64]);
    MPID_nem_ib_cm_ringbuf_released[index_slot / 64] |= (1ULL << (index_slot & 63));
    //dprintf("released[index/64]=%016lx\n", vc_ib->ibcom->remote_ringbuf->remote_released[index / 64]);
    uint16_t index_tail = ((uint16_t) (MPID_nem_ib_cm_ringbuf_tail + 1) % MPID_NEM_IB_CM_NSEG);
    //dprintf("tail+1=%d,index_tail=%d\n", vc_ib->ibcom->rsr_seq_num_tail + 1, index_tail);
    //dprintf("released=%016lx\n", vc_ib->ibcom->remote_ringbuf->remote_released[index_tail / 64]);
    while (1) {
        if (((MPID_nem_ib_cm_ringbuf_released[index_tail / 64] >> (index_tail & 63)) & 1) == 1) {
            MPID_nem_ib_cm_ringbuf_tail++;
            MPID_nem_ib_cm_ringbuf_released[index_tail / 64] &= ~(1ULL << (index_tail & 63));
            dprintf("MPID_nem_ib_cm_ringbuf_tail,incremented to %d\n", MPID_nem_ib_cm_ringbuf_tail);
            index_tail = (uint16_t) (index_tail + 1) % MPID_NEM_IB_CM_NSEG;
        }
        else {
            break;
        }
    }

    /* A slot of the ringbuf is released, so kick progress engine */
    if (MPID_nem_ib_cm_ringbuf_tail != old_ringbuf_tail) {
        MPID_nem_ib_cm_progress();
    }

  fn_exit:
    MPIDI_FUNC_EXIT(MPID_STATE_MPID_NEM_IB_CM_RELEASE);
    return mpi_errno;
    //fn_fail:
    goto fn_exit;
}

#undef FUNCNAME
#define FUNCNAME MPID_nem_ib_cm_poll
#undef FCNAME
#define FCNAME MPIDI_QUOTE(FUNCNAME)
int MPID_nem_ib_cm_poll()
{
    int mpi_errno = MPI_SUCCESS;
    int ibcom_errno;
    uint16_t i;
    MPIDI_STATE_DECL(MPID_STATE_MPID_NEM_IB_CM_POLL);
    MPIDI_FUNC_ENTER(MPID_STATE_MPID_NEM_IB_CM_POLL);
    /* scratch pad is freed after receiving CLOSE */
    if (!MPID_nem_ib_scratch_pad) {
        dprintf("cm_poll,MPID_nem_ib_scratch_pad is zero\n");
        goto fn_exit;
    }

    /* Wrap-around tolerant by using "!=" */
    for (i = MPID_nem_ib_cm_ringbuf_tail + 1; i != MPID_nem_ib_cm_ringbuf_head; i++) {

        /* Memory layout is (CAS-word:SYN#0:SYN#1:...:SYN#N:CMD#0:CMD#1:...CMD#M) */
        void *slot = (MPID_nem_ib_scratch_pad + MPID_NEM_IB_CM_OFF_CMD +
                      sizeof(MPID_nem_ib_cm_cmd_t) * ((uint16_t) (i % MPID_NEM_IB_CM_NSEG)));
        volatile uint8_t *head_flag = (uint8_t *) slot;
        if (*head_flag == MPID_NEM_IB_CM_HEAD_FLAG_ZERO) {
            continue;
        }       /* Incoming message hasn't arrived */

        switch (*head_flag) {
        case MPID_NEM_IB_CM_SYNACK:{
                volatile MPID_nem_ib_cm_cmd_synack_t *synack_tail_flag =
                    (MPID_nem_ib_cm_cmd_synack_t *) slot;
                while (synack_tail_flag->tail_flag.tail_flag != MPID_NEM_IB_COM_MAGIC) {
                    /* __asm__ __volatile__("pause;":::"memory"); */
                }

                MPID_nem_ib_cm_cmd_synack_t *synack = (MPID_nem_ib_cm_cmd_synack_t *) slot;
                MPID_nem_ib_cm_req_t *req = (MPID_nem_ib_cm_req_t *) synack->initiator_req;
                req->ringbuf_index = synack->initiator_ringbuf_index;
                req->ibcom->incoming_connection_tx -= 1;        /* SYNACK */
                dprintf
                    ("cm_poll,synack detected!,%d->%d[%d],responder_req=%p,ringbuf_index=%d,tx=%d\n",
                     req->responder_rank, MPID_nem_ib_myrank, i,
                     synack->responder_req, synack->initiator_ringbuf_index,
                     req->ibcom->outstanding_connection_tx);
                /* Deduct it from the packet */
                VC_FIELD(MPID_nem_ib_conns[req->responder_rank].vc,
                         connection_state) |= MPID_NEM_IB_CM_REMOTE_QP_RESET;
                /* Skip QP state transition on race condition */
                if (!
                    (VC_FIELD
                     (MPID_nem_ib_conns[req->responder_rank].vc,
                      connection_state) & MPID_NEM_IB_CM_LOCAL_QP_RTS)) {
                    ibcom_errno =
                        MPID_nem_ib_com_rts(MPID_nem_ib_conns[req->responder_rank].fd,
                                            synack->qpnum, synack->lid, &(synack->gid));
                    MPIU_ERR_CHKANDJUMP(ibcom_errno, mpi_errno, MPI_ERR_OTHER,
                                        "**MPID_nem_ib_com_rts");
                    /* Connect ring buffer */
                    ibcom_errno =
                        MPID_nem_ib_com_connect_ringbuf(MPID_nem_ib_conns
                                                        [req->responder_rank].fd,
                                                        synack->ringbuf_type, synack->rmem,
                                                        synack->rkey, synack->ringbuf_nslot,
                                                        synack->remote_vc, 1);
                    MPIU_ERR_CHKANDJUMP(ibcom_errno, mpi_errno, MPI_ERR_OTHER,
                                        "**MPID_nem_ib_com_connect_ringbuf");
                    dprintf("connect_ringbuf,%d-%d=%d\n",
                            VC_FIELD(MPID_nem_ib_conns[req->responder_rank].vc,
                                     ibcom->sseq_num),
                            VC_FIELD(MPID_nem_ib_conns[req->responder_rank].vc,
                                     ibcom->lsr_seq_num_tail),
                            MPID_nem_ib_diff16(VC_FIELD
                                               (MPID_nem_ib_conns[req->responder_rank].vc,
                                                ibcom->sseq_num),
                                               VC_FIELD(MPID_nem_ib_conns
                                                        [req->responder_rank].vc,
                                                        ibcom->lsr_seq_num_tail))
);
                    /* Record state transition for race condition detection */
                    VC_FIELD(MPID_nem_ib_conns[req->responder_rank].vc,
                             connection_state) |= MPID_NEM_IB_CM_LOCAL_QP_RTS;
                }

                req->state = MPID_NEM_IB_CM_ACK1;
                if (MPID_nem_ib_ncqe_scratch_pad < MPID_NEM_IB_COM_MAX_CQ_CAPACITY &&
                    req->ibcom->ncom_scratch_pad < MPID_NEM_IB_COM_MAX_SQ_CAPACITY) {

                    dprintf("cm_poll,sending ack1,req=%p,ringbuf_index=%d\n", req,
                            req->ringbuf_index);
                    MPID_nem_ib_cm_cmd_ack1_t *cmd =
                        (MPID_nem_ib_cm_cmd_ack1_t *) req->ibcom->
                        icom_mem[MPID_NEM_IB_COM_SCRATCH_PAD_FROM];
                    MPID_NEM_IB_CM_COMPOSE_ACK1(cmd, req, synack->responder_req);
                    dprintf
                        ("cm_poll,composing ack1,cmd->responder_req=%p,cmd->rmem=%p,rkey=%08x,ringbuf_nslot=%d,remote_vc=%p\n",
                         cmd->responder_req, cmd->rmem, cmd->rkey, cmd->ringbuf_nslot,
                         cmd->remote_vc);
                    MPID_nem_ib_cm_cmd_shadow_t *shadow = (MPID_nem_ib_cm_cmd_shadow_t *)
                        MPIU_Malloc(sizeof(MPID_nem_ib_cm_cmd_shadow_t));
                    shadow->type = req->state;
                    shadow->req = req;
                    dprintf("shadow=%p,shadow->req=%p\n", shadow, shadow->req);
                    mpi_errno =
                        MPID_nem_ib_cm_cmd_core(req->responder_rank, shadow, (void *) cmd,
                                                sizeof(MPID_nem_ib_cm_cmd_ack1_t), 0,
                                                req->ringbuf_index);
                    MPIU_ERR_CHKANDJUMP(mpi_errno, mpi_errno, MPI_ERR_OTHER,
                                        "**MPID_nem_ib_cm_send_core");
                }
                else {
                    MPID_NEM_IB_CM_COMPOSE_ACK1((MPID_nem_ib_cm_cmd_ack1_t *) &
                                                (req->cmd), req, synack->responder_req);
                    MPID_nem_ib_cm_sendq_enqueue(&MPID_nem_ib_cm_sendq, req);
                }

                *head_flag = MPID_NEM_IB_CM_HEAD_FLAG_ZERO;     /* Clear head-flag */
                /* Clear all possible tail-flag slots */
                MPID_NEM_IB_CM_CLEAR_TAIL_FLAGS(slot);

                /* Explicitly release CAS word because
                 * ConnectX-3 doesn't support safe CAS with PCI device and CPU */
                MPID_nem_ib_cm_cas_release(MPID_nem_ib_conns[req->responder_rank].vc);
                break;
            }
        case MPID_NEM_IB_CM_ALREADY_ESTABLISHED:
        case MPID_NEM_IB_CM_RESPONDER_IS_CONNECTING:
            {
                volatile MPID_nem_ib_cm_cmd_synack_t *synack_tail_flag =
                    (MPID_nem_ib_cm_cmd_synack_t *) slot;
                while (synack_tail_flag->tail_flag.tail_flag != MPID_NEM_IB_COM_MAGIC) {
                    /* __asm__ __volatile__("pause;":::"memory"); */
                }

                MPID_nem_ib_cm_cmd_synack_t *synack = (MPID_nem_ib_cm_cmd_synack_t *) slot;
                MPID_nem_ib_cm_req_t *req = (MPID_nem_ib_cm_req_t *) synack->initiator_req;
                /* These mean the end of CM-op, so decrement here. */
                req->ibcom->outstanding_connection_tx -= 1;
                req->ibcom->incoming_connection_tx -= 2;
                dprintf
                    ("cm_poll,established or connecting detected!,%d->%d[%d],responder_req=%p,ringbuf_index=%d,tx=%d\n",
                     req->responder_rank, MPID_nem_ib_myrank, i,
                     synack->responder_req, synack->initiator_ringbuf_index,
                     req->ibcom->outstanding_connection_tx);
                /* cm_release calls cm_progress, so we have to clear scratch_pad here. */
                *head_flag = MPID_NEM_IB_CM_HEAD_FLAG_ZERO;     /* Clear head-flag */
                /* Clear all possible tail-flag slots */
                MPID_NEM_IB_CM_CLEAR_TAIL_FLAGS(slot);
                /* The initiator release the slot for responder */
                MPID_nem_ib_cm_release(req->responder_ringbuf_index);
                /* Kick ask-send commands waiting for connection */
                MPID_nem_ib_ringbuf_progress();
                /* Kick send commands waiting for connection.
                 * This might be a dupe when running-ahead transaction kicked it when receiving ACK1. */
                dprintf("cm_poll,kick progress engine for %d\n", req->responder_rank);
                MPID_nem_ib_send_progress(MPID_nem_ib_conns[req->responder_rank].vc);
                /* Let the following connection request go */
                VC_FIELD(MPID_nem_ib_conns[req->responder_rank].vc, connection_guard) = 0;
                /* Call cm_request_release twice.
                 * If ref_count == 2, the memory of request is released here.
                 * If ref_count == 3, the memory of request will be released on draining SCQ of SYN. */
                MPID_nem_ib_cm_request_release(req);
                MPID_nem_ib_cm_request_release(req);

                /* Explicitly release CAS word because
                 * ConnectX-3 doesn't support safe CAS with PCI device and CPU */
                MPID_nem_ib_cm_cas_release(MPID_nem_ib_conns[req->responder_rank].vc);
                break;
            }
        case MPID_NEM_IB_CM_ACK1:{
                volatile MPID_nem_ib_cm_cmd_ack1_t *ack1_tail_flag =
                    (MPID_nem_ib_cm_cmd_ack1_t *) slot;
                while (ack1_tail_flag->tail_flag.tail_flag != MPID_NEM_IB_COM_MAGIC) {
                    /* __asm__ __volatile__("pause;":::"memory"); */
                }

                MPID_nem_ib_cm_cmd_ack1_t *ack1 = (MPID_nem_ib_cm_cmd_ack1_t *) slot;
                MPID_nem_ib_cm_req_t *req = (MPID_nem_ib_cm_req_t *) ack1->responder_req;
                req->ibcom->incoming_connection_tx -= 1;        /* ACK1 */
                dprintf("cm_poll,ack1 detected!,%d->%d[%d],responder_req=%p,tx=%d\n",
                        req->initiator_rank, MPID_nem_ib_myrank, i,
                        ack1->responder_req, req->ibcom->outstanding_connection_tx);
                /* Deduct it from the packet */
                VC_FIELD(MPID_nem_ib_conns[req->initiator_rank].vc,
                         connection_state) |=
                    (MPID_NEM_IB_CM_REMOTE_QP_RESET | MPID_NEM_IB_CM_REMOTE_QP_RTS);
                /* Skip QP createion on race condition */
                if (!
                    (VC_FIELD
                     (MPID_nem_ib_conns[req->initiator_rank].vc,
                      connection_state) & MPID_NEM_IB_CM_LOCAL_QP_RTS)) {
                    ibcom_errno =
                        MPID_nem_ib_com_rts(MPID_nem_ib_conns[req->initiator_rank].fd,
                                            ack1->qpnum, ack1->lid, &(ack1->gid));
                    MPIU_ERR_CHKANDJUMP(ibcom_errno, mpi_errno, MPI_ERR_OTHER,
                                        "**MPID_nem_ib_com_rts");
                    /* Connect ring buffer */
                    ibcom_errno =
                        MPID_nem_ib_com_connect_ringbuf(MPID_nem_ib_conns
                                                        [req->initiator_rank].fd,
                                                        ack1->ringbuf_type, ack1->rmem,
                                                        ack1->rkey, ack1->ringbuf_nslot,
                                                        ack1->remote_vc, 1);
                    MPIU_ERR_CHKANDJUMP(ibcom_errno, mpi_errno, MPI_ERR_OTHER,
                                        "**MPID_nem_ib_com_connect_ringbuf");
                    dprintf("connect_ringbuf,%d-%d=%d\n",
                            VC_FIELD(MPID_nem_ib_conns[req->initiator_rank].vc,
                                     ibcom->sseq_num),
                            VC_FIELD(MPID_nem_ib_conns[req->initiator_rank].vc,
                                     ibcom->lsr_seq_num_tail),
                            MPID_nem_ib_diff16(VC_FIELD
                                               (MPID_nem_ib_conns[req->initiator_rank].vc,
                                                ibcom->sseq_num),
                                               VC_FIELD(MPID_nem_ib_conns
                                                        [req->initiator_rank].vc,
                                                        ibcom->lsr_seq_num_tail))
);
                    MPID_nem_ib_vc_onconnect(MPID_nem_ib_conns[req->initiator_rank].vc);
                    /* Record state transition for race condition detection */
                    VC_FIELD(MPID_nem_ib_conns[req->initiator_rank].vc,
                             connection_state) |= MPID_NEM_IB_CM_LOCAL_QP_RTS;
                }

                req->state = MPID_NEM_IB_CM_ACK2;
                if (MPID_nem_ib_ncqe_scratch_pad < MPID_NEM_IB_COM_MAX_CQ_CAPACITY &&
                    req->ibcom->ncom_scratch_pad < MPID_NEM_IB_COM_MAX_SQ_CAPACITY) {

                    dprintf
                        ("cm_poll,sending ack2,req=%p,ringbuf_index=%d,initiator_rank=%d,tx=%d\n",
                         req, req->ringbuf_index, req->initiator_rank,
                         req->ibcom->outstanding_connection_tx);
                    MPID_nem_ib_cm_cmd_ack2_t *cmd =
                        (MPID_nem_ib_cm_cmd_ack2_t *) req->ibcom->
                        icom_mem[MPID_NEM_IB_COM_SCRATCH_PAD_FROM];
                    MPID_NEM_IB_CM_COMPOSE_ACK2(cmd, ack1->initiator_req);
                    MPID_nem_ib_cm_cmd_shadow_t *shadow = (MPID_nem_ib_cm_cmd_shadow_t *)
                        MPIU_Malloc(sizeof(MPID_nem_ib_cm_cmd_shadow_t));
                    shadow->type = req->state;
                    shadow->req = req;
                    dprintf("shadow=%p,shadow->req=%p\n", shadow, shadow->req);
                    mpi_errno =
                        MPID_nem_ib_cm_cmd_core(req->initiator_rank, shadow, (void *) cmd,
                                                sizeof(MPID_nem_ib_cm_cmd_ack2_t), 0,
                                                req->ringbuf_index);
                    MPIU_ERR_CHKANDJUMP(mpi_errno, mpi_errno, MPI_ERR_OTHER,
                                        "**MPID_nem_ib_cm_send_core");
                }
                else {
                    MPID_NEM_IB_CM_COMPOSE_ACK2((MPID_nem_ib_cm_cmd_ack2_t *) &
                                                (req->cmd), ack1->initiator_req);
                    MPID_nem_ib_cm_sendq_enqueue(&MPID_nem_ib_cm_sendq, req);
                }

                /* cm_release calls cm_progress, so we have to clear scratch_pad here. */
                *head_flag = MPID_NEM_IB_CM_HEAD_FLAG_ZERO;     /* Clear head-flag */
                /* Clear all possible tail-flag slots */
                MPID_NEM_IB_CM_CLEAR_TAIL_FLAGS(slot);
                /* The responder release the slot for initiator */
                MPID_nem_ib_cm_release(req->initiator_ringbuf_index);
                /* Kick ask-send commands waiting for connection */
                MPID_nem_ib_ringbuf_progress();
                /* Kick send commands waiting for connection.
                 * This might be a dupe when running-ahead transaction kicked it when receiving ACK2. */
                dprintf("cm_poll,kick progress engine for %d\n", req->initiator_rank);
                MPID_nem_ib_send_progress(MPID_nem_ib_conns[req->initiator_rank].vc);
            }
            //goto common_tail;
            break;
        case MPID_NEM_IB_CM_ACK2:{
                volatile MPID_nem_ib_cm_cmd_ack2_t *ack2_tail_flag =
                    (MPID_nem_ib_cm_cmd_ack2_t *) slot;
                while (ack2_tail_flag->tail_flag.tail_flag != MPID_NEM_IB_COM_MAGIC) {
                    /* __asm__ __volatile__("pause;":::"memory"); */
                }
                MPID_nem_ib_cm_cmd_ack2_t *ack2 = (MPID_nem_ib_cm_cmd_ack2_t *) slot;
                MPID_nem_ib_cm_req_t *req = (MPID_nem_ib_cm_req_t *) ack2->initiator_req;
                dprintf("cm_poll,ack2 detected!,%d->%d[%d],connection_tx=%d\n",
                        req->responder_rank, MPID_nem_ib_myrank, i,
                        req->ibcom->outstanding_connection_tx);
                req->ibcom->incoming_connection_tx -= 1;        /* ACK2 */
                /* Deduct it from the packet */
                if (!
                    (VC_FIELD
                     (MPID_nem_ib_conns[req->responder_rank].vc,
                      connection_state) & MPID_NEM_IB_CM_REMOTE_QP_RTS)) {
                    MPID_nem_ib_vc_onconnect(MPID_nem_ib_conns[req->responder_rank].vc);
                    /* Record state transition for race condition detection */
                    VC_FIELD(MPID_nem_ib_conns[req->responder_rank].vc,
                             connection_state) |= MPID_NEM_IB_CM_REMOTE_QP_RTS;
                }

                /* cm_release calls cm_progress, so we have to clear scratch_pad here. */
                *head_flag = MPID_NEM_IB_CM_HEAD_FLAG_ZERO;     /* Clear head-flag */
                /* Clear all possible tail-flag slots */
                MPID_NEM_IB_CM_CLEAR_TAIL_FLAGS(slot);
                /* The initiator release the slot for responder */
                MPID_nem_ib_cm_release(req->responder_ringbuf_index);
                /* Acquire ring-buffer slot now that it's connected if requested so */
                if (req->ask_on_connect &&
                    VC_FIELD(MPID_nem_ib_conns[req->responder_rank].vc,
                             ibcom->local_ringbuf_type) == MPID_NEM_IB_RINGBUF_SHARED) {
                    dprintf("cm_poll,ack2,ask on connect\n");
                    mpi_errno =
                        MPID_nem_ib_ringbuf_ask_fetch(MPID_nem_ib_conns[req->responder_rank].vc);
                    MPIU_ERR_CHKANDJUMP(mpi_errno, mpi_errno, MPI_ERR_OTHER,
                                        "**MPID_nem_ib_ringbuf_ask_fetch");
                }

                /* Kick ask-send commands waiting for connection */
                MPID_nem_ib_ringbuf_progress();
                /* Kick send commands waiting for connection.
                 * This might be a dupe when running-ahead transaction kicked it when receiving ACK1. */
                dprintf("cm_poll,kick progress engine for %d\n", req->responder_rank);
                MPID_nem_ib_send_progress(MPID_nem_ib_conns[req->responder_rank].vc);
                /* Let the following connection request go */
                VC_FIELD(MPID_nem_ib_conns[req->responder_rank].vc, connection_guard) = 0;
                /* Finalize protocol because there is no referer in cm_poll and sendq.
                 * Note that there might be one which sent ACK1 in cm_drain_scq. */
                MPID_nem_ib_cm_request_release(req);
            }
            //common_tail:
            //*head_flag = MPID_NEM_IB_CM_HEAD_FLAG_ZERO; /* Clear head-flag */
            ///* Clear all possible tail-flag slots */
            //MPID_NEM_IB_CM_CLEAR_TAIL_FLAGS(slot);
            break;
        default:
            printf("unknown connection command\n");
            MPIU_ERR_CHKANDJUMP(1, mpi_errno, MPI_ERR_OTHER, "**MPID_nem_ib_cm_poll");
            break;
        }
    }

  fn_exit:
    MPIDI_FUNC_EXIT(MPID_STATE_MPID_NEM_IB_CM_POLL);
    return mpi_errno;
  fn_fail:
    goto fn_exit;
}

#endif

#undef FUNCNAME
#define FUNCNAME MPID_nem_ib_ringbuf_alloc
#undef FCNAME
#define FCNAME MPIDI_QUOTE(FUNCNAME)
int MPID_nem_ib_ringbuf_alloc(MPIDI_VC_t * vc)
{
    int mpi_errno = MPI_SUCCESS;
    int i;
    MPIDI_STATE_DECL(MPID_STATE_MPID_NEM_IB_RINGBUF_ALLOC);
    MPIDI_FUNC_ENTER(MPID_STATE_MPID_NEM_IB_RINGBUF_ALLOC);
    if (!MPID_nem_ib_ringbuf) {
        MPID_nem_ib_ringbuf = MPIU_Calloc(1, sizeof(MPID_nem_ib_ringbuf_t) * MPID_NEM_IB_NRINGBUF);
        MPIU_ERR_CHKANDJUMP(!MPID_nem_ib_ringbuf, mpi_errno, MPI_ERR_OTHER, "**malloc");
    }

#if 0   /* Degug, "#if 1" to make exclusive ring-buffers not available */
    //if (MPID_nem_ib_myrank == 0) {
    for (i = 0; i < MPID_NEM_IB_NRINGBUF - 1; i++) {
        MPID_nem_ib_ringbuf_acquired[i / 64] |= (1ULL << (i & 63));
    }
    //}
#endif

    int found = 0;
    /* [MPID_NEM_IB_NRINGBUF-1] holds shared ring buffer */
    for (i = 0; i < MPID_NEM_IB_NRINGBUF - 1; i++) {
        if (((MPID_nem_ib_ringbuf_acquired[i / 64] >> (i & 63)) & 1) == 0) {
            found = 1;
            break;
        }
    }

    if (found) {
        MPID_nem_ib_ringbuf_acquired[i / 64] |= (1ULL << (i & 63));
        if (!MPID_nem_ib_ringbuf[i].start) {
            MPID_nem_ib_ringbuf[i].type = MPID_NEM_IB_RINGBUF_EXCLUSIVE;
            MPID_nem_ib_ringbuf[i].start = MPID_nem_ib_rdmawr_to_alloc(MPID_NEM_IB_RINGBUF_NSLOT);
            MPIU_ERR_CHKANDJUMP(!MPID_nem_ib_ringbuf[i].start, mpi_errno,
                                MPI_ERR_OTHER, "**MPID_nem_ib_rdma_to_alloc");
            MPID_nem_ib_ringbuf[i].nslot = MPID_NEM_IB_RINGBUF_NSLOT;
            memset(MPID_nem_ib_ringbuf[i].remote_released, 0,
                   (MPID_NEM_IB_RINGBUF_NSLOT + 63) / 64);
            MPID_nem_ib_ringbuf_allocated[i / 64] |= (1ULL << (i & 63));
        }
        VC_FIELD(vc, ibcom->remote_ringbuf) = &MPID_nem_ib_ringbuf[i];
        dprintf("ringbuf_alloc,start=%p\n", MPID_nem_ib_ringbuf[i].start);
        VC_FIELD(vc, ibcom->rsr_seq_num_poll) = 0;
        VC_FIELD(vc, ibcom->rsr_seq_num_tail) = -1;
        VC_FIELD(vc, ibcom->rsr_seq_num_tail_last_sent) = -1;
        MPID_nem_ib_ringbuf[i].vc = vc;
        dprintf
            ("ringbuf_alloc,i=%d,pg_rank=%d,ibcom=%p,ibcom->remote_ringbuf=%p\n",
             i, vc->pg_rank, VC_FIELD(vc, ibcom), VC_FIELD(vc, ibcom->remote_ringbuf));
    }
    else {
        if (!MPID_nem_ib_ringbuf[i].start) {
            MPID_nem_ib_ringbuf[i].type = MPID_NEM_IB_RINGBUF_SHARED;
            MPID_nem_ib_ringbuf[i].start = MPID_nem_ib_rdmawr_to_alloc(MPID_NEM_IB_RINGBUF_NSLOT);
            MPIU_ERR_CHKANDJUMP(!MPID_nem_ib_ringbuf[i].start, mpi_errno,
                                MPI_ERR_OTHER, "**MPID_nem_ib_rdma_to_alloc");
            MPID_nem_ib_ringbuf[i].nslot = MPID_NEM_IB_RINGBUF_NSLOT;
            memset(MPID_nem_ib_ringbuf[i].remote_released, 0,
                   (MPID_NEM_IB_RINGBUF_NSLOT + 63) / 64);
            MPID_nem_ib_ringbuf_allocated[i / 64] |= (1ULL << (i & 63));
        }
        MPID_nem_ib_ringbuf[i].ref_count++;
        VC_FIELD(vc, ibcom->remote_ringbuf) = &MPID_nem_ib_ringbuf[i];
        dprintf("ringbuf_alloc,not found\n");
    }

  fn_exit:
    MPIDI_FUNC_EXIT(MPID_STATE_MPID_NEM_IB_RINGBUF_ALLOC);
    return mpi_errno;
  fn_fail:
    goto fn_exit;
}

#undef FUNCNAME
#define FUNCNAME MPID_nem_ib_ringbuf_free
#undef FCNAME
#define FCNAME MPIDI_QUOTE(FUNCNAME)
int MPID_nem_ib_ringbuf_free(MPIDI_VC_t * vc)
{
    int mpi_errno = MPI_SUCCESS;
    int i;
    MPIDI_STATE_DECL(MPID_STATE_MPID_NEM_IB_RINGBUF_FREE);
    MPIDI_FUNC_ENTER(MPID_STATE_MPID_NEM_IB_RINGBUF_FREE);
    /* No ring-buffer is allocated */
    if (!VC_FIELD(vc, ibcom->remote_ringbuf)) {
        goto fn_exit;
    }

    int index =
        ((uint8_t *) VC_FIELD(vc, ibcom->remote_ringbuf) -
         (uint8_t *) & MPID_nem_ib_ringbuf[0]) / sizeof(MPID_nem_ib_ringbuf_t);
    dprintf("ringbuf_free,index=%d\n", index);
    switch (VC_FIELD(vc, ibcom->remote_ringbuf)->type) {
    case MPID_NEM_IB_RINGBUF_EXCLUSIVE:
        dprintf("ringbuf_free,start=%p\n", VC_FIELD(vc, ibcom->remote_ringbuf)->start);
        MPID_nem_ib_rdmawr_to_free(VC_FIELD(vc, ibcom->remote_ringbuf)->start,
                                   MPID_NEM_IB_RINGBUF_NSLOT);
        VC_FIELD(vc, ibcom->remote_ringbuf)->start = NULL;      /* initialize for re-allocate */
        MPID_nem_ib_ringbuf_allocated[index / 64] &= ~(1ULL << (index & 63));
        VC_FIELD(vc, ibcom->remote_ringbuf) = NULL;
        MPID_nem_ib_ringbuf_acquired[index / 64] &= ~(1ULL << (index & 63));
        dprintf("ringbuf_free,exclucsive,allocated=%0lx\n",
                MPID_nem_ib_ringbuf_allocated[index / 64]);
        break;
    case MPID_NEM_IB_RINGBUF_SHARED:
        dprintf("ringbuf_free,shared,ref_count=%d\n",
                VC_FIELD(vc, ibcom->remote_ringbuf)->ref_count);
        MPIU_Assert(VC_FIELD(vc, ibcom->remote_ringbuf)->ref_count > 0);
        if (--VC_FIELD(vc, ibcom->remote_ringbuf)->ref_count == 0) {
            MPID_nem_ib_rdmawr_to_free(VC_FIELD(vc, ibcom->remote_ringbuf)->start,
                                       MPID_NEM_IB_RINGBUF_NSLOT);
            VC_FIELD(vc, ibcom->remote_ringbuf)->start = NULL;  /* initialize for re-allocate */
            MPID_nem_ib_ringbuf_allocated[index / 64] &= ~(1ULL << (index & 63));
            dprintf("ringbuf_free,shared,allocated=%0lx\n",
                    MPID_nem_ib_ringbuf_allocated[index / 64]);
        }
        VC_FIELD(vc, ibcom->remote_ringbuf) = NULL;
    default:
        printf("unknown ring-buffer type\n");
        MPIU_ERR_CHKANDJUMP(1, mpi_errno, MPI_ERR_OTHER, "**MPID_nem_ib_ringbuf_free");
        break;
    }

    int found = 0;
    for (i = 0; i < (MPID_NEM_IB_NRINGBUF + 63) / 64; i++) {
        if (MPID_nem_ib_ringbuf_allocated[i] != 0) {
            found = 1;
            break;
        }
    }

    if (!found) {
        MPIU_Free(MPID_nem_ib_ringbuf);
        MPID_nem_ib_ringbuf = NULL;
    }

  fn_exit:
    MPIDI_FUNC_EXIT(MPID_STATE_MPID_NEM_IB_RINGBUF_FREE);
    return mpi_errno;
  fn_fail:
    goto fn_exit;
}
