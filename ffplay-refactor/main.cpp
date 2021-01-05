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
	ms->init();
	ms_right_->init();
	ms_top_->init();
	ms_bottom_->init();
	ms->is_master_ = true;

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

	ms->input_filename = "D:\\ffmpeg\\ffmpeg-4.2.1-win32-shared\\bin\\7-1.MP4";
	ms->window = SDL_CreateWindow("OnePlayer1", 0, 0,
		ms->default_width, ms->default_height, SDL_WINDOW_OPENGL | SDL_WINDOW_BORDERLESS);
	SDL_SetHint(SDL_HINT_RENDER_SCALE_QUALITY, "linear");
	if (ms->window)
	{
		ms->renderer = SDL_CreateRenderer(ms->window, -1, SDL_RENDERER_ACCELERATED | SDL_RENDERER_PRESENTVSYNC);
		if (!ms->renderer)
		{
			av_log(NULL, AV_LOG_WARNING, "Failed to initialize a hardware accelerated renderer: %s\n", SDL_GetError());
			ms->renderer = SDL_CreateRenderer(ms->window, -1, 0);
		}
		if (ms->renderer)
		{
			if (!SDL_GetRendererInfo(ms->renderer, &ms->renderer_info))
				av_log(NULL, AV_LOG_VERBOSE, "Initialized %s renderer.\n", ms->renderer_info.name);
		}
	}
	if (!ms->window || !ms->renderer || !ms->renderer_info.num_texture_formats)
	{
		av_log(NULL, AV_LOG_FATAL, "Failed to create window or renderer: %s", SDL_GetError());
		do_exit(ms);
	}

	ms_right_->input_filename = "D:\\ffmpeg\\ffmpeg-4.2.1-win32-shared\\bin\\7-2.MP4";
	ms_right_->window = SDL_CreateWindow("OnePlayer2", 960, 0,
		ms_right_->default_width, ms_right_->default_height, SDL_WINDOW_OPENGL | SDL_WINDOW_BORDERLESS);
	SDL_SetHint(SDL_HINT_RENDER_SCALE_QUALITY, "linear");
	if (ms_right_->window)
	{
		ms_right_->renderer = SDL_CreateRenderer(ms_right_->window, -1, SDL_RENDERER_ACCELERATED | SDL_RENDERER_PRESENTVSYNC);
		if (!ms_right_->renderer)
		{
			av_log(NULL, AV_LOG_WARNING, "Failed to initialize a hardware accelerated renderer: %s\n", SDL_GetError());
			ms_right_->renderer = SDL_CreateRenderer(ms_right_->window, -1, 0);
		}
		if (ms_right_->renderer)
		{
			if (!SDL_GetRendererInfo(ms_right_->renderer, &ms_right_->renderer_info))
				av_log(NULL, AV_LOG_VERBOSE, "Initialized %s renderer.\n", ms_right_->renderer_info.name);
		}
	}
	if (!ms_right_->window || !ms_right_->renderer || !ms_right_->renderer_info.num_texture_formats)
	{
		av_log(NULL, AV_LOG_FATAL, "Failed to create window or renderer: %s", SDL_GetError());
		do_exit(ms_right_);
	}

	ms_top_->input_filename = "D:\\ffmpeg\\ffmpeg-4.2.1-win32-shared\\bin\\7-3.MP4";
	ms_top_->window = SDL_CreateWindow("OnePlayer3", 0, 540,
		ms_top_->default_width, ms_top_->default_height, SDL_WINDOW_OPENGL | SDL_WINDOW_BORDERLESS);
	SDL_SetHint(SDL_HINT_RENDER_SCALE_QUALITY, "linear");
	if (ms_top_->window)
	{
		ms_top_->renderer = SDL_CreateRenderer(ms_top_->window, -1, SDL_RENDERER_ACCELERATED | SDL_RENDERER_PRESENTVSYNC);
		if (!ms_top_->renderer)
		{
			av_log(NULL, AV_LOG_WARNING, "Failed to initialize a hardware accelerated renderer: %s\n", SDL_GetError());
			ms_top_->renderer = SDL_CreateRenderer(ms_top_->window, -1, 0);
		}
		if (ms_top_->renderer)
		{
			if (!SDL_GetRendererInfo(ms_top_->renderer, &ms_top_->renderer_info))
				av_log(NULL, AV_LOG_VERBOSE, "Initialized %s renderer.\n", ms_top_->renderer_info.name);
		}
	}
	if (!ms_top_->window || !ms_top_->renderer || !ms_top_->renderer_info.num_texture_formats)
	{
		av_log(NULL, AV_LOG_FATAL, "Failed to create window or renderer: %s", SDL_GetError());
		do_exit(ms_top_);
	}

	ms_bottom_->input_filename = "D:\\ffmpeg\\ffmpeg-4.2.1-win32-shared\\bin\\7-4.MP4";
	ms_bottom_->window = SDL_CreateWindow("OnePlayer4", 960, 540,
		ms_bottom_->default_width, ms_bottom_->default_height, SDL_WINDOW_OPENGL | SDL_WINDOW_BORDERLESS);
	SDL_SetHint(SDL_HINT_RENDER_SCALE_QUALITY, "linear");
	if (ms_bottom_->window)
	{
		ms_bottom_->renderer = SDL_CreateRenderer(ms_bottom_->window, -1, SDL_RENDERER_ACCELERATED | SDL_RENDERER_PRESENTVSYNC);
		if (!ms_bottom_->renderer)
		{
			av_log(NULL, AV_LOG_WARNING, "Failed to initialize a hardware accelerated renderer: %s\n", SDL_GetError());
			ms_bottom_->renderer = SDL_CreateRenderer(ms_bottom_->window, -1, 0);
		}
		if (ms_bottom_->renderer)
		{
			if (!SDL_GetRendererInfo(ms_bottom_->renderer, &ms_bottom_->renderer_info))
				av_log(NULL, AV_LOG_VERBOSE, "Initialized %s renderer.\n", ms_bottom_->renderer_info.name);
		}
	}
	if (!ms_bottom_->window || !ms_bottom_->renderer || !ms_bottom_->renderer_info.num_texture_formats)
	{
		av_log(NULL, AV_LOG_FATAL, "Failed to create window or renderer: %s", SDL_GetError());
		do_exit(ms_bottom_);
	}

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


