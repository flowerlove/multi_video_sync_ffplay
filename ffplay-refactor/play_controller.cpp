#include "play_controller.h"

/* 计算一帧的持续时间 */
double vp_duration(MediaState *ms, Frame * vp, Frame * nextvp)
{
	if (vp->serial == nextvp->serial)
	{
		double duration = nextvp->pts - vp->pts;
		if (isnan(duration) || duration <= 0 || duration > ms->max_frame_duration)
			return vp->duration;
		else
			return duration;
	}
	else
	{
		return 0.0;
	}
}

/* 视频帧显示时长计算 */
double compute_target_delay(double delay, MediaState *ms)
{
	double sync_threshold, diff = 0;
	double temp_delay = delay;
	if (MediaState::global_ms_->get_master_sync_type() != AV_SYNC_VIDEO_MASTER)
	{
		auto video_clock = ms->vidclk.get_clock();
		auto master_clock = ms->is_master_ ? MediaState::global_ms_->get_master_clock() : MediaState::global_ms_->get_external_clock();
		diff = video_clock - master_clock; //计算视频时钟和主时钟的差值

		sync_threshold = FFMAX(MediaState::global_ms_->AV_SYNC_THRESHOLD_MIN, FFMIN(MediaState::global_ms_->AV_SYNC_THRESHOLD_MAX, delay));
		if (!isnan(diff) && fabs(diff) < ms->max_frame_duration)
		{
			if (diff <= -sync_threshold)
				delay = FFMAX(0, delay + diff);
			else if (diff >= sync_threshold && delay > MediaState::global_ms_->AV_SYNC_FRAMEDUP_THRESHOLD)
				delay = delay + diff;
			else if (diff >= sync_threshold)
				delay = 2 * delay;

			if(temp_delay < delay)
				std::cout << "Delay:" << ms->filename << " " << diff << " " << delay << std::endl;
		}
	}
	return delay;
}

void update_video_pts(MediaState * ms, double pts, int64_t pos, int serial)
{
	ms->vidclk.set_clock(pts, serial);
	if(ms->is_master_)
		ms->sync_clock_to_slave(&ms->extclk, &ms->vidclk);
}

void stream_seek(MediaState * ms, int64_t pos, int64_t rel, int seek_by_bytes)
{
	if (!ms->seek_req)
	{
		ms->seek_pos = pos; 
		ms->seek_rel = rel; 
		ms->seek_flags &= ~AVSEEK_FLAG_BYTE; 
		if (seek_by_bytes)
			ms->seek_flags |= AVSEEK_FLAG_BYTE;
		ms->seek_req = 1; 
		SDL_CondSignal(ms->continue_read_thread);
	}
}

/* pause or resume the video 暂停/播放视频 */
void stream_toggle_pause(MediaState * ms)
{
	//如果当前状态就是暂停，则接下来进入播放状态，需要更新vidclk(播放变暂停不需要更新)
	if (ms->paused)
	{
		ms->frame_timer += av_gettime_relative() / 1000000.0 - ms->vidclk.get_last_updated();
		if (ms->read_pause_return != AVERROR(ENOSYS))
		{
			ms->vidclk.set_paused(0); //重启视频时钟
		}
		//更新视频时钟
		ms->vidclk.set_clock(ms->vidclk.get_clock(), *ms->vidclk.get_serial());
	}
	//不论暂停还是播放，均更新外部时钟
	MediaState::global_ms_->extclk.set_clock(MediaState::global_ms_->extclk.get_clock(), *MediaState::global_ms_->extclk.get_serial());
	//反转状态
	ms->paused = !ms->paused;
	ms->audclk.set_paused(!ms->paused);
	ms->vidclk.set_paused(!ms->paused);
	ms->extclk.set_paused(!ms->paused);
}

/* 暂停/播放 */
void toggle_pause(MediaState * ms)
{
	stream_toggle_pause(ms);
	ms->step = 0;
}

/* 静音/非静音 */
void toggle_mute(MediaState * ms)
{
	ms->muted = !ms->muted;
}

