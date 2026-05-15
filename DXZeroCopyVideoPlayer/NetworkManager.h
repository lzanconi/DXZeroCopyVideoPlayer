#pragma once
#include <string>
#include <thread>
#include <atomic>
#include <vector>
#include <cstdint>

// Forward declarations
class IApp;
class VideoSource;

// Typedef to avoid including winsock2.h in the header
#if defined(_WIN32) || defined(_WIN64)
typedef uintptr_t SOCKET;
#else
typedef int SOCKET;
#endif

class NetworkManager
{
public:
    NetworkManager(const std::string& serverIP, int port, IApp* appInterface);
    ~NetworkManager();

    // Starts the background networking thread
    void Start();

    // Stops the background networking thread and joins it
    void Stop();

private:
    // Main loop for the background thread to handle reconnections
    void Run();

    // Handles the timing and sending of position data to the server
    void HandlePositionSend(SOCKET socket);

    std::string serverIP;
    int port;
    std::thread workerThread;
    std::atomic<bool> running;
    IApp* appInterface;

    // Configuration for position sending
    double positions_delay_ms = -130.0;
    double positions_framerate = 60.0;
};