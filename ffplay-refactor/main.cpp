#include "play_controller.h"
#include "read_stream.h"

int left_thread(void* arg)
{
	MediaState* ms = (MediaState*)(arg);
	// 打开流
	bool result = stream_open(ms, ms->input_filename);
	if (!result)
	{
		av_log(NULL, AV_LOG_FATAL, "Failed to initialize MediaState!\n");
		do_exit(ms);
	}

	return 0;
}

int right_thread(void* arg)
{
	MediaState* ms_right_ = (MediaState*)(arg);

	// 打开流
	bool result = stream_open(ms_right_, ms_right_->input_filename);
	if (!result)
	{
		av_log(NULL, AV_LOG_FATAL, "Failed to initialize MediaState!\n");
		do_exit(ms_right_);
	}

	return 0;
}

int top_thread(void* arg)
{
	MediaState* ms_top_ = (MediaState*)(arg);

	// 打开流
	bool result = stream_open(ms_top_, ms_top_->input_filename);
	if (!result)
	{
		av_log(NULL, AV_LOG_FATAL, "Failed to initialize MediaState!\n");
		do_exit(ms_top_);
	}

	return 0;
}

int bottom_thread(void* arg)
{
	MediaState* ms_bottom_ = (MediaState*)(arg);

	// 打开流
	bool result = stream_open(ms_bottom_, ms_bottom_->input_filename);
	if (!result)
	{
		av_log(NULL, AV_LOG_FATAL, "Failed to initialize MediaState!\n");
		do_exit(ms_bottom_);
	}

	return 0;
}

int main(int argc, char* argv[])
{
	av_init_packet(&MediaState::flush_pkt);
	MediaState::flush_pkt.data = (uint8_t*)&MediaState::flush_pkt;
	MediaState* ms = (MediaState*)av_malloc(sizeof(MediaState));
	MediaState* ms_right_ = (MediaState*)av_malloc(sizeof(MediaState));
	MediaState* ms_top_ = (MediaState*)av_malloc(sizeof(MediaState));
	MediaState* ms_bottom_ = (MediaState*)av_malloc(sizeof(MediaState));
	MediaState::global_ms_ = ms;
	MediaState::global_ms_right_ = ms_right_;
	MediaState::global_ms_top_ = ms_top_;
	MediaState::global_ms_bottom_ = ms_bottom_;
	ms->init();
	ms_right_->init();
	ms_top_->init();
	ms_bottom_->init();
	ms->is_master_ = true;
	
	int video_width = 960;
	int video_height = 540;

	ms->xleft = 0;
	ms->ytop = 0;
	ms->input_filename = "D:\\ffmpeg\\5-1.MP4";
	ms->rect_.x = ms->xleft;
	ms->rect_.y = ms->ytop;
	ms->rect_.w = video_width;
	ms->rect_.h = video_height;
	ms->default_width = video_width;
	ms->default_height = video_height;

	ms_right_->xleft = video_width;
	ms_right_->ytop = 0;
	ms_right_->input_filename = "D:\\ffmpeg\\5-2.MP4";
	ms_right_->rect_.x = ms_right_->xleft;
	ms_right_->rect_.y = ms_right_->ytop;
	ms_right_->rect_.w = video_width;
	ms_right_->rect_.h = video_height;

	ms_top_->xleft = 0;
	ms_top_->ytop = video_height;
	ms_top_->input_filename = "D:\\ffmpeg\\5-3.MP4";
	ms_top_->rect_.x = ms_top_->xleft;
	ms_top_->rect_.y = ms_top_->ytop;
	ms_top_->rect_.w = video_width;
	ms_top_->rect_.h = video_height;

	ms_bottom_->xleft = video_width;
	ms_bottom_->ytop = video_height;
	ms_bottom_->input_filename = "D:\\ffmpeg\\5-4.MP4";
	ms_bottom_->rect_.x = ms_bottom_->xleft;
	ms_bottom_->rect_.y = ms_bottom_->ytop;
	ms_bottom_->rect_.w = video_width;
	ms_bottom_->rect_.h = video_height;

	av_log_set_flags(AV_LOG_SKIP_REPEATED);

	//avdevice_register_all();
	avformat_network_init();

	/* 初始化SDL子系统 */
	if (SDL_Init(SDL_INIT_VIDEO | SDL_INIT_AUDIO | SDL_INIT_TIMER))
	{
		av_log(NULL, AV_LOG_FATAL, "Could not initialize SDL - %s\n", SDL_GetError());
		av_log(NULL, AV_LOG_FATAL, "(Did you set the DISPLAY variable?)\n");
		exit(1);
	}

	//设置事件的处理状态，以下两种事件将自动从事件队列中删除
	SDL_EventState(SDL_SYSWMEVENT, SDL_IGNORE);
	SDL_EventState(SDL_USEREVENT, SDL_IGNORE);

	MediaState::window = SDL_CreateWindow("OnePlayer1", 0, 0,
		/*ms_bottom_->rect_.x + */video_width * 2,
		/*ms_bottom_->rect_.y + */video_height * 2,
		SDL_WINDOW_OPENGL);
	if (MediaState::window)
	{
		MediaState::renderer = SDL_CreateRenderer(MediaState::window, -1, SDL_RENDERER_ACCELERATED | SDL_RENDERER_PRESENTVSYNC);
		if (!MediaState::renderer)
		{
			av_log(NULL, AV_LOG_WARNING, "Failed to initialize a hardware accelerated renderer: %s\n", SDL_GetError());
			MediaState::renderer = SDL_CreateRenderer(MediaState::window, -1, 0);
		}
		if (MediaState::renderer)
		{
			if (!SDL_GetRendererInfo(MediaState::renderer, &MediaState::renderer_info))
				av_log(NULL, AV_LOG_VERBOSE, "Initialized %s renderer.\n", MediaState::renderer_info.name);
		}
	}
	if (!MediaState::window || !MediaState::renderer || !MediaState::renderer_info.num_texture_formats)
	{
		av_log(NULL, AV_LOG_FATAL, "Failed to create window or renderer: %s", SDL_GetError());
		do_exit(ms);
	}
	SDL_ShowWindow(MediaState::window);
	SDL_Thread* left_thread_ = SDL_CreateThread(left_thread, "Left_Thread", ms);
	SDL_Thread* right_thread_ = SDL_CreateThread(right_thread, "Right_Thread", ms_right_);
	SDL_Thread* top_thread_ = SDL_CreateThread(top_thread, "top_Thread", ms_top_);
	SDL_Thread* bottom_thread_ = SDL_CreateThread(bottom_thread, "Bottm_Thread", ms_bottom_);

	//进入事件循环
	SDL_WaitThread(left_thread_, NULL);
	SDL_WaitThread(right_thread_, NULL);
	SDL_WaitThread(top_thread_, NULL);
	SDL_WaitThread(bottom_thread_, NULL);
	event_loop_event(ms, ms_right_, ms_top_, ms_bottom_);

	return 0;
}


