#include <ngx_config.h>
#include <ngx_rtmp.h>
#include <ngx_rtmp_cmd_module.h>
#include <ngx_rtmp_codec_module.h>
#include "ngx_rtmp_fmp4.h"


#define NGX_RTMP_FMP4_BUFSIZE           (1024*1024)
#define NGX_RTMP_FMP4_MAX_MDAT          (10*1024*1024)
#define NGX_RTMP_FMP4_MAX_SAMPLES       1024
#define NGX_RTMP_FMP4_DIR_ACCESS        0744


static ngx_rtmp_publish_pt              next_publish;
static ngx_rtmp_close_stream_pt         next_close_stream;
static ngx_rtmp_stream_begin_pt         next_stream_begin;
static ngx_rtmp_stream_eof_pt           next_stream_eof;


typedef struct {
    uint32_t                            timestamp;
    uint32_t                            duration;
} ngx_rtmp_fmp4_frag_t;

typedef struct {
    ngx_uint_t                          id;
    ngx_uint_t                          opened;
    ngx_uint_t                          mdat_size;
    ngx_uint_t                          sample_count;
    ngx_uint_t                          sample_mask;
    ngx_fd_t                            fd;
    char                                type;
    uint32_t                            earliest_pres_time;
    uint32_t                            latest_pres_time;
    ngx_rtmp_fmp4_sample_t               samples[NGX_RTMP_FMP4_MAX_SAMPLES];
} ngx_rtmp_fmp4_track_t;

typedef struct{
    ngx_flag_t                          fragmented_mp4;
    ngx_uint_t                          winfrags;
    ngx_str_t                           path;
    ngx_msec_t                          fraglen;//len of fragment
    ngx_flag_t                          nested;
    ngx_msec_t                          playlen;//playlist length
}ngx_rtmp_fmp4_app_conf_t;



typedef struct {
    unsigned                            opened:1;//is context opened?
    ngx_rtmp_fmp4_frag_t                *frags; /* circular 2 * winfrags + 1 */
    ngx_uint_t                          nfrags; //number frag of a fragment
    uint64_t                            frag; //current fragment, ex 2.m4s
    ngx_str_t                           name;//name of stream
    ngx_str_t                           playlist;//link of playlist
    ngx_str_t                           playlist_bak;//playlist bak file name
    ngx_str_t                           initMp4;
    ngx_str_t                           stream;//stream path, ex /path/to/1.m4s
    ngx_uint_t                          id;//id of context    
    unsigned                            has_video:1;//if this context has video
    unsigned                            has_audio:1;//if this context has audio
    ngx_rtmp_fmp4_track_t               audio;
    ngx_rtmp_fmp4_track_t               video;
} ngx_rtmp_fmp4_ctx_t;//current context

