#define UNICODE
#define _UNICODE

#include <windows.h>
#include <windowsx.h>
#include <commctrl.h>
#include <richedit.h>
#include <shobjidl.h>
#include <filesystem>
#include <string>
#include <vector>
#include <fstream>

#include "TypingEngine.h"
#include "OsdWindow.h"

namespace fs = std::filesystem;

// 自定义 UI 消息
#define WM_APP_FILE_START (WM_APP + 2)
#define WM_APP_PROGRESS   (WM_APP + 3)

#define ID_TREEVIEW 101
#define ID_EDITOR 102
#define ID_BTN_OPEN 103
#define ID_BTN_START 104
#define ID_BTN_PAUSE 105
#define ID_BTN_RESUME 106
#define ID_BTN_RESET 107
#define ID_EDIT_DELAY 201
#define ID_EDIT_INTERVAL 202
#define ID_COMBO_MODE 203

HWND hTree, hEditor, hBtnOpen, hBtnStart, hBtnPause, hBtnResume, hBtnReset;
HWND hLblDelay, hEditDelay, hLblInterval, hEditInterval, hLblMode, hComboMode;
HWND hLblHotkey;
TypingEngine engine;
std::wstring currentProjectPath = L"";

std::string WStringToString(const std::wstring& wstr) {
    if (wstr.empty()) return std::string();
    int size_needed = WideCharToMultiByte(CP_UTF8, 0, &wstr[0], (int)wstr.size(), NULL, 0, NULL, NULL);
    std::string strTo(size_needed, 0);
    WideCharToMultiByte(CP_UTF8, 0, &wstr[0], (int)wstr.size(), &strTo[0], size_needed, NULL, NULL);
    return strTo;
}

void PopulateTreeView(HWND hTree, HTREEITEM hParent, const fs::path& path) {
    for (const auto& entry : fs::directory_iterator(path)) {
        std::wstring filename = entry.path().filename().wstring();
        if (filename[0] == L'.') continue;

        TVINSERTSTRUCTW tvis = {0};
        tvis.hParent = hParent;
        tvis.hInsertAfter = TVI_LAST;
        tvis.item.mask = TVIF_TEXT | TVIF_PARAM;
        tvis.item.pszText = (LPWSTR)filename.c_str();
        tvis.item.lParam = entry.is_directory() ? 0 : 1;
        HTREEITEM hItem = (HTREEITEM)SendMessageW(hTree, TVM_INSERTITEMW, 0, (LPARAM)&tvis);

        if (entry.is_directory()) PopulateTreeView(hTree, hItem, entry.path());
    }
}

std::wstring GetItemFullPath(HWND hTree, HTREEITEM hItem) {
    std::wstring relPath = L"";
    HTREEITEM hCurrent = hItem;
    HTREEITEM hRoot = TreeView_GetRoot(hTree);

    while (hCurrent != NULL && hCurrent != hRoot) {
        wchar_t buffer[MAX_PATH];
        TVITEMW tvi = {0};
        tvi.mask = TVIF_TEXT;
        tvi.hItem = hCurrent;
        tvi.pszText = buffer;
        tvi.cchTextMax = MAX_PATH;
        TreeView_GetItem(hTree, &tvi);

        if (relPath.empty()) relPath = buffer;
        else relPath = std::wstring(buffer) + L"\\" + relPath;
        hCurrent = TreeView_GetParent(hTree, hCurrent);
    }
    fs::path fullPath = fs::path(currentProjectPath) / relPath;
    return fullPath.wstring();
}

std::wstring GetItemRelativePath(HWND hTree, HTREEITEM hItem) {
    std::wstring relPath = L"";
    HTREEITEM hCurrent = hItem;
    HTREEITEM hRoot = TreeView_GetRoot(hTree);

    while (hCurrent != NULL && hCurrent != hRoot) {
        wchar_t buffer[MAX_PATH];
        TVITEMW tvi = {0};
        tvi.mask = TVIF_TEXT;
        tvi.hItem = hCurrent;
        tvi.pszText = buffer;
        tvi.cchTextMax = MAX_PATH;
        TreeView_GetItem(hTree, &tvi);

        if (relPath.empty()) relPath = buffer;
        else relPath = std::wstring(buffer) + L"/" + relPath;
        hCurrent = TreeView_GetParent(hTree, hCurrent);
    }
    wchar_t rootBuf[MAX_PATH];
    TVITEMW tviRoot = {0};
    tviRoot.mask = TVIF_TEXT;
    tviRoot.hItem = hRoot;
    tviRoot.pszText = rootBuf;
    tviRoot.cchTextMax = MAX_PATH;
    TreeView_GetItem(hTree, &tviRoot);

    if (relPath.empty()) relPath = rootBuf;
    else relPath = std::wstring(rootBuf) + L"/" + relPath;
    return relPath;
}

