#include "OsdWindow.h"
#include <gdiplus.h>
#pragma comment (lib,"Gdiplus.lib")

using namespace Gdiplus;

HWND OsdWindow::hOsdWnd = NULL;
int OsdWindow::currentNumber = 0;
ULONG_PTR gdiplusToken;

void OsdWindow::Init(HINSTANCE hInstance) {
    GdiplusStartupInput gdiplusStartupInput;
    GdiplusStartup(&gdiplusToken, &gdiplusStartupInput, NULL);

    WNDCLASSEX wc = { sizeof(WNDCLASSEX) };
    wc.lpfnWndProc = WndProc;
    wc.hInstance = hInstance;
    wc.lpszClassName = "OsdWindowClass";
    wc.hbrBackground = (HBRUSH)GetStockObject(BLACK_BRUSH);
    RegisterClassEx(&wc);

    int sw = GetSystemMetrics(SM_CXSCREEN);
    int sh = GetSystemMetrics(SM_CYSCREEN);

    hOsdWnd = CreateWindowEx(
            WS_EX_LAYERED | WS_EX_TRANSPARENT | WS_EX_TOPMOST | WS_EX_TOOLWINDOW,
            "OsdWindowClass", "", WS_POPUP,
            0, 0, sw, sh, NULL, NULL, hInstance, NULL);

    // 设置黑色为透明色，整体不透明度255
    SetLayeredWindowAttributes(hOsdWnd, RGB(0, 0, 0), 255, LWA_COLORKEY);
}

void OsdWindow::ShowNumber(int number) {
    currentNumber = number;
    // 极其关键：SW_SHOWNA 代表显示但不激活窗口，绝不抢走代码编辑器的焦点
    ShowWindow(hOsdWnd, SW_SHOWNA);
    InvalidateRect(hOsdWnd, NULL, TRUE);
    UpdateWindow(hOsdWnd);
}

void OsdWindow::Hide() {
    ShowWindow(hOsdWnd, SW_HIDE);
}

LRESULT CALLBACK OsdWindow::WndProc(HWND hwnd, UINT msg, WPARAM wParam, LPARAM lParam) {
    if (msg == WM_PAINT) {
        PAINTSTRUCT ps;
        HDC hdc = BeginPaint(hwnd, &ps);

        Graphics graphics(hdc);
        graphics.SetSmoothingMode(SmoothingModeAntiAlias);

        FontFamily  fontFamily(L"Arial");
        Font        font(&fontFamily, 200, FontStyleBold, UnitPixel);
        SolidBrush  solidBrush(Color(255, 255, 100, 100)); // 红色大字

        std::wstring text = std::to_wstring(currentNumber);

        StringFormat format;
        format.SetAlignment(StringAlignmentCenter);
        format.SetLineAlignment(StringAlignmentCenter);

        RECT rect;
        GetClientRect(hwnd, &rect);
        RectF layoutRect(0, 0, (REAL)rect.right, (REAL)rect.bottom);

        graphics.DrawString(text.c_str(), -1, &font, layoutRect, &format, &solidBrush);

        EndPaint(hwnd, &ps);
        return 0;
    }
    return DefWindowProc(hwnd, msg, wParam, lParam);
}