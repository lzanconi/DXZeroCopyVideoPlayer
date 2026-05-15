#pragma once
#include "IApp.h"
#include "customtypes.h"
#include <windows.h>

class IRenderer;
class VideoSource;	
struct AVBufferRef;
struct AVPacket;
struct AVFrame;

class App : public IApp
{
public:
	App(int width, int height);
	~App();

	VideoSource* GetBackgroundVideo() override;
	std::vector<float> GetPositions() override;
	double GetLastPTS() override;
	int64_t GetBGCaptureTimeNS() override;

	void Run();

private:
	static LRESULT CALLBACK WndProc(HWND hwnd, UINT msg, WPARAM wp, LPARAM lp);
	LRESULT HandleMessage(HWND hwnd, UINT msg, WPARAM wp, LPARAM lp);
	void TriggerFullscreen();
	void TriggerWindowedMode();

public:
	static AppState state;

private:
	bool isFullscreen = false;
	IRenderer *renderer = nullptr;	
	AVBufferRef* hw_ctx;
	AVPacket* raw_packet;
	AVFrame* frame;
	WNDCLASS wndClass = { 0 };
	MSG msg = { 0 };
	HWND hwnd = nullptr;
	// Stores window position before going fullscreen
	RECT windowRect = { 0 };	
};