void CollectCheckedTasks(HWND hTree, HTREEITEM hItem, std::vector<FileTask>& tasks) {
    if (hItem == NULL) return;

    if (TreeView_GetCheckState(hTree, hItem)) {
        TVITEMW tvi = {0};
        tvi.mask = TVIF_PARAM;
        tvi.hItem = hItem;
        TreeView_GetItem(hTree, &tvi);

        if (tvi.lParam == 1) {
            std::wstring fullPath = GetItemFullPath(hTree, hItem);
            std::wstring relPathW = GetItemRelativePath(hTree, hItem);
            std::string relPath = WStringToString(relPathW);

            std::ifstream file(fullPath.c_str(), std::ios::binary);
            if (file) {
                std::string content((std::istreambuf_iterator<char>(file)), std::istreambuf_iterator<char>());
                // 【修改】这里将 hItem 传给任务对象
                tasks.push_back({"./" + relPath, content, (void*)hItem});
            }
        }
    }

    HTREEITEM hChild = TreeView_GetChild(hTree, hItem);
    while (hChild != NULL) {
        CollectCheckedTasks(hTree, hChild, tasks);
        hChild = TreeView_GetNextSibling(hTree, hChild);
    }
}

void LoadFileToEditor(const std::wstring& filePath) {
    std::error_code ec;
    if (fs::file_size(filePath, ec) > 5 * 1024 * 1024) {
        SetWindowTextW(hEditor, L"// 文件过大或不是文本文件，无法预览。");
        return;
    }
    std::ifstream file(filePath.c_str(), std::ios::binary);
    if (!file) return;
    std::string content((std::istreambuf_iterator<char>(file)), std::istreambuf_iterator<char>());

    int len = MultiByteToWideChar(CP_UTF8, 0, content.c_str(), -1, NULL, 0);
    if (len > 0) {
        std::wstring wcontent(len, 0);
        MultiByteToWideChar(CP_UTF8, 0, content.c_str(), -1, &wcontent[0], len);
        SetWindowTextW(hEditor, wcontent.c_str());
    }
}

void CheckAllChildren(HWND hTree, HTREEITEM hParent, BOOL fCheck) {
    HTREEITEM hChild = TreeView_GetChild(hTree, hParent);
    while (hChild != NULL) {
        TreeView_SetCheckState(hTree, hChild, fCheck);
        CheckAllChildren(hTree, hChild, fCheck);
        hChild = TreeView_GetNextSibling(hTree, hChild);
    }
}

void OpenProjectFolder(HWND hwnd) {
    IFileOpenDialog* pfd;
    if (SUCCEEDED(CoCreateInstance(CLSID_FileOpenDialog, NULL, CLSCTX_INPROC_SERVER, IID_PPV_ARGS(&pfd)))) {
        DWORD dwOptions;
        pfd->GetOptions(&dwOptions);
        pfd->SetOptions(dwOptions | FOS_PICKFOLDERS);

        if (SUCCEEDED(pfd->Show(hwnd))) {
            IShellItem* psi;
            if (SUCCEEDED(pfd->GetResult(&psi))) {
                PWSTR pszPath;
                if (SUCCEEDED(psi->GetDisplayName(SIGDN_FILESYSPATH, &pszPath))) {
                    currentProjectPath = pszPath;
                    TreeView_DeleteAllItems(hTree);
                    fs::path p(currentProjectPath);
                    TVINSERTSTRUCTW tvis = {0};
                    tvis.hParent = TVI_ROOT;
                    tvis.hInsertAfter = TVI_LAST;
                    tvis.item.mask = TVIF_TEXT | TVIF_PARAM;
                    std::wstring rootName = p.filename().wstring();
                    tvis.item.pszText = (LPWSTR)rootName.c_str();
                    tvis.item.lParam = 0;
                    HTREEITEM hRoot = (HTREEITEM)SendMessageW(hTree, TVM_INSERTITEMW, 0, (LPARAM)&tvis);

                    PopulateTreeView(hTree, hRoot, p);
                    TreeView_Expand(hTree, hRoot, TVE_EXPAND);
                    CoTaskMemFree(pszPath);
                }
                psi->Release();
            }
        }
        pfd->Release();
    }
}