static void * ngx_rtmp_fmp4_create_app_conf(ngx_conf_t *cf);
static char * ngx_rtmp_fmp4_merge_app_conf(ngx_conf_t *cf, void *parent, void *child);
static ngx_int_t ngx_rtmp_fmp4_postconfiguration(ngx_conf_t *cf);
static ngx_int_t ngx_rtmp_fmp4_video(ngx_rtmp_session_t *s, ngx_rtmp_header_t *h, ngx_chain_t *in);
static ngx_int_t ngx_rtmp_fmp4_audio(ngx_rtmp_session_t *s, ngx_rtmp_header_t *h, ngx_chain_t *in);
static ngx_int_t ngx_rtmp_fmp4_publish(ngx_rtmp_session_t *s, ngx_rtmp_publish_t *v);
static ngx_int_t ngx_rtmp_fmp4_close_stream(ngx_rtmp_session_t *s, ngx_rtmp_close_stream_t *v);
static ngx_int_t ngx_rtmp_fmp4_stream_begin(ngx_rtmp_session_t *s, ngx_rtmp_stream_begin_t *v);
static ngx_int_t ngx_rtmp_fmp4_stream_eof(ngx_rtmp_session_t *s, ngx_rtmp_stream_eof_t *v);
static ngx_int_t ngx_rtmp_fmp4_close_fragments(ngx_rtmp_session_t *s);
static void ngx_rtmp_fmp4_next_frag(ngx_rtmp_session_t *s);
static ngx_int_t ngx_rtmp_fmp4_write_playlist(ngx_rtmp_session_t *s);
static ngx_int_t ngx_rtmp_fmp4_write_init(ngx_rtmp_session_t *s);
static ngx_int_t ngx_rtmp_fmp4_ensure_directory(ngx_rtmp_session_t *s);
static ngx_int_t ngx_rtmp_fmp4_append(ngx_rtmp_session_t *s, ngx_chain_t *in, ngx_rtmp_fmp4_track_t *t, ngx_int_t key, uint32_t timestamp, uint32_t delay);
static ngx_int_t ngx_rtmp_fmp4_open_fragment(ngx_rtmp_session_t *s, ngx_rtmp_fmp4_track_t *t, ngx_uint_t id, char type);
static ngx_int_t ngx_rtmp_fmp4_open_fragments(ngx_rtmp_session_t *s);
static void ngx_rtmp_fmp4_update_fragments(ngx_rtmp_session_t *s, ngx_int_t boundary, uint32_t timestamp);
static ngx_rtmp_fmp4_frag_t * ngx_rtmp_fmp4_get_frag(ngx_rtmp_session_t *s, ngx_int_t n);
static void ngx_rtmp_fmp4_close_fragment(ngx_rtmp_session_t *s, ngx_rtmp_fmp4_track_t *t);

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
    &ngx_rtmp_fmp4_module_ctx,          /* module context */
    ngx_rtmp_fmp4_commands,             /* module directives */
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
    ngx_rtmp_fmp4_app_conf_t *conf;
    conf = ngx_pcalloc(cf->pool, sizeof(ngx_rtmp_fmp4_app_conf_t));
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
    ngx_chain_t *in){
    ngx_rtmp_fmp4_app_conf_t *acf;
    ngx_rtmp_fmp4_ctx_t * ctx;
    ngx_rtmp_codec_ctx_t      *codec_ctx;
    uint8_t                    ftype, htype;
    u_char                    *p;
    uint32_t                   delay;

    acf = ngx_rtmp_get_module_app_conf(s, ngx_rtmp_fmp4_module);
    ctx = ngx_rtmp_get_module_ctx(s, ngx_rtmp_fmp4_module);
    codec_ctx = ngx_rtmp_get_module_ctx(s, ngx_rtmp_codec_module);
    if (acf == NULL || !acf->fragmented_mp4 || ctx == NULL || codec_ctx == NULL ||
        codec_ctx->avc_header == NULL || h->mlen < 5){
        return NGX_OK;
    }
     /* Only H264 is supported */

    if (codec_ctx->video_codec_id != NGX_RTMP_VIDEO_H264) {
        return NGX_OK;
    }

    if (in->buf->last - in->buf->pos < 5) {
        return NGX_ERROR;
    }
    ftype = (in->buf->pos[0] & 0xf0) >> 4;

    /* skip AVC config */

    htype = in->buf->pos[1];
    if (htype != 1) {
        return NGX_OK;
    }
    p = (u_char *) &delay;

    p[0] = in->buf->pos[4];
    p[1] = in->buf->pos[3];
    p[2] = in->buf->pos[2];
    p[3] = 0;
    //context has video data
    ctx->has_video = 1;
    in->buf->pos += 5;
    return ngx_rtmp_fmp4_append(s, in, &ctx->video, ftype == 1, h->timestamp,
                                delay);
}

static ngx_int_t
ngx_rtmp_fmp4_audio(ngx_rtmp_session_t *s, ngx_rtmp_header_t *h,
    ngx_chain_t *in){
    u_char                     htype;
    ngx_rtmp_fmp4_ctx_t       *ctx;
    ngx_rtmp_codec_ctx_t      *codec_ctx;
    ngx_rtmp_fmp4_app_conf_t  *acf;

    acf = ngx_rtmp_get_module_app_conf(s, ngx_rtmp_fmp4_module);
    ctx = ngx_rtmp_get_module_ctx(s, ngx_rtmp_fmp4_module);
    codec_ctx = ngx_rtmp_get_module_ctx(s, ngx_rtmp_codec_module);

    if (acf == NULL || !acf->fragmented_mp4 || ctx == NULL ||
        codec_ctx == NULL || h->mlen < 2){
        return NGX_OK;
    }
    /* Only AAC is supported */

    if (codec_ctx->audio_codec_id != NGX_RTMP_AUDIO_AAC ||
        codec_ctx->aac_header == NULL)
    {
        return NGX_OK;
    }

    if (in->buf->last - in->buf->pos < 2) {
        return NGX_ERROR;
    }

    /* skip AAC config */

    htype = in->buf->pos[1];
    if (htype != 1) {
        return NGX_OK;
    }

    ctx->has_audio = 1;

    /* skip RTMP & AAC headers */

    in->buf->pos += 2;

    return ngx_rtmp_fmp4_append(s, in, &ctx->audio, 0, h->timestamp, 0);
}

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
        //we locate a mem that can hold all frag info of a playlist + 1
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
    //we create init.mp4 file
    ctx->initMp4.data = ngx_palloc(s->connection->pool, acf->path.len + ctx->name.len + sizeof("-init.mp4") + 1);    
    p = ngx_cpymem(ctx->initMp4.data, acf->path.data, acf->path.len);
    if (p[-1] != '/') {
        *p++ = '/';
    }
    p = ngx_cpymem(p, ctx->name.data, ctx->name.len);
    if(acf->nested){
        *p++ = '/';
    }else{
        *p++ = '-';
    }
    p = ngx_cpymem(p, "init.mp4", sizeof("init.mp4") - 1);
    ctx->initMp4.len = p - ctx->initMp4.data;
    *p =0;
    ngx_rtmp_fmp4_ensure_directory(s);
    next:
        return next_publish(s, v);
}

