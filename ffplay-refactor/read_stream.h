#ifndef READ_STREAM_H_
#define READ_STREAM_H_

#include "play_controller.h"
#include "decoder.h"
#include "video.h"
#include "audio.h"

int decode_interrupt_cb(void *ctx);
int is_realtime(AVFormatContext *ctx);
int stream_has_enough_packets(AVStream *st, int stream_id, PacketQueue *queue);
int is_pkt_in_play_range(MediaState*ms, AVFormatContext* ic, AVPacket* pkt);

inline int allow_loop()
{
	return 1;
}

inline int64_t get_stream_start_time(AVFormatContext * ic, int index)
{
	int64_t stream_start_time = ic->streams[index]->start_time;
	return stream_start_time != AV_NOPTS_VALUE ? stream_start_time : 0;
}

inline int64_t get_pkt_ts(AVPacket * pkt)
{
	return pkt->pts == AV_NOPTS_VALUE ? pkt->dts : pkt->pts;
}

inline double ts_as_second(int64_t ts, AVFormatContext * ic, int index)
{
	return ts * av_q2d(ic->streams[index]->time_base);
}

inline double get_ic_start_time(MediaState* ms, AVFormatContext * ic)
{   
	return (ms->start_time != AV_NOPTS_VALUE ? ms->start_time : 0) / 1000000;
}

int stream_component_open(MediaState *ms, int stream_index);
void stream_component_close(MediaState *ms, int stream_index);
int read_thread(void *arg);
int refresh_thread(void* arg);
void stream_close(MediaState *ms);
bool stream_open(MediaState* ms, const char *filename);

void do_exit(MediaState *ms);


#endif // !READ_STREAM_H_

