#include <ngx_config.h>
#include <ngx_rtmp.h>
#include <ngx_rtmp_cmd_module.h>
#include <ngx_rtmp_codec_module.h>
#include "ngx_rtmp_fmp4.h"


static ngx_rtmp_publish_pt              next_publish;
static ngx_rtmp_close_stream_pt         next_close_stream;
static ngx_rtmp_stream_begin_pt         next_stream_begin;
static ngx_rtmp_stream_eof_pt           next_stream_eof;


static void * ngx_rtmp_fmp4_create_app_conf(ngx_conf_t *cf);
static char * ngx_rtmp_fmp4_merge_app_conf(ngx_conf_t *cf, void *parent, void *child);
static ngx_int_t ngx_rtmp_fmp4_postconfiguration(ngx_conf_t *cf);
static ngx_int_t ngx_rtmp_fmp4_video(ngx_rtmp_session_t *s, ngx_rtmp_header_t *h, ngx_chain_t *in);
static ngx_int_t ngx_rtmp_fmp4_audio(ngx_rtmp_session_t *s, ngx_rtmp_header_t *h, ngx_chain_t *in);
static ngx_int_t ngx_rtmp_fmp4_publish(ngx_rtmp_session_t *s, ngx_rtmp_publish_t *v);
static ngx_int_t ngx_rtmp_fmp4_close_stream(ngx_rtmp_session_t *s, ngx_rtmp_close_stream_t *v);
static ngx_int_t ngx_rtmp_fmp4_stream_begin(ngx_rtmp_session_t *s, ngx_rtmp_stream_begin_t *v);
static ngx_int_t ngx_rtmp_fmp4_stream_eof(ngx_rtmp_session_t *s, ngx_rtmp_stream_eof_t *v);


typedef struct {
} ngx_rtmp_fmp4_frag_t;

typedef struct{
    ngx_flag_t                          fragmented_mp4;
    ngx_uint_t                          winfrags;
    ngx_str_t                           path;
    ngx_msec_t                          fraglen;//len of fragment
    ngx_flag_t                          nested;
    ngx_msec_t                          playlen;//playlist length
}ngx_rtmp_fmp4_app_conf_t;



typedef struct {
    ngx_rtmp_hls_frag_t                *frags; /* circular 2 * winfrags + 1 */
    ngx_str_t                           name;//name of stream
    ngx_str_t                           playlist;//link of playlist
    ngx_str_t                           playlist_bak;//playlist bak file name
    ngx_str_t                           stream;//stream path, ex /path/to/1.m4s
} ngx_rtmp_fmp4_ctx_t;

static ngx_command_t ngx_rtmp_fmp4_commands[] = {
    {
        ngx_string("fragmented_mp4"),
        NGX_RTMP_MAIN_CONF|NGX_RTMP_SRV_CONF|NGX_RTMP_APP_CONF|NGX_CONF_TAKE1,
        ngx_conf_set_flag_slot,
        NGX_RTMP_APP_CONF_OFFSET,
        offsetof(ngx_rtmp_fmp4_app_conf_t, fragmented_mp4),
        NULL
    },
    { 
        ngx_string("fmp4_path"),
        NGX_RTMP_MAIN_CONF|NGX_RTMP_SRV_CONF|NGX_RTMP_APP_CONF|NGX_CONF_TAKE1,
        ngx_conf_set_str_slot,
        NGX_RTMP_APP_CONF_OFFSET,
        offsetof(ngx_rtmp_fmp4_app_conf_t, path),
        NULL 
    },
    { 
        ngx_string("fmp4_nested"),
        NGX_RTMP_MAIN_CONF|NGX_RTMP_SRV_CONF|NGX_RTMP_APP_CONF|NGX_CONF_TAKE1,
        ngx_conf_set_flag_slot,
        NGX_RTMP_APP_CONF_OFFSET,
        offsetof(ngx_rtmp_fmp4_app_conf_t, nested),
        NULL 
    },
      { ngx_string("fmp4_fragment"),
      NGX_RTMP_MAIN_CONF|NGX_RTMP_SRV_CONF|NGX_RTMP_APP_CONF|NGX_CONF_TAKE1,
      ngx_conf_set_msec_slot,
      NGX_RTMP_APP_CONF_OFFSET,
      offsetof(ngx_rtmp_fmp4_app_conf_t, fraglen),
      NULL },
      { ngx_string("fmp4_playlist_length"),
      NGX_RTMP_MAIN_CONF|NGX_RTMP_SRV_CONF|NGX_RTMP_APP_CONF|NGX_CONF_TAKE1,
      ngx_conf_set_msec_slot,
      NGX_RTMP_APP_CONF_OFFSET,
      offsetof(ngx_rtmp_fmp4_app_conf_t, playlen),
      NULL },
    ngx_null_command
};

