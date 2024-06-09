#include <windows.h>
#include <iostream>
#include <chrono>
#include <thread>
// Function to capture the screen and save it as a bitmap file
#include <Windows.h>
#include <iostream>
void CaptureScreen(const char* filePath) {
    HDESK hInputDesktop = OpenInputDesktop(0, FALSE, GENERIC_ALL);
    if (!hInputDesktop) {
        std::cerr << "OpenInputDesktop failed: " << GetLastError() << std::endl;
        return;
    }
    if (!SetThreadDesktop(hInputDesktop)) {
        std::cerr << "SetThreadDesktop failed: " << GetLastError() << std::endl;
        CloseDesktop(hInputDesktop);
        return;
    }
    HDC hScreenDC = GetDC(NULL);
    if (!hScreenDC) {
        std::cerr << "GetDC failed: " << GetLastError() << std::endl;
        CloseDesktop(hInputDesktop);
        return;
    }
    HDC hMemoryDC = CreateCompatibleDC(hScreenDC);
    if (!hMemoryDC) {
        std::cerr << "CreateCompatibleDC failed: " << GetLastError() << std::endl;
        ReleaseDC(NULL, hScreenDC);
        CloseDesktop(hInputDesktop);
        return;
    }
    int screenWidth = GetSystemMetrics(SM_CXSCREEN);
    int screenHeight = GetSystemMetrics(SM_CYSCREEN);
    HBITMAP hBitmap = CreateCompatibleBitmap(hScreenDC, screenWidth, screenHeight);
    if (!hBitmap) {
        std::cerr << "CreateCompatibleBitmap failed: " << GetLastError() << std::endl;
        DeleteDC(hMemoryDC);
        ReleaseDC(NULL, hScreenDC);
        CloseDesktop(hInputDesktop);
        return;
    }
    HGDIOBJ hOldBitmap = SelectObject(hMemoryDC, hBitmap);
    if (!BitBlt(hMemoryDC, 0, 0, screenWidth, screenHeight, hScreenDC, 0, 0, SRCCOPY)) {
        std::cerr << "BitBlt failed: " << GetLastError() << std::endl;
        SelectObject(hMemoryDC, hOldBitmap);
        DeleteObject(hBitmap);
        DeleteDC(hMemoryDC);
        ReleaseDC(NULL, hScreenDC);
        CloseDesktop(hInputDesktop);
        return;
    }
    BITMAPINFOHEADER bi = { 0 };
    bi.biSize = sizeof(BITMAPINFOHEADER);
    bi.biWidth = screenWidth;
    bi.biHeight = -screenHeight; // Negative to indicate a top-down DIB
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
        CloseDesktop(hInputDesktop);
        return;
    }
    // Create a file to save the bitmap
    HANDLE hFile = CreateFileA(filePath, GENERIC_WRITE, 0, NULL, CREATE_ALWAYS, FILE_ATTRIBUTE_NORMAL, NULL);
    if (hFile != INVALID_HANDLE_VALUE) {
        // Create a BITMAPFILEHEADER structure to hold the file header
        BITMAPFILEHEADER bf = { 0 };
        bf.bfType = 0x4D42; // 'BM'
        bf.bfOffBits = sizeof(BITMAPFILEHEADER) + sizeof(BITMAPINFOHEADER);
        bf.bfSize = bf.bfOffBits + dataSize;
        // Write the file header and the bitmap data to the file
        DWORD written;
        WriteFile(hFile, &bf, sizeof(bf), &written, NULL);
        WriteFile(hFile, &bi, sizeof(bi), &written, NULL);
        WriteFile(hFile, pData, dataSize, &written, NULL);
        CloseHandle(hFile);
    }
    delete[] pData;
    SelectObject(hMemoryDC, hOldBitmap);
    DeleteObject(hBitmap);
    DeleteDC(hMemoryDC);
    ReleaseDC(NULL, hScreenDC);
    CloseDesktop(hInputDesktop);
}
int main() {
    int captureCount = 0;
    while (true) {
        std::string filePath = "C:\\screenshot\\screenshot_" + std::to_string(captureCount) + ".bmp";
        std::cout << "Screenshot saved as " << filePath << std::endl;
        CaptureScreen(filePath.c_str());
        captureCount++;
        std::this_thread::sleep_for(std::chrono::seconds(1));
    }
}