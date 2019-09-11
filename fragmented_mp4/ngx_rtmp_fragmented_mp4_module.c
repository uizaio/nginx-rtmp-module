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
#define NGX_RTMP_FRAGMENTED_MP4_MAX_SAMPLES       1024
#define NGX_RTMP_FRAGMENTED_MP4_MAX_MDAT          (10*1024*1024)

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
    ngx_rtmp_fmp4_sample_t               samples[NGX_RTMP_FRAGMENTED_MP4_MAX_SAMPLES];
} ngx_rtmp_fragmented_mp4_track_t;

typedef struct {
    uint32_t                            timestamp;
    uint64_t                            id;
    uint64_t                            key_id;
    double                              duration;
} ngx_rtmp_fragmented_mp4_frag_t;

static void * ngx_rtmp_fragmented_mp4_create_app_conf(ngx_conf_t *cf);
static char * ngx_rtmp_fragmented_mp4_merge_app_conf(ngx_conf_t *cf, void *parent, void *child);
static ngx_int_t ngx_rtmp_fragmented_mp4_postconfiguration(ngx_conf_t *cf);
static ngx_int_t ngx_rtmp_fragmented_mp4_write_init_segments(ngx_rtmp_session_t *s);
static ngx_int_t ngx_rtmp_fragmented_mp4_append(ngx_rtmp_session_t *s, ngx_chain_t *in, 
    ngx_rtmp_fragmented_mp4_track_t *t, ngx_int_t key, uint32_t timestamp, uint32_t delay);
static ngx_rtmp_fragmented_mp4_frag_t * ngx_rtmp_fragmented_mp4_get_frag(ngx_rtmp_session_t *s,
     ngx_int_t n);
static void ngx_rtmp_fragmented_mp4_update_fragments(ngx_rtmp_session_t *s, 
    ngx_int_t boundary, uint32_t timestamp);
static ngx_int_t ngx_rtmp_fragmented_mp4_open_fragment(ngx_rtmp_session_t *s, ngx_rtmp_fragmented_mp4_track_t *t, ngx_uint_t id, char type);
static ngx_int_t ngx_rtmp_fragmented_mp4_open_fragments(ngx_rtmp_session_t *s);
static ngx_int_t ngx_rtmp_fragmented_mp4_rename_file(u_char *src, u_char *dst);


typedef struct{
    ngx_flag_t                          fragmented_mp4;
    ngx_uint_t                          winfrags;
    ngx_str_t                           path;
    ngx_msec_t                          fraglen;//len of fragment
    ngx_flag_t                          nested;
    ngx_msec_t                          playlen;//playlist length
}ngx_rtmp_fragmented_mp4_app_conf_t;


