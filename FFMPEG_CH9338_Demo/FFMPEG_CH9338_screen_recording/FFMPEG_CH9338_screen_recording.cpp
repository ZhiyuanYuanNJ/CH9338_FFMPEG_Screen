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
#include "../CH375/CH375DLL.H"
#pragma comment(lib, "../CH375/WCHKMFU64.lib")
int g_dev_index = -1;
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

int main() {
    if (serch_device("VID_1A86&PID_8026&MI_01") == false) {
        SDL_Log("无法打开CH9338");
        return -1;
    }
    AVFormatContext* fmt_ctx = avformat_alloc_context();
    AVCodecContext* enc_ctx = NULL;
    SwsContext* sws_ctx = NULL;
    AVOutputFormat* ofmt = NULL;
    AVFormatContext* ofmt_ctx = avformat_alloc_context();
    int video_stream_index = -1;
    int ret;
    avformat_network_init();

    avdevice_register_all();

    // 初始化输入设备
    const AVInputFormat* ifmt = av_find_input_format("gdigrab");
    AVDictionary* options = NULL;
    av_dict_set(&options, "framerate", "30", 0);
    av_dict_set(&options, "offset_x", "0", 0);
    av_dict_set(&options, "offset_y", "0", 0);
    av_dict_set(&options, "video_size", "1920x1080", 0);
    av_dict_set(&options, "probesize", "32", 0);
    av_dict_set(&options, "fflags", "flush_packets+discardcorrupt", 0);
    av_dict_set(&options, "flags", "low_delay", 0);
    av_dict_set(&options, "start_time_realtime", 0, 0);

    if ((ret = avformat_open_input(&fmt_ctx, "desktop", ifmt, &options)) < 0) {
        fprintf(stderr, "无法获取gdigrab\n");
        return -1;
    }
    av_dict_free(&options);
    if (avformat_find_stream_info(fmt_ctx, NULL) < 0) {
        fprintf(stderr, "无法获取流信息\n");
        return -1;
    }

    // 查找视频流
    for (int i = 0; i < fmt_ctx->nb_streams; i++) {
        if (fmt_ctx->streams[i]->codecpar->codec_type == AVMEDIA_TYPE_VIDEO) {
            video_stream_index = i;
            break;
        }
    }

    if (video_stream_index == -1) {
        fprintf(stderr, "未找到视频流\n");
        return -1;
    }
    // 初始化编码器
    const AVCodec* encoder = avcodec_find_encoder_by_name("libx264");
    enc_ctx = avcodec_alloc_context3(encoder);
    AVCodecParameters* codecpar = fmt_ctx->streams[video_stream_index]->codecpar;
    enc_ctx->pix_fmt = AV_PIX_FMT_YUV420P;
    enc_ctx->width = codecpar->width;
    enc_ctx->height = codecpar->height;
    enc_ctx->time_base = { 1, 30 };  // 保持原设置，但需要与流同步
    enc_ctx->framerate = { 30, 1 };  // 新增帧率设置
    enc_ctx->max_b_frames = 0;
    enc_ctx->gop_size = 1;
    enc_ctx->qmin = 28;
    enc_ctx->qmax = 28;
    enc_ctx->qblur = 0.0;

    AVDictionary* enc_opts = NULL;
    av_dict_set(&enc_opts, "preset", "ultrafast", 0);
    av_dict_set(&enc_opts, "tune", "zerolatency", 0);
    av_dict_set(&enc_opts, "crf", "28", 0);
    av_dict_set(&enc_opts, "max_delay", "0", 0);
    enc_ctx->hw_device_ctx = NULL;
    enc_ctx->color_range = AVCOL_RANGE_MPEG;
    if (avcodec_open2(enc_ctx, encoder, &enc_opts) < 0) {
        fprintf(stderr, "无法打开编码器\n");
        return -1;
    }

    // 初始化像素格式转换
    sws_ctx = sws_getContext(
        codecpar->width, codecpar->height, (AVPixelFormat)codecpar->format,
        enc_ctx->width, enc_ctx->height, AV_PIX_FMT_YUV420P,
        SWS_BICUBLIN, NULL, NULL, NULL);

    if (!sws_ctx) {
        fprintf(stderr, "无法初始化像素格式转换上下文\n");
        return -1;
    }
    char dev_index[5];
    sprintf_s(dev_index, sizeof(dev_index), "%d", g_dev_index);
    av_dict_set(&options, "device_index", dev_index, 0);
    av_dict_set(&options, "w_endpoint", "1", 0);
    av_dict_set(&options, "r_endpoint", "1", 0);
    av_dict_set(&options, "rw_timeout", "30", 0);
    // 初始化输出
    avformat_alloc_output_context2(&ofmt_ctx, NULL, "h264", "ch375://download");
    if (avio_open2(&ofmt_ctx->pb, ofmt_ctx->url, AVIO_FLAG_WRITE, NULL, &options) < 0) {
        av_log(NULL, AV_LOG_ERROR, "无法打开输出\n");
        return -1;
    }

    AVStream* stream = avformat_new_stream(ofmt_ctx, encoder);
    // 同步编码器与输出流的时间基
    stream->time_base = enc_ctx->time_base;
    avcodec_parameters_from_context(stream->codecpar, enc_ctx);

    if (avformat_write_header(ofmt_ctx, NULL) < 0) {
        printf("avformat_write_header fail!\n");
    }

    // 主循环
    AVPacket* pkt = av_packet_alloc();
    AVFrame* frame = av_frame_alloc();
    frame->format = enc_ctx->pix_fmt;
    frame->width = enc_ctx->width;
    frame->height = enc_ctx->height;
    av_frame_get_buffer(frame, 0);

    while(1) {
        if (av_read_frame(fmt_ctx, pkt) < 0) break;
        if (pkt->stream_index == video_stream_index) {
            AVRational input_time_base = fmt_ctx->streams[video_stream_index]->time_base;
            unsigned int untime = GetTickCount();
            pkt->pts = untime;
            pkt->dts = untime;

            AVFrame* input_frame = av_frame_alloc();
            input_frame->width = codecpar->width;
            input_frame->height = codecpar->height;
            input_frame->format = (AVPixelFormat)codecpar->format;
            av_frame_get_buffer(input_frame, 0);

            av_image_fill_arrays(input_frame->data, input_frame->linesize, pkt->data + (pkt->size - codecpar->width * codecpar->height * 4),
                (AVPixelFormat)codecpar->format, codecpar->width, codecpar->height, 1);

            ret = sws_scale(sws_ctx, input_frame->data, input_frame->linesize, 0,
                codecpar->height, frame->data, frame->linesize);
            enc_ctx->flags |= AV_CODEC_FLAG_GLOBAL_HEADER;

            if (ret > 0)
                if (avcodec_send_frame(enc_ctx, frame) < 0) break;

            while ((ret = avcodec_receive_packet(enc_ctx, pkt)) >= 0) {
                if (ret == AVERROR(EAGAIN) || ret == AVERROR_EOF)
                    return -1;
                else if (ret < 0) {
                    fprintf(stderr, "Error encoding audio frame\n");
                    exit(1);
                }
                pkt->stream_index = stream->index;

                if (av_interleaved_write_frame(ofmt_ctx, pkt) < 0) {
                    printf("av_interleaved_write_frame fail!\n");
                }
                av_packet_unref(pkt);
            }
            av_frame_free(&input_frame);
        }
        av_packet_unref(pkt);
    }
    // 清理资源
    av_write_trailer(ofmt_ctx);
    sws_freeContext(sws_ctx);
    avformat_close_input(&fmt_ctx);
    avcodec_free_context(&enc_ctx);
    avio_closep(&ofmt_ctx->pb);
    return 0;
}
