/*
* Copyright (C) ducla@uiza.io 2019
*/
#include <ngx_config.h>
#include <ngx_core.h>
#include <ngx_rtmp.h>
#include <ngx_rtmp_codec_module.h>
#include <ngx_rtmp_cmd_module.h>
#include "ngx_rtmp_transcode_module.h"
#include "ngx_rtmp_notify_module.h"

static ngx_rtmp_publish_pt              next_publish;
static ngx_rtmp_close_stream_pt         next_close_stream;
static ngx_rtmp_stream_begin_pt         next_stream_begin;
static ngx_rtmp_stream_eof_pt           next_stream_eof;

static ngx_int_t ngx_rtmp_transcode_postconfiguration(ngx_conf_t *cf);
static void * ngx_rtmp_transcode_create_app_conf(ngx_conf_t *cf);
static char * ngx_rtmp_transcode_merge_app_conf(ngx_conf_t *cf,
       void *parent, void *child);
char* ngx_rtmp_transcode_limit_bandwidth(ngx_conf_t *cf, ngx_command_t *cmd, void *conf);
static ngx_int_t
ngx_rtmp_transcode_merge_limit_ingest(ngx_array_t *prev, ngx_array_t *ingest_limit);


#define NGX_RTMP_TRANSCODE_NAMING_SEQUENTIAL  1
#define NGX_RTMP_TRANSCODE_NAMING_TIMESTAMP   2
#define NGX_RTMP_TRANSCODE_NAMING_SYSTEM      3
#define NGX_RTMP_TRANSCODE_DIR_ACCESS        0744

static ngx_conf_enum_t                  ngx_rtmp_transcode_naming_slots[] = {
    { ngx_string("sequential"),         NGX_RTMP_TRANSCODE_NAMING_SEQUENTIAL },
    { ngx_string("timestamp"),          NGX_RTMP_TRANSCODE_NAMING_TIMESTAMP  },
    { ngx_string("system"),             NGX_RTMP_TRANSCODE_NAMING_SYSTEM     },
    { ngx_null_string,                  0 }
};

typedef struct {
    ngx_uint_t value;
} ngx_rtmp_limit_bandwidth_t;

typedef struct {
    ngx_flag_t                          transcode;
    ngx_str_t                           path;
    ngx_flag_t                          nested;
    ngx_msec_t                          fraglen;
    ngx_msec_t                          playlen;    
    ngx_str_t                           format;
    ngx_uint_t                          naming;
    ngx_flag_t                          cleanup;
    ngx_flag_t                          dvr;
    ngx_str_t                           dvr_path;
    ngx_flag_t                          hide_stream_key;
    ngx_array_t                         limit_ingest;
} ngx_rtmp_transcode_app_conf_t;


