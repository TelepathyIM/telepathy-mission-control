#ifndef _MCD_FILTERS_INT_H
#define _MCD_FILTERS_INT_H

#include "mcd-filtering.h"

/* Internals of the filter-processing mechanism */

void dispose_state_machine_data (mcdf_context_t sm_ctx);
void start_channel_handler (mcdf_context_t ctx);
void dispose_channel_request (ChanHandlerReq * request);
void drop_channel_handler (mcdf_context_t ctx);
void enter_state_machine (guint request_id, ChanHandlerReq * chan_req);




#endif
