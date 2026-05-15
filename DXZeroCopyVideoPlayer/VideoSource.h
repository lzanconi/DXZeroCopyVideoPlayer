#pragma once
#include <string>
#include <atomic>
#include <vector>

extern "C" {
#include <libavcodec/avcodec.h>
#include <libavformat/avformat.h>
#include <libavutil/hwcontext.h>
#include <libavutil/frame.h>
}

// Forward declarations for FFmpeg structs to keep the header clean
struct AVFormatContext;
struct AVCodecContext;
struct AVBufferRef;
struct AVFrame;
struct AVPacket;

class IRenderer;
class DXShader;

class VideoSource
{
private:
    AVFormatContext* formatCtx = nullptr;
    AVCodecContext* codecCtx = nullptr;
    int streamID = -1;
    double startTime = 0;
    double pauseTime = 0;
    double totalPausedTime = 0;
    double lastPTS = -1.0;

    std::string filename;
    bool isInitialized = false;
    bool isPaused = false;
    bool looped = false;

    float fadeInDuration = 2.5f;
    float fadeOutDuration = 1.0f;
    std::atomic<int64_t> bg_capture_time_ns;
	int videoWidth = 0;
	int videoHeight = 0;

public:
    std::vector<float> positions;

public:
    VideoSource();
    ~VideoSource();

    // Initializes FFmpeg contexts and hardware decoding
    bool Open(const std::string& file, AVBufferRef* hwDeviceCtx);

    // Resets the video to the first frame
    void Rewind();

    // Starts or resumes playback
    void Play(double currentTime);

    // Main update loop: decodes and renders frames
    bool UpdateAndRender(IRenderer* renderer, AVFrame* frm, AVPacket* raw_packet, int slot);

    // Cleans up FFmpeg resources
    void Close();

    // Utility getters and setters
    double GetDurationInSeconds() const;
    void SetLooped(bool l);
    double GetLastPTS();
    double GetAdjustedStartTime() const;
    int64_t GetBGCaptureTimeNS();
    bool IsPaused() const;
    void SetFadeInDuration(float d);
    void SetFadeOutDuration(float d);
    int GetVideoWidth() const;
    int GetVideoHeight() const;
	bool Ready() const { return isInitialized; }

    static enum AVPixelFormat get_hw_format(AVCodecContext* ctx, const enum AVPixelFormat* pix_fmts) {
        for (const enum AVPixelFormat* p = pix_fmts; *p != -1; p++) {
            if (*p == AV_PIX_FMT_D3D11) return *p;
        }
        return AV_PIX_FMT_NONE;
    }

private:
    float CalculateAlpha(double currentTime);
};

