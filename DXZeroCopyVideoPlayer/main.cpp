#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#include <d3d11.h>
#include <dxgi1_2.h>
#include <d3dcompiler.h>
#include <iostream>
#include <string>
#include <chrono>
#include "App.h"
#include "VideoSource.h"
#include "DXShader.h"
#include "DXRenderer.h"
#include "utils.h"

// FFmpeg headers (Must be extern "C")
extern "C" {
#include <libavcodec/avcodec.h>
#include <libavformat/avformat.h>
#include <libavutil/hwcontext.h>
#include <libavutil/hwcontext_d3d11va.h>
#include <libavutil/frame.h> // Required for av_frame_alloc

}

// Linker pragmas (Alternative to Project Properties)
#pragma comment(lib, "d3d11.lib")
#pragma comment(lib, "dxgi.lib")
#pragma comment(lib, "avcodec.lib")
#pragma comment(lib, "avformat.lib")
#pragma comment(lib, "avutil.lib")
#pragma comment(lib, "d3dcompiler.lib")

int main()
{
	try
	{
		App app(1280, 720);
		app.Run();
	}
	catch (const std::exception& ex)
	{
		std::cerr << "Application error: " << ex.what() << std::endl;
		return -1;
	}	

	return 0;
}