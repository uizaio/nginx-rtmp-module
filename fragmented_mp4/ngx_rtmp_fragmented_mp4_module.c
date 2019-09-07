#include <ngx_config.h>
#include <ngx_rtmp.h>
#include <ngx_rtmp_cmd_module.h>
#include <ngx_rtmp_codec_module.h>
#include "ngx_rtmp_fmp4.h"


static ngx_rtmp_publish_pt              next_publish;
static ngx_rtmp_close_stream_pt         next_close_stream;
static ngx_rtmp_stream_begin_pt         next_stream_begin;
static ngx_rtmp_stream_eof_pt           next_stream_eof;

#define NGX_RTMP_FRAGMENTED_MP4_BUFSIZE           (1024*1024)

static void * ngx_rtmp_fragmented_mp4_create_app_conf(ngx_conf_t *cf);
static char * ngx_rtmp_fragmented_mp4_merge_app_conf(ngx_conf_t *cf, void *parent, void *child);
static ngx_int_t ngx_rtmp_fragmented_mp4_postconfiguration(ngx_conf_t *cf);

typedef struct{
    ngx_flag_t fragmented_mp4;
}ngx_rtmp_fragmented_mp4_app_conf_t;

typedef struct{
    ngx_str_t                           playlist;
    ngx_str_t                           stream;
} ngx_rtmp_fragmented_mp4_ctx_t;

static ngx_command_t ngx_rtmp_fragmented_mp4_commands[] = {
    {
        ngx_string("fragmented_mp4"),
        NGX_RTMP_MAIN_CONF|NGX_RTMP_SRV_CONF|NGX_RTMP_APP_CONF|NGX_CONF_TAKE1,
        ngx_conf_set_flag_slot,
        NGX_RTMP_APP_CONF_OFFSET,
        offsetof(ngx_rtmp_fragmented_mp4_app_conf_t, fragmented_mp4),
        NULL
    },
    ngx_null_command
};

static ngx_rtmp_module_t ngx_rtmp_fragmented_mp4_module_ctx = {
    NULL,                               /* preconfiguration */
    ngx_rtmp_fragmented_mp4_postconfiguration,    /* postconfiguration */

    NULL,                               /* create main configuration */
    NULL,                               /* init main configuration */

    NULL,                               /* create server configuration */
    NULL,                               /* merge server configuration */

    ngx_rtmp_fragmented_mp4_create_app_conf,      /* create location configuration */
    ngx_rtmp_fragmented_mp4_merge_app_conf,       /* merge location configuration */
};

