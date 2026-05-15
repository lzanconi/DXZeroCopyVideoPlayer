#include "VideoSource.h"
#include <iostream>
#include "utils.h"
#include "IRenderer.h"

extern "C" {
#include <libavcodec/avcodec.h>
#include <libavformat/avformat.h>
#include <libavutil/hwcontext.h>
#include <libavutil/frame.h>

}

VideoSource::VideoSource() : bg_capture_time_ns(0) {}

VideoSource::~VideoSource()
{
	Close();
}

bool VideoSource::Open(const std::string& file, AVBufferRef* hwDeviceCtx)
{

    filename = file;
    //Opens the video file and reads the header to understand the container format
    if (avformat_open_input(&formatCtx, file.c_str(), NULL, NULL) < 0)
    {
        std::cerr << "Failed to open: " << file << std::endl;
        return false;
    }

    //Analyzes the file to get detailed information about the streams (video, audio, etc.)
    if (avformat_find_stream_info(formatCtx, NULL) < 0)
        return false;

    //Specifically searches for the primary video stream within the file and returns its index
    streamID = av_find_best_stream(formatCtx, AVMEDIA_TYPE_VIDEO, -1, -1, NULL, 0);
    if (streamID < 0)
        return false;

    //Looks up the appropriate decoder (like H.264, HEVC, VP9 etc.) based on the video's codec ID
    const AVCodec* decoder = avcodec_find_decoder(formatCtx->streams[streamID]->codecpar->codec_id);
    if (!decoder)
    {
		std::cerr << "Fauled to find decoder for codec id: " << formatCtx->streams[streamID]->codecpar->codec_id << std::endl;
		return false;
    }

    //Creates a codec context which holds the settings and state for the decoding process
    codecCtx = avcodec_alloc_context3(decoder);
    //Copies the settings from the file (like resolution and framerate) into the decoder context
    avcodec_parameters_to_context(codecCtx, formatCtx->streams[streamID]->codecpar);

	videoWidth = formatCtx->streams[streamID]->codecpar->width;
	videoHeight = formatCtx->streams[streamID]->codecpar->height;

    //Receives a reference to a hardware device context (created in App.h) and assigns it to the codec
    codecCtx->hw_device_ctx = av_buffer_ref(hwDeviceCtx);

    //Tells FFmpeg to pick AV_PIX_FMT_D3D11 format if it can to ensure the decoding happens directly on the GPU rather than the CPU
	codecCtx->get_format = get_hw_format;

    //Opens the decoder with the configured settings
    if (avcodec_open2(codecCtx, decoder, NULL) < 0)
    {
		std::cerr << "Failed to open codec." << std::endl;
        return false;
    }

    //Sets isInitialized to true allowing UpdateAndRender() to start processing frames
    isInitialized = true;
    return true;
}

void VideoSource::Close()
{
    if (codecCtx) 
        avcodec_free_context(&codecCtx);
    
    if (formatCtx) 
        avformat_close_input(&formatCtx);
    
    isInitialized = false;
}


bool VideoSource::UpdateAndRender(IRenderer* renderer, AVFrame* frame, AVPacket* raw_packet, int slot)
{
    if (!isInitialized)
        return true;

	double currentTime = GetTimeStd();

    if (startTime <= 0)
        return true;

    double playPos = currentTime - startTime;

	float alpha = CalculateAlpha(currentTime);  

    //std::cout << "LastPTS: " << lastPTS << std::endl;

    if (playPos > lastPTS)
    {
		bool frameDecoded = false;

        while (!frameDecoded)
        {
            if (av_read_frame(formatCtx, raw_packet) >= 0)
            {
                if (raw_packet->stream_index == streamID)
                {
                    if (avcodec_send_packet(codecCtx, raw_packet) == 0)
                    {
                        if (avcodec_receive_frame(codecCtx, frame) >= 0)
                        {
                            renderer->RenderFrame(frame);
                            av_frame_unref(frame);
                            lastPTS = playPos;
                            frameDecoded = true;
                            bg_capture_time_ns = std::chrono::duration_cast<std::chrono::nanoseconds>(
                                std::chrono::steady_clock::now().time_since_epoch()).count();
                        }
                    }
                }

                av_packet_unref(raw_packet);
            }
            else
            {
                if (looped)
                {
					Rewind();
					Play(GetTimeStd());
                    frameDecoded = true;
                }
                else
                {
                    return false;
                }
            }
            /*if (av_read_frame(formatCtx, raw_packet) < 0 && looped) 
            {
                av_seek_frame(formatCtx, streamID, 0, AVSEEK_FLAG_BACKWARD);
                continue;
            }

            if (raw_packet->stream_index == streamID && avcodec_send_packet(codecCtx, raw_packet) == 0) 
            {
                while (avcodec_receive_frame(codecCtx, frame) == 0) 
                {
                    renderer->RenderFrame(frame);
                    av_frame_unref(frame);
                    lastPTS = playPos;
                    frameDecoded = true;
                    bg_capture_time_ns = std::chrono::duration_cast<std::chrono::nanoseconds>(
                        std::chrono::steady_clock::now().time_since_epoch()).count();
                }
            }
            av_packet_unref(raw_packet);*/
        }
    }


	return true;
}



void VideoSource::Rewind()
{
    if (!isInitialized)
        return;

    av_seek_frame(formatCtx, streamID, 0, AVSEEK_FLAG_BACKWARD);
    avcodec_flush_buffers(codecCtx);
	lastPTS = -1.0; 
}

void VideoSource::Play(double startTime)
{
    this->startTime = startTime;
	lastPTS = -1.0; 
}

void VideoSource::SetLooped(bool l) { looped = l; }
double VideoSource::GetLastPTS() { return lastPTS; }
double VideoSource::GetAdjustedStartTime() const { return startTime + totalPausedTime; }
int64_t VideoSource::GetBGCaptureTimeNS() { return bg_capture_time_ns; }
bool VideoSource::IsPaused() const { return isPaused; }
void VideoSource::SetFadeInDuration(float d) { fadeInDuration = d; }
void VideoSource::SetFadeOutDuration(float d) { fadeOutDuration = d; }

double VideoSource::GetDurationInSeconds() const
{
    if (!formatCtx || streamID < 0)
        return 0;

    return (double)formatCtx->streams[streamID]->duration * av_q2d(formatCtx->streams[streamID]->time_base);
}

int VideoSource::GetVideoWidth() const
{
    return videoWidth;
}

int VideoSource::GetVideoHeight() const
{
	return videoHeight;
}

float VideoSource::CalculateAlpha(double currentTime)
{
    double elapsed = currentTime;
	double totalDuration = GetDurationInSeconds();
	float alpha = 1.0f;

    if (elapsed < fadeInDuration && fadeInDuration > 0)
    {
        alpha = (float)(elapsed / fadeInDuration);
    }
    else if (elapsed > (totalDuration - fadeOutDuration) && fadeOutDuration > 0)
    {
        double timeRemaining = totalDuration - elapsed;
        alpha = (float)(timeRemaining / fadeOutDuration);
    }

    if (alpha < 0.0f)
        alpha = 0.0f;

    if (alpha > 1.0f)
        alpha = 1.0f;

    return alpha;
}
