#include "media_state.h"
MediaState* MediaState::global_ms_ = NULL;
MediaState* MediaState::global_ms_right_ = NULL;
MediaState* MediaState::global_ms_top_ = NULL;
MediaState* MediaState::global_ms_bottom_ = NULL;

AVPacket MediaState::flush_pkt = { 0 };

SDL_RendererInfo MediaState::renderer_info = {0};
SDL_Renderer* MediaState::renderer = NULL;
SDL_Window* MediaState::window = NULL;
SDL_AudioDeviceID MediaState::audio_dev;

MediaState::~MediaState()
{
}

/* 检测FrameQueue是否可读，然后对Frame的相关变量进行赋值并写入队列 */
int MediaState::queue_picture(AVFrame * src_frame, double pts, double duration, int64_t pos, int serial)
{
	Frame *vp;

	//取FrameQueue的当前可写的节点赋给vp
	if (!(vp = pict_fq.frame_queue_peek_writable()))
		return -1;

	//拷贝解码好的帧的相关数据给节点保存
	vp->sar = src_frame->sample_aspect_ratio;
	vp->uploaded = 0;

	vp->width = src_frame->width;
	vp->height = src_frame->height;
	vp->format = src_frame->format;

	vp->pts = pts;
	vp->duration = duration;
	vp->pos = pos;
	vp->serial = serial;

	set_default_window_size(vp->width, vp->height, vp->sar);
	this->default_width = vp->width;
	this->default_height = vp->height;
	av_frame_move_ref(vp->frame, src_frame);

	//push写好的节点到队列
	pict_fq.frame_queue_push();
	return 0;

}

/* 获取主时钟的类型 */
int MediaState::get_master_sync_type()
{
	if (av_sync_type == AV_SYNC_VIDEO_MASTER) 
	{
		if (video_st)
			return AV_SYNC_VIDEO_MASTER;
		else
			return AV_SYNC_AUDIO_MASTER;
	}
	else if (av_sync_type == AV_SYNC_AUDIO_MASTER) 
	{
		if (audio_st)
			return AV_SYNC_AUDIO_MASTER;
		else
			return AV_SYNC_EXTERNAL_CLOCK;
	}
	else 
	{
		return AV_SYNC_EXTERNAL_CLOCK;
	}
}

/* 获取主时钟的当前值 */
double MediaState::get_master_clock()
{
	double val;
	switch (get_master_sync_type()) 
	{
	case AV_SYNC_VIDEO_MASTER:
		val = vidclk.get_clock();
		break;
	case AV_SYNC_AUDIO_MASTER:
		val = audclk.get_clock();
		break;
	default:
		val = extclk.get_clock();
		break;
	}
	return val;
}

double MediaState::get_video_clock()
{
	return audclk.get_clock();
}

double MediaState::get_audio_clock()
{
	return audclk.get_clock();
}

double MediaState::get_external_clock()
{
	return extclk.get_clock();
}

void MediaState::sync_clock_to_slave(Clock * c, Clock * slave)
{
	double clock = c->get_clock();
	double slave_clock = slave->get_clock();
	if (!isnan(slave_clock) && (isnan(clock) || fabs(clock - slave_clock) > AV_NOSYNC_THRESHOLD))
		c->set_clock(slave_clock, *slave->get_serial());
}

void MediaState::init()
{
	is_master_ = false;
	screen_width = 0;
	screen_height = 0;
	loop = 1;
	autoexit = 1;
	framedrop = -1;
	show_status = 1;
	rdftspeed = 0.02;
	sws_flags = SWS_BICUBIC;    //图像转换默认算法
	start_time = AV_NOPTS_VALUE;
	duration = AV_NOPTS_VALUE;
	cursor_hidden = 0;
	exit_on_keydown = 0;
	exit_on_mousedown = 0;
	is_full_screen = 0;
	seek_by_bytes = -1;
	seek_interval = 10;
	renderer_info = { 0 };
}

