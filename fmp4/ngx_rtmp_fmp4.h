

#ifndef _NGX_RTMP_FMP4_H_INCLUDED_
#define _NGX_RTMP_FMP4_H_INCLUDED_


#include <ngx_config.h>
#include <ngx_core.h>
#include <ngx_rtmp.h>


#define NGX_RTMP_FMP4_SAMPLE_SIZE        0x01
#define NGX_RTMP_FMP4_SAMPLE_DURATION    0x02
#define NGX_RTMP_FMP4_SAMPLE_DELAY       0x04
#define NGX_RTMP_FMP4_SAMPLE_KEY         0x08

typedef struct {
    uint32_t        size;
    uint32_t        duration;
    uint32_t        delay;
    uint32_t        timestamp;
    unsigned        key:1;
} ngx_rtmp_fmp4_sample_t;

ngx_int_t ngx_rtmp_mp4_write_ftyp(ngx_buf_t *b);
u_char * ngx_rtmp_fmp4_start_box(ngx_buf_t *b, const char box[4]);
ngx_int_t  ngx_rtmp_fmp4_box(ngx_buf_t *b, const char box[4]);
ngx_int_t ngx_rtmp_fmp4_write_ftyp(ngx_buf_t *b);
ngx_int_t ngx_rtmp_fmp4_write_matrix(ngx_buf_t *buf, uint32_t a, uint32_t b, uint32_t c, uint32_t d, uint32_t tx, uint32_t ty);
ngx_int_t ngx_rtmp_fmp4_write_mdhd(ngx_buf_t *b);
ngx_int_t ngx_rtmp_fmp4_write_hdlr(ngx_buf_t *b, int isVideo);
ngx_int_t ngx_rtmp_fmp4_data(ngx_buf_t *b, void *data, size_t n);
ngx_int_t ngx_rtmp_fmp4_write_vmhd(ngx_buf_t *b);
ngx_int_t ngx_rtmp_fmp4_write_smhd(ngx_buf_t *b);
ngx_int_t ngx_rtmp_fmp4_write_dref(ngx_buf_t *b);
ngx_int_t ngx_rtmp_fmp4_write_dinf(ngx_buf_t *b);
ngx_int_t ngx_rtmp_fmp4_write_avc(ngx_rtmp_session_t *s, ngx_buf_t *b);
ngx_int_t ngx_rtmp_fmp4_write_mp4a(ngx_rtmp_session_t *s, ngx_buf_t *b);
ngx_int_t ngx_rtmp_fmp4_write_esds(ngx_rtmp_session_t *s, ngx_buf_t *b);
ngx_int_t ngx_rtmp_fmp4_write_stsd(ngx_rtmp_session_t *s, ngx_buf_t *b, int isVideo);
ngx_int_t ngx_rtmp_fmp4_write_stsc(ngx_buf_t *b);
ngx_int_t ngx_rtmp_fmp4_write_stts(ngx_buf_t *b);
ngx_int_t ngx_rtmp_fmp4_write_stsz(ngx_buf_t *b);
ngx_int_t ngx_rtmp_fmp4_write_stco(ngx_buf_t *b);
ngx_int_t ngx_rtmp_fmp4_write_stbl(ngx_rtmp_session_t *s, ngx_buf_t *b, int isVideo);
ngx_int_t ngx_rtmp_fmp4_write_minf(ngx_rtmp_session_t *s, ngx_buf_t *b, int isVideo);
ngx_int_t ngx_rtmp_fmp4_write_mfhd(ngx_buf_t *b, uint32_t index);
ngx_int_t ngx_rtmp_fmp4_write_mdia(ngx_rtmp_session_t *s, ngx_buf_t *b,int isVideo);
ngx_int_t ngx_rtmp_fmp4_write_trak(ngx_rtmp_session_t *s, ngx_buf_t *b, int isVideo);
ngx_int_t ngx_rtmp_fmp4_write_tkhd(ngx_rtmp_session_t *s, ngx_buf_t *b, int isVideo);
ngx_int_t ngx_rtmp_fmp4_write_moov(ngx_rtmp_session_t *s, ngx_buf_t *b);
ngx_int_t ngx_rtmp_fmp4_write_mvex(ngx_buf_t *b);
ngx_int_t ngx_rtmp_fmp4_write_mvhd(ngx_buf_t *b);
u_char * ngx_rtmp_fmp4_start_box(ngx_buf_t *b, const char box[4]);
ngx_int_t ngx_rtmp_fmp4_update_box_size(ngx_buf_t *b, u_char *p);
ngx_int_t ngx_rtmp_fmp4_write_meta(ngx_buf_t *b);
ngx_int_t ngx_rtmp_fmp4_write_udta(ngx_buf_t *b);
ngx_int_t ngx_rtmp_fmp4_write_avcc(ngx_rtmp_session_t *s, ngx_buf_t *b);
ngx_int_t ngx_rtmp_fmp4_put_descr(ngx_buf_t *b, int tag, size_t size);
ngx_int_t ngx_rtmp_fmp4_write_moof(ngx_buf_t *b, uint32_t video_earliest_pres_time,
    uint32_t video_sample_count, ngx_rtmp_fmp4_sample_t *video_samples,
    ngx_uint_t video_sample_mask, uint32_t audio_earliest_pres_time, int32_t audio_sample_count, ngx_rtmp_fmp4_sample_t *audio_samples,
    ngx_uint_t audio_sample_mask, uint32_t index);
ngx_int_t ngx_rtmp_fmp4_write_traf(ngx_buf_t *b, uint32_t earliest_pres_time,
    uint32_t sample_count, ngx_rtmp_fmp4_sample_t *samples,
    ngx_uint_t sample_mask, u_char *moof_pos, uint32_t next_sample_count, 
    ngx_rtmp_fmp4_sample_t *next_samples, ngx_uint_t next_sample_mask, int isVideo);
ngx_int_t ngx_rtmp_fmp4_write_tfhd(ngx_buf_t *b, uint32_t track_id);
ngx_int_t ngx_rtmp_fmp4_write_tfdt(ngx_buf_t *b, uint32_t earliest_pres_time);
ngx_int_t ngx_rtmp_fmp4_write_trun(ngx_buf_t *b, uint32_t sample_count,
    ngx_rtmp_fmp4_sample_t *samples, ngx_uint_t sample_mask, u_char *moof_pos, 
    uint32_t next_sample_count,
    ngx_rtmp_fmp4_sample_t *next_samples, ngx_uint_t next_sample_mask, uint32_t isVideo);
ngx_int_t ngx_rtmp_fmp4_write_sidx(ngx_buf_t *b, uint32_t earliest_pres_time, uint32_t latest_pres_time,
 ngx_uint_t reference_size, uint32_t reference_id);
 ngx_uint_t ngx_rtmp_fmp4_write_mdat(ngx_buf_t *b, ngx_uint_t size);


#endif /* _NGX_RTMP_FMP4_H_INCLUDED_ */
