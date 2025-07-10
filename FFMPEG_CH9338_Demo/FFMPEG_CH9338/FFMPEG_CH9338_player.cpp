// FFMPEG_CH9338_screen_recording.cpp : 此文件包含 "main" 函数。程序执行将在此处开始并结束。
//
extern "C" {
#include <libavcodec/avcodec.h>
#include <libavformat/avformat.h>
#include <libavdevice/avdevice.h>
#include <libavutil/imgutils.h>
#include <libswscale/swscale.h>
}
#include <Windows.h>
#include <iostream>
#include <SDL3/SDL.h>
#include <thread>
#include "../CH375/CH375DLL.H"
#pragma comment(lib, "../CH375/WCHKMFU64.lib")
SDL_Renderer* renderer = NULL;
SDL_Texture* texture = NULL;
AVFormatContext* fmt_ctx = NULL;
int g_dev_index = -1;
bool g_b_exit = false;
int video_stream = -1;
AVCodecContext* codec_ctx = NULL;
std::chrono::steady_clock::time_point last_update_time = std::chrono::steady_clock::now();
struct SwsContext* sws_ctx = NULL;
bool serch_device(const char* dev_name)
{
    char* pdevName = NULL;
    char  devName[MAX_DEVICE_PATH_SIZE] = "";
    for (ULONG i = 0; i < 3; i++)
    {
        pdevName = (char*)CH375GetDeviceName(i);
        if (pdevName != NULL)
        {
            strcpy_s(devName, MAX_DEVICE_PATH_SIZE, pdevName);
            CharUpperBuffA(devName, strlen(devName));
            if (strstr(devName, dev_name) != NULL)
            {
                g_dev_index = i;
                return true;
            }
        }
    }
    return false;
}

void PlayThread()
{
    AVPacket* pkt = av_packet_alloc();
    AVFrame* frame = av_frame_alloc();
    static int64_t last_pts = AV_NOPTS_VALUE;
    // 主播放循环
    do {
        last_update_time = std::chrono::steady_clock::now();
        if (av_read_frame(fmt_ctx, pkt) >= 0) {
            if (pkt->stream_index == video_stream) {
                avcodec_send_packet(codec_ctx, pkt);
                while (avcodec_receive_frame(codec_ctx, frame) == 0) {
                    AVFrame* yuv_frame = av_frame_alloc();
                    yuv_frame->format = AV_PIX_FMT_YUV420P;
                    yuv_frame->width = codec_ctx->width;
                    yuv_frame->height = codec_ctx->height;
                    av_frame_get_buffer(yuv_frame, 0);
                    sws_scale(sws_ctx,
                        (const uint8_t* const*)frame->data, frame->linesize,
                        0, codec_ctx->height,
                        yuv_frame->data, yuv_frame->linesize);

                    if (last_pts != AV_NOPTS_VALUE) {
                        int64_t delta_pts = frame->pts - last_pts;
                        int delay = av_rescale_q(delta_pts,
                            fmt_ctx->streams[video_stream]->time_base,
                            { 1, 1000 });
                        SDL_Delay(delay);
                    }
                    last_pts = frame->pts;
                    // 更新SDL纹理
                    SDL_UpdateYUVTexture(texture, NULL,
                        yuv_frame->data[0], yuv_frame->linesize[0],
                        yuv_frame->data[1], yuv_frame->linesize[1],
                        yuv_frame->data[2], yuv_frame->linesize[2]);

                    // 渲染帧
                    SDL_RenderClear(renderer);
                    SDL_RenderTexture(renderer, texture, NULL, NULL);
                    SDL_RenderPresent(renderer);

                    av_frame_free(&yuv_frame);
                    SDL_Delay(1000 / av_q2d(fmt_ctx->streams[video_stream]->avg_frame_rate));
                }
            }
        }
    } while (!g_b_exit);
    av_packet_unref(pkt);
    av_packet_free(&pkt);
}

/* handle an event sent by the GUI */
void event_loop()
{
    SDL_Event event;
    while (SDL_WaitEvent(&event) && !g_b_exit) {
        switch (event.type) {
        case SDL_EVENT_QUIT: // 窗口关闭事件
            g_b_exit = true;
            return;

        case SDL_EVENT_KEY_DOWN: // 键盘按下事件
            if (event.key.key == SDLK_ESCAPE) {
                g_b_exit = true;
                return;
            }
            break;

        case SDL_EVENT_WINDOW_EXPOSED: // 窗口需要重绘
        case SDL_EVENT_WINDOW_RESIZED: // 窗口尺寸变化
            // 重新绘制背景
            SDL_SetRenderDrawColor(renderer, 16, 0, 16, 255);
            SDL_RenderClear(renderer);
            SDL_RenderPresent(renderer);
            break;

        case SDL_EVENT_MOUSE_MOTION: // 鼠标移动事件
            /*SDL_Log("鼠标位置: (%d, %d)",
                    event.motion.x,
                    event.motion.y);*/
            break;

        default:
            // 处理其他事件类型...
            break;
        }
    }
}

