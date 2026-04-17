#pragma once
#include <windows.h>
#include <string>

class OsdWindow {
public:
    static void ShowNumber(int number);
    static void Hide();
    static void Init(HINSTANCE hInstance);
private:
    static LRESULT CALLBACK WndProc(HWND hwnd, UINT msg, WPARAM wParam, LPARAM lParam);
    static HWND hOsdWnd;
    static int currentNumber;
};