//When user stops streaming
static ngx_int_t
ngx_rtmp_fmp4_close_fragments(ngx_rtmp_session_t *s){
    ngx_rtmp_fmp4_ctx_t         *ctx;
    ctx = ngx_rtmp_get_module_ctx(s, ngx_rtmp_fmp4_module);
    if (ctx == NULL || !ctx->opened) {
        return NGX_OK;
    }    
    //close temp file
    ngx_rtmp_fmp4_close_fragment(s, &ctx->video);
    ngx_rtmp_fmp4_close_fragment(s, &ctx->audio); 
    ngx_log_error(NGX_LOG_ERR, s->connection->log, 0,
                          "fmp4: create new m4s file");
    ngx_rtmp_fmp4_next_frag(s);
    ngx_rtmp_fmp4_write_playlist(s);
    ctx->opened = 0; //close context
    ctx->id++;
    return NGX_OK;
}

static ngx_int_t 
ngx_rtmp_fmp4_write_init(ngx_rtmp_session_t *s){
    ngx_rtmp_fmp4_app_conf_t        *acf;
    ngx_rtmp_fmp4_ctx_t             *ctx;
    ngx_fd_t                        fd;
    static u_char                   buffer[2048];//init.mp4 > 2KB?
    ngx_buf_t                       b;
    ngx_int_t                       rc;

    acf = ngx_rtmp_get_module_app_conf(s, ngx_rtmp_fmp4_module);
    ctx = ngx_rtmp_get_module_ctx(s, ngx_rtmp_fmp4_module);
    fd = ngx_open_file(ctx->initMp4.data, NGX_FILE_WRONLY,
                       NGX_FILE_TRUNCATE, NGX_FILE_DEFAULT_ACCESS);
    if (fd == NGX_INVALID_FILE) {
        ngx_log_error(NGX_LOG_ERR, s->connection->log, ngx_errno,
                      "fmp4: " ngx_open_file_n " failed: '%V'",
                      &ctx->playlist_bak);
        return NGX_ERROR;
    }    
    b.start = buffer;
    b.end = buffer + sizeof(buffer);
    b.pos = b.last = b.start;
    ngx_rtmp_fmp4_write_ftyp(&b);
    ngx_rtmp_fmp4_write_moov(s, &b);
    rc = ngx_write_fd(fd, b.start, (size_t) (b.last - b.start));
    if (rc == NGX_ERROR) {
        ngx_log_error(NGX_LOG_ERR, s->connection->log, ngx_errno,
                      "fmp4: writing init failed");
    }

    ngx_close_file(fd);
    return NGX_OK;
}

static ngx_int_t
ngx_rtmp_fmp4_write_playlist(ngx_rtmp_session_t *s){
    ngx_rtmp_fmp4_ctx_t       *ctx;
    ngx_rtmp_fmp4_app_conf_t  *acf;

    acf = ngx_rtmp_get_module_app_conf(s, ngx_rtmp_fmp4_module);
    ctx = ngx_rtmp_get_module_ctx(s, ngx_rtmp_fmp4_module);
    if (ctx->id == 0) {
        ngx_rtmp_fmp4_write_init(s);
    }
    return NGX_OK;
}