// 定时检查线程函数
void watchdog_thread(std::thread::native_handle_type worker_handle) {
    std::this_thread::sleep_for(std::chrono::seconds(1));
    while (1) {
        // 计算时间差
        auto now = std::chrono::steady_clock::now();
        std::chrono::duration<double> elapsed;
        elapsed = now - last_update_time;
        // 检查超时
        if (elapsed.count() > 0.5f) {
            g_b_exit = true;
            std::cerr << "Timeout detected! Force terminating worker thread." << std::endl;
            TerminateThread(worker_handle, 0);
            return;
        }
    }
}

int main()
{
    if (serch_device("VID_1A86&PID_8026&MI_01") == false) {
        SDL_Log("无法打开CH9338");
        return -1;
    }
    // 初始化FFmpeg
    avformat_network_init();

    AVDictionary* options = NULL;
    char dev_index[5];
    sprintf_s(dev_index, sizeof(dev_index), "%d", g_dev_index);
    av_dict_set(&options, "device_index", dev_index, 0);
    av_dict_set(&options, "w_endpoint", "1", 0);
    av_dict_set(&options, "r_endpoint", "1", 0);
    av_dict_set(&options, "rw_timeout", "30", 0);
    // 打开
    
    if (avformat_open_input(&fmt_ctx, "ch375://upload", NULL, &options) != 0) {
        av_dict_free(&options);
        return -1;
    }
    fmt_ctx->flags |= AVFMT_FLAG_NOBUFFER | AVFMT_FLAG_DISCARD_CORRUPT;
    fmt_ctx->probesize = 32;
    // 获取流信息
    if (avformat_find_stream_info(fmt_ctx, NULL) < 0) {
        SDL_Log("未找到流信息");
        return -1;
    }

    // 3. 查找视频流
    for (unsigned i = 0; i < fmt_ctx->nb_streams; i++) {
        if (fmt_ctx->streams[i]->codecpar->codec_type == AVMEDIA_TYPE_VIDEO) {
            video_stream = i;
            break;
        }
    }

    // 4. 初始化解码器
    const AVCodec* codec = avcodec_find_decoder(fmt_ctx->streams[video_stream]->codecpar->codec_id);
    codec_ctx = avcodec_alloc_context3(codec);
    avcodec_parameters_to_context(codec_ctx, fmt_ctx->streams[video_stream]->codecpar);
    avcodec_open2(codec_ctx, codec, NULL);

    // 5. 初始化SDL
    SDL_Init(SDL_INIT_VIDEO);
    SDL_Window* window = SDL_CreateWindow("Video Player",
        codec_ctx->width, codec_ctx->height,
        SDL_WINDOW_RESIZABLE);
    renderer = SDL_CreateRenderer(window, NULL);

    // 创建YUV纹理
    texture = SDL_CreateTexture(renderer,
        SDL_PIXELFORMAT_IYUV,
        SDL_TEXTUREACCESS_STREAMING,
        codec_ctx->width, codec_ctx->height);

    // 初始化缩放上下文
    sws_ctx = sws_getContext(
        codec_ctx->width, codec_ctx->height, codec_ctx->pix_fmt, // 输入参数
        codec_ctx->width, codec_ctx->height, AV_PIX_FMT_YUV420P, // 输出参数必须与yuv_frame->format一致
        SWS_BILINEAR, NULL, NULL, NULL);

    std::thread Play_thread(PlayThread);
    auto native_handle = Play_thread.native_handle();
    std::thread watchdog([=]() {
        watchdog_thread(native_handle);
        });
    event_loop();
    Play_thread.join();
    watchdog.join();
    // 清理资源
    sws_freeContext(sws_ctx);
    SDL_DestroyTexture(texture);
    SDL_DestroyRenderer(renderer);
    SDL_DestroyWindow(window);
    avcodec_free_context(&codec_ctx);
    avformat_close_input(&fmt_ctx);
    av_dict_free(&options);
    SDL_Quit();
    return 0;
}