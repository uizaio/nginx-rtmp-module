
/*
 * Copyright (C) NamND
 */


#ifndef _NGX_RTMP_STAT_MODULE_H_INCLUDED_
#define _NGX_RTMP_STAT_MODULE_H_INCLUDED_


#define NGX_RTMP_STAT_FORMAT_NONE          0
#define NGX_RTMP_STAT_FORMAT_XML           1
#define NGX_RTMP_STAT_FORMAT_PROMETHEUS    2

#define STYLESHEET_DEFAULT "static/stat.xsl"

// 40Kb
#define METRIC_DEFAULT_SIZE 40960
// rtmp

#define RTMP_PID_FMT "# HELP node_rtmp_pid pid value of rtmp.\n"  \
                    "# TYPE node_rtmp_pid gauge\n"  \
                    "node_rtmp_pid{ngx_version=\"%s\",rtmp_version=\"%s\"} %ui\n"

#define RTMP_UPTIME_FMT "# HELP node_rtmp_uptime_seconds This is the total number of seconds uptime.\n"    \
                    "# TYPE node_rtmp_uptime_seconds counter\n" \
                    "node_rtmp_uptime_seconds{ngx_version=\"%s\",rtmp_version=\"%s\"} %T\n"
                
#define RTMP_NACCEPTED_FMT  "# HELP node_rtmp_naccepted Total number of rtmp accepted.\n"  \
                    "# TYPE node_rtmp_naccepted counter\n"  \
                    "node_rtmp_naccepted{ngx_version=\"%s\",rtmp_version=\"%s\"} %ui\n"

#define RTMP_BW_IN_FMT  "# HELP node_rtmp_bw_in_bits Bits of bandwidth.\n" \
                    "# TYPE node_rtmp_bw_in_bits gauge\n"   \
                    "node_rtmp_bw_in_bits{ngx_version=\"%s\",rtmp_version=\"%s\"} %uL\n"

#define RTMP_BYTES_IN_FMT "# HELP node_rtmp_bytes_in_bytes Number of bytes inputted and still.\n" \
                    "# TYPE node_rtmp_bytes_in_bytes gauge\n"   \
                    "node_rtmp_bytes_in_bytes{ngx_version=\"%s\",rtmp_version=\"%s\"} %uL\n"

#define RTMP_BW_OUT_FMT "# HELP node_rtmp_bw_out_bits Bits of bandwidth.\n" \
                    "# TYPE node_rtmp_bw_out_bits gauge\n"   \
                    "node_rtmp_bw_out_bits{ngx_version=\"%s\",rtmp_version=\"%s\"} %uL\n"

#define RTMP_BYTES_OUT_FMT  "# HELP node_rtmp_bytes_out_bytes Number of bytes outputted and still.\n" \
                    "# TYPE node_rtmp_bytes_out_bytes gauge\n"   \
                    "node_rtmp_bytes_out_bytes{ngx_version=\"%s\",rtmp_version=\"%s\"} %uL\n"
// server/application/stream
#define RTMP_STREAM_TIME_FMT    "# HELP node_rtmp_stream_time_milliseconds This is the total number of milliseconds time.\n"    \
                    "# TYPE node_rtmp_stream_time_milliseconds counter\n" \
                    "node_rtmp_stream_time_milliseconds{pid=\"%ui\",app_name=\"%V\",session_id=\"\",stream_name=\"%s\"} %i\n"

#define RTMP_STREAM_BW_IN_FMT   "# HELP node_rtmp_stream_bw_in_bits Bandwidth inputted of stream.\n" \
                    "# TYPE node_rtmp_stream_bw_in_bits gauge\n"   \
                    "node_rtmp_stream_bw_in_bits{pid=\"%ui\",app_name=\"%V\",stream_name=\"%s\"} %uL\n"

#define RTMP_STREAM_BYTES_IN_FMT    "# HELP node_rtmp_stream_bytes_in_bytes Bytes inputted of stream and still.\n" \
                    "# TYPE node_rtmp_stream_bytes_in_bytes gauge\n"   \
                    "node_rtmp_stream_bytes_in_bytes{pid=\"%ui\",app_name=\"%V\",stream_name=\"%s\"} %i\n"

#define RTMP_STREAM_BW_OUT_FMT  "# HELP node_rtmp_stream_bw_out_bits Bandwidth outputted of stream.\n" \
                    "# TYPE node_rtmp_stream_bw_out_bits gauge\n"   \
                    "node_rtmp_stream_bw_out_bits{pid=\"%ui\",app_name=\"%V\",stream_name=\"%s\"} %uL\n"

#define RTMP_STREAM_BYTES_OUT_FMT   "# HELP node_rtmp_stream_bytes_out_bytes Bytes outputted of stream and still.\n" \
                    "# TYPE node_rtmp_stream_bytes_out_bytes gauge\n"   \
                    "node_rtmp_stream_bytes_out_bytes{pid=\"%ui\",app_name=\"%V\",stream_name=\"%s\"} %i\n"

