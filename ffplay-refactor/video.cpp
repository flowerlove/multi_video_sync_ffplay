#include "video.h"

/* 解码一帧视频 */
int get_video_frame(MediaState * ms, AVFrame * frame)
{
	int got_picture;
	//解码
	if ((got_picture = ms->viddec.decoder_decode_frame(frame, NULL)) < 0)
		return -1;

	//若解码成功
	if (got_picture)
	{
		frame->sample_aspect_ratio = av_guess_sample_aspect_ratio(ms->ic, ms->video_st, frame);

		if (ms->framedrop > 0 || (ms->framedrop && ms->get_master_sync_type() != AV_SYNC_VIDEO_MASTER))
		{
			double dpts = NAN;
			if (frame->pts != AV_NOPTS_VALUE)
			{
				dpts = av_q2d(ms->video_st->time_base) * frame->pts;
				double master_clock = MediaState::global_ms_->get_master_clock();
				double diff = dpts - master_clock;
				double diff_delay = diff - ms->frame_last_filter_delay;
				int videoq_nb_packets = ms->video_pq.get_nb_packets();
				int viddec_pkt_serial = ms->viddec.get_pkt_serial();
				int vidclk_serial = *ms->vidclk.get_serial();

				if (!isnan(diff) && fabs(diff) < AV_NOSYNC_THRESHOLD && diff_delay < 0 &&
					viddec_pkt_serial == vidclk_serial && videoq_nb_packets)
				{
					std::cout << "FrameDrop~" << "masterClock:"<< master_clock << " dpts:" << dpts << " Diff:" << diff << " diff_delay:" << diff_delay
						<< " vq_nb_packets:" << videoq_nb_packets << " viddec_pkt_serial:" << viddec_pkt_serial
						<< " vidclk_serial:" << vidclk_serial << std::endl;
					ms->frame_drops_early++; 
					av_frame_unref(frame);   
					got_picture = 0;         
				}
			}
		}
	}

	return got_picture;
}

/* 视频解码线程函数 */
int video_thread(void * arg)
{
	MediaState *ms = (MediaState*)arg;
	AVFrame *frame = av_frame_alloc();
	double pts;
	double duration;
	int ret;
	AVRational tb = ms->video_st->time_base; //获取时基
	AVRational frame_rate = av_guess_frame_rate(ms->ic, ms->video_st, NULL); //获取帧率

	if (ms->is_master_)
	{
		ms->frame_rate_ = frame_rate.num / frame_rate.den;
		ms->AV_SYNC_THRESHOLD_MIN = 1.0f / (double)ms->frame_rate_  * 2;
		ms->AV_SYNC_THRESHOLD_MAX = 2 * ms->AV_SYNC_THRESHOLD_MIN;
		ms->AV_SYNC_FRAMEDUP_THRESHOLD = 4 * ms->AV_SYNC_THRESHOLD_MIN;
	}

	if (!frame)
	{
		return AVERROR(ENOMEM);
	}

	while (true)
	{
		//解码获取一帧视频画面
		ret = get_video_frame(ms, frame);
		//解码结束
		if (ret < 0)
		{
			av_frame_free(&frame);
			return ret;
		}
			
		if (!ret)
			continue;

		//用帧率估计帧时长
		duration = (frame_rate.num && frame_rate.den ? av_q2d({ frame_rate.den, frame_rate.num }) : 0);
		//将pts转化为以秒为单位
		pts = (frame->pts == AV_NOPTS_VALUE) ? NAN : frame->pts * av_q2d(tb);
		//将解码后的帧存入FrameQueue
		ret = ms->queue_picture(frame, pts, duration, frame->pkt_pos, ms->viddec.get_pkt_serial());
		av_frame_unref(frame); //引用计数减1

		if (ret < 0)
		{
			av_frame_free(&frame);
			return ret;
		}
			
	}

	av_frame_free(&frame);
	return 0;
}

/* 字幕解码线程函数，与音视频的有些不一样 */
int subtitle_thread(void * arg)
{
	MediaState *ms = (MediaState*)arg;
	Frame *sp;
	int got_subtitle;
	double pts;

	while (true) 
	{
		if (!(sp = ms->sub_fq.frame_queue_peek_writable()))
			return 0;

		if ((got_subtitle = ms->subdec.decoder_decode_frame(NULL, &sp->sub)) < 0)
			break;

		pts = 0;

		if (got_subtitle && sp->sub.format == 0) 
		{
			if (sp->sub.pts != AV_NOPTS_VALUE)
				pts = sp->sub.pts / (double)AV_TIME_BASE;
			sp->pts = pts;
			sp->serial = ms->subdec.get_pkt_serial();
			sp->width = ms->subdec.get_avctx()->width;
			sp->height = ms->subdec.get_avctx()->height;
			sp->uploaded = 0;

			ms->sub_fq.frame_queue_push();
		}
		else if (got_subtitle) 
		{
			avsubtitle_free(&sp->sub);
		}
	}
	return 0;
}

