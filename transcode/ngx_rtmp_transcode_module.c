/*
* Copyright (C) ducla@uiza.io 2019
*/
#include <ngx_config.h>
#include <ngx_core.h>
#include <ngx_rtmp.h>
#include <ngx_rtmp_cmd_module.h>

static ngx_rtmp_publish_pt              next_publish;
static ngx_rtmp_close_stream_pt         next_close_stream;
static ngx_rtmp_stream_begin_pt         next_stream_begin;
static ngx_rtmp_stream_eof_pt           next_stream_eof;

static ngx_int_t ngx_rtmp_transcode_postconfiguration(ngx_conf_t *cf);
static void * ngx_rtmp_transcode_create_app_conf(ngx_conf_t *cf);
static char * ngx_rtmp_transcode_merge_app_conf(ngx_conf_t *cf,
       void *parent, void *child);
static ngx_int_t ngx_rtmp_transcode_ensure_directory(ngx_rtmp_session_t *s);
static ngx_int_t ngx_rtmp_transcode_cleanup_dir(ngx_str_t *ppath, ngx_msec_t playlen);
#if (nginx_version >= 1011005)
static ngx_msec_t
#else
static time_t
#endif
ngx_rtmp_transcode_cleanup(void *data);


#define NGX_RTMP_TRANSCODE_NAMING_SEQUENTIAL  1
#define NGX_RTMP_TRANSCODE_NAMING_TIMESTAMP   2
#define NGX_RTMP_TRANSCODE_NAMING_SYSTEM      3
#define NGX_RTMP_TRANSCODE_DIR_ACCESS        0744

typedef struct {
    ngx_str_t                           path;
    ngx_msec_t                          playlen;
} ngx_rtmp_transcode_cleanup_t;

static ngx_conf_enum_t                  ngx_rtmp_transcode_naming_slots[] = {
    { ngx_string("sequential"),         NGX_RTMP_TRANSCODE_NAMING_SEQUENTIAL },
    { ngx_string("timestamp"),          NGX_RTMP_TRANSCODE_NAMING_TIMESTAMP  },
    { ngx_string("system"),             NGX_RTMP_TRANSCODE_NAMING_SYSTEM     },
    { ngx_null_string,                  0 }
};

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
    ngx_path_t                         *slot1;
    ngx_path_t                         *slot2;
} ngx_rtmp_transcode_app_conf_t;

typedef struct {
} ngx_rtmp_transcode_ctx_t;

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

    return conf;
}

static char *
ngx_rtmp_transcode_merge_app_conf(ngx_conf_t *cf, void *parent, void *child)
{
    ngx_rtmp_transcode_app_conf_t    *prev = parent;
    ngx_rtmp_transcode_app_conf_t    *conf = child;    
    ngx_rtmp_transcode_cleanup_t     *cleanup1, *cleanup2;

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

    if (conf->transcode && conf->path.len && conf->dvr_path.len){
        cleanup1 = ngx_pcalloc(cf->pool, sizeof(*cleanup1));
        if (cleanup1 == NULL) {
            return NGX_CONF_ERROR;
        }
        cleanup1->path = conf->path;
        cleanup1->playlen = conf->playlen;
        conf->slot1 = ngx_pcalloc(cf->pool, sizeof(*conf->slot1));
        if (conf->slot1 == NULL) {
            return NGX_CONF_ERROR;
        }
        //http://nginx.org/en/docs/dev/development_guide.html keyword: ngx_path_t
        //this handler will be run time to time
        conf->slot1->manager = ngx_rtmp_transcode_cleanup;
        conf->slot1->name = conf->path;
        conf->slot1->data = cleanup1;
        conf->slot1->conf_file = cf->conf_file->file.name.data;
        conf->slot1->line = cf->conf_file->line;

        if (ngx_add_path(cf, &conf->slot1) != NGX_OK) {
            return NGX_CONF_ERROR;
        }

        cleanup2 = ngx_pcalloc(cf->pool, sizeof(*cleanup2));
        if (cleanup2 == NULL) {
            return NGX_CONF_ERROR;
        }
        cleanup2->path = conf->dvr_path;
        cleanup2->playlen = conf->playlen;
        conf->slot2 = ngx_pcalloc(cf->pool, sizeof(*conf->slot2));
        if (conf->slot2 == NULL) {
            return NGX_CONF_ERROR;
        }
        //http://nginx.org/en/docs/dev/development_guide.html keyword: ngx_path_t
        //this handler will be run time to time
        conf->slot2->manager = ngx_rtmp_transcode_cleanup;
        conf->slot2->name = conf->dvr_path;
        conf->slot2->data = cleanup2;
        conf->slot2->conf_file = cf->conf_file->file.name.data;
        conf->slot2->line = cf->conf_file->line;

        if (ngx_add_path(cf, &conf->slot2) != NGX_OK) {
            return NGX_CONF_ERROR;
        }
    }    
    return NGX_CONF_OK;
}


