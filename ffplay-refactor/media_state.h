#ifndef MEDIA_STATE_H_
#define MEDIA_STATE_H_

#include "decoder.h"
#include "clock.h"
#include "sdl_utility.h"
#include "define.h"

constexpr int SAMPLE_ARRAY_SIZE = (8 * 65536);
constexpr int MAX_QUEUE_SIZE = (64 * 1024 * 1024);
constexpr int MIN_FRAMES = 60;
constexpr int EXTERNAL_CLOCK_MIN_FRAMES = 2;
constexpr int EXTERNAL_CLOCK_MAX_FRAMES = 10;
constexpr int SDL_AUDIO_MIN_BUFFER_SIZE = 512;
constexpr int SDL_AUDIO_MAX_CALLBACKS_PER_SEC = 30;
constexpr double SDL_VOLUME_STEP = (0.75);
constexpr int AUDIO_DIFF_AVG_NB = 20;
constexpr double REFRESH_RATE = 0.0001;
constexpr int USE_ONEPASS_SUBTITLE_RENDER = 1;
constexpr int FF_QUIT_EVENT = (SDL_USEREVENT + 2);    //自定义的退出事件
enum ShowMode
{
	SHOW_MODE_NONE = -1,
	SHOW_MODE_VIDEO = 0,
	SHOW_MODE_WAVES,
	SHOW_MODE_RDFT,
	SHOW_MODE_NB
};

class MediaState
{
public:
	MediaState() = default;
	~MediaState();

	const char* input_filename;
	const char* window_title;
	SDL_Rect rect_;
	int render_width;
	int render_height;
	int default_width;
	int default_height;
	int screen_width;
	int screen_height;
	int loop = 1;
	int autoexit = 1;
	int framedrop = -1;
	int show_status = 1;
	bool is_master_ = false;
	double rdftspeed = 0.02;
	unsigned sws_flags = SWS_BICUBIC;    //图像转换默认算法
	int64_t start_time = AV_NOPTS_VALUE;
	int64_t duration = AV_NOPTS_VALUE;
	int64_t cursor_last_shown;
	int cursor_hidden = 0;
	int exit_on_keydown = 0;
	int exit_on_mousedown = 0;
	int is_full_screen = 0;
	int seek_by_bytes = -1;
	float seek_interval = 10;
	int64_t audio_callback_time;

	int queue_picture(AVFrame *src_frame, double pts, double duration, int64_t pos, int serial);
	int get_master_sync_type();
	double get_master_clock();
	double get_video_clock();
	double get_audio_clock();
	double get_external_clock();
	void sync_clock_to_slave(Clock *c, Clock *slave);
	void init();

	SDL_Thread *read_tid;  
	int abort_request;      
	int force_refresh;      
	int paused;             
	int last_paused;        
	int queue_attachments_req;  
	int seek_req;           
	int seek_flags;         
	int64_t seek_pos;       
	int64_t seek_rel;       
	int read_pause_return;
	AVFormatContext *ic;    
	int realtime;           

	Clock audclk;           
	Clock vidclk;           
	Clock extclk;           

	FrameQueue pict_fq;       
	FrameQueue sub_fq;       
	FrameQueue samp_fq;       

	Decoder auddec;         
	Decoder viddec;         
	Decoder subdec;         

	int av_sync_type;       
	double audio_clock;     
	int audio_clock_serial; 
	double audio_diff_cum; 
	double audio_diff_avg_coef;
	double audio_diff_threshold;
	int audio_diff_avg_count;
	int audio_stream;       
	AVStream *audio_st;     
	PacketQueue audio_pq;     
	int audio_hw_buf_size;   
	uint8_t *audio_buf;     
	uint8_t *audio_buf1;    
	unsigned int audio_buf_size; 
	unsigned int audio_buf1_size;
	int audio_buf_index;    
	int audio_write_buf_size;   
	int audio_volume;       
	int muted;              
	struct AudioParams audio_src;   
	struct AudioParams audio_tgt;   
	struct SwrContext *swr_ctx;     
	int frame_drops_early;
	int frame_drops_late;
	
	int show_mode;
	int16_t sample_array[SAMPLE_ARRAY_SIZE];    
	int sample_array_index; 
	int last_i_start;
	int xpos;
	double last_vis_time;
	SDL_Texture *vis_texture;   
	SDL_Texture *sub_texture;   
	SDL_Texture *vid_texture;   

	int subtitle_stream;        
	AVStream *subtitle_st;      
	PacketQueue subtitle_pq;      

	double frame_timer;         
	double frame_last_returned_time;    
	double frame_last_filter_delay;     
	int video_stream;       
	AVStream *video_st;     
	PacketQueue video_pq;     
	double max_frame_duration;      
	struct SwsContext *img_convert_ctx; 
	struct SwsContext *sub_convert_ctx; 
	int eof;             

	char *filename;     
	int width, height, xleft, ytop;     
	int step;           
	
	int last_video_stream, last_audio_stream, last_subtitle_stream;

	SDL_cond *continue_read_thread; 
	SDL_Thread* refresh_tid;

	double remaining_time = 0.0f;

	static MediaState* global_ms_;
	static MediaState* global_ms_right_;
	static MediaState* global_ms_top_;
	static MediaState* global_ms_bottom_;
	static AVPacket flush_pkt;

	static SDL_Window* window;
	static SDL_Renderer* renderer;
	static SDL_RendererInfo renderer_info;
	static SDL_AudioDeviceID audio_dev;

	int frame_rate_;
	/* 时钟差异小于该阈值则认为已同步，不必进行时钟同步 */
	double AV_SYNC_THRESHOLD_MIN = 0.0166667;; // 0.0166667;
	/* 时钟差异大于该阈值则需要进行时钟同步 */
	double AV_SYNC_THRESHOLD_MAX = 0.0333334; // 0.0333334;
	/* 如果一帧的时长大于该阈值，则为了实现同步，不再重复显示/播放该帧 */
	double AV_SYNC_FRAMEDUP_THRESHOLD = 0.1; //  0.0666668;
};


#endif // !MEDIA_STATE_H_

