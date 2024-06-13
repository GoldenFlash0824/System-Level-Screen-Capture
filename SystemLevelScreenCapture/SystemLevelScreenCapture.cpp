extern "C" {
#include <libavcodec/avcodec.h>
#include <libavformat/avformat.h>
#include <libswscale/swscale.h>
}

#include <winsock2.h>
#include <ws2tcpip.h>
#include <windows.h>
#include <iostream>
#include <chrono>
#include <thread>

// Link with Ws2_32.lib and Gdi32.lib
#pragma comment(lib, "Ws2_32.lib")
#pragma comment(lib, "Gdi32.lib")

#define SERVER_PORT 8762
#define SERVER_IP "127.0.0.1"

// Function to send encoded data to the server
bool SendEncodedData(SOCKET clientSocket, const uint8_t* data, int dataSize) {
    int bytesSent = 0;
    while (bytesSent < dataSize) {
        int result = send(clientSocket, reinterpret_cast<const char*>(data + bytesSent), dataSize - bytesSent, 0);
        if (result == SOCKET_ERROR) {
            std::cerr << "Send failed with error: " << WSAGetLastError() << std::endl;
            return false;
        }
        bytesSent += result;
    }
    return true;
}

void CaptureAndEncodeScreen(SOCKET clientSocket, AVCodecContext* codecContext, AVFrame* frame, AVPacket* pkt) {
    HDC hScreenDC = GetDC(NULL);
    if (!hScreenDC) {
        std::cerr << "GetDC failed: " << GetLastError() << std::endl;
        return;
    }

    HDC hMemoryDC = CreateCompatibleDC(hScreenDC);
    if (!hMemoryDC) {
        std::cerr << "CreateCompatibleDC failed: " << GetLastError() << std::endl;
        ReleaseDC(NULL, hScreenDC);
        return;
    }

    int screenWidth = GetSystemMetrics(SM_CXSCREEN);
    int screenHeight = GetSystemMetrics(SM_CYSCREEN);
    HBITMAP hBitmap = CreateCompatibleBitmap(hScreenDC, screenWidth, screenHeight);
    if (!hBitmap) {
        std::cerr << "CreateCompatibleBitmap failed: " << GetLastError() << std::endl;
        DeleteDC(hMemoryDC);
        ReleaseDC(NULL, hScreenDC);
        return;
    }

    HGDIOBJ hOldBitmap = SelectObject(hMemoryDC, hBitmap);
    if (!BitBlt(hMemoryDC, 0, 0, screenWidth, screenHeight, hScreenDC, 0, 0, SRCCOPY)) {
        std::cerr << "BitBlt failed: " << GetLastError() << std::endl;
        SelectObject(hMemoryDC, hOldBitmap);
        DeleteObject(hBitmap);
        DeleteDC(hMemoryDC);
        ReleaseDC(NULL, hScreenDC);
        return;
    }

    BITMAPINFOHEADER bi = { 0 };
    bi.biSize = sizeof(BITMAPINFOHEADER);
    bi.biWidth = screenWidth;
    bi.biHeight = -screenHeight;
    bi.biPlanes = 1;
    bi.biBitCount = 24;
    bi.biCompression = BI_RGB;
    int dataSize = ((screenWidth * bi.biBitCount + 31) / 32) * 4 * screenHeight;
    BYTE* pData = new BYTE[dataSize];
    if (!GetDIBits(hMemoryDC, hBitmap, 0, screenHeight, pData, (BITMAPINFO*)&bi, DIB_RGB_COLORS)) {
        std::cerr << "GetDIBits failed: " << GetLastError() << std::endl;
        delete[] pData;
        SelectObject(hMemoryDC, hOldBitmap);
        DeleteObject(hBitmap);
        DeleteDC(hMemoryDC);
        ReleaseDC(NULL, hScreenDC);
        return;
    }

    SwsContext* swsContext = sws_getContext(screenWidth, screenHeight, AV_PIX_FMT_BGR24,
        screenWidth, screenHeight, AV_PIX_FMT_YUV420P,
        SWS_BILINEAR, NULL, NULL, NULL);

    uint8_t* srcData[1] = { pData };
    int srcLinesize[1] = { 3 * screenWidth };

    sws_scale(swsContext, srcData, srcLinesize, 0, screenHeight, frame->data, frame->linesize);

    frame->pts++;

    if (avcodec_send_frame(codecContext, frame) < 0) {
        std::cerr << "Error sending frame to encoder" << std::endl;
    }

    while (avcodec_receive_packet(codecContext, pkt) == 0) {
        SendEncodedData(clientSocket, pkt->data, pkt->size);
        av_packet_unref(pkt);
    }

    sws_freeContext(swsContext);
    delete[] pData;
    SelectObject(hMemoryDC, hOldBitmap);
    DeleteObject(hBitmap);
    DeleteDC(hMemoryDC);
    ReleaseDC(NULL, hScreenDC);
}