LRESULT CALLBACK WndProc(HWND hwnd, UINT msg, WPARAM wParam, LPARAM lParam) {
    switch (msg) {
        case WM_CREATE: {
            LoadLibraryA("Msftedit.dll");

            RegisterHotKey(hwnd, 1, 0, VK_F8);
            RegisterHotKey(hwnd, 2, 0, VK_F9);

            hBtnOpen = CreateWindowExW(0, L"BUTTON", L"打开项目", WS_CHILD | WS_VISIBLE, 0, 0, 0, 0, hwnd, (HMENU)ID_BTN_OPEN, NULL, NULL);
            hTree = CreateWindowExW(WS_EX_CLIENTEDGE, WC_TREEVIEWW, L"", WS_CHILD | WS_VISIBLE | WS_BORDER | TVS_HASLINES | TVS_LINESATROOT | TVS_HASBUTTONS | TVS_CHECKBOXES | TVS_SHOWSELALWAYS, 0, 0, 0, 0, hwnd, (HMENU)ID_TREEVIEW, NULL, NULL);
            hEditor = CreateWindowExW(WS_EX_CLIENTEDGE, L"RICHEDIT50W", L"", WS_CHILD | WS_VISIBLE | WS_BORDER | WS_VSCROLL | WS_HSCROLL | ES_MULTILINE | ES_WANTRETURN | ES_AUTOVSCROLL | ES_READONLY, 0, 0, 0, 0, hwnd, (HMENU)ID_EDITOR, NULL, NULL);

            hLblDelay = CreateWindowExW(0, L"STATIC", L"开始延时(秒):", WS_CHILD | WS_VISIBLE, 0, 0, 0, 0, hwnd, NULL, NULL, NULL);
            hEditDelay = CreateWindowExW(WS_EX_CLIENTEDGE, L"EDIT", L"3", WS_CHILD | WS_VISIBLE | ES_NUMBER, 0, 0, 0, 0, hwnd, (HMENU)ID_EDIT_DELAY, NULL, NULL);

            hLblInterval = CreateWindowExW(0, L"STATIC", L"输入间隔(ms):", WS_CHILD | WS_VISIBLE, 0, 0, 0, 0, hwnd, NULL, NULL, NULL);
            hEditInterval = CreateWindowExW(WS_EX_CLIENTEDGE, L"EDIT", L"5", WS_CHILD | WS_VISIBLE | ES_NUMBER, 0, 0, 0, 0, hwnd, (HMENU)ID_EDIT_INTERVAL, NULL, NULL);

            hLblMode = CreateWindowExW(0, L"STATIC", L"输入模式:", WS_CHILD | WS_VISIBLE, 0, 0, 0, 0, hwnd, NULL, NULL, NULL);
            hComboMode = CreateWindowExW(0, WC_COMBOBOXW, L"", CBS_DROPDOWNLIST | WS_CHILD | WS_VISIBLE, 0, 0, 0, 0, hwnd, (HMENU)ID_COMBO_MODE, NULL, NULL);
            SendMessageW(hComboMode, CB_ADDSTRING, 0, (LPARAM)L"连续输入模式");
            SendMessageW(hComboMode, CB_ADDSTRING, 0, (LPARAM)L"Vim模式");
            SendMessageW(hComboMode, CB_SETCURSEL, 0, 0);

            hBtnStart  = CreateWindowExW(0, L"BUTTON", L"开始打字", WS_CHILD | WS_VISIBLE, 0, 0, 0, 0, hwnd, (HMENU)ID_BTN_START, NULL, NULL);
            hBtnPause  = CreateWindowExW(0, L"BUTTON", L"暂停[F8]", WS_CHILD | WS_VISIBLE, 0, 0, 0, 0, hwnd, (HMENU)ID_BTN_PAUSE, NULL, NULL);
            hBtnResume = CreateWindowExW(0, L"BUTTON", L"继续[F8]", WS_CHILD | WS_VISIBLE, 0, 0, 0, 0, hwnd, (HMENU)ID_BTN_RESUME, NULL, NULL);
            hBtnReset  = CreateWindowExW(0, L"BUTTON", L"重置[F9]", WS_CHILD | WS_VISIBLE, 0, 0, 0, 0, hwnd, (HMENU)ID_BTN_RESET, NULL, NULL);

            // hLblHotkey = CreateWindowExW(0, L"STATIC", L"✨ 强烈推荐：键盘按 [F8] 暂停/继续，[F9] 重置（能完美避免鼠标点击导致目标编辑器丢失焦点）", WS_CHILD | WS_VISIBLE, 0, 0, 0, 0, hwnd, NULL, NULL, NULL);

            CoInitializeEx(NULL, COINIT_APARTMENTTHREADED | COINIT_DISABLE_OLE1DDE);

            // 【新增】=========================================
            // 获取系统现代 UI 字体（支持 ClearType 平滑抗锯齿）
            NONCLIENTMETRICSW ncm = { sizeof(NONCLIENTMETRICSW) };
            SystemParametersInfoW(SPI_GETNONCLIENTMETRICS, sizeof(NONCLIENTMETRICSW), &ncm, 0);
            HFONT hFont = CreateFontIndirectW(&ncm.lfMessageFont);

            // 遍历并一键将现代字体应用到刚才创建的所有子控件上
            EnumChildWindows(hwnd, [](HWND child, LPARAM font) -> BOOL {
                SendMessageW(child, WM_SETFONT, font, TRUE);
                return TRUE;
            }, (LPARAM)hFont);

            // 【关键新增】绑定打字引擎状态到 UI 层的同步回调
            engine.SetCallbacks(
                    [hwnd](void* treeItem) {
                        PostMessageW(hwnd, WM_APP_FILE_START, (WPARAM)treeItem, 0);
                    },
                    [hwnd](size_t progress) {
                        PostMessageW(hwnd, WM_APP_PROGRESS, (WPARAM)progress, 0);
                    }
            );

            break;
        }

            // ================= 新增：处理引擎发来的后台消息 =================
        case WM_APP_FILE_START: {
            HTREEITEM hItem = (HTREEITEM)wParam;
            if (hItem) {
                // 1. 展开该文件所有的父目录节点 (满足需求：自动展开)
                TreeView_EnsureVisible(hTree, hItem);

                // 2. 选中该文件节点，使其高亮 (满足需求：醒目展示正在输入哪个文件)
                TreeView_SelectItem(hTree, hItem);

                // 3. 读取内容加载到右侧框准备追踪
                std::wstring path = GetItemFullPath(hTree, hItem);
                LoadFileToEditor(path);
            }
            break;
        }

        case WM_APP_PROGRESS: {
            long progress = (long)wParam;

            // 定义选中文本的背景颜色：护眼的浅蓝色 (SkyBlue)
            CHARFORMAT2W cf = {0};
            cf.cbSize = sizeof(cf);
            cf.dwMask = CFM_BACKCOLOR;
            cf.crBackColor = RGB(173, 216, 230);

            // 选中从 0 到 当前进度 的所有字符
            CHARRANGE cr = {0, progress};
            SendMessageW(hEditor, EM_EXSETSEL, 0, (LPARAM)&cr);

            // 应用高亮背景色
            SendMessageW(hEditor, EM_SETCHARFORMAT, SCF_SELECTION, (LPARAM)&cf);

            // 隐藏系统默认的选中反色效果，让底色露出来
            SendMessageW(hEditor, EM_HIDESELECTION, TRUE, 0);

            // 自动向下滚动，光标跟随
            SendMessageW(hEditor, EM_SCROLLCARET, 0, 0);
            break;
        }
            // ================================================================

        case WM_HOTKEY: {
            if (wParam == 1) {
                if (engine.GetState() == EngineState::TYPING) {
                    engine.Pause();
                } else if (engine.GetState() == EngineState::PAUSED) {
                    engine.Resume(
                            [](int num) { OsdWindow::ShowNumber(num); },
                            []() { OsdWindow::Hide(); }
                    );
                }
            } else if (wParam == 2) {
                if (engine.GetState() != EngineState::TYPING) engine.Reset();
            }
            break;
        }

        case WM_SIZE: {
            int width = LOWORD(lParam);
            int height = HIWORD(lParam);
            int pad = 10;

            int topH = 30;
            MoveWindow(hBtnOpen, pad, pad, 100, topH, TRUE);

            int bottomAreaH = 100;
            int midH = height - pad * 2 - topH - bottomAreaH;

            if (midH > 0) {
                int treeW = 250;
                int editorX = pad + treeW + pad;
                int editorW = width - editorX - pad;
                MoveWindow(hTree, pad, pad * 2 + topH, treeW, midH, TRUE);
                MoveWindow(hEditor, editorX, pad * 2 + topH, editorW, midH, TRUE);
            }

            int row1Y = height - bottomAreaH + 5;
            MoveWindow(hLblDelay, pad, row1Y + 3, 90, 20, TRUE);
            MoveWindow(hEditDelay, pad + 95, row1Y, 50, 25, TRUE);
            MoveWindow(hLblInterval, pad + 160, row1Y + 3, 100, 20, TRUE);
            MoveWindow(hEditInterval, pad + 265, row1Y, 50, 25, TRUE);
            MoveWindow(hLblMode, pad + 330, row1Y + 3, 70, 20, TRUE);
            MoveWindow(hComboMode, pad + 405, row1Y, 150, 100, TRUE);

            int row2Y = height - 60;
            MoveWindow(hBtnStart, pad, row2Y, 80, 30, TRUE);
            MoveWindow(hBtnPause, pad + 90, row2Y, 80, 30, TRUE);
            MoveWindow(hBtnResume, pad + 180, row2Y, 80, 30, TRUE);
            MoveWindow(hBtnReset, pad + 270, row2Y, 80, 30, TRUE);

            MoveWindow(hLblHotkey, pad, row2Y + 35, width - pad * 2, 20, TRUE);
            break;
        }

        case WM_APP + 1: {
            HTREEITEM hItem = (HTREEITEM)wParam;
            BOOL fCheck = TreeView_GetCheckState(hTree, hItem);
            CheckAllChildren(hTree, hItem, fCheck);
            break;
        }

        case WM_COMMAND: {
            if (HIWORD(wParam) == EN_CHANGE && LOWORD(wParam) == ID_EDIT_INTERVAL) {
                wchar_t buf[32];
                GetWindowTextW(hEditInterval, buf, 32);
                int interval = _wtoi(buf);
                if (interval > 0) engine.UpdateInterval(interval);
            }

            if (LOWORD(wParam) == ID_BTN_OPEN) OpenProjectFolder(hwnd);
            else if (LOWORD(wParam) == ID_BTN_START) {
                std::vector<FileTask> tasks;
                HTREEITEM hRoot = TreeView_GetRoot(hTree);
                CollectCheckedTasks(hTree, hRoot, tasks);

                if (tasks.empty()) {
                    MessageBoxW(hwnd, L"请先在左侧勾选至少一个文件！", L"错误", MB_ICONWARNING);
                    return 0;
                }

                wchar_t buf[32];
                GetWindowTextW(hEditDelay, buf, 32);
                int delay = _wtoi(buf);
                if (delay <= 0) delay = 3;

                GetWindowTextW(hEditInterval, buf, 32);
                int interval = _wtoi(buf);
                if (interval <= 0) interval = 50;

                int modeIdx = SendMessageW(hComboMode, CB_GETCURSEL, 0, 0);
                InputMode mode = (modeIdx == 1) ? InputMode::VIM : InputMode::CONTINUOUS;

                engine.SetTasks(tasks);
                engine.SetConfig(delay, interval, mode);

                engine.Start(
                        [](int num) { OsdWindow::ShowNumber(num); },
                        []() { OsdWindow::Hide(); }
                );
            }
            else if (LOWORD(wParam) == ID_BTN_PAUSE) engine.Pause();
            else if (LOWORD(wParam) == ID_BTN_RESUME) {
                engine.Resume(
                        [](int num) { OsdWindow::ShowNumber(num); },
                        []() { OsdWindow::Hide(); }
                );
            }
            else if (LOWORD(wParam) == ID_BTN_RESET) {
                if (engine.GetState() != EngineState::TYPING) engine.Reset();
            }
            break;
        }

        case WM_NOTIFY: {
            LPNMHDR lpnmh = (LPNMHDR)lParam;
            if (lpnmh->idFrom == ID_TREEVIEW) {

                // ================= 新增：自定义绘制，强制醒目高亮 =================
                if (lpnmh->code == NM_CUSTOMDRAW) {
                    LPNMTVCUSTOMDRAW pTVCD = (LPNMTVCUSTOMDRAW)lParam;
                    if (pTVCD->nmcd.dwDrawStage == CDDS_PREPAINT) {
                        return CDRF_NOTIFYITEMDRAW; // 告诉系统我们要干预项的绘制
                    }
                    else if (pTVCD->nmcd.dwDrawStage == CDDS_ITEMPREPAINT) {
                        // 如果该节点当前是被选中的状态
                        if (pTVCD->nmcd.uItemState & CDIS_SELECTED) {
                            pTVCD->clrText = RGB(255, 255, 255); // 纯白文字
                            pTVCD->clrTextBk = RGB(0, 102, 204); // 极度醒目的深蓝色背景

                            // 极其关键：清除系统的选中标记，强制系统使用我们指定的颜色，不让它变灰
                            pTVCD->nmcd.uItemState &= ~CDIS_SELECTED;
                        }
                        return CDRF_NEWFONT;
                    }
                    return CDRF_DODEFAULT;
                }
                // ==================================================================

                // 下面是原有的选中加载与复选框勾选逻辑
                LPNMTREEVIEWW pnmTV = (LPNMTREEVIEWW)lParam;
                if (pnmTV->hdr.code == TVN_SELCHANGEDW) {
                    if (pnmTV->itemNew.lParam == 1) {
                        std::wstring path = GetItemFullPath(hTree, pnmTV->itemNew.hItem);
                        LoadFileToEditor(path);
                    }
                }
                else if (pnmTV->hdr.code == NM_CLICK) {
                    DWORD pos = GetMessagePos();
                    POINT pt = { GET_X_LPARAM(pos), GET_Y_LPARAM(pos) };
                    ScreenToClient(hTree, &pt);
                    TVHITTESTINFO ht = {0};
                    ht.pt = pt;
                    TreeView_HitTest(hTree, &ht);
                    if (ht.flags & TVHT_ONITEMSTATEICON) {
                        PostMessageW(hwnd, WM_APP + 1, (WPARAM)ht.hItem, 0);
                    }
                }
            }
            break;
        }

        case WM_DESTROY:
            UnregisterHotKey(hwnd, 1);
            UnregisterHotKey(hwnd, 2);
            CoUninitialize();
            PostQuitMessage(0);
            break;
        default:
            return DefWindowProcW(hwnd, msg, wParam, lParam);
    }
    return 0;
}

