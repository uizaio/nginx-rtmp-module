/*
 * To change this license header, choose License Headers in Project Properties.
 * To change this template file, choose Tools | Templates
 * and open the template in the editor.
 */

/* 
 * File:   ngx_rtmp_notify_module.h
 * Author: ducla
 *
 * Created on September 26, 2019, 3:53 PM
 */

#ifndef NGX_RTMP_NOTIFY_MODULE_H
#define NGX_RTMP_NOTIFY_MODULE_H

#include <ngx_config.h>
#include <ngx_core.h>
#include "ngx_rtmp_cmd_module.h"

typedef struct {
    ngx_uint_t                                  flags;
    u_char                                      name[NGX_RTMP_MAX_NAME];
    u_char                                      args[NGX_RTMP_MAX_ARGS];
    ngx_event_t                                 update_evt;
    time_t                                      start;
    ngx_str_t                                   stream_id;//stream id of live entity
} ngx_rtmp_notify_ctx_t;

#endif /* NGX_RTMP_NOTIFY_MODULE_H */