/* 改变音量 */
void update_volume(MediaState * ms, int sign, double step)
{
	double volume_level = ms->audio_volume ? (20 * log(ms->audio_volume / (double)SDL_MIX_MAXVOLUME) / log(10)) : -1000.0;
	int new_volume = lrint(SDL_MIX_MAXVOLUME * pow(10.0, (volume_level + sign * step) / 20.0));
	ms->audio_volume = av_clip(ms->audio_volume == new_volume ? (ms->audio_volume + sign) : new_volume, 0, SDL_MIX_MAXVOLUME);
}

void step_to_next_frame(MediaState * ms)
{
	if (ms->paused)
		stream_toggle_pause(ms);
	ms->step = 1; 
}

/* 全屏/非全屏切换 */
void toggle_full_screen(MediaState * ms)
{
	ms->is_full_screen = !ms->is_full_screen;
	//SDL_SetWindowFullscreen(ms->window, ms->is_full_screen ? SDL_WINDOW_FULLSCREEN_DESKTOP : 0);
}

/* 切换显示模式 */
void toggle_audio_display(MediaState * ms)
{
	//先拿到当前显示模式
	int next = ms->show_mode;

	//切换到下一种符合当前媒体文件的模式
	do
	{
		next = (next + 1) % SHOW_MODE_NB;
	} while (next != ms->show_mode && (next == SHOW_MODE_VIDEO && !ms->video_st || next != SHOW_MODE_VIDEO && !ms->audio_st));

	//如果可以找到该种模式，则强制刷新为新的模式，否则不变
	if (ms->show_mode != next)
	{
		ms->force_refresh = 1;
		ms->show_mode = next;
	}
}

