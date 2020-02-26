#ifndef NGX_RTMP_NOTIFY_MODULE_H
#define NGX_RTMP_NOTIFY_MODULE_H

typedef struct {
    ngx_uint_t                                  flags;
    u_char                                      name[NGX_RTMP_MAX_NAME];
    u_char                                      args[NGX_RTMP_MAX_ARGS];
    ngx_event_t                                 update_evt;
    time_t                                      start;
    ngx_array_t                                 *params;
} ngx_rtmp_notify_ctx_t;

extern ngx_module_t  ngx_rtmp_notify_module;

#endif /* NGX_RTMP_NOTIFY_MODULE_H */