int main() {
    WSADATA wsaData;
    SOCKET clientSocket = INVALID_SOCKET;

    if (WSAStartup(MAKEWORD(2, 2), &wsaData) != 0) {
        std::cerr << "WSAStartup failed" << std::endl;
        return 1;
    }

    clientSocket = socket(AF_INET, SOCK_STREAM, IPPROTO_TCP);
    if (clientSocket == INVALID_SOCKET) {
        std::cerr << "Socket creation failed" << std::endl;
        WSACleanup();
        return 1;
    }

    sockaddr_in serverAddr;
    serverAddr.sin_family = AF_INET;
    serverAddr.sin_port = htons(SERVER_PORT);
    if (inet_pton(AF_INET, SERVER_IP, &serverAddr.sin_addr) <= 0) {
        std::cerr << "Invalid IP address" << std::endl;
        closesocket(clientSocket);
        WSACleanup();
        return 1;
    }

    if (connect(clientSocket, (sockaddr*)&serverAddr, sizeof(serverAddr)) == SOCKET_ERROR) {
        std::cerr << "Connect failed with error: " << WSAGetLastError() << std::endl;
        closesocket(clientSocket);
        WSACleanup();
        return 1;
    }

    const AVCodec* codec = avcodec_find_encoder(AV_CODEC_ID_H264);
    if (!codec) {
        std::cerr << "Codec not found" << std::endl;
        closesocket(clientSocket);
        WSACleanup();
        return 1;
    }

    AVCodecContext* codecContext = avcodec_alloc_context3(codec);
    if (!codecContext) {
        std::cerr << "Could not allocate video codec context" << std::endl;
        closesocket(clientSocket);
        WSACleanup();
        return 1;
    }

    codecContext->bit_rate = 400000;
    codecContext->width = GetSystemMetrics(SM_CXSCREEN);
    codecContext->height = GetSystemMetrics(SM_CYSCREEN);
    codecContext->time_base = { 1, 30 };
    codecContext->framerate = { 30, 1 };
    codecContext->gop_size = 10;
    codecContext->max_b_frames = 1;
    codecContext->pix_fmt = AV_PIX_FMT_YUV420P;

    if (avcodec_open2(codecContext, codec, NULL) < 0) {
        std::cerr << "Could not open codec" << std::endl;
        avcodec_free_context(&codecContext);
        closesocket(clientSocket);
        WSACleanup();
        return 1;
    }

    AVFrame* frame = av_frame_alloc();
    if (!frame) {
        std::cerr << "Could not allocate video frame" << std::endl;
        avcodec_free_context(&codecContext);
        closesocket(clientSocket);
        WSACleanup();
        return 1;
    }
    frame->format = codecContext->pix_fmt;
    frame->width = codecContext->width;
    frame->height = codecContext->height;
    if (av_frame_get_buffer(frame, 32) < 0) {
        std::cerr << "Could not allocate the video frame data" << std::endl;
        av_frame_free(&frame);
        avcodec_free_context(&codecContext);
        closesocket(clientSocket);
        WSACleanup();
        return 1;
    }

    AVPacket* pkt = av_packet_alloc();
    if (!pkt) {
        std::cerr << "Could not allocate AVPacket" << std::endl;
        av_frame_free(&frame);
        avcodec_free_context(&codecContext);
        closesocket(clientSocket);
        WSACleanup();
        return 1;
    }

    while (true) {
        CaptureAndEncodeScreen(clientSocket, codecContext, frame, pkt);
        std::this_thread::sleep_for(std::chrono::milliseconds(100));
    }

    av_packet_free(&pkt);
    av_frame_free(&frame);
    avcodec_free_context(&codecContext);
    closesocket(clientSocket);
    WSACleanup();
    return 0;
}