static ngx_command_t ngx_rtmp_transcode_commands[] = {
    { ngx_string("transcode"),
      NGX_RTMP_MAIN_CONF|NGX_RTMP_SRV_CONF|NGX_RTMP_APP_CONF|NGX_CONF_TAKE1,
      ngx_conf_set_flag_slot,
      NGX_RTMP_APP_CONF_OFFSET,
      offsetof(ngx_rtmp_transcode_app_conf_t, transcode),
      NULL },
      { ngx_string("transcode_path"),
      NGX_RTMP_MAIN_CONF|NGX_RTMP_SRV_CONF|NGX_RTMP_APP_CONF|NGX_CONF_TAKE1,
      ngx_conf_set_str_slot,
      NGX_RTMP_APP_CONF_OFFSET,
      offsetof(ngx_rtmp_transcode_app_conf_t, path),
      NULL },
      { ngx_string("transcode_nested"),
      NGX_RTMP_MAIN_CONF|NGX_RTMP_SRV_CONF|NGX_RTMP_APP_CONF|NGX_CONF_TAKE1,
      ngx_conf_set_flag_slot,
      NGX_RTMP_APP_CONF_OFFSET,
      offsetof(ngx_rtmp_transcode_app_conf_t, nested),
      NULL },
      { ngx_string("transcode_fragment"),
      NGX_RTMP_MAIN_CONF|NGX_RTMP_SRV_CONF|NGX_RTMP_APP_CONF|NGX_CONF_TAKE1,
      ngx_conf_set_msec_slot,
      NGX_RTMP_APP_CONF_OFFSET,
      offsetof(ngx_rtmp_transcode_app_conf_t, fraglen),
      NULL },
      { ngx_string("transcode_playlist"),
      NGX_RTMP_MAIN_CONF|NGX_RTMP_SRV_CONF|NGX_RTMP_APP_CONF|NGX_CONF_TAKE1,
      ngx_conf_set_msec_slot,
      NGX_RTMP_APP_CONF_OFFSET,
      offsetof(ngx_rtmp_transcode_app_conf_t, playlen),
      NULL },
      { ngx_string("transcode_format"),
      NGX_RTMP_MAIN_CONF|NGX_RTMP_SRV_CONF|NGX_RTMP_APP_CONF|NGX_CONF_TAKE1,
      ngx_conf_set_str_slot,
      NGX_RTMP_APP_CONF_OFFSET,
      offsetof(ngx_rtmp_transcode_app_conf_t, format),
      NULL },
      { ngx_string("transcode_fragment_naming"),
      NGX_RTMP_MAIN_CONF|NGX_RTMP_SRV_CONF|NGX_RTMP_APP_CONF|NGX_CONF_TAKE1,
      ngx_conf_set_enum_slot,
      NGX_RTMP_APP_CONF_OFFSET,
      offsetof(ngx_rtmp_transcode_app_conf_t, naming),
      &ngx_rtmp_transcode_naming_slots },
      { ngx_string("transcode_cleanup"),
      NGX_RTMP_MAIN_CONF|NGX_RTMP_SRV_CONF|NGX_RTMP_APP_CONF|NGX_CONF_TAKE1,
      ngx_conf_set_flag_slot,
      NGX_RTMP_APP_CONF_OFFSET,
      offsetof(ngx_rtmp_transcode_app_conf_t, cleanup),
      NULL },
      { ngx_string("transcode_dvr"),
      NGX_RTMP_MAIN_CONF|NGX_RTMP_SRV_CONF|NGX_RTMP_APP_CONF|NGX_CONF_TAKE1,
      ngx_conf_set_flag_slot,
      NGX_RTMP_APP_CONF_OFFSET,
      offsetof(ngx_rtmp_transcode_app_conf_t, dvr),
      NULL },
      { ngx_string("transcode_dvr_path"),
      NGX_RTMP_MAIN_CONF|NGX_RTMP_SRV_CONF|NGX_RTMP_APP_CONF|NGX_CONF_TAKE1,
      ngx_conf_set_str_slot,
      NGX_RTMP_APP_CONF_OFFSET,
      offsetof(ngx_rtmp_transcode_app_conf_t, dvr_path),
      NULL },
      { ngx_string("transcode_hide_stream_key"),
      NGX_RTMP_MAIN_CONF|NGX_RTMP_SRV_CONF|NGX_RTMP_APP_CONF|NGX_CONF_TAKE1,
      ngx_conf_set_flag_slot,
      NGX_RTMP_APP_CONF_OFFSET,
      offsetof(ngx_rtmp_transcode_app_conf_t, hide_stream_key),
      NULL },
      { ngx_string("transcode_limit_bandwidth"),
      NGX_RTMP_MAIN_CONF|NGX_RTMP_SRV_CONF|NGX_RTMP_APP_CONF|NGX_CONF_1MORE,
      ngx_rtmp_transcode_limit_bandwidth,
      NGX_RTMP_APP_CONF_OFFSET,
      0,
      NULL
      },
      ngx_null_command
};

static ngx_rtmp_module_t  ngx_rtmp_transcode_module_ctx = {
    NULL,                               /* preconfiguration */
    ngx_rtmp_transcode_postconfiguration,    /* postconfiguration */

    NULL,                               /* create main configuration */
    NULL,                               /* init main configuration */

    NULL,                               /* create server configuration */
    NULL,                               /* merge server configuration */

    ngx_rtmp_transcode_create_app_conf,      /* create location configuration */
    ngx_rtmp_transcode_merge_app_conf,       /* merge location configuration */
};