typedef struct{
    ngx_str_t                                   playlist;
    ngx_str_t                                   playlist_bak;
    time_t                                      start_time;
    ngx_str_t                                   stream; //save stream of context
    unsigned                                    opened:1;
    unsigned                                    has_video:1;
    unsigned                                    has_audio:1;
    ngx_uint_t                                  nfrags;
    ngx_uint_t                                  frag;
    ngx_rtmp_fragmented_mp4_frag_t               *frags; /* circular 2 * winfrags + 1 */
    ngx_uint_t                                  id; //id of context
    ngx_str_t                                   name; //application name
    ngx_rtmp_fragmented_mp4_track_t               audio;
    ngx_rtmp_fragmented_mp4_track_t               video;
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
      { ngx_string("fmp4_fragment"),
      NGX_RTMP_MAIN_CONF|NGX_RTMP_SRV_CONF|NGX_RTMP_APP_CONF|NGX_CONF_TAKE1,
      ngx_conf_set_msec_slot,
      NGX_RTMP_APP_CONF_OFFSET,
      offsetof(ngx_rtmp_fragmented_mp4_app_conf_t, fraglen),
      NULL },
      { ngx_string("fmp4_playlist_length"),
      NGX_RTMP_MAIN_CONF|NGX_RTMP_SRV_CONF|NGX_RTMP_APP_CONF|NGX_CONF_TAKE1,
      ngx_conf_set_msec_slot,
      NGX_RTMP_APP_CONF_OFFSET,
      offsetof(ngx_rtmp_fragmented_mp4_app_conf_t, playlen),
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
 * Every configs must be set here before using
 * */
static void * ngx_rtmp_fragmented_mp4_create_app_conf(ngx_conf_t *cf){
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

/**
 *  Video message processor
 *  we write data to a temp file raw.m4v.
 *  @param s
 * @param h
 * @param in ngx_chain_t is a strucutre that contains a chain of memory buffer
 **/
static ngx_int_t ngx_rtmp_fragmented_mp4_video(ngx_rtmp_session_t *s, ngx_rtmp_header_t *h, ngx_chain_t *in){
    ngx_rtmp_fragmented_mp4_app_conf_t  *fmacf;
    ngx_rtmp_fragmented_mp4_ctx_t       *ctx;
    ngx_rtmp_codec_ctx_t      *codec_ctx;
    uint8_t                    ftype, htype;
    uint32_t                   delay;
    u_char                    *p;

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

    if (in->buf->last - in->buf->pos < 5) {
        return NGX_ERROR;
    }
    ftype = (in->buf->pos[0] & 0xf0) >> 4;
    htype = in->buf->pos[1];
    if (htype != 1) {
        return NGX_OK;
    }
    p = (u_char *) &delay;

    p[0] = in->buf->pos[4];
    p[1] = in->buf->pos[3];
    p[2] = in->buf->pos[2];
    p[3] = 0;
    ctx->has_video = 1;//stream has video signal
    in->buf->pos += 5;

    return ngx_rtmp_fragmented_mp4_append(s, in, &ctx->video, ftype == 1, h->timestamp,
                                delay);
}

/**
 * Audio message processor 
 **/
static ngx_int_t ngx_rtmp_fragmented_mp4_audio(ngx_rtmp_session_t *s, ngx_rtmp_header_t *h, ngx_chain_t *in){
    u_char                     htype;
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
        codec_ctx->aac_header == NULL){
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
    return ngx_rtmp_fragmented_mp4_append(s, in, &ctx->audio, 0, h->timestamp, 0);
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
                      "fmp4: '%s' exists and is not a directory", path);

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
    //fraglen default is 5000ms
    ngx_conf_merge_msec_value(conf->fraglen, prev->fraglen, 5000);
    //playlen default is 30000ms
    ngx_conf_merge_msec_value(conf->playlen, prev->playlen, 30000);  
    //playlistlen = 30s, fraglen = 5s --> winfrags = 6 --> nfrags = 6 (number of frag for each playlist)  
    if (conf->fraglen) {
        conf->winfrags = conf->playlen / conf->fraglen;
    }
    ngx_conf_merge_str_value(conf->path, prev->path, "");
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
    if (fmacf == NULL || !fmacf->fragmented_mp4 || fmacf->path.len == 0) {
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
    if (ngx_strstr(v->name, "..")) {
        ngx_log_error(NGX_LOG_INFO, s->connection->log, 0,
                      "fmp4: bad stream name: '%s'", v->name);
        return NGX_ERROR;
    }
                      
    ctx->name.len = ngx_strlen(v->name);
    ctx->name.data = ngx_palloc(s->connection->pool, ctx->name.len + 1);
    if (ctx->name.data == NULL) {
        return NGX_ERROR;
    }
    *ngx_cpymem(ctx->name.data, v->name, ctx->name.len) = 0;
    len = fmacf->path.len + 1 + ctx->name.len + sizeof(".m3u8");
    if (fmacf->nested) {
        len += sizeof("/index") - 1;
    }
    ctx->playlist.data = ngx_palloc(s->connection->pool, len);
    p = ngx_cpymem(ctx->playlist.data, fmacf->path.data, fmacf->path.len);
    if (p[-1] != '/') {
        *p++ = '/';
    }
    p = ngx_cpymem(p, ctx->name.data, ctx->name.len);
    ctx->stream.len = p - ctx->playlist.data + 1;
    ctx->stream.data = ngx_palloc(s->connection->pool,
                                  ctx->stream.len + NGX_INT32_LEN +
                                  sizeof(".m4x"));
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
    ctx->playlist_bak.data = ngx_palloc(s->connection->pool,
                                        ctx->playlist.len + sizeof(".bak"));
    p = ngx_cpymem(ctx->playlist_bak.data, ctx->playlist.data,
                   ctx->playlist.len);
    p = ngx_cpymem(p, ".bak", sizeof(".bak") - 1);

    ctx->playlist_bak.len = p - ctx->playlist_bak.data;

    *p = 0;
    ngx_log_error(NGX_LOG_DEBUG, s->connection->log, 0,
                   "fmp4: playlist='%V' playlist_bak='%V' stream_pattern='%V'",
                   &ctx->playlist, &ctx->playlist_bak, &ctx->stream);

    ctx->start_time = ngx_time();//when user start publishing data

    if (ngx_rtmp_fragmented_mp4_ensure_directory(s) != NGX_OK) {
        return NGX_ERROR;
    }
    *p = 0;
    next:
        return next_publish(s, v);
}

static void
ngx_rtmp_fragmented_mp4_next_frag(ngx_rtmp_session_t *s)
{
    ngx_rtmp_fragmented_mp4_ctx_t       *ctx;
    ngx_rtmp_fragmented_mp4_app_conf_t  *fmacf;

    fmacf = ngx_rtmp_get_module_app_conf(s, ngx_rtmp_fragmented_mp4_module);
    ctx = ngx_rtmp_get_module_ctx(s, ngx_rtmp_fragmented_mp4_module); 
    ngx_log_error(NGX_LOG_ERR, s->connection->log, ngx_errno,
                          "fmp4: nfrags %d winfrags %d frag %d", ctx->nfrags, fmacf->winfrags, ctx->frag);      
    if (ctx->nfrags == fmacf->winfrags) {
        ctx->frag++;//increase number of frags in a winfrags
    } else {
        ctx->nfrags++;//increase number of winfrags
    }
}

/**
 * playlist will be update after creating new m4s file.
 * how to update: create a playlist.bak and overwrite it to old file
 **/ 
static ngx_int_t
ngx_rtmp_fragmented_mp4_write_playlist(ngx_rtmp_session_t *s)
{
    static u_char                       buffer[NGX_RTMP_FRAGMENTED_MP4_BUFSIZE];
    ngx_rtmp_fragmented_mp4_ctx_t       *ctx;
    ngx_rtmp_fragmented_mp4_app_conf_t  *fmacf;
    ngx_rtmp_codec_ctx_t                *codec_ctx;
    ngx_fd_t                            fd;
    u_char                              *p, *end;
    ngx_uint_t                          i, max_frag;
    ssize_t                             n;
    ngx_uint_t                          first_media;
    ngx_rtmp_fragmented_mp4_frag_t                 *t;
    
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
    fd = ngx_open_file(ctx->playlist_bak.data, NGX_FILE_WRONLY,
                       NGX_FILE_TRUNCATE, NGX_FILE_DEFAULT_ACCESS);    
    if (fd == NGX_INVALID_FILE) {
        ngx_log_error(NGX_LOG_ERR, s->connection->log, ngx_errno,
                      "fmp4: open failed: '%V'", &ctx->playlist_bak);
        return NGX_ERROR;
    }
    max_frag = fmacf->fraglen / 1000;

    for (i = 0; i < ctx->nfrags; i++) {
        t = ngx_rtmp_fragmented_mp4_get_frag(s, i);
        if (t->duration > max_frag) {
            max_frag = (ngx_uint_t) (t->duration + .5);
        }
        if(i == 0){
            first_media = t->id;
        }
    }
    p = buffer;
    end = p + sizeof(buffer);
    p = ngx_slprintf(p, end,
                     "#EXTM3U\n"
                     "#EXT-X-VERSION:7\n"
                     "#EXT-X-MEDIA-SEQUENCE:%uL\n"//the first media segment in the list
                     "#EXT-X-TARGETDURATION:%ui\n",
                     first_media, max_frag);
    //EVENT: the playlist can only be appended to, VOD: the playlist must not change
    p = ngx_slprintf(p, end, "#EXT-X-PLAYLIST-TYPE: EVENT\n");
    p = ngx_slprintf(p, end, "#EXT-X-MAP:URI=\"init.mp4\"\n");
    n = ngx_write_fd(fd, buffer, p - buffer);
    if (n < 0) {
        ngx_log_error(NGX_LOG_ERR, s->connection->log, ngx_errno,
                      "fmp4: " ngx_write_fd_n " failed: '%V'",
                      &ctx->playlist_bak);
        ngx_close_file(fd);
        return NGX_ERROR;
    }
    

    for (i = 0; i < ctx->nfrags; i++) {
        t = ngx_rtmp_fragmented_mp4_get_frag(s, i);
        p = buffer;
        end = p + sizeof(buffer);
        // prev_key_id = t->key_id;
        p = ngx_slprintf(p, end,
                         "#EXTINF:%.3f,\n"
                         "%uL.m4s\n",
                         t->duration, t->id);
        n = ngx_write_fd(fd, buffer, p - buffer);
        if (n < 0) {
            ngx_log_error(NGX_LOG_ERR, s->connection->log, ngx_errno,
                          "fmp4: " ngx_write_fd_n " failed '%V'",
                          &ctx->playlist_bak);
            ngx_close_file(fd);
            return NGX_ERROR;
        }
    }
    //FIXME: how to send this part when stream stop?
    p = buffer;
    end = p + sizeof(buffer);
    p = ngx_slprintf(p, end, "#EXT-X-ENDLIST");
    n = ngx_write_fd(fd, buffer, p - buffer);
    if (n < 0) {
        ngx_log_error(NGX_LOG_ERR, s->connection->log, ngx_errno,
                      "fmp4: " ngx_write_fd_n " failed: '%V'",
                      &ctx->playlist_bak);
        ngx_close_file(fd);
        return NGX_ERROR;
    }
    ngx_close_file(fd);
    //remove old file and create a new file from bak
    if (ngx_rtmp_fragmented_mp4_rename_file(ctx->playlist_bak.data, ctx->playlist.data)
        == NGX_FILE_ERROR)
    {
        ngx_log_error(NGX_LOG_ERR, s->connection->log, ngx_errno,
                      "fmp4: rename failed: '%V'->'%V'",
                      &ctx->playlist_bak, &ctx->playlist);
        return NGX_ERROR;
    }

    return NGX_OK;
}

/**
 * We need to merge sound and video data to a file.
 * How this file is formated?
 * styp
 * sidx --> video data
 * sidx --> sound data
 * moof
 * mdat
 * @param vt video
 * @param st sound
 **/
static void
ngx_rtmp_fragmented_mp4_close_fragment(ngx_rtmp_session_t *s, ngx_rtmp_fragmented_mp4_track_t *vt, 
ngx_rtmp_fragmented_mp4_track_t *at)
{
    u_char                    *pos, *pos1;
    size_t                     vleft, aleft;
    ssize_t                    vn,an;
    ngx_fd_t                   fd;
    ngx_buf_t                  b;
    ngx_rtmp_fragmented_mp4_ctx_t       *ctx;
    ngx_rtmp_fragmented_mp4_frag_t      *f;
    ngx_uint_t                  reference_size;

    static u_char              vbuffer[NGX_RTMP_FRAGMENTED_MP4_BUFSIZE]; //video buffer
    static u_char              abuffer[NGX_RTMP_FRAGMENTED_MP4_BUFSIZE]; //audio buffer

    if (!vt->opened || !at->opened) {
        return;
    }
    ctx = ngx_rtmp_get_module_ctx(s, ngx_rtmp_fragmented_mp4_module);
    b.start = vbuffer;
    b.end = vbuffer + sizeof(vbuffer);
    b.pos = b.last = b.start;
    ngx_rtmp_fmp4_write_styp(&b);
    pos = b.last;
    b.last += 88; /* leave room for sidx */    
    /** create moof box */
    ngx_rtmp_fmp4_write_moof(&b, vt->earliest_pres_time, vt->sample_count,
                            vt->samples, vt->sample_mask, vt->id,
                            at->earliest_pres_time, at->sample_count,
                            at->samples, at->sample_mask);
    pos1 = b.last;
    b.last = pos;
    //refrence_size = size of moof + size of mdat
    reference_size = vt->mdat_size + at->mdat_size;
    //sidx for video
    ngx_rtmp_fmp4_write_sidx(&b, reference_size + 8 + (pos1 - (pos + 88)),
                            vt->earliest_pres_time, vt->latest_pres_time, 1);
    //sidx for audio
    ngx_rtmp_fmp4_write_sidx(&b, reference_size + 8 + (pos1 - (pos + 88)),
                            at->earliest_pres_time, at->latest_pres_time, 2);
    b.last = pos1;
    ngx_rtmp_fmp4_write_mdat(&b, reference_size + 8);
    f = ngx_rtmp_fragmented_mp4_get_frag(s, ctx->nfrags);
    //we only support m4s file format
    *ngx_sprintf(ctx->stream.data + ctx->stream.len, "%uD.m4s",
                 f->id) = 0;
    fd = ngx_open_file(ctx->stream.data, NGX_FILE_RDWR,
                       NGX_FILE_TRUNCATE, NGX_FILE_DEFAULT_ACCESS);
    if (fd == NGX_INVALID_FILE) {
        ngx_log_error(NGX_LOG_ERR, s->connection->log, ngx_errno,
                      "fmp4: error creating fmp4 temp file");
        goto done;
    }

    if (ngx_write_fd(fd, b.pos, (size_t) (b.last - b.pos)) == NGX_ERROR) {
        goto done;
    }

    vleft = (size_t) vt->mdat_size;
    aleft = (size_t) at->mdat_size;
    ngx_log_error(NGX_LOG_INFO, s->connection->log, ngx_errno,
                      "fmp4: vleft %lu aleft %lu", vleft, aleft);
    #if (NGX_WIN32)
        if (SetFilePointer(vt->fd, 0, 0, FILE_BEGIN) == INVALID_SET_FILE_POINTER || 
            SetFilePointer(at->fd, 0, 0, FILE_BEGIN) == INVALID_SET_FILE_POINTER) {
            ngx_log_error(NGX_LOG_ERR, s->connection->log, 0,
                        "fmp4: SetFilePointer error");
            goto done;
        }
    #else
        if (lseek(vt->fd, 0, SEEK_SET) == -1 || lseek(at->fd, 0, SEEK_SET) == -1) {
            ngx_log_error(NGX_LOG_ERR, s->connection->log, ngx_errno,
                        "fmp4: lseek error");
            goto done;
        }
    #endif
    //write sound/video data to file
    //FIXME: end of file character
    while (vleft > 0) {
        vn = ngx_read_fd(vt->fd, vbuffer, ngx_min(sizeof(vbuffer), vleft));
        if (vn == NGX_ERROR) {
            break;
        }

        vn = ngx_write_fd(fd, vbuffer, (size_t) vn);
        if (vn == NGX_ERROR) {
            break;
        }

        vleft -= vn;
    }
    while (aleft > 0) {
        an = ngx_read_fd(at->fd, abuffer, ngx_min(sizeof(abuffer), aleft));
        if (an == NGX_ERROR) {
            break;
        }

        an = ngx_write_fd(fd, abuffer, (size_t) an);
        if (an == NGX_ERROR) {
            break;
        }

        aleft -= an;
    }
    done:
        if (fd != NGX_INVALID_FILE) {
            ngx_close_file(fd);
        }
        //close raw files
        ngx_close_file(vt->fd);
        ngx_close_file(at->fd);
        vt->fd = NGX_INVALID_FILE;
        at->fd = NGX_INVALID_FILE;
        vt->opened = 0;
        at->opened = 0;
}

static ngx_int_t
ngx_rtmp_fragmented_mp4_close_fragments(ngx_rtmp_session_t *s)
{
    ngx_rtmp_fragmented_mp4_ctx_t  *ctx;

    ctx = ngx_rtmp_get_module_ctx(s, ngx_rtmp_fragmented_mp4_module);
    if (ctx == NULL || !ctx->opened) {
        return NGX_OK;
    }    
    //we must mix video and sound to a file
    // ngx_rtmp_fragmented_mp4_close_fragment(s, &ctx->video);
    // ngx_rtmp_fragmented_mp4_close_fragment(s, &ctx->audio);
    ngx_rtmp_fragmented_mp4_close_fragment(s, &ctx->video, &ctx->audio);

    ngx_rtmp_fragmented_mp4_next_frag(s);
    ngx_log_error(NGX_LOG_ERR, s->connection->log, 0,
                      "fmp4: write playlist");
    ngx_rtmp_fragmented_mp4_write_playlist(s);

    ctx->id++;
    ngx_log_error(NGX_LOG_ERR, s->connection->log, 0,
                      "fmp4: close fragment ctx->id %d", ctx->id);
    ctx->opened = 0;
    return NGX_OK;
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
    ngx_log_error(NGX_LOG_ERR, s->connection->log, 0,
                      "fmp4: close stream");
    ngx_rtmp_fragmented_mp4_close_fragments(s);
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
                      "fmp4: error creating fragmented mp4 init file");
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
                      "fmp4: writing init failed");
    }
    ngx_close_file(fd);    

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
static ngx_int_t
ngx_rtmp_fragmented_mp4_append(ngx_rtmp_session_t *s, ngx_chain_t *in,
    ngx_rtmp_fragmented_mp4_track_t *t, ngx_int_t key, uint32_t timestamp, uint32_t delay)
{
    u_char                 *p;
    size_t                  size, bsize;
    ngx_rtmp_fmp4_sample_t  *smpl;

    static u_char           buffer[NGX_RTMP_FRAGMENTED_MP4_BUFSIZE];

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

    ngx_rtmp_fragmented_mp4_update_fragments(s, key, timestamp);

    if (t->sample_count == 0) {
        t->earliest_pres_time = timestamp;
    }

    t->latest_pres_time = timestamp;

    if (t->sample_count < NGX_RTMP_FRAGMENTED_MP4_MAX_SAMPLES) {

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

/**
 * @param uint32_t timestamp timestamp in the header
 **/
static void
ngx_rtmp_fragmented_mp4_update_fragments(ngx_rtmp_session_t *s, ngx_int_t boundary,
    uint32_t timestamp)
{
    int32_t                    d;
    ngx_int_t                  hit;
    ngx_rtmp_fragmented_mp4_ctx_t      *ctx;
    ngx_rtmp_fragmented_mp4_frag_t      *f;
    ngx_rtmp_fragmented_mp4_app_conf_t  *fmacf;

    fmacf = ngx_rtmp_get_module_app_conf(s, ngx_rtmp_fragmented_mp4_module);
    ctx = ngx_rtmp_get_module_ctx(s, ngx_rtmp_fragmented_mp4_module);
    f = ngx_rtmp_fragmented_mp4_get_frag(s, ctx->nfrags);

    d = (int32_t) (timestamp - f->timestamp);

    if (d >= 0) {

        f->duration = timestamp - f->timestamp;
        //check if fraglen is hit
        hit = (f->duration >= fmacf->fraglen);

        /* keep fragment lengths within 2x factor for dash.js  */
        if (f->duration >= fmacf->fraglen * 2) {
            boundary = 1;
        }

    } else {

        /* sometimes clients generate slightly unordered frames */

        hit = (-d > 1000);
    }

    if (ctx->has_video && !hit) {
        boundary = 0;
    }

    if (!ctx->has_video && ctx->has_audio) {
        boundary = hit;
    }

    if (ctx->audio.mdat_size >= NGX_RTMP_FRAGMENTED_MP4_MAX_MDAT) {
        boundary = 1;
    }

    if (ctx->video.mdat_size >= NGX_RTMP_FRAGMENTED_MP4_MAX_MDAT) {
        boundary = 1;
    }

    if (!ctx->opened) {
        boundary = 1;
    }
    //we need to create new fragment file if fraglen is hit
    if (boundary) {
        ngx_rtmp_fragmented_mp4_close_fragments(s);
        ngx_rtmp_fragmented_mp4_open_fragments(s);

        f = ngx_rtmp_fragmented_mp4_get_frag(s, ctx->nfrags);
        f->timestamp = timestamp;
    }
}
static ngx_rtmp_fragmented_mp4_frag_t *
ngx_rtmp_fragmented_mp4_get_frag(ngx_rtmp_session_t *s, ngx_int_t n)
{
    ngx_rtmp_fragmented_mp4_ctx_t       *ctx;
    ngx_rtmp_fragmented_mp4_app_conf_t  *fmacf;

    fmacf = ngx_rtmp_get_module_app_conf(s, ngx_rtmp_fragmented_mp4_module);
    ctx = ngx_rtmp_get_module_ctx(s, ngx_rtmp_fragmented_mp4_module);

    return &ctx->frags[(ctx->frag + n) % (fmacf->winfrags * 2 + 1)];
}

static ngx_int_t
ngx_rtmp_fragmented_mp4_open_fragment(ngx_rtmp_session_t *s, ngx_rtmp_fragmented_mp4_track_t *t,
    ngx_uint_t id, char type)
{
    ngx_rtmp_fragmented_mp4_ctx_t   *ctx;
    ngx_rtmp_fragmented_mp4_frag_t *f;

    if (t->opened) {
        return NGX_OK;
    }

    ngx_log_debug2(NGX_LOG_DEBUG_RTMP, s->connection->log, 0,
                   "fmp4: open fragment id=%ui, type='%c'", id, type);

    ctx = ngx_rtmp_get_module_ctx(s, ngx_rtmp_fragmented_mp4_module);
    ngx_log_error(NGX_LOG_ERR, s->connection->log, 0,
                   "fmp4: open fragment id=%ui, type='%c'", id, type);
    *ngx_sprintf(ctx->stream.data + ctx->stream.len, "raw.m4%c", type) = 0;

    t->fd = ngx_open_file(ctx->stream.data, NGX_FILE_RDWR,
                          NGX_FILE_TRUNCATE, NGX_FILE_DEFAULT_ACCESS);

    if (t->fd == NGX_INVALID_FILE) {
        ngx_log_error(NGX_LOG_ERR, s->connection->log, ngx_errno,
                      "fmp4: error creating fragment file");
        return NGX_ERROR;
    }    
    t->id = id;
    ngx_log_error(NGX_LOG_ERR, s->connection->log, 0,
                      "fmp4: t->id %d", t->id);
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
    f = ngx_rtmp_fragmented_mp4_get_frag(s, ctx->nfrags);
    ngx_memzero(f, sizeof(*f));
    f->id = id;
    return NGX_OK;
}

static ngx_int_t
ngx_rtmp_fragmented_mp4_open_fragments(ngx_rtmp_session_t *s)
{
    ngx_rtmp_fragmented_mp4_ctx_t  *ctx;

    ctx = ngx_rtmp_get_module_ctx(s, ngx_rtmp_fragmented_mp4_module);

    if (ctx->opened) {
        return NGX_OK;
    }

    ngx_rtmp_fragmented_mp4_open_fragment(s, &ctx->video, ctx->id, 'v');

    ngx_rtmp_fragmented_mp4_open_fragment(s, &ctx->audio, ctx->id, 'a');

    ctx->opened = 1;

    return NGX_OK;
}

static ngx_int_t
ngx_rtmp_fragmented_mp4_rename_file(u_char *src, u_char *dst)
{
    /* rename file with overwrite */

#if (NGX_WIN32)
    return MoveFileEx((LPCTSTR) src, (LPCTSTR) dst, MOVEFILE_REPLACE_EXISTING);
#else
    return ngx_rename_file(src, dst);
#endif
}