static void
ngx_rtmp_fmp4_next_frag(ngx_rtmp_session_t *s){
    ngx_rtmp_fmp4_ctx_t         *ctx;
    ngx_rtmp_fmp4_app_conf_t    *acf;

    acf = ngx_rtmp_get_module_app_conf(s, ngx_rtmp_fmp4_module);
    ctx = ngx_rtmp_get_module_ctx(s, ngx_rtmp_fmp4_module);
    ngx_log_error(NGX_LOG_INFO, s->connection->log, 0,
                   "fmp4: nfrags %d frag %d", ctx->nfrags, ctx->frag);
    if (ctx->nfrags == acf->winfrags) {
        ctx->frag++;
    } else {
        ctx->nfrags++;
    }
}

static ngx_int_t
ngx_rtmp_fmp4_close_stream(ngx_rtmp_session_t *s, ngx_rtmp_close_stream_t *v){
    ngx_rtmp_fmp4_app_conf_t        *acf;
    ngx_rtmp_fmp4_ctx_t             *ctx;

    acf = ngx_rtmp_get_module_app_conf(s, ngx_rtmp_fmp4_module);

    ctx = ngx_rtmp_get_module_ctx(s, ngx_rtmp_fmp4_module);

    if (acf == NULL || !acf->fragmented_mp4 || ctx == NULL) {
        goto next;
    }
    ngx_log_error(NGX_LOG_INFO, s->connection->log, 0,
                   "fmp4: close stream");
    ngx_rtmp_fmp4_close_fragments(s);
    next:
        return next_close_stream(s, v);
}

static ngx_int_t
ngx_rtmp_fmp4_stream_begin(ngx_rtmp_session_t *s, ngx_rtmp_stream_begin_t *v)
{
    return next_stream_begin(s, v);
}

static ngx_int_t
ngx_rtmp_fmp4_stream_eof(ngx_rtmp_session_t *s, ngx_rtmp_stream_eof_t *v)
{
    ngx_log_error(NGX_LOG_INFO, s->connection->log, 0,
                   "fmp4: stream eof");
    ngx_rtmp_fmp4_close_fragments(s);
    return next_stream_eof(s, v);
}

static ngx_int_t
ngx_rtmp_fmp4_ensure_directory(ngx_rtmp_session_t *s){
    size_t                     len;
    ngx_file_info_t            fi;
    ngx_rtmp_fmp4_ctx_t       *ctx;
    ngx_rtmp_fmp4_app_conf_t  *acf;

    static u_char              path[NGX_MAX_PATH + 1];

    acf = ngx_rtmp_get_module_app_conf(s, ngx_rtmp_fmp4_module);

    *ngx_snprintf(path, sizeof(path) - 1, "%V", &acf->path) = 0;

    if (ngx_file_info(path, &fi) == NGX_FILE_ERROR) {

        if (ngx_errno != NGX_ENOENT) {
            ngx_log_error(NGX_LOG_ERR, s->connection->log, ngx_errno,
                          "fmp4: " ngx_file_info_n " failed on '%V'",
                          &acf->path);
            return NGX_ERROR;
        }

        /* ENOENT */

        if (ngx_create_dir(path, NGX_RTMP_FMP4_DIR_ACCESS) == NGX_FILE_ERROR) {
            ngx_log_error(NGX_LOG_ERR, s->connection->log, ngx_errno,
                          "fmp4: " ngx_create_dir_n " failed on '%V'",
                          &acf->path);
            return NGX_ERROR;
        }

        ngx_log_debug1(NGX_LOG_DEBUG_RTMP, s->connection->log, 0,
                       "fmp4: directory '%V' created", &acf->path);

    } else {

        if (!ngx_is_dir(&fi)) {
            ngx_log_error(NGX_LOG_ERR, s->connection->log, 0,
                          "fmp4: '%V' exists and is not a directory",
                          &acf->path);
            return  NGX_ERROR;
        }

        ngx_log_debug1(NGX_LOG_DEBUG_RTMP, s->connection->log, 0,
                       "fmp4: directory '%V' exists", &acf->path);
    }

    if (!acf->nested) {
        return NGX_OK;
    }

    ctx = ngx_rtmp_get_module_ctx(s, ngx_rtmp_fmp4_module);

    len = acf->path.len;
    if (acf->path.data[len - 1] == '/') {
        len--;
    }

    *ngx_snprintf(path, sizeof(path) - 1, "%*s/%V", len, acf->path.data,
                  &ctx->name) = 0;

    if (ngx_file_info(path, &fi) != NGX_FILE_ERROR) {

        if (ngx_is_dir(&fi)) {
            ngx_log_debug1(NGX_LOG_DEBUG_RTMP, s->connection->log, 0,
                           "fmp4: directory '%s' exists", path);
            return NGX_OK;
        }

        ngx_log_error(NGX_LOG_ERR, s->connection->log, 0,
                      "fmp4: '%s' exists and is not a directory", path);

        return  NGX_ERROR;
    }

    if (ngx_errno != NGX_ENOENT) {
        ngx_log_error(NGX_LOG_ERR, s->connection->log, ngx_errno,
                      "fmp4: " ngx_file_info_n " failed on '%s'", path);
        return NGX_ERROR;
    }

    /* NGX_ENOENT */

    if (ngx_create_dir(path, NGX_RTMP_FMP4_DIR_ACCESS) == NGX_FILE_ERROR) {
        ngx_log_error(NGX_LOG_ERR, s->connection->log, ngx_errno,
                      "fmp4: " ngx_create_dir_n " failed on '%s'", path);
        return NGX_ERROR;
    }

    ngx_log_debug1(NGX_LOG_DEBUG_RTMP, s->connection->log, 0,
                   "fmp4: directory '%s' created", path);

    return NGX_OK;
}