ngx_module_t  ngx_rtmp_transcode_module = {
    NGX_MODULE_V1,
    &ngx_rtmp_transcode_module_ctx,          /* module context */
    ngx_rtmp_transcode_commands,             /* module directives */
    NGX_RTMP_MODULE,                    /* module type */
    NULL,                               /* init master */
    NULL,                               /* init module */
    NULL,                               /* init process */
    NULL,                               /* init thread */
    NULL,                               /* exit thread */
    NULL,                               /* exit process */
    NULL,                               /* exit master */
    NGX_MODULE_V1_PADDING
};



static void *
ngx_rtmp_transcode_create_app_conf(ngx_conf_t *cf)
{
    ngx_rtmp_transcode_app_conf_t *conf;

    conf = ngx_pcalloc(cf->pool, sizeof(ngx_rtmp_transcode_app_conf_t));
    if (conf == NULL) {
        return NULL;
    }

    conf->transcode = NGX_CONF_UNSET;
    conf->fraglen = NGX_CONF_UNSET_MSEC;
    conf->playlen = NGX_CONF_UNSET_MSEC;
    conf->cleanup = NGX_CONF_UNSET;
    conf->nested = NGX_CONF_UNSET;
    conf->naming = NGX_CONF_UNSET_UINT;
    conf->cleanup = NGX_CONF_UNSET;
    conf->dvr = NGX_CONF_UNSET;
    conf->hide_stream_key = NGX_CONF_UNSET;
    if (ngx_array_init(&conf->limit_ingest, cf->pool, 1,
                       sizeof(ngx_rtmp_limit_bandwidth_t))
        != NGX_OK)
    {
        return NULL;
    }

    return conf;
}

static ngx_int_t
ngx_rtmp_transcode_merge_limit_ingest(ngx_array_t *prev, ngx_array_t *ingest_limit)
{
    void   *p;

    if (prev->nelts == 0) {
        return NGX_OK;
    }

    if (ingest_limit->nelts == 0) {
        *ingest_limit = *prev;
        return NGX_OK;
    }

    p = ngx_array_push_n(ingest_limit, prev->nelts);
    if (p == NULL) {
        return NGX_ERROR;
    }

    ngx_memcpy(p, prev->elts, prev->size * prev->nelts);

    return NGX_OK;
}

static char *
ngx_rtmp_transcode_merge_app_conf(ngx_conf_t *cf, void *parent, void *child)
{
    ngx_rtmp_transcode_app_conf_t    *prev = parent;
    ngx_rtmp_transcode_app_conf_t    *conf = child;

    ngx_conf_merge_value(conf->transcode, prev->transcode, 0);
    ngx_conf_merge_msec_value(conf->fraglen, prev->fraglen, 5000);
    ngx_conf_merge_msec_value(conf->playlen, prev->playlen, 30000);
    ngx_conf_merge_value(conf->cleanup, prev->cleanup, 1);
    ngx_conf_merge_value(conf->nested, prev->nested, 0);
    ngx_conf_merge_value(conf->dvr, prev->dvr, 0);
    ngx_conf_merge_value(conf->hide_stream_key, prev->hide_stream_key, 0);

    ngx_conf_merge_str_value(conf->path, prev->path, "");
    ngx_conf_merge_str_value(conf->dvr_path, prev->dvr_path, "");
    ngx_conf_merge_uint_value(conf->naming, prev->naming,
                              NGX_RTMP_TRANSCODE_NAMING_SEQUENTIAL);
    ngx_conf_merge_str_value(conf->format, prev->format, "fmp4");
    if (ngx_rtmp_transcode_merge_limit_ingest(&prev->limit_ingest, &conf->limit_ingest) != NGX_OK) {
        return NGX_CONF_ERROR;
    }

    return NGX_CONF_OK;
}



static ngx_int_t
ngx_rtmp_transcode_publish(ngx_rtmp_session_t *s, ngx_rtmp_publish_t *v)
{

    return next_publish(s, v);
}

static ngx_int_t
ngx_rtmp_transcode_close_stream(ngx_rtmp_session_t *s, ngx_rtmp_close_stream_t *v)
{
    return next_close_stream(s, v);
}