/* 显示每一帧的关键函数，包括视频和字幕的显示 */
void video_refresh(void * opaque, double * remaining_time)
{
	MediaState *ms = (MediaState*)opaque;
	double time;

	Frame *sp, *sp2;

	if (ms && ms->paused && MediaState::global_ms_->get_master_sync_type() == AV_SYNC_EXTERNAL_CLOCK && ms->realtime)
	{
		check_external_clock_speed(ms);
	}

	if (ms->show_mode != SHOW_MODE_VIDEO && ms->audio_st)
	{
		time = av_gettime_relative() / 1000000.0;
		if (ms->force_refresh || ms->last_vis_time + ms->rdftspeed < time)
		{
			video_display(ms);
			ms->last_vis_time = time;
		}
		*remaining_time = FFMIN(*remaining_time, ms->last_vis_time + ms->rdftspeed - time);
	}

	if (ms->video_st)
	{
		do {
			if (ms->pict_fq.frame_queue_nb_remaining() == 0)
			{
			}
			else
			{
				double last_duration, duration, delay;
				
				Frame *vp, *lastvp;
				
				lastvp = ms->pict_fq.frame_queue_peek_last();
				vp = ms->pict_fq.frame_queue_peek();

				if (vp->serial != ms->video_pq.get_serial())
				{
					ms->pict_fq.frame_queue_next();
					continue;
				}

				if (lastvp->serial != vp->serial)
					ms->frame_timer = av_gettime_relative() / 1000000.0;
				do {
					if (ms->paused)
						break;

					last_duration = vp_duration(ms, lastvp, vp);
					delay = compute_target_delay(last_duration, ms); 

					time = av_gettime_relative() / 1000000.0;
					if (time < ms->frame_timer + delay)
					{
						*remaining_time = FFMIN(ms->frame_timer + delay - time, *remaining_time); 
						continue;
					}

					ms->frame_timer += delay;

					if (delay > 0 && time - ms->frame_timer > MediaState::global_ms_->AV_SYNC_THRESHOLD_MAX)
						ms->frame_timer = time;

					SDL_LockMutex(ms->pict_fq.get_mutex());
					if (!isnan(vp->pts))
						update_video_pts(ms, vp->pts, vp->pos, vp->serial); 
					SDL_UnlockMutex(ms->pict_fq.get_mutex());

					if (ms->pict_fq.frame_queue_nb_remaining() > 1)
					{
						Frame *nextvp = ms->pict_fq.frame_queue_peek_next();
						duration = vp_duration(ms, vp, nextvp);
						if (!ms->step && (ms->framedrop > 0 || (ms->framedrop && ms->get_master_sync_type() != AV_SYNC_VIDEO_MASTER)) &&
							time > ms->frame_timer + duration)
						{
							ms->frame_drops_late++;
							ms->pict_fq.frame_queue_next();
							continue;
						}
					}

					if (ms->subtitle_st)
					{
						while (ms->sub_fq.frame_queue_nb_remaining() > 0)
						{
							sp = ms->sub_fq.frame_queue_peek();

							if (ms->sub_fq.frame_queue_nb_remaining() > 1)
								sp2 = ms->sub_fq.frame_queue_peek_next();
							else
								sp2 = NULL;

							if (sp->serial != ms->subtitle_pq.get_serial()
								|| (ms->vidclk.get_pts() > (sp->pts + ((float)sp->sub.end_display_time / 1000)))
								|| (sp2 && ms->vidclk.get_pts() > (sp2->pts + ((float)sp2->sub.start_display_time / 1000))))
							{
								if (sp->uploaded)
								{
									int i;
									for (i = 0; i < sp->sub.num_rects; i++)
									{
										AVSubtitleRect *sub_rect = sp->sub.rects[i]; 
										uint8_t *pixels;
										int pitch, j;

										if (!SDL_LockTexture(ms->sub_texture, (SDL_Rect *)sub_rect, (void **)&pixels, &pitch))
										{
											for (j = 0; j < sub_rect->h; j++, pixels += pitch)
												memset(pixels, 0, sub_rect->w << 2);
											SDL_UnlockTexture(ms->sub_texture);
										}
									}
								}
								ms->sub_fq.frame_queue_next();
							}
							else
							{
								break;
							}
						}
					}

					ms->pict_fq.frame_queue_next();
					ms->force_refresh = 1;

					if (ms->step && !ms->paused)
						stream_toggle_pause(ms);
				} while (0);
			}
			/* 显示图片/音频/字幕 */
			if (ms->force_refresh && ms->show_mode == SHOW_MODE_VIDEO && ms->pict_fq.get_rindex_shown())
				video_display(ms);
		} while (0);
	}
	ms->force_refresh = 0;
	////显示一些信息
	//if (ms->show_status != 1)
	//{
	//	static int64_t last_time;
	//	int64_t cur_time;
	//	int aqsize, vqsize, sqsize;
	//	double av_diff;

	//	cur_time = av_gettime_relative();
	//	if (!last_time || (cur_time - last_time) >= 30000)
	//	{
	//		aqsize = 0;
	//		vqsize = 0;
	//		sqsize = 0;
	//		if (ms->audio_st)
	//			aqsize = ms->audio_pq.get_size();
	//		if (ms->video_st)
	//			vqsize = ms->video_pq.get_size();
	//		if (ms->subtitle_st)
	//			sqsize = ms->subtitle_pq.get_size();
	//		av_diff = 0;
	//		if (ms->audio_st && ms->video_st)
	//			av_diff = ms->audclk.get_clock() - ms->vidclk.get_clock();
	//		else if (ms->video_st)
	//			av_diff = ms->get_master_clock() - ms->vidclk.get_clock();
	//		else if (ms->audio_st)
	//			av_diff = ms->get_master_clock() - ms->audclk.get_clock();
	//		av_log(NULL, AV_LOG_INFO,
	//			"%7.2f %s:%7.3f fd=%4d aq=%5dKB vq=%5dKB sq=%5dB f=%" PRId64 "/%" PRId64 "   \r",
	//			ms->get_master_clock(),
	//			(ms->audio_st && ms->video_st) ? "A-V" : (ms->video_st ? "M-V" : (ms->audio_st ? "M-A" : "   ")),
	//			av_diff,
	//			ms->frame_drops_early + ms->frame_drops_late,
	//			aqsize / 1024,
	//			vqsize / 1024,
	//			sqsize,
	//			ms->video_st ? ms->viddec.get_avctx()->pts_correction_num_faulty_dts : 0,
	//			ms->video_st ? ms->viddec.get_avctx()->pts_correction_num_faulty_pts : 0);
	//		fflush(stdout);
	//		last_time = cur_time;
	//	}
	//}
}