int WINAPI WinMain(HINSTANCE hInstance, HINSTANCE hPrevInstance, LPSTR lpCmdLine, int nCmdShow) {
    SetProcessDPIAware();
    INITCOMMONCONTROLSEX icex;
    icex.dwSize = sizeof(INITCOMMONCONTROLSEX);
    icex.dwICC = ICC_TREEVIEW_CLASSES | ICC_STANDARD_CLASSES;
    InitCommonControlsEx(&icex);

    OsdWindow::Init(hInstance);

    WNDCLASSEXW wc = { sizeof(WNDCLASSEXW) };
    wc.lpfnWndProc = WndProc;
    wc.hInstance = hInstance;
    wc.hCursor = LoadCursor(NULL, IDC_ARROW);
    wc.hbrBackground = (HBRUSH)(COLOR_WINDOW + 1);
    wc.lpszClassName = L"AutoCoderClass";
    RegisterClassExW(&wc);

    HWND hwnd = CreateWindowExW(
            WS_EX_TOPMOST,
            L"AutoCoderClass", L"AutoCoder - 代码自动敲击工具",
            WS_OVERLAPPEDWINDOW | WS_CLIPCHILDREN,
            CW_USEDEFAULT, CW_USEDEFAULT, 900, 650,
            NULL, NULL, hInstance, NULL);

    ShowWindow(hwnd, nCmdShow);
    UpdateWindow(hwnd);

    MSG msg;
    while (GetMessage(&msg, NULL, 0, 0)) {
        TranslateMessage(&msg);
        DispatchMessage(&msg);
    }
    return (int)msg.wParam;
}