static ngx_int_t
ngx_rtmp_fmp4_append(ngx_rtmp_session_t *s, ngx_chain_t *in,
    ngx_rtmp_fmp4_track_t *t, ngx_int_t key, uint32_t timestamp, uint32_t delay){
    u_char                 *p;
    size_t                  size, bsize;
    ngx_rtmp_fmp4_sample_t  *smpl;

    static u_char           buffer[NGX_RTMP_FMP4_BUFSIZE];
    p = buffer;
    size = 0;

    for (; in && size < sizeof(buffer); in = in->next) {

        bsize = (size_t) (in->buf->last - in->buf->pos);
        if (size + bsize > sizeof(buffer)) {
            bsize = (size_t) (sizeof(buffer) - size);
        }

        p = ngx_cpymem(p, in->buf->pos, bsize);
        size += bsize;
    }    

    ngx_rtmp_fmp4_update_fragments(s, key, timestamp);
    //set earliest presentation time of fragment
    if (t->sample_count == 0) {
        t->earliest_pres_time = timestamp;
    }
    t->latest_pres_time = timestamp;
    if (t->sample_count < NGX_RTMP_FMP4_MAX_SAMPLES) {
        if (ngx_write_fd(t->fd, buffer, size) == NGX_ERROR) {
            ngx_log_error(NGX_LOG_ERR, s->connection->log, ngx_errno,
                          "fmp4: " ngx_write_fd_n " failed");
            return NGX_ERROR;
        }

        smpl = &t->samples[t->sample_count];

        smpl->delay = delay;
        smpl->size = (uint32_t) size;
        smpl->duration = 0;
        smpl->timestamp = timestamp;
        smpl->key = (key ? 1 : 0);

        if (t->sample_count > 0) {
            smpl = &t->samples[t->sample_count - 1];
            smpl->duration = timestamp - smpl->timestamp;
        }

        t->sample_count++;
        t->mdat_size += (ngx_uint_t) size;
    }
    return NGX_OK;

}

static void
ngx_rtmp_fmp4_update_fragments(ngx_rtmp_session_t *s, ngx_int_t boundary, uint32_t timestamp){
    int32_t                    d;
    ngx_rtmp_fmp4_frag_t      *f;
    ngx_rtmp_fmp4_ctx_t       *ctx;
    ngx_int_t                  hit;

    ctx = ngx_rtmp_get_module_ctx(s, ngx_rtmp_fmp4_module);
    f = ngx_rtmp_fmp4_get_frag(s, ctx->nfrags);//get current fragment 

    d = (int32_t) (timestamp - f->timestamp);   
    if (d >= 0) {
        f->duration = timestamp - f->timestamp;
        hit = (f->duration >= acf->fraglen);
    }else{
        hit = (-d > 1000);
    }
    if (ctx->has_video && !hit) {
        boundary = 0;
    }

    if (!ctx->has_video && ctx->has_audio) {
        boundary = hit;
    }

    if (ctx->audio.mdat_size >= NGX_RTMP_FMP4_MAX_MDAT) {
        boundary = 1;
    }

    if (ctx->video.mdat_size >= NGX_RTMP_FMP4_MAX_MDAT) {
        boundary = 1;
    }
    if (!ctx->opened) {
        boundary = 1;
    }
    if (boundary) {
        //close audio and video frag (m4s file)
        ngx_rtmp_fmp4_close_fragments(s);
        ngx_rtmp_fmp4_open_fragments(s);
        f = ngx_rtmp_fmp4_get_frag(s, ctx->nfrags);
        f->timestamp = timestamp;
    }
    

}

