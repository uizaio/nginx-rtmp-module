

#ifndef _NGX_RTMP_FMP4_H_INCLUDED_
#define _NGX_RTMP_FMP4_H_INCLUDED_


#include <ngx_config.h>
#include <ngx_core.h>
#include <ngx_rtmp.h>


ngx_int_t
ngx_rtmp_mp4_write_ftyp();
static u_char *
ngx_rtmp_mp4_start_box(ngx_buf_t *b, const char box[4]);
static ngx_int_t 
ngx_rtmp_fmp4_box(ngx_buf_t *b, const char box[4]);
static ngx_int_t
ngx_rtmp_fmp4_write_ftyp(ngx_buf_t *b);
static ngx_int_t
ngx_rtmp_fmp4_write_matrix(ngx_buf_t *buf, uint32_t a, uint32_t b, uint32_t c, uint32_t d, uint32_t tx, uint32_t ty);
static ngx_int_t ngx_rtmp_fmp4_write_mdhd(ngx_buf_t *b);
static ngx_int_t ngx_rtmp_fmp4_write_hdlr(ngx_buf_t *b, int isVideo);
static ngx_int_t ngx_rtmp_fmp4_data(ngx_buf_t *b, void *data, size_t n);
static ngx_int_t ngx_rtmp_fmp4_write_vmhd(ngx_buf_t *b);
static ngx_int_t ngx_rtmp_fmp4_write_smhd(ngx_buf_t *b);
static ngx_int_t ngx_rtmp_fmp4_write_dref(ngx_buf_t *b);
static ngx_int_t ngx_rtmp_fmp4_write_dinf(ngx_buf_t *b);
static ngx_int_t ngx_rtmp_fmp4_write_avc(ngx_rtmp_session_t *s, ngx_buf_t *b);
static ngx_int_t ngx_rtmp_mp4_write_mp4a(ngx_rtmp_session_t *s, ngx_buf_t *b);
static ngx_int_t ngx_rtmp_fmp4_write_esds(ngx_rtmp_session_t *s, ngx_buf_t *b);
static ngx_int_t ngx_rtmp_fmp4_write_stsd(ngx_rtmp_session_t *s, ngx_buf_t *b, int isVideo);
static ngx_int_t ngx_rtmp_fmp4_write_stsc(ngx_buf_t *b);
static ngx_int_t ngx_rtmp_fmp4_write_stts(ngx_buf_t *b);
static ngx_int_t ngx_rtmp_fmp4_write_stsz(ngx_buf_t *b);
static ngx_int_t ngx_rtmp_fmp4_write_stco(ngx_buf_t *b);
static ngx_int_t ngx_rtmp_fmp4_write_stbl(ngx_rtmp_session_t *s, ngx_buf_t *b, int isVideo);
static ngx_int_t ngx_rtmp_fmp4_write_minf(ngx_rtmp_session_t *s, ngx_buf_t *b, int isVideo);
static ngx_int_t ngx_rtmp_fmp4_write_mdia(ngx_buf_t *b,int isVideo);
static ngx_int_t ngx_rtmp_fmp4_write_trak(ngx_rtmp_session_t *s, ngx_buf_t *b, int isVideo);
static ngx_int_t ngx_rtmp_fmp4_write_tkhd(ngx_rtmp_session_t *s, ngx_buf_t *b, int isVideo);
ngx_int_t ngx_rtmp_fmp4_write_moov(ngx_rtmp_session_t *s, ngx_buf_t *b);
static ngx_int_t ngx_rtmp_fmp4_write_mvex(ngx_buf_t *b);
static ngx_int_t ngx_rtmp_fmp4_write_mvhd(ngx_buf_t *b);
static u_char * ngx_rtmp_fmp4_start_box(ngx_buf_t *b, const char box[4]);
static ngx_int_t ngx_rtmp_fmp4_update_box_size(ngx_buf_t *b, u_char *p);
static ngx_int_t ngx_rtmp_fmp4_write_meta(ngx_buf_t *b);
static ngx_int_t ngx_rtmp_fmp4_write_udta(ngx_buf_t *b);
static ngx_int_t ngx_rtmp_fmp4_write_avcc(ngx_rtmp_session_t *s, ngx_buf_t *b);
static ngx_int_t ngx_rtmp_fmp4_put_descr(ngx_buf_t *b, int tag, size_t size);


#endif /* _NGX_RTMP_FMP4_H_INCLUDED_ */
