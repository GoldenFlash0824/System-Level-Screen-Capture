#include <winsock2.h>
#include <ws2tcpip.h>
#include <windows.h>
#include <iostream>
#include <chrono>
#include <thread>

// Link with Ws2_32.lib and Gdi32.lib
#pragma comment(lib, "Ws2_32.lib")
#pragma comment(lib, "Gdi32.lib")

#define SERVER_PORT 12345
#define SERVER_IP "127.0.0.1"

// Function to send bitmap data to the server
bool SendBitmapData(SOCKET clientSocket, const BYTE* data, int dataSize) {
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

void CaptureScreen(SOCKET clientSocket) {
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
    bi.biBitCount = 32;
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

    BITMAPFILEHEADER bfh;
    memset(&bfh, 0, sizeof(bfh));
    bfh.bfType = 0x4D42; // 'BM'
    bfh.bfOffBits = sizeof(BITMAPFILEHEADER) + sizeof(BITMAPINFOHEADER);
    bfh.bfSize = bfh.bfOffBits + dataSize;

    send(clientSocket, reinterpret_cast<const char*>(&bfh), sizeof(bfh), 0);
    send(clientSocket, reinterpret_cast<const char*>(&bi), sizeof(bi), 0);
    SendBitmapData(clientSocket, pData, dataSize);

    delete[] pData;
    SelectObject(hMemoryDC, hOldBitmap);
    DeleteObject(hBitmap);
    DeleteDC(hMemoryDC);
    ReleaseDC(NULL, hScreenDC);
}

// Function to capture the entire screen and send data to Electron
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

    while (true) {
        CaptureScreen(clientSocket);
        std::this_thread::sleep_for(std::chrono::milliseconds(100));
    }

    closesocket(clientSocket);
    WSACleanup();
    return 0;
}