void do_refresh(MediaState* ms)
{
	if (ms->remaining_time > 0.0)
		av_usleep((int64_t)(ms->remaining_time * 1000000.0));
	ms->remaining_time = REFRESH_RATE;
	if (ms->show_mode != SHOW_MODE_NONE && (!ms->paused || ms->force_refresh))
		video_refresh(ms, &ms->remaining_time);
	SDL_PumpEvents();
}

/* 循环刷新画面，同时检测是否有事件 */
void refresh_loop_wait_event(MediaState* crop_stream_left_half, MediaState* crop_stream_right_half,
	MediaState* crop_stream_top_half, MediaState* crop_stream_bottom_half, SDL_Event * event)
{
	SDL_PumpEvents();
	while (!SDL_PeepEvents(event, 1, SDL_GETEVENT, SDL_FIRSTEVENT, SDL_LASTEVENT))
	{
		do_refresh(crop_stream_left_half);
		do_refresh(crop_stream_right_half);
		do_refresh(crop_stream_top_half);
		do_refresh(crop_stream_bottom_half);
	}
}

//void event_loop_one(MediaState* crop_stream_left_half)
//{
//	if (crop_stream_left_half == NULL)
//		return;
//	SDL_Event event;
//	double incr, pos, frac;
//	double x;
//
//	//响应各类事件
//	switch (event.type)
//	{
//	case SDL_KEYDOWN:
//		if (crop_stream_left_half->exit_on_keydown || event.key.keysym.sym == SDLK_ESCAPE || event.key.keysym.sym == SDLK_q)
//		{
//			//do_exit(crop_stream_left_half);
//			break;
//		}
//		if (!crop_stream_left_half->width)
//			break;
//		switch (event.key.keysym.sym)
//		{
//			//全屏/非全屏
//		case SDLK_f:
//			toggle_full_screen(crop_stream_left_half);
//			crop_stream_left_half->force_refresh = 1;
//			break;
//			//暂停/播放
//		case SDLK_p:
//		case SDLK_SPACE:
//			toggle_pause(crop_stream_left_half);
//			break;
//			//静音/非静音
//		case SDLK_m:
//			toggle_mute(crop_stream_left_half);
//			break;
//			//乘号和0增加音量
//		case SDLK_KP_MULTIPLY:
//		case SDLK_0:
//			update_volume(crop_stream_left_half, 1, SDL_VOLUME_STEP);
//			break;
//			//除号和9降低音量
//		case SDLK_KP_DIVIDE:
//		case SDLK_9:
//			update_volume(crop_stream_left_half, -1, SDL_VOLUME_STEP);
//			break;
//			//逐帧播放或暂停时的seek
//		case SDLK_s:
//			step_to_next_frame(crop_stream_left_half);
//			break;
//			//切换显示模式
//		case SDLK_w:
//			toggle_audio_display(crop_stream_left_half);
//			break;
//			//短快退
//		case SDLK_LEFT:
//			incr = crop_stream_left_half->seek_interval ? -crop_stream_left_half->seek_interval : -10.0; //快退时间数，10秒
//			goto do_seek;
//			//短快进
//		case SDLK_RIGHT:
//			incr = crop_stream_left_half->seek_interval ? crop_stream_left_half->seek_interval : 10.0;
//			goto do_seek;
//			//长快进
//		case SDLK_UP:
//			incr = 60.0;
//			goto do_seek;
//			//长快退
//		case SDLK_DOWN:
//			incr = -60.0;
//		do_seek:
//			if (crop_stream_left_half->seek_by_bytes)
//			{
//				pos = -1;
//				if (pos < 0 && crop_stream_left_half->video_stream >= 0)
//					pos = crop_stream_left_half->pict_fq.frame_queue_last_pos();
//				if (pos < 0 && crop_stream_left_half->audio_stream >= 0)
//					pos = crop_stream_left_half->pict_fq.frame_queue_last_pos();
//				if (pos < 0)
//					pos = avio_tell(crop_stream_left_half->ic->pb);
//				if (crop_stream_left_half->ic->bit_rate)
//					incr *= crop_stream_left_half->ic->bit_rate / 8.0;
//				else
//					incr *= 180000.0;
//				pos += incr;
//				stream_seek(crop_stream_left_half, pos, incr, 1);
//			}
//			else
//			{
//				pos = crop_stream_left_half->get_master_clock();
//				if (isnan(pos))
//					pos = (double)crop_stream_left_half->seek_pos / AV_TIME_BASE;
//				pos += incr;
//				if (crop_stream_left_half->ic->start_time != AV_NOPTS_VALUE && pos < crop_stream_left_half->ic->start_time / (double)AV_TIME_BASE)
//					pos = crop_stream_left_half->ic->start_time / (double)AV_TIME_BASE;
//				stream_seek(crop_stream_left_half, (int64_t)(pos * AV_TIME_BASE), (int64_t)(incr * AV_TIME_BASE), 0);
//			}
//			break;
//		default:
//			break;
//		}
//		break;
//		//按下鼠标
//	case SDL_MOUSEBUTTONDOWN:
//		//if (crop_stream_left_half->exit_on_mousedown)
//		//{
//		//	do_exit(crop_stream_left_half);
//		//	break;
//		//}
//		//如果是按下左键
//		//if (event.button.button == SDL_BUTTON_LEFT)
//		//{
//		//	static int64_t last_mouse_left_click = 0;
//		//	if (av_gettime_relative() - last_mouse_left_click <= 500000)
//		//	{
//		//		toggle_full_screen(crop_stream_left_half);
//		//		crop_stream_left_half->force_refresh = 1;
//		//		last_mouse_left_click = 0;
//		//	}
//		//	else
//		//	{
//		//		last_mouse_left_click = av_gettime_relative(); //如果是不是双击则设置为当前时间
//		//	}
//		//}
//		//鼠标移动
//	case SDL_MOUSEMOTION:
//		////如果本来鼠标是隐藏的，那么设置为可见，并重置标志位
//		//if (crop_stream_left_half->cursor_hidden)
//		//{
//		//	SDL_ShowCursor(1);
//		//	crop_stream_left_half->cursor_hidden = 0;
//		//}
//		////记录鼠标刚开始显示的时间
//		//crop_stream_left_half->cursor_last_shown = av_gettime_relative();
//		//if (event.type == SDL_MOUSEBUTTONDOWN)
//		//{
//		//	if (event.button.button != SDL_BUTTON_RIGHT)
//		//		break;
//		//	x = event.button.x;
//		//}
//		//else
//		//{
//		//	if (!(event.motion.state & SDL_BUTTON_RMASK))
//		//		break;
//		//	x = event.motion.x;
//		//}
//		//if (crop_stream_left_half->seek_by_bytes || crop_stream_left_half->ic->duration <= 0)
//		//{
//		//	uint64_t size = avio_size(crop_stream_left_half->ic->pb);
//		//	stream_seek(crop_stream_left_half, size * x / crop_stream_left_half->width, 0, 1);
//		//}
//		////默认
//		//else
//		//{
//		//	int64_t ts;
//		//	int ns, hh, mm, ss;
//		//	int tns, thh, tmm, tss;
//		//	tns = crop_stream_left_half->ic->duration / 1000000LL; //long long变量，将视频总时长转换为秒
//		//	//下面3句将总时长拆分为x小时x分钟x秒
//		//	thh = tns / 3600; //小时
//		//	tmm = (tns % 3600) / 60; //分钟
//		//	tss = (tns % 60); //秒
//		//	frac = x / crop_stream_left_half->width; //鼠标位置在视频当前宽度的相对位置
//		//	ns = frac * tns;  //计算鼠标位置对应于视频本身的时间
//		//	//将其转化为x小时x分钟x秒，用于显示seek后的时间
//		//	hh = ns / 3600;
//		//	mm = (ns % 3600) / 60;
//		//	ss = (ns % 60);
//		//	av_log(NULL, AV_LOG_INFO,
//		//		"Seek to %2.0f%% (%2d:%02d:%02d) of total duration (%2d:%02d:%02d)       \n", frac * 100,
//		//		hh, mm, ss, thh, tmm, tss);
//		//	//将seek后的时间转换回微秒，以进行seek操作
//		//	ts = frac * crop_stream_left_half->ic->duration;
//		//	if (crop_stream_left_half->ic->start_time != AV_NOPTS_VALUE)
//		//		ts += crop_stream_left_half->ic->start_time; //加上视频的起始时间
//		//	stream_seek(crop_stream_left_half, ts, 0, 0); //进行seek
//		//}
//		break;
//		//窗口事件
//	case SDL_WINDOWEVENT:
//		//switch (event.window.event)
//		//{
//		//	//调节大小，直接赋予新数据即可
//		//case SDL_WINDOWEVENT_RESIZED:
//		//	crop_stream_left_half->screen_width = crop_stream_left_half->width = event.window.data1;
//		//	crop_stream_left_half->screen_height = crop_stream_left_half->height = event.window.data2;
//		//	if (crop_stream_left_half->vis_texture) {
//		//		SDL_DestroyTexture(crop_stream_left_half->vis_texture);
//		//		crop_stream_left_half->vis_texture = NULL;
//		//	}
//		//	//重新绘制窗口
//		//case SDL_WINDOWEVENT_EXPOSED:
//		//	crop_stream_left_half->force_refresh = 1;
//		//}
//		break;
//		//SDL退出和自定义退出
//	case SDL_QUIT:
//	case FF_QUIT_EVENT: //自定义事件，用于出错时的主动退出
//		//do_exit(crop_stream_left_half);
//		break;
//	default:
//		break;
//	}
//}


