/*
 * To change this license header, choose License Headers in Project Properties.
 * To change this template file, choose Tools | Templates
 * and open the template in the editor.
 */

/* 
 * File:   ngx_rtmp_hls_module.h
 * Author: ducla
 *
 * Created on September 26, 2019, 5:22 PM
 */

#ifndef NGX_RTMP_HLS_MODULE_H
#define NGX_RTMP_HLS_MODULE_H

#include "ngx_rtmp_mpegts.h"

typedef struct {
    uint64_t                            id;
    uint64_t                            key_id;
    ngx_str_t                          *datetime;
    double                              duration;
    unsigned                            active:1;
    unsigned                            discont:1; /* before */
} ngx_rtmp_hls_frag_t;


typedef struct {
    ngx_str_t                           suffix;
    ngx_array_t                         args;
} ngx_rtmp_hls_variant_t;


typedef struct {
    unsigned                            opened:1;

    ngx_rtmp_mpegts_file_t              file;

    ngx_str_t                           playlist;
    ngx_str_t                           playlist_bak;
    ngx_str_t                           var_playlist;
    ngx_str_t                           var_playlist_bak;
    ngx_str_t                           stream;
    ngx_str_t                           keyfile;
    ngx_str_t                           name;
    u_char                              key[16];

    uint64_t                            frag;
    uint64_t                            frag_ts;
    uint64_t                            key_id;
    ngx_uint_t                          nfrags;
    ngx_rtmp_hls_frag_t                *frags; /* circular 2 * winfrags + 1 */

    ngx_uint_t                          audio_cc;
    ngx_uint_t                          video_cc;
    ngx_uint_t                          key_frags;

    uint64_t                            aframe_base;
    uint64_t                            aframe_num;

    ngx_buf_t                          *aframe;
    uint64_t                            aframe_pts;
    ngx_str_t                           stream_id;

    ngx_rtmp_hls_variant_t             *var;
} ngx_rtmp_hls_ctx_t;

typedef struct {
    ngx_flag_t                          hls;
    ngx_msec_t                          fraglen;
    ngx_msec_t                          max_fraglen;
    ngx_msec_t                          muxdelay;
    ngx_msec_t                          sync;
    ngx_msec_t                          playlen;
    ngx_uint_t                          winfrags;
    ngx_flag_t                          continuous;
    ngx_flag_t                          nested;
    ngx_str_t                           path;
    ngx_uint_t                          naming;
    ngx_uint_t                          datetime;
    ngx_uint_t                          slicing;
    ngx_uint_t                          type;
    ngx_path_t                         *slot;
    ngx_msec_t                          max_audio_delay;
    size_t                              audio_buffer_size;
    ngx_flag_t                          cleanup;
    ngx_array_t                        *variant;
    ngx_str_t                           base_url;
    ngx_int_t                           granularity;
    ngx_flag_t                          keys;
    ngx_str_t                           key_path;
    ngx_str_t                           key_url;
    ngx_uint_t                          frags_per_key;
    ngx_flag_t                          hide_stream_key;
} ngx_rtmp_hls_app_conf_t;

extern ngx_module_t  ngx_rtmp_hls_module;

#endif /* NGX_RTMP_HLS_MODULE_H */