static ngx_int_t
ngx_rtmp_transcode_stream_begin(ngx_rtmp_session_t *s, ngx_rtmp_stream_begin_t *v)
{    
    return next_stream_begin(s, v);
}

static ngx_int_t
ngx_rtmp_transcode_stream_eof(ngx_rtmp_session_t *s, ngx_rtmp_stream_eof_t *v)
{    
    return next_stream_eof(s, v);
}

static ngx_int_t
ngx_rtmp_transcode_video(ngx_rtmp_session_t *s, ngx_rtmp_header_t *h,
    ngx_chain_t *in)
{
    ngx_rtmp_codec_ctx_t            *codec_ctx;
    ngx_uint_t                      video_rate;
    ngx_rtmp_transcode_app_conf_t   *tscf;
    ngx_rtmp_limit_bandwidth_t      *limits;
    ngx_rtmp_notify_ctx_t           *notify_ctx;
    ngx_str_t                       *stier;
    size_t                          tier;

    tscf = ngx_rtmp_get_module_app_conf(s, ngx_rtmp_transcode_module);

    notify_ctx = ngx_rtmp_get_module_ctx(s, ngx_rtmp_notify_module);
    if(notify_ctx->params->nelts > 1){
        stier = notify_ctx->params->elts;
        tier = ngx_atoi(stier[1].data, stier[1].len);        
    }else{
        tier = 0;
    }
    if(tier != 0 && tier <= tscf->limit_ingest.nelts){
        limits = tscf->limit_ingest.elts;
        if(tscf->limit_ingest.nelts > 0){
            codec_ctx = ngx_rtmp_get_module_ctx(s, ngx_rtmp_codec_module);
            if(codec_ctx != NULL){
                video_rate = codec_ctx->video_data_rate;
                if(video_rate > limits[tier-1].value){
                    ngx_log_error(NGX_LOG_ERR, s->connection->log, 0,
                            "transcode: Video rate is over limit!");
                    return NGX_ERROR;
                }
            }else{
                ngx_log_error(NGX_LOG_ERR, s->connection->log, 0,
                            "transcode: No video rate");
            }  
        }   
    }    
    return NGX_OK;
}

char* ngx_rtmp_transcode_limit_bandwidth(ngx_conf_t *cf, ngx_command_t *cmd, void *conf)
{
    ngx_rtmp_transcode_app_conf_t   *tscf = conf;
    ngx_str_t                       *value;
    ngx_uint_t                      i;
    ngx_uint_t                      v;
    ngx_rtmp_limit_bandwidth_t      *vbt;
    value = cf->args->elts;
    for(i = 1; i < cf->args->nelts; i++){
        v = ngx_atoi(value[i].data, value[i].len);
        if(v == NGX_ERROR){
            return NGX_CONF_ERROR;
        }
        vbt = ngx_array_push(&tscf->limit_ingest);
        if(vbt == NULL){
            return NGX_CONF_ERROR;
        }
        vbt->value = v;
    }
    return NGX_CONF_OK;
}

static ngx_int_t
ngx_rtmp_transcode_postconfiguration(ngx_conf_t *cf)
{
    ngx_rtmp_handler_pt        *h;
    ngx_rtmp_core_main_conf_t  *cmcf;

    cmcf = ngx_rtmp_conf_get_module_main_conf(cf, ngx_rtmp_core_module);
    h = ngx_array_push(&cmcf->events[NGX_RTMP_MSG_VIDEO]);
    *h = ngx_rtmp_transcode_video;

    next_publish = ngx_rtmp_publish;
    ngx_rtmp_publish = ngx_rtmp_transcode_publish;

    next_close_stream = ngx_rtmp_close_stream;
    ngx_rtmp_close_stream = ngx_rtmp_transcode_close_stream;

    next_stream_begin = ngx_rtmp_stream_begin;
    ngx_rtmp_stream_begin = ngx_rtmp_transcode_stream_begin;

    next_stream_eof = ngx_rtmp_stream_eof;
    ngx_rtmp_stream_eof = ngx_rtmp_transcode_stream_eof;

    return NGX_OK;
}