/* 创建并显示视频窗口 */
int video_open(MediaState * ms)
{
	int w, h;

	if (ms->screen_width)
	{
		w = ms->screen_width;
		h = ms->screen_height;
	}
	else
	{
		w = ms->default_width;
		h = ms->default_height;
	}

	//SDL_SetWindowSize(ms->window, w, h);
	//SDL_SetWindowPosition(ms->window, SDL_WINDOWPOS_CENTERED, SDL_WINDOWPOS_CENTERED);
	//if (ms->is_full_screen)
	//	SDL_SetWindowFullscreen(ms->window, SDL_WINDOW_FULLSCREEN_DESKTOP);

	ms->width = w;
	ms->height = h;

	return 0;
}

/* 显示音频波形 */
void video_audio_display(MediaState * ms)
{
	int i, i_start, x, y1, y, ys, delay, n, nb_display_channels;
	int ch, channels, h, h2;
	int64_t time_diff;
	int rdft_bits, nb_freq;

	for (rdft_bits = 1; (1 << rdft_bits) < 2 * ms->height; rdft_bits++)
		;
	nb_freq = 1 << (rdft_bits - 1);

	channels = ms->audio_tgt.channels;
	nb_display_channels = channels;
	if (!ms->paused)
	{
		int data_used = ms->show_mode == SHOW_MODE_WAVES ? ms->width : (2 * nb_freq);
		n = 2 * channels;
		delay = ms->audio_write_buf_size;
		delay /= n;

		if (ms->audio_callback_time)
		{
			time_diff = av_gettime_relative() - ms->audio_callback_time;
			delay -= (time_diff * ms->audio_tgt.freq) / 1000000;
		}

		delay += 2 * data_used;
		if (delay < data_used)
			delay = data_used;

		i_start = x = compute_mod(ms->sample_array_index - delay * channels, SAMPLE_ARRAY_SIZE);
		if (ms->show_mode == SHOW_MODE_WAVES)
		{
			h = INT_MIN;
			for (i = 0; i < 1000; i += channels)
			{
				int idx = (SAMPLE_ARRAY_SIZE + x - i) % SAMPLE_ARRAY_SIZE;
				int a = ms->sample_array[idx];
				int b = ms->sample_array[(idx + 4 * channels) % SAMPLE_ARRAY_SIZE];
				int c = ms->sample_array[(idx + 5 * channels) % SAMPLE_ARRAY_SIZE];
				int d = ms->sample_array[(idx + 9 * channels) % SAMPLE_ARRAY_SIZE];
				int score = a - d;
				if (h < score && (b ^ c) < 0) 
				{
					h = score;
					i_start = idx;
				}
			}
		}

		ms->last_i_start = i_start;
	}
	else
	{
		i_start = ms->last_i_start;
	}

	if (ms->show_mode == SHOW_MODE_WAVES)
	{
		h = ms->height / nb_display_channels;
		h2 = (h * 9) / 20;
		for (ch = 0; ch < nb_display_channels; ch++) 
		{
			i = i_start + ch;
			y1 = ms->ytop + ch * h + (h / 2);
			for (x = 0; x < ms->width; x++) 
			{
				y = (ms->sample_array[i] * h2) >> 15;
				if (y < 0) 
				{
					y = -y;
					ys = y1 - y;
				}
				else 
				{
					ys = y1;
				}
				//fill_rectangle(ms->xleft + x, ys, 1, y, ms->renderer);
				i += channels;
				if (i >= SAMPLE_ARRAY_SIZE)
					i -= SAMPLE_ARRAY_SIZE;
			}
		}

		//SDL_SetRenderDrawColor(ms->renderer, 0, 0, 255, 255);

		for (ch = 1; ch < nb_display_channels; ch++)
		{
			y = ms->ytop + ch * h;
			//fill_rectangle(ms->xleft, y, ms->width, 1, ms->renderer);
		}
	}
}

