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
#define NGX_RTMP_FRAGMENTED_MP4_DIR_ACCESS        0744

static void * ngx_rtmp_fragmented_mp4_create_app_conf(ngx_conf_t *cf);
static char * ngx_rtmp_fragmented_mp4_merge_app_conf(ngx_conf_t *cf, void *parent, void *child);
static ngx_int_t ngx_rtmp_fragmented_mp4_postconfiguration(ngx_conf_t *cf);

typedef struct{
    ngx_flag_t                          fragmented_mp4;
    ngx_uint_t                          winfrags;
    ngx_str_t                           path;
    ngx_flag_t                          nested;
}ngx_rtmp_fragmented_mp4_app_conf_t;

typedef struct {
    uint32_t                            timestamp;
    uint32_t                            duration;
} ngx_rtmp_fragmented_mp4_frag_t;

typedef struct{
    ngx_str_t                                   playlist;
    ngx_str_t                                   playlist_bak;
    time_t                                      start_time;
    ngx_str_t                                   stream; //save stream of context
    unsigned                                    opened:1;
    ngx_rtmp_fragmented_mp4_frag_t               *frags; /* circular 2 * winfrags + 1 */
    ngx_uint_t                                  id; //id of context
    ngx_str_t                                   name; //application name
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
    { ngx_string("fmp4_path"),
      NGX_RTMP_MAIN_CONF|NGX_RTMP_SRV_CONF|NGX_RTMP_APP_CONF|NGX_CONF_TAKE1,
      ngx_conf_set_str_slot,
      NGX_RTMP_APP_CONF_OFFSET,
      offsetof(ngx_rtmp_fragmented_mp4_app_conf_t, path),
      NULL },
      { ngx_string("fmp4_nested"),
      NGX_RTMP_MAIN_CONF|NGX_RTMP_SRV_CONF|NGX_RTMP_APP_CONF|NGX_CONF_TAKE1,
      ngx_conf_set_flag_slot,
      NGX_RTMP_APP_CONF_OFFSET,
      offsetof(ngx_rtmp_fragmented_mp4_app_conf_t, nested),
      NULL },
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

static ngx_int_t
ngx_rtmp_fragmented_mp4_ensure_directory(ngx_rtmp_session_t *s)
{
    size_t                     len;
    ngx_file_info_t            fi;
    ngx_rtmp_fragmented_mp4_ctx_t       *ctx;
    ngx_rtmp_fragmented_mp4_app_conf_t  *mfacf;

    static u_char              path[NGX_MAX_PATH + 1];

    mfacf = ngx_rtmp_get_module_app_conf(s, ngx_rtmp_fragmented_mp4_module);

    *ngx_snprintf(path, sizeof(path) - 1, "%V", &mfacf->path) = 0;

    if (ngx_file_info(path, &fi) == NGX_FILE_ERROR) {

        if (ngx_errno != NGX_ENOENT) {
            ngx_log_error(NGX_LOG_ERR, s->connection->log, ngx_errno,
                          "fmp4: " ngx_file_info_n " failed on '%V'",
                          &mfacf->path);
            return NGX_ERROR;
        }

        /* ENOENT */

        if (ngx_create_dir(path, NGX_RTMP_FRAGMENTED_MP4_DIR_ACCESS) == NGX_FILE_ERROR) {
            ngx_log_error(NGX_LOG_ERR, s->connection->log, ngx_errno,
                          "fmp4: " ngx_create_dir_n " failed on '%V'",
                          &mfacf->path);
            return NGX_ERROR;
        }

        ngx_log_debug1(NGX_LOG_DEBUG_RTMP, s->connection->log, 0,
                       "fmp4: directory '%V' created", &mfacf->path);

    } else {

        if (!ngx_is_dir(&fi)) {
            ngx_log_error(NGX_LOG_ERR, s->connection->log, 0,
                          "fmp4: '%V' exists and is not a directory",
                          &mfacf->path);
            return  NGX_ERROR;
        }

        ngx_log_debug1(NGX_LOG_DEBUG_RTMP, s->connection->log, 0,
                       "fmp4: directory '%V' exists", &mfacf->path);
    }

    if (!mfacf->nested) {
        return NGX_OK;
    }

    ctx = ngx_rtmp_get_module_ctx(s, ngx_rtmp_fragmented_mp4_module);

    len = mfacf->path.len;
    if (mfacf->path.data[len - 1] == '/') {
        len--;
    }

    *ngx_snprintf(path, sizeof(path) - 1, "%*s/%V", len, mfacf->path.data,
                  &ctx->name) = 0;

    if (ngx_file_info(path, &fi) != NGX_FILE_ERROR) {

        if (ngx_is_dir(&fi)) {
            ngx_log_debug1(NGX_LOG_DEBUG_RTMP, s->connection->log, 0,
                           "fmp4: directory '%s' exists", path);
            return NGX_OK;
        }

        ngx_log_error(NGX_LOG_ERR, s->connection->log, 0,
                      "dash: '%s' exists and is not a directory", path);

        return  NGX_ERROR;
    }

    if (ngx_errno != NGX_ENOENT) {
        ngx_log_error(NGX_LOG_ERR, s->connection->log, ngx_errno,
                      "fmp4: " ngx_file_info_n " failed on '%s'", path);
        return NGX_ERROR;
    }

    /* NGX_ENOENT */

    if (ngx_create_dir(path, NGX_RTMP_FRAGMENTED_MP4_DIR_ACCESS) == NGX_FILE_ERROR) {
        ngx_log_error(NGX_LOG_ERR, s->connection->log, ngx_errno,
                      "fmp4: " ngx_create_dir_n " failed on '%s'", path);
        return NGX_ERROR;
    }

    ngx_log_debug1(NGX_LOG_DEBUG_RTMP, s->connection->log, 0,
                   "fmp4: directory '%s' created", path);

    return NGX_OK;
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
    u_char                    *p;
    size_t                     len;
    ngx_rtmp_fragmented_mp4_ctx_t       *ctx;
    ngx_rtmp_fragmented_mp4_frag_t      *f;
    ngx_rtmp_fragmented_mp4_app_conf_t  *fmacf;
    fmacf = ngx_rtmp_get_module_app_conf(s, ngx_rtmp_fragmented_mp4_module);
    if (fmacf == NULL || !fmacf->fragmented_mp4 /**|| fmacf->path.len == 0**/) {
        goto next;
    }
    ctx = ngx_rtmp_get_module_ctx(s, ngx_rtmp_fragmented_mp4_module);
    if (ctx == NULL) {
        ctx = ngx_pcalloc(s->connection->pool, sizeof(ngx_rtmp_fragmented_mp4_ctx_t));
        if (ctx == NULL) {
            goto next;
        }
        ngx_rtmp_set_ctx(s, ctx, ngx_rtmp_fragmented_mp4_module);

    }else{
        if (ctx->opened) {
            goto next;
        }

        f = ctx->frags;
        ngx_memzero(ctx, sizeof(ngx_rtmp_fragmented_mp4_ctx_t));
        ctx->frags = f;
    }    
    if (ctx->frags == NULL) {
        ctx->frags = ngx_pcalloc(s->connection->pool,
                                 sizeof(ngx_rtmp_fragmented_mp4_frag_t) *
                                 (fmacf->winfrags * 2 + 1));
        if (ctx->frags == NULL) {
            return NGX_ERROR;
        }
    }
    //when recv publish command, we reset id context to 0?
    ctx->id = 0;
    ngx_log_error(NGX_LOG_INFO, s->connection->log, 0,
                      "fmp4: streamname: %s", v->name);
    if (ngx_strstr(v->name, "..")) {
        ngx_log_error(NGX_LOG_INFO, s->connection->log, 0,
                      "fmp4: bad stream name: '%s'", v->name);
        return NGX_ERROR;
    }
    ngx_log_error(NGX_LOG_INFO, s->connection->log, 0,
                      "fmp4: streamname: %s", v->name);
    ctx->name.len = ngx_strlen(v->name);
    ctx->name.data = ngx_palloc(s->connection->pool, ctx->name.len + 1);
    ngx_log_error(NGX_LOG_INFO, s->connection->log, 0,
                      "fmp4: streamname: '%s'", ctx->name.data);
    if (ctx->name.data == NULL) {
        return NGX_ERROR;
    }
    ngx_log_error(NGX_LOG_INFO, s->connection->log, 0,
                      "fmp4: create playlist: '%s'", v->name);
    *ngx_cpymem(ctx->name.data, v->name, ctx->name.len) = 0;
    len = fmacf->path.len + 1 + ctx->name.len + sizeof(".m3u8");
    if (fmacf->nested) {
        len += sizeof("/index") - 1;
    }
    ngx_log_error(NGX_LOG_INFO, s->connection->log, 0,
                      "fmp4: create playlist: '%s'", ctx->name.data);
    ctx->playlist.data = ngx_palloc(s->connection->pool, len);
    p = ngx_cpymem(ctx->playlist.data, fmacf->path.data, fmacf->path.len);
    if (p[-1] != '/') {
        *p++ = '/';
    }
    p = ngx_cpymem(p, ctx->name.data, ctx->name.len);
    ngx_memcpy(ctx->stream.data, ctx->playlist.data, ctx->stream.len - 1);
    ctx->stream.data[ctx->stream.len - 1] = (fmacf->nested ? '/' : '-');
    if (fmacf->nested) {
        p = ngx_cpymem(p, "/index.m3u8", sizeof("/index.m3u8") - 1);//remove \0 character of c string
    } else {
        p = ngx_cpymem(p, ".m3u8", sizeof(".m3u8") - 1);
    }
    ctx->playlist.len = p - ctx->playlist.data;
    ctx->playlist_bak.len = p - ctx->playlist_bak.data;

    *p = 0;

    ngx_log_debug3(NGX_LOG_DEBUG_RTMP, s->connection->log, 0,
                   "dash: playlist='%V' playlist_bak='%V' stream_pattern='%V'",
                   &ctx->playlist, &ctx->playlist_bak, &ctx->stream);

    ctx->start_time = ngx_time();//when user start publishing data

    if (ngx_rtmp_fragmented_mp4_ensure_directory(s) != NGX_OK) {
        return NGX_ERROR;
    }
    *p = 0;
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
    ngx_fd_t               fd;
    ngx_int_t              rc;
    ngx_rtmp_fragmented_mp4_ctx_t   *ctx;
    ngx_buf_t              b;
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
    ngx_log_error(NGX_LOG_ERR, s->connection->log, ngx_errno,
                      "fmp4: context id: %u", ctx->id);
    if (ctx->id == 0) {
        ngx_log_error(NGX_LOG_ERR, s->connection->log, ngx_errno,
                      "fmp4: init segment");
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
