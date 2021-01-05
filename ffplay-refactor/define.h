#pragma once
#include <stdint.h>
extern "C"
{
#include <libswscale\swscale.h>
#include <libavformat\avformat.h>
};

/* 通用Frame结构，包含了解码的视音频和字幕数据 */
struct Frame
{
	AVFrame* frame;
	AVSubtitle sub;
	int serial;
	double pts;
	double duration;
	int64_t pos;
	int width;
	int height;
	int format;
	AVRational sar;
	int uploaded;
	int flip_v;
};

/* 音频参数，用于复制SDL中与FFmpeg兼容的参数并加上符合FFmpeg的参数 */
struct AudioParams
{
	int freq;
	int channels;
	int64_t channel_layout;
	enum AVSampleFormat fmt;
	int frame_size;
	int bytes_per_sec;
};