static ngx_int_t
ngx_rtmp_transcode_publish(ngx_rtmp_session_t *s, ngx_rtmp_publish_t *v)
{
    ngx_rtmp_transcode_app_conf_t *tacf;

    tacf = ngx_rtmp_get_module_app_conf(s, ngx_rtmp_transcode_module);
    if (tacf == NULL || !tacf->transcode || tacf->path.len == 0) {
        goto next;
    }
    if (s->auto_pushed) {
        goto next;
    }
    if (ngx_rtmp_transcode_ensure_directory(s) != NGX_OK) {
        return NGX_ERROR;
    }
    next:
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

/**
 * Clean up stream directory
 * @param ppath
 * @param playlen
 * @return 
 */
static ngx_int_t ngx_rtmp_transcode_cleanup_dir(ngx_str_t *ppath, ngx_msec_t playlen){
    ngx_dir_t               dir;
    time_t                  mtime, max_age;
    ngx_err_t               err;
    ngx_str_t               name, spath;
    u_char                 *p;
    ngx_int_t               nentries, nerased;
    u_char                  path[NGX_MAX_PATH + 1];

    ngx_log_debug2(NGX_LOG_DEBUG_RTMP, ngx_cycle->log, 0,
                   "transcode: cleanup path='%V' playlen=%M",
                   ppath, playlen);
    if (ngx_open_dir(ppath, &dir) != NGX_OK) {
        ngx_log_debug1(NGX_LOG_DEBUG_RTMP, ngx_cycle->log, ngx_errno,
                      "transcode: cleanup open dir failed '%V'", ppath);
        return NGX_ERROR;
    }

    nentries = 0;
    nerased = 0;

    for ( ;; ) {
        ngx_set_errno(0);
        if (ngx_read_dir(&dir) == NGX_ERROR) {
            err = ngx_errno;

            if (ngx_close_dir(&dir) == NGX_ERROR) {
                ngx_log_error(NGX_LOG_CRIT, ngx_cycle->log, ngx_errno,
                              "transcode: cleanup " ngx_close_dir_n " \"%V\" failed",
                              ppath);
            }

            if (err == NGX_ENOMOREFILES) {
                return nentries - nerased;
            }

            ngx_log_error(NGX_LOG_CRIT, ngx_cycle->log, err,
                          "transcode: cleanup " ngx_read_dir_n
                          " '%V' failed", ppath);
            return NGX_ERROR;
        }

        name.data = ngx_de_name(&dir);
        if (name.data[0] == '.') {
            continue;
        }

        name.len = ngx_de_namelen(&dir);

        p = ngx_snprintf(path, sizeof(path) - 1, "%V/%V", ppath, &name);
        *p = 0;

        spath.data = path;
        spath.len = p - path;

        nentries++;

        if (!dir.valid_info && ngx_de_info(path, &dir) == NGX_FILE_ERROR) {
            ngx_log_error(NGX_LOG_CRIT, ngx_cycle->log, ngx_errno,
                          "transcode: cleanup " ngx_de_info_n " \"%V\" failed",
                          &spath);

            continue;
        }

        if (ngx_de_is_dir(&dir)) {

            if (ngx_rtmp_transcode_cleanup_dir(&spath, playlen) == 0) {
                ngx_log_debug1(NGX_LOG_DEBUG_RTMP, ngx_cycle->log, 0,
                               "transcode: cleanup dir '%V'", &name);

                /*
                 * null-termination gets spoiled in win32
                 * version of ngx_open_dir
                 */

                *p = 0;

                if (ngx_delete_dir(path) == NGX_FILE_ERROR) {
                    ngx_log_error(NGX_LOG_ERR, ngx_cycle->log, ngx_errno,
                                  "transcode: cleanup " ngx_delete_dir_n
                                  " failed on '%V'", &spath);
                } else {
                    nerased++;
                }
            }

            continue;
        }
        if (!ngx_de_is_file(&dir)) {
            continue;
        }

        if (name.len >= 4 && name.data[name.len - 4] == '.' &&
                             name.data[name.len - 3] == 'm' &&
                             name.data[name.len - 2] == '4' &&
                             name.data[name.len -1] == 's')
        {
            max_age = playlen / 500;

        } else if (name.len >= 5 && name.data[name.len - 5] == '.' &&
                                    name.data[name.len - 4] == 'm' &&
                                    name.data[name.len - 3] == '3' &&
                                    name.data[name.len - 2] == 'u' &&
                                    name.data[name.len - 1] == '8')
        {
            if(name.data[name.len - 6] == '0' || name.data[name.len - 6] == '1' || name.data[name.len - 6] == '2'){
                max_age = playlen / 1000;
            }else{
                max_age = 30 * 24 * 60 * 60;//1 month
            }

        } else if (name.len >= 4 && name.data[name.len - 4] == '.' &&
                                    name.data[name.len - 3] == 'm' &&
                                    name.data[name.len - 2] == 'p' &&
                                    name.data[name.len - 1] == 'd')
        {
            max_age = playlen / 1000;

        }else if(name.len >=4 && name.data[name.len - 4] == '.' &&
                                 name.data[name.len - 3] == 't' &&
                                 name.data[name.len - 2] == 'm' &&
                                 name.data[name.len - 1] == 'p'){
            max_age = playlen / 1000;
        }else if(name.len >= 4 && name.data[name.len - 4] == '.' &&
                                 name.data[name.len - 3] == 'm' &&
                                 name.data[name.len - 2] == 'p' &&
                                 name.data[name.len - 1] == '4'){
            max_age = 30 * 24 * 60 * 60;//1 month
        } else {
            ngx_log_debug1(NGX_LOG_DEBUG_RTMP, ngx_cycle->log, 0,
                           "transcode: cleanup skip unknown file type '%V'", &name);
            continue;
        }
        mtime = ngx_de_mtime(&dir);
        if (mtime + max_age > ngx_cached_time->sec) {
            continue;
        }

        ngx_log_debug3(NGX_LOG_DEBUG_RTMP, ngx_cycle->log, 0,
                       "transcode: cleanup '%V' mtime=%T age=%T",
                       &name, mtime, ngx_cached_time->sec - mtime);
        ngx_log_error(NGX_LOG_CRIT, ngx_cycle->log, ngx_errno,
                              "transcode: delete file %s", path);
        if (ngx_delete_file(path) == NGX_FILE_ERROR) {
            ngx_log_error(NGX_LOG_ERR, ngx_cycle->log, ngx_errno,
                          "transcode: cleanup " ngx_delete_file_n " failed on '%V'",
                          &spath);
            continue;
        }

        nerased++;
    }
    return NGX_OK;
}

#if (nginx_version >= 1011005)
static ngx_msec_t
#else
static time_t
#endif
ngx_rtmp_transcode_cleanup(void *data){
    // ngx_log_error(NGX_LOG_CRIT, ngx_cycle->log, 0,
    //                           "transcode: cleanup started");
    ngx_rtmp_transcode_cleanup_t *cleanup = data;

    ngx_rtmp_transcode_cleanup_dir(&cleanup->path, cleanup->playlen);

#if (nginx_version >= 1011005)
    return cleanup->playlen * 2;
#else
    return cleanup->playlen / 500;
#endif
}

static ngx_int_t
ngx_rtmp_transcode_ensure_directory(ngx_rtmp_session_t *s)
{
    // size_t                     len;
    ngx_file_info_t            fi;
    // ngx_rtmp_transcode_ctx_t       *ctx;
    ngx_rtmp_transcode_app_conf_t  *tacf;

    static u_char              path[NGX_MAX_PATH + 1];
    tacf = ngx_rtmp_get_module_app_conf(s, ngx_rtmp_transcode_module);

    *ngx_snprintf(path, sizeof(path) - 1, "%V", &tacf->path) = 0;
    if (ngx_file_info(path, &fi) == NGX_FILE_ERROR) {
        if (ngx_errno != NGX_ENOENT) {
            ngx_log_error(NGX_LOG_ERR, s->connection->log, ngx_errno,
                          "transcode: " ngx_file_info_n " failed on '%V'",
                          &tacf->path);
            return NGX_ERROR;
        }
        if (ngx_create_dir(path, NGX_RTMP_TRANSCODE_DIR_ACCESS) == NGX_FILE_ERROR) {
            ngx_log_error(NGX_LOG_ERR, s->connection->log, ngx_errno,
                          "transcode: " ngx_create_dir_n " failed on '%V'",
                          &tacf->path);
            return NGX_ERROR;
        }

        ngx_log_debug1(NGX_LOG_DEBUG_RTMP, s->connection->log, 0,
                       "transcode: directory '%V' created", &tacf->path);
    }else{
        if (!ngx_is_dir(&fi)) {
            ngx_log_error(NGX_LOG_ERR, s->connection->log, 0,
                          "transcode: '%V' exists and is not a directory",
                          &tacf->path);
            return  NGX_ERROR;
        }

        ngx_log_debug1(NGX_LOG_DEBUG_RTMP, s->connection->log, 0,
                       "transcode: directory '%V' exists", &tacf->path);
    }
    return NGX_OK;
}

static ngx_int_t
ngx_rtmp_transcode_postconfiguration(ngx_conf_t *cf)
{

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