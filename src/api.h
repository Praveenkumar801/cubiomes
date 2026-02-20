#ifndef API_H_
#define API_H_

#include <microhttpd.h>

/*
 * MHD access-handler callback (see MHD_AccessHandlerCallback).
 * Register this with MHD_start_daemon().
 */
enum MHD_Result handle_request(void *cls,
                                struct MHD_Connection *connection,
                                const char            *url,
                                const char            *method,
                                const char            *version,
                                const char            *upload_data,
                                size_t                *upload_data_size,
                                void                 **con_cls);

/*
 * MHD request-completed callback.
 * Register this via MHD_OPTION_NOTIFY_COMPLETED.
 */
void cleanup_request(void *cls, struct MHD_Connection *connection,
                     void **con_cls, enum MHD_RequestTerminationCode toe);

#endif /* API_H_ */