ngx_module_t  ngx_rtmp_fragmented_mp4_module = {
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
 * */
static void * ngx_rtmp_fragmented_mp4_create_app_conf(ngx_conf_t *cf){
    ngx_rtmp_fragmented_mp4_app_conf_t *conf;
    conf = ngx_pcalloc(cf->pool, sizeof(ngx_rtmp_fragmented_mp4_app_conf_t));
    if (conf == NULL) {
        return NULL;
    }
    conf->fragmented_mp4 = NGX_CONF_UNSET;
    return conf;
}

/**
 *  Video message processor
 *  @param s
 * @param h
 * @param in ngx_chain_t is a strucutre that contains a chain of memory buffer
 **/
static ngx_int_t ngx_rtmp_fragmented_mp4_video(ngx_rtmp_session_t *s, ngx_rtmp_header_t *h, ngx_chain_t *in){
    ngx_rtmp_fragmented_mp4_app_conf_t  *fmacf;
    ngx_rtmp_fragmented_mp4_ctx_t       *ctx;
    ngx_rtmp_codec_ctx_t      *codec_ctx;

    fmacf = ngx_rtmp_get_module_app_conf(s, ngx_rtmp_fragmented_mp4_module);
    ctx = ngx_rtmp_get_module_ctx(s, ngx_rtmp_fragmented_mp4_module);
    codec_ctx = ngx_rtmp_get_module_ctx(s, ngx_rtmp_codec_module);

    if (fmacf == NULL || !fmacf->fragmented_mp4 || ctx == NULL || codec_ctx == NULL ||
        codec_ctx->avc_header == NULL || h->mlen < 5){ //ASK: why h->mlen must 5 but in hls is 1?
        return NGX_OK;
    }
    /* Only H264 is supported */

    if (codec_ctx->video_codec_id != NGX_RTMP_VIDEO_H264) {
        return NGX_OK;
    }

}

/**
 * Audio message processor 
 **/
static ngx_int_t ngx_rtmp_fragmented_mp4_audio(ngx_rtmp_session_t *s, ngx_rtmp_header_t *h, ngx_chain_t *in){
    ngx_rtmp_fragmented_mp4_ctx_t       *ctx;
    ngx_rtmp_codec_ctx_t      *codec_ctx;
    ngx_rtmp_fragmented_mp4_app_conf_t  *fmacf;
    fmacf = ngx_rtmp_get_module_app_conf(s, ngx_rtmp_fragmented_mp4_module);
    ctx = ngx_rtmp_get_module_ctx(s, ngx_rtmp_fragmented_mp4_module);
    codec_ctx = ngx_rtmp_get_module_ctx(s, ngx_rtmp_codec_module);
    if (fmacf == NULL || !fmacf->fragmented_mp4 || ctx == NULL ||
        codec_ctx == NULL || h->mlen < 2)
    {
        return NGX_OK;
    }

    /* Only AAC is supported */

    if (codec_ctx->audio_codec_id != NGX_RTMP_AUDIO_AAC ||
        codec_ctx->aac_header == NULL)
    {
        return NGX_OK;
    }
}

/**
 * Get app configs
 * */
static char * ngx_rtmp_fragmented_mp4_merge_app_conf(ngx_conf_t *cf, void *parent, void *child){
    ngx_rtmp_fragmented_mp4_app_conf_t *prev = parent;
    ngx_rtmp_fragmented_mp4_app_conf_t *conf = child;

    ngx_conf_merge_value(conf->fragmented_mp4, prev->fragmented_mp4, 0);
    return NGX_CONF_OK;
}

static ngx_int_t
ngx_rtmp_fragmented_mp4_publish(ngx_rtmp_session_t *s, ngx_rtmp_publish_t *v)
{

    next:
        return next_publish(s, v);
}

/**
 * Used to close file resources
 **/
static ngx_int_t
ngx_rtmp_fragmented_mp4_close_stream(ngx_rtmp_session_t *s, ngx_rtmp_close_stream_t *v)
{
    ngx_rtmp_fragmented_mp4_ctx_t       *ctx;
    ngx_rtmp_fragmented_mp4_app_conf_t  *fmcf;

    fmcf = ngx_rtmp_get_module_app_conf(s, ngx_rtmp_fragmented_mp4_module);

    ctx = ngx_rtmp_get_module_ctx(s, ngx_rtmp_fragmented_mp4_module);
    if (fmcf == NULL || !fmcf->fragmented_mp4 || ctx == NULL) {
        goto next;
    }

    next:
        return next_close_stream(s, v);
}

static ngx_int_t
ngx_rtmp_fragmented_mp4_stream_begin(ngx_rtmp_session_t *s, ngx_rtmp_stream_begin_t *v)
{
    return next_stream_begin(s, v);
}

static ngx_int_t
ngx_rtmp_fragmented_mp4_write_init_segments(ngx_rtmp_session_t *s)
{
    ngx_rtmp_fragmented_mp4_ctx_t   *ctx;
    ngx_rtmp_codec_ctx_t  *codec_ctx;
    static u_char          buffer[NGX_RTMP_FRAGMENTED_MP4_BUFSIZE];
    
    ctx = ngx_rtmp_get_module_ctx(s, ngx_rtmp_fragmented_mp4_module);
    codec_ctx = ngx_rtmp_get_module_ctx(s, ngx_rtmp_codec_module);
    if (ctx == NULL || codec_ctx == NULL) {
        return NGX_ERROR;
    }
    *ngx_sprintf(ctx->stream.data + ctx->stream.len, "init.mp4") = 0;
    fd = ngx_open_file(ctx->stream.data, NGX_FILE_RDWR, NGX_FILE_TRUNCATE,
                       NGX_FILE_DEFAULT_ACCESS);
    if (fd == NGX_INVALID_FILE) {
        ngx_log_error(NGX_LOG_ERR, s->connection->log, ngx_errno,
                      "dash: error creating fragmented mp4 init file");
        return NGX_ERROR;
    }
    b.start = buffer;
    b.end = b.start + sizeof(buffer);
    b.pos = b.last = b.start;

    ngx_rtmp_fmp4_write_ftyp(&b);
    ngx_rtmp_fmp4_write_moov(s, &b);    
    rc = ngx_write_fd(fd, b.start, (size_t) (b.last - b.start));
    if (rc == NGX_ERROR) {
        ngx_log_error(NGX_LOG_ERR, s->connection->log, ngx_errno,
                      "dash: writing audio init failed");
    }

    ngx_close_file(fd);

    return NGX_OK;
}

static ngx_int_t
ngx_rtmp_fragmented_mp4_write_playlist(ngx_rtmp_session_t *s)
{
    static u_char              buffer[NGX_RTMP_FRAGMENTED_MP4_BUFSIZE];
    ngx_rtmp_fragmented_mp4_ctx_t *ctx;
    ngx_rtmp_fragmented_mp4_app_conf_t  *fmacf;
    ngx_rtmp_codec_ctx_t      *codec_ctx;
    
    fmacf = ngx_rtmp_get_module_app_conf(s, ngx_rtmp_fragmented_mp4_module);
    ctx = ngx_rtmp_get_module_ctx(s, ngx_rtmp_fragmented_mp4_module);
    codec_ctx = ngx_rtmp_get_module_ctx(s, ngx_rtmp_codec_module);
    if (fmacf == NULL || ctx == NULL || codec_ctx == NULL) {
        return NGX_ERROR;
    }

    if (ctx->id == 0) {
        //if this is the first streame, we need to create init segment file
        ngx_rtmp_fragmented_mp4_write_init_segments(s);
    }
    //now we need to create a playlist
}

static ngx_int_t
ngx_rtmp_fragmented_mp4_close_fragments(ngx_rtmp_session_t *s)
{
    ngx_rtmp_fragmented_mp4_ctx_t  *ctx;

    ctx = ngx_rtmp_get_module_ctx(s, ngx_rtmp_fragmented_mp4_module);
    if (ctx == NULL || !ctx->opened) {
        return NGX_OK;
    }

    //need to write data to file *.m4s
    //jump to next fragment
    //and update/create playlist
    ngx_rtmp_fragmented_mp4_write_playlist(s);
    ctx->id++;
    ctx->opened = 0;

    return NGX_OK;
}

/**
 * End of stream
 **/
static ngx_int_t
ngx_rtmp_fragmented_mp4_stream_eof(ngx_rtmp_session_t *s, ngx_rtmp_stream_eof_t *v)
{
    ngx_rtmp_fragmented_mp4_close_fragments(s);
    return next_stream_eof(s, v);
}

static ngx_int_t ngx_rtmp_fragmented_mp4_postconfiguration(ngx_conf_t *cf){
    ngx_rtmp_handler_pt        *h;
    ngx_rtmp_core_main_conf_t  *cmcf;
    cmcf = ngx_rtmp_conf_get_module_main_conf(cf, ngx_rtmp_core_module);
    // https://www.nginx.com/resources/wiki/extending/api/alloc/#ngx-array-push
    //create a new element on the array and returns a pointer to this element
    h = ngx_array_push(&cmcf->events[NGX_RTMP_MSG_VIDEO]);
    *h = ngx_rtmp_fragmented_mp4_video;

    h = ngx_array_push(&cmcf->events[NGX_RTMP_MSG_AUDIO]);
    *h = ngx_rtmp_fragmented_mp4_audio;

    next_publish = ngx_rtmp_publish;
    ngx_rtmp_publish = ngx_rtmp_fragmented_mp4_publish;

    next_close_stream = ngx_rtmp_close_stream;
    ngx_rtmp_close_stream = ngx_rtmp_fragmented_mp4_close_stream;

    next_stream_begin = ngx_rtmp_stream_begin;
    ngx_rtmp_stream_begin = ngx_rtmp_fragmented_mp4_stream_begin;

    next_stream_eof = ngx_rtmp_stream_eof;
    ngx_rtmp_stream_eof = ngx_rtmp_fragmented_mp4_stream_eof;

    return NGX_OK;
}