static ngx_rtmp_module_t ngx_rtmp_fmp4_module_ctx = {
    NULL,                               /* preconfiguration */
    ngx_rtmp_fmp4_postconfiguration,    /* postconfiguration */

    NULL,                               /* create main configuration */
    NULL,                               /* init main configuration */

    NULL,                               /* create server configuration */
    NULL,                               /* merge server configuration */

    ngx_rtmp_fmp4_create_app_conf,      /* create location configuration */
    ngx_rtmp_fmp4_merge_app_conf,       /* merge location configuration */
};

ngx_module_t  ngx_rtmp_fmp4_module = {
    NGX_MODULE_V1,
    &ngx_rtmp_fragmented_mp4_module_ctx,          /* module context */
    ngx_rtmp_fragmented_mp4_commands,             /* module directives */
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

/**
 * Allocate memory for location specific configuration
 * Every configs must be set here before using
 * */
static void * ngx_rtmp_fmp4_create_app_conf(ngx_conf_t *cf){
    ngx_rtmp_fragmented_mp4_app_conf_t *conf;
    conf = ngx_pcalloc(cf->pool, sizeof(ngx_rtmp_fragmented_mp4_app_conf_t));
    if (conf == NULL) {
        return NULL;
    }    
    conf->fragmented_mp4 = NGX_CONF_UNSET;
    conf->nested = NGX_CONF_UNSET;
    conf->fraglen = NGX_CONF_UNSET_MSEC;
    //if not set, it'll be 0
    conf->playlen = NGX_CONF_UNSET_MSEC;
    return conf;
}

static ngx_int_t
ngx_rtmp_fmp4_video(ngx_rtmp_session_t *s, ngx_rtmp_header_t *h,
    ngx_chain_t *in)
{

}

static ngx_int_t
ngx_rtmp_fmp4_audio(ngx_rtmp_session_t *s, ngx_rtmp_header_t *h,
    ngx_chain_t *in)
{}

/**
 * When we start publishing stream
 * */
static ngx_int_t
ngx_rtmp_fmp4_publish(ngx_rtmp_session_t *s, ngx_rtmp_publish_t *v)
{
    ngx_rtmp_fmp4_app_conf_t        *acf;
    ngx_rtmp_fmp4_frag_t             *f; //data fragment
    ngx_rtmp_fmp4_ctx_t             *ctx;
    u_char                          *p;
    size_t                          len;


    acf = ngx_rtmp_get_module_app_conf(s, ngx_rtmp_fmp4_module);
    if (acf == NULL || !acf->fragmented_mp4 || acf->path.len == 0) {
        goto next;
    }
    if (s->auto_pushed) {
        goto next;
    }
    ngx_log_debug2(NGX_LOG_DEBUG_RTMP, s->connection->log, 0,
                   "fmp4: publish: name='%s' type='%s'",
                   v->name, v->type);
    ctx = ngx_rtmp_get_module_ctx(s, ngx_rtmp_fmp4_module);
    if (ctx == NULL) {
        ctx = ngx_pcalloc(s->connection->pool, sizeof(ngx_rtmp_fmp4_ctx_t));
        ngx_rtmp_set_ctx(s, ctx, ngx_rtmp_fmp4_module);
    }else{
        f = ctx->frags;
        ngx_memzero(ctx, sizeof(ngx_rtmp_fmp4_ctx_t));//clear ctx by filling it by zero value
        ctx->frags = f;

    }
    if (ctx->frags == NULL) {
        ctx->frags = ngx_pcalloc(s->connection->pool,
                                 sizeof(ngx_rtmp_fmp4_frag_t) *
                                 (acf->winfrags * 2 + 1));
        if (ctx->frags == NULL) {
            return NGX_ERROR;
        }
    }
    if (ngx_strstr(v->name, "..")) {
        ngx_log_error(NGX_LOG_ERR, s->connection->log, 0,
                      "fmp4: bad stream name: '%s'", v->name);
        return NGX_ERROR;
    }
    //set name of stream
    ctx->name.len = ngx_strlen(v->name);
    ctx->name.data = ngx_palloc(s->connection->pool, ctx->name.len + 1);
    if (ctx->name.data == NULL) {
        return NGX_ERROR;
    }
    *ngx_cpymem(ctx->name.data, v->name, ctx->name.len) = 0;
    len = acf->path.len + 1 + ctx->name.len + sizeof(".m3u8");
    if (acf->nested) {
        len += sizeof("/index") - 1;
    }
    //set path of stream
    ctx->playlist.data = ngx_palloc(s->connection->pool, len);
    p = ngx_cpymem(ctx->playlist.data, acf->path.data, acf->path.len);
    if (p[-1] != '/') {
        *p++ = '/';
    }
    p = ngx_cpymem(p, ctx->name.data, ctx->name.len);
    ctx->stream.len = p - ctx->playlist.data + 1;
    ctx->stream.data = ngx_palloc(s->connection->pool,
                                  ctx->stream.len + NGX_INT64_LEN +
                                  sizeof(".m4s"));
    ngx_memcpy(ctx->stream.data, ctx->playlist.data, ctx->stream.len - 1);
    ctx->stream.data[ctx->stream.len - 1] = (acf->nested ? '/' : '-');
    if (acf->nested) {
        p = ngx_cpymem(p, "/index.m3u8", sizeof("/index.m3u8") - 1);
    } else {
        p = ngx_cpymem(p, ".m3u8", sizeof(".m3u8") - 1);
    }
    ctx->playlist.len = p - ctx->playlist.data;
    *p = 0;

    ctx->playlist_bak.data = ngx_palloc(s->connection->pool,
                                        ctx->playlist.len + sizeof(".bak"));
    p = ngx_cpymem(ctx->playlist_bak.data, ctx->playlist.data,
                   ctx->playlist.len);
    p = ngx_cpymem(p, ".bak", sizeof(".bak") - 1);
    ctx->playlist_bak.len = p - ctx->playlist_bak.data;
    *p = 0;
    next:
        return next_publish(s, v);
}

static ngx_int_t
ngx_rtmp_fmp4_close_stream(ngx_rtmp_session_t *s, ngx_rtmp_close_stream_t *v)
{}

static ngx_int_t
ngx_rtmp_fmp4_stream_begin(ngx_rtmp_session_t *s, ngx_rtmp_stream_begin_t *v)
{}

static ngx_int_t
ngx_rtmp_fmp4_stream_eof(ngx_rtmp_session_t *s, ngx_rtmp_stream_eof_t *v)
{}

/**
 * Get app configs
 * */
static char * ngx_rtmp_fmp4_merge_app_conf(ngx_conf_t *cf, void *parent, void *child){
    ngx_rtmp_fmp4_app_conf_t *prev = parent;
    ngx_rtmp_fmp4_app_conf_t *conf = child;

    ngx_conf_merge_value(conf->fragmented_mp4, prev->fragmented_mp4, 0);
    //fraglen default is 5000ms
    ngx_conf_merge_msec_value(conf->fraglen, prev->fraglen, 5000);
    //playlen default is 30000ms
    ngx_conf_merge_msec_value(conf->playlen, prev->playlen, 30000);  
    //playlistlen = 30s, fraglen = 5s --> winfrags = 6 --> we have 6 file in a playlist, each file has 5s duration 
    if (conf->fraglen) {
        conf->winfrags = conf->playlen / conf->fraglen;
    }
    ngx_conf_merge_str_value(conf->path, prev->path, "");
    return NGX_CONF_OK;
}

static ngx_int_t ngx_rtmp_fmp4_postconfiguration(ngx_conf_t *cf){
    ngx_rtmp_handler_pt        *h;
    ngx_rtmp_core_main_conf_t  *cmcf;
    cmcf = ngx_rtmp_conf_get_module_main_conf(cf, ngx_rtmp_core_module);
    // https://www.nginx.com/resources/wiki/extending/api/alloc/#ngx-array-push
    //create a new element on the array and returns a pointer to this element
    h = ngx_array_push(&cmcf->events[NGX_RTMP_MSG_VIDEO]);
    *h = ngx_rtmp_fmp4_video;

    h = ngx_array_push(&cmcf->events[NGX_RTMP_MSG_AUDIO]);
    *h = ngx_rtmp_fmp4_audio;

    next_publish = ngx_rtmp_publish;
    ngx_rtmp_publish = ngx_rtmp_fmp4_publish;

    next_close_stream = ngx_rtmp_close_stream;
    ngx_rtmp_close_stream = ngx_rtmp_fmp4_close_stream;

    next_stream_begin = ngx_rtmp_stream_begin;
    ngx_rtmp_stream_begin = ngx_rtmp_fmp4_stream_begin;

    next_stream_eof = ngx_rtmp_stream_eof;
    ngx_rtmp_stream_eof = ngx_rtmp_fmp4_stream_eof;

    return NGX_OK;
}