void video_image_display(MediaState * ms)
{
	Frame *vp;
	Frame *sp = NULL;
	SDL_Rect rect;

	//取要显示的视频帧
	vp = ms->pict_fq.frame_queue_peek_last();

	//字幕显示
	if (ms->subtitle_st)
	{
		if (ms->sub_fq.frame_queue_nb_remaining() > 0)
		{
			sp = ms->sub_fq.frame_queue_peek();

			if (vp->pts >= sp->pts + ((float)sp->sub.start_display_time / 1000))
			{
				if (!sp->uploaded)
				{
					uint8_t* pixels[4];
					int pitch[4];
					int i;
					if (!sp->width || !sp->height)
					{
						sp->width = vp->width;
						sp->height = vp->height;
					}
					if (realloc_texture(&MediaState::renderer, &ms->sub_texture, SDL_PIXELFORMAT_ARGB8888, sp->width, sp->height, SDL_BLENDMODE_BLEND, 1) < 0)
						return;

					for (i = 0; i < sp->sub.num_rects; i++)
					{
						AVSubtitleRect *sub_rect = sp->sub.rects[i];
						sub_rect->x = av_clip(sub_rect->x, 0, sp->width);
						sub_rect->y = av_clip(sub_rect->y, 0, sp->height);
						sub_rect->w = av_clip(sub_rect->w, 0, sp->width - sub_rect->x);
						sub_rect->h = av_clip(sub_rect->h, 0, sp->height - sub_rect->y);

						ms->sub_convert_ctx = sws_getCachedContext(ms->sub_convert_ctx,
							sub_rect->w, sub_rect->h, AV_PIX_FMT_PAL8,
							sub_rect->w, sub_rect->h, AV_PIX_FMT_BGRA,
							0, NULL, NULL, NULL);
						if (!ms->sub_convert_ctx)
						{
							av_log(NULL, AV_LOG_FATAL, "Cannot initialize the conversion context\n");
							return;
						}
						if (!SDL_LockTexture(ms->sub_texture, (SDL_Rect *)sub_rect, (void **)pixels, pitch))
						{
							sws_scale(ms->sub_convert_ctx, (const uint8_t * const *)sub_rect->data, sub_rect->linesize,
								0, sub_rect->h, pixels, pitch);
							SDL_UnlockTexture(ms->sub_texture);
						}
					}
					sp->uploaded = 1; //标记为已渲染，避免下次再次渲染，节约资源
				}
			}
			else
				sp = NULL;
		}
	}


	if (!vp->uploaded) {
		if (upload_texture(&MediaState::renderer, &ms->vid_texture, vp->frame, &ms->img_convert_ctx) < 0)
			return;
		vp->uploaded = 1; //更新一次纹理后置1
		vp->flip_v = vp->frame->linesize[0] < 0;
	}

	//if (ms->is_master_)
	{
		set_sdl_yuv_conversion_mode(vp->frame);

		if(MediaState::global_ms_->vid_texture)
			SDL_RenderCopyEx(MediaState::renderer, MediaState::global_ms_->vid_texture, NULL, &MediaState::global_ms_->rect_, 0, NULL, vp->flip_v ? SDL_FLIP_VERTICAL : (SDL_RendererFlip)0);
		if (MediaState::global_ms_right_->vid_texture)
			SDL_RenderCopyEx(MediaState::renderer, MediaState::global_ms_right_->vid_texture, NULL, &MediaState::global_ms_right_->rect_, 0, NULL, vp->flip_v ? SDL_FLIP_VERTICAL : (SDL_RendererFlip)0);
		if (MediaState::global_ms_top_->vid_texture)
			SDL_RenderCopyEx(MediaState::renderer, MediaState::global_ms_top_->vid_texture, NULL, &MediaState::global_ms_top_->rect_, 0, NULL, vp->flip_v ? SDL_FLIP_VERTICAL : (SDL_RendererFlip)0);
		if (MediaState::global_ms_bottom_->vid_texture)
			SDL_RenderCopyEx(MediaState::renderer, MediaState::global_ms_bottom_->vid_texture, NULL, &MediaState::global_ms_bottom_->rect_, 0, NULL, vp->flip_v ? SDL_FLIP_VERTICAL : (SDL_RendererFlip)0);

		set_sdl_yuv_conversion_mode(NULL);
	}

	//复制视频纹理到渲染器

	//set_sdl_yuv_conversion_mode(NULL);
	//再复制字幕纹理到渲染器，故视频相当于字幕的背景
	if (sp)
	{
		if (USE_ONEPASS_SUBTITLE_RENDER)
			SDL_RenderCopy(MediaState::renderer, ms->sub_texture, NULL, &rect);
		else
		{
			int i;
			double xratio = (double)rect.w / (double)sp->width;
			double yratio = (double)rect.h / (double)sp->height;
			for (i = 0; i < sp->sub.num_rects; i++) {
				SDL_Rect *sub_rect = (SDL_Rect*)sp->sub.rects[i];
				SDL_Rect target = { rect.x + sub_rect->x * xratio, rect.y + sub_rect->y * yratio,
									sub_rect->w * xratio, sub_rect->h * yratio };
				SDL_RenderCopy(MediaState::renderer, ms->sub_texture, sub_rect, &target);
			}
		}
	}
}

/* 显示当前图片或音频波形 */
void video_display(MediaState * ms)
{
	//如果窗口未显示，则显示窗口
	if (!ms->width)
		video_open(ms);

	if (ms->is_master_)
	{
		SDL_SetRenderDrawColor(MediaState::renderer, 0, 0, 0, 255);
		SDL_RenderClear(MediaState::renderer);
	}
	//纹理处理
	if (ms->audio_st && ms->show_mode != SHOW_MODE_VIDEO)
		video_audio_display(ms); //显示仅有音频的文件
	else if (ms->video_st)
		video_image_display(ms); //显示一帧视频画面

	if(ms->is_master_)
		SDL_RenderPresent(MediaState::renderer); //渲染输出画面
}