static ngx_rtmp_fmp4_frag_t *
ngx_rtmp_fmp4_get_frag(ngx_rtmp_session_t *s, ngx_int_t n){
    ngx_rtmp_fmp4_ctx_t       *ctx;
    ngx_rtmp_fmp4_app_conf_t  *acf;

    acf = ngx_rtmp_get_module_app_conf(s, ngx_rtmp_fmp4_module);
    ctx = ngx_rtmp_get_module_ctx(s, ngx_rtmp_fmp4_module);
    int test = (ctx->frag + n) % (acf->winfrags * 2 + 1);
    ngx_log_error(NGX_LOG_ERR, s->connection->log, 0,
                          "fmp4: get frag: %d", test);
    return &ctx->frags[(ctx->frag + n) % (acf->winfrags * 2 + 1)];
}

static ngx_int_t
ngx_rtmp_fmp4_open_fragments(ngx_rtmp_session_t *s){
    ngx_rtmp_fmp4_ctx_t  *ctx;

    ngx_log_debug0(NGX_LOG_DEBUG_RTMP, s->connection->log, 0,
                   "fmp4: open fragments");

    ctx = ngx_rtmp_get_module_ctx(s, ngx_rtmp_fmp4_module);

    if (ctx->opened) {
        return NGX_OK;
    }

    //write video data
    ngx_rtmp_fmp4_open_fragment(s, &ctx->video, ctx->id, 'v');
    //write audio data
    ngx_rtmp_fmp4_open_fragment(s, &ctx->audio, ctx->id, 'a');

    ctx->opened = 1;

    return NGX_OK;
}

//open temp file to write data
static ngx_int_t
ngx_rtmp_fmp4_open_fragment(ngx_rtmp_session_t *s, ngx_rtmp_fmp4_track_t *t,
    ngx_uint_t id, char type){
    ngx_rtmp_fmp4_ctx_t   *ctx;

    if (t->opened) {
        return NGX_OK;
    }
    ngx_log_debug2(NGX_LOG_DEBUG_RTMP, s->connection->log, 0,
                   "fmp4: open fragment id=%ui, type='%c'", id, type);

    ctx = ngx_rtmp_get_module_ctx(s, ngx_rtmp_fmp4_module);

    *ngx_sprintf(ctx->stream.data + ctx->stream.len, "raw.m4%c", type) = 0;

    t->fd = ngx_open_file(ctx->stream.data, NGX_FILE_RDWR,
                          NGX_FILE_TRUNCATE, NGX_FILE_DEFAULT_ACCESS);
    if (t->fd == NGX_INVALID_FILE) {
        ngx_log_error(NGX_LOG_ERR, s->connection->log, ngx_errno,
                      "fmp4: error creating fragment file");
        return NGX_ERROR;
    }

    t->id = id;
    t->type = type;
    t->sample_count = 0;
    t->earliest_pres_time = 0;
    t->latest_pres_time = 0;
    t->mdat_size = 0;
    t->opened = 1;

    if (type == 'v') {
        t->sample_mask = NGX_RTMP_FMP4_SAMPLE_SIZE|
                         NGX_RTMP_FMP4_SAMPLE_DURATION|
                         NGX_RTMP_FMP4_SAMPLE_DELAY|
                         NGX_RTMP_FMP4_SAMPLE_KEY;
    } else {
        t->sample_mask = NGX_RTMP_FMP4_SAMPLE_SIZE|
                         NGX_RTMP_FMP4_SAMPLE_DURATION;
    }
    return NGX_OK;
}

//close temp file and prepare for new data
static void
ngx_rtmp_fmp4_close_fragment(ngx_rtmp_session_t *s, ngx_rtmp_fmp4_track_t *t){
    done:
        //close temp file
        ngx_close_file(t->fd);

        t->fd = NGX_INVALID_FILE;
        t->opened = 0;
}

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