#pragma once
#include <stdint.h>
extern "C"
{
#include <libswscale\swscale.h>
#include <libavformat\avformat.h>
};

/* ͨ��Frame�ṹ�������˽��������Ƶ����Ļ���� */
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

/* ��Ƶ���������ڸ���SDL����FFmpeg���ݵĲ��������Ϸ���FFmpeg�Ĳ��� */
struct AudioParams
{
	int freq;
	int channels;
	int64_t channel_layout;
	enum AVSampleFormat fmt;
	int frame_size;
	int bytes_per_sec;
};