/* 处理事件的循环 */
void event_loop_event(MediaState * crop_stream_left_half, MediaState* crop_stream_right_half,
	MediaState* crop_stream_top_half, MediaState* crop_stream_bottom_half)
{
	SDL_Event event;
	double incr, pos, frac;

	while (true)
	{

		refresh_loop_wait_event(crop_stream_left_half, crop_stream_right_half, crop_stream_top_half, crop_stream_bottom_half, &event);
		//event_loop_one(crop_stream_left_half);
		//event_loop_one(crop_stream_right_half);
		//event_loop_one(crop_stream_top_half);
		//event_loop_one(crop_stream_bottom_half);
	}
}

void check_external_clock_speed(MediaState* is)
{
	if (is->video_stream >= 0 && is->video_pq.get_nb_packets() <= EXTERNAL_CLOCK_MIN_FRAMES ||
		is->audio_stream >= 0 && is->audio_pq.get_nb_packets() <= EXTERNAL_CLOCK_MIN_FRAMES) {
		is->extclk.set_clock_speed(FFMAX(EXTERNAL_CLOCK_SPEED_MIN, is->extclk.get_clock_speed() - EXTERNAL_CLOCK_SPEED_STEP));
	}
	else if ((is->video_stream < 0 || is->video_pq.get_nb_packets() > EXTERNAL_CLOCK_MAX_FRAMES) &&
		(is->audio_stream < 0 || is->audio_pq.get_nb_packets() > EXTERNAL_CLOCK_MAX_FRAMES)) {
		is->extclk.set_clock_speed(FFMIN(EXTERNAL_CLOCK_SPEED_MAX, is->extclk.get_clock_speed() + EXTERNAL_CLOCK_SPEED_STEP));
	}
	else {
		double speed = is->extclk.get_clock_speed();
		if (speed != 1.0)
			is->extclk.set_clock_speed(speed + EXTERNAL_CLOCK_SPEED_STEP * (1.0 - speed) / fabs(1.0 - speed));
	}
}