#define RTMP_STREAM_BW_AUDIO_FMT    "# HELP node_rtmp_stream_bw_audio_bits Audio bandwidth of stream.\n" \
                    "# TYPE node_rtmp_stream_bw_audio_bits gauge\n"   \
                    "node_rtmp_stream_bw_audio_bits{pid=\"%ui\",app_name=\"%V\",stream_name=\"%s\"} %uL\n"

#define RTMP_STREAM_BW_VIDEO_FMT    "# HELP node_rtmp_stream_bw_video_bits Video bandwidth of stream.\n" \
                    "# TYPE node_rtmp_stream_bw_video_bits gauge\n"   \
                    "node_rtmp_stream_bw_video_bits{pid=\"%ui\",app_name=\"%V\",stream_name=\"%s\"} %uL\n"

#define RTMP_STREAM_NCLIENTS_FMT    "# HELP node_rtmp_stream_nclients Total client of stream.\n"  \
                    "# TYPE node_rtmp_stream_nclients counter\n"  \
                    "node_rtmp_stream_nclients{pid=\"%ui\",app_name=\"%V\",session_id=\"\",stream_name=\"%s\"} %ui\n"

// client
#define RTMP_CLIENT_ID_FMT    "# HELP node_rtmp_client_id Id of client.\n"  \
                    "# TYPE node_rtmp_client_id untyped\n"  \
                    "node_rtmp_client_id{pid=\"%ui\",app_name=\"%V\",session_id=\"\",stream_name=\"%s\",client=\"%V\"} %ui\n"

#define RTMP_CLIENT_TIME_FMT    "# HELP node_rtmp_client_time_milliseconds This is the total milliseconds of a client.\n"    \
                    "# TYPE node_rtmp_client_time_milliseconds counter\n" \
                    "node_rtmp_client_time_milliseconds{pid=\"%ui\",app_name=\"%V\",session_id=\"\",stream_name=\"%s\",client=\"%V\"} %i\n"

#define RTMP_CLIENT_DROPPED_FMT "# HELP node_rtmp_client_dropped.\n"    \
                    "# TYPE node_rtmp_client_dropped gauge\n" \
                    "node_rtmp_client_dropped{pid=\"%ui\",app_name=\"%V\",session_id=\"\",stream_name=\"%s\",client=\"%V\"} %ui\n"

#define RTMP_CLIENT_AVSYNC_FMT  "# HELP node_rtmp_client_avsync Number of clients dropped.\n"    \
                    "# TYPE node_rtmp_client_avsync counter\n" \
                    "node_rtmp_client_avsync{pid=\"%ui\",app_name=\"%V\",session_id=\"\",stream_name=\"%s\",client=\"%V\"} %D\n"

#define RTMP_CLIENT_TIMESTAMP_FMT   "# HELP node_rtmp_client_timestamp_milliseconds.\n"    \
                    "# TYPE node_rtmp_client_timestamp_milliseconds counter\n"   \
                    "node_rtmp_client_timestamp_milliseconds{pid=\"%ui\",app_name=\"%V\",session_id=\"\",stream_name=\"%s\",client=\"%V\"} %D\n"
// meta/video
#define RTMP_META_VIDEO_FPS_FMT    "# HELP node_rtmp_meta_video_fps Frame Rate of video.\n"    \
                    "# TYPE node_rtmp_meta_video_fps gauge\n"   \
                    "node_rtmp_meta_video_fps{pid=\"%ui\",app_name=\"%V\",session_id=\"\",stream_name=\"%s\",codec=\"%s %s %.1f\",res=\"%uix%ui\"} %ui\n"

#define RTMP_META_VIDEO_TIME_FMT    "# HELP node_rtmp_meta_video_time_milliseconds This is the total number of milliseconds time.\n"    \
                    "# TYPE node_rtmp_meta_video_time_milliseconds counter\n" \
                    "node_rtmp_meta_video_time_milliseconds{pid=\"%ui\",app_name=\"%V\",session_id=\"\",stream_name=\"%s\",codec=\"%s %s %.1f\",res=\"%uix%ui\",fps=\"%ui\"} %i\n"

#define RTMP_META_VIDEO_COMPAT_FMT  "# HELP node_rtmp_meta_video_compat.\n"    \
                    "# TYPE node_rtmp_meta_video_compat untyped\n"   \
                    "node_rtmp_meta_video_compat{pid=\"%ui\",app_name=\"%V\",session_id=\"\",stream_name=\"%s\",codec=\"%s %s %.1f\",res=\"%uix%ui\",fps=\"%ui\"} %ui\n"
// meta/audio
#define RTMP_META_AUDIO_FREQ_FMT    "# HELP node_rtmp_meta_audio_freq_Hz Sample Rate of audio.\n"    \
                    "# TYPE node_rtmp_meta_audio_freq_Hz gauge\n"   \
                    "node_rtmp_meta_audio_freq_Hz{pid=\"%ui\",app_name=\"%V\",session_id=\"\",stream_name=\"%s\",codec=\"%s %s\",channels=\"%ui\"} %ui\n"
#endif /* _NGX_RTMP_STAT_MODULE_H_INCLUDED_ */
