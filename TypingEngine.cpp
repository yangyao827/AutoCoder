#include "TypingEngine.h"

TypingEngine::TypingEngine() : state(EngineState::IDLE) {}

TypingEngine::~TypingEngine() {
    Reset();
    if (worker.joinable()) worker.join();
}

void TypingEngine::SetTasks(const std::vector<FileTask>& t) { tasks = t; }
void TypingEngine::SetConfig(int delay, int interval, InputMode m) { startDelaySec = delay; intervalMs = interval; mode = m; }
void TypingEngine::UpdateInterval(int ms) { intervalMs = ms; }

void TypingEngine::SetCallbacks(FileStartCallback onFile, ProgressCallback onProg) {
    onFileStartCb = onFile;
    onProgressCb = onProg;
}

void TypingEngine::Start(std::function<void(int)> onCountdown, std::function<void()> onTypingStart) {
    if (state == EngineState::IDLE) {
        state = EngineState::COUNTDOWN;
        if (worker.joinable()) worker.join();
        currentFileIndex = 0;
        worker = std::thread(&TypingEngine::WorkerThread, this, onCountdown, onTypingStart);
    }
}

void TypingEngine::Pause() { if (state == EngineState::TYPING) state = EngineState::PAUSED; }

void TypingEngine::Resume(std::function<void(int)> onCountdown, std::function<void()> onTypingStart) {
    if (state == EngineState::PAUSED) {
        state = EngineState::COUNTDOWN;
        std::thread([this, onCountdown, onTypingStart]() {
            for (int i = 3; i > 0; --i) {
                if (state == EngineState::IDLE) return;
                if (onCountdown) onCountdown(i);
                Sleep(1000);
            }
            if (state != EngineState::IDLE) {
                state = EngineState::TYPING;
                if (onTypingStart) onTypingStart();
            }
        }).detach();
    }
}

void TypingEngine::Reset() { state = EngineState::IDLE; }

// 【关键修复 1】：将按下的动作和抬起的动作彻底分离为两次投递，防止终端漏键
void TypingEngine::SendVKey(WORD vkey, int sleepTime) {
    INPUT input = {};
    input.type = INPUT_KEYBOARD;
    input.ki.wVk = vkey;
    SendInput(1, &input, sizeof(INPUT)); // 按下

    input.ki.dwFlags = KEYEVENTF_KEYUP;
    SendInput(1, &input, sizeof(INPUT)); // 抬起

    Sleep(sleepTime);
}

// 【关键修复 2】：为字符串输入提供强制降速保护，并且同样分离按下和抬起
void TypingEngine::TypeString(const std::string& str, bool reportProgress, int forceInterval) {
    int len = MultiByteToWideChar(CP_UTF8, 0, str.c_str(), -1, NULL, 0);
    if (len <= 1) return;

    std::wstring wstr(len - 1, 0);
    MultiByteToWideChar(CP_UTF8, 0, str.c_str(), -1, &wstr[0], len - 1);

    long richEditOffset = 0;

    for (size_t i = 0; i < wstr.length(); ++i) {
        while (state == EngineState::PAUSED || state == EngineState::COUNTDOWN) Sleep(100);
        if (state == EngineState::IDLE) return;

        if (reportProgress && onProgressCb) onProgressCb(richEditOffset);

        // 如果 forceInterval 有值，则使用强制慢速；否则使用用户设置的极限速度
        int currentInterval = (forceInterval > 0) ? forceInterval : intervalMs.load();

        wchar_t c = wstr[i];

        if (c == L'\r') {
            continue;
        } else if (c == L'\n') {
            SendVKey(VK_RETURN, currentInterval);
            richEditOffset++;
        } else if (c == L'\t') {
            SendVKey(VK_TAB, currentInterval);
            richEditOffset++;
        } else if (c == L' ') {
            SendVKey(VK_SPACE, currentInterval);
            richEditOffset++;
        } else {
            // UNICODE 输入也要分离按下和抬起动作
            INPUT input = {};
            input.type = INPUT_KEYBOARD;
            input.ki.wVk = 0;
            input.ki.wScan = c;
            input.ki.dwFlags = KEYEVENTF_UNICODE;
            SendInput(1, &input, sizeof(INPUT)); // 按下

            input.ki.dwFlags = KEYEVENTF_UNICODE | KEYEVENTF_KEYUP;
            SendInput(1, &input, sizeof(INPUT)); // 抬起

            richEditOffset++;
        }
        Sleep(currentInterval);
    }
    if (reportProgress && onProgressCb) onProgressCb(richEditOffset);
}

void TypingEngine::WorkerThread(std::function<void(int)> onCountdown, std::function<void()> onTypingStart) {
    for (int i = startDelaySec; i > 0; --i) {
        if (state == EngineState::IDLE) return;
        if (onCountdown) onCountdown(i);
        Sleep(1000);
    }

    state = EngineState::TYPING;
    if (onTypingStart) onTypingStart();

    for (; currentFileIndex < tasks.size(); ++currentFileIndex) {
        while (state == EngineState::PAUSED || state == EngineState::COUNTDOWN) Sleep(100);
        if (state == EngineState::IDLE) return;

        if (onFileStartCb) onFileStartCb(tasks[currentFileIndex].treeItem);

        if (mode == InputMode::VIM) {
            ExecuteVimCommand(tasks[currentFileIndex].filePath, tasks[currentFileIndex].content);
        } else {
            ExecuteContinuousCommand(tasks[currentFileIndex].filePath, tasks[currentFileIndex].content);
        }
    }
    state = EngineState::IDLE;
}

void TypingEngine::ExecuteContinuousCommand(const std::string& filepath, const std::string& content) {
    if (currentFileIndex > 0) {
        TypeString("\n\n");
        TypeString(std::string(100, '-') + "\n");
    }
    TypeString("// " + filepath + "\n");
    TypeString(content, true, -1);
    if (state != EngineState::IDLE) TypeString("\n");
}

void TypingEngine::ExecuteVimCommand(const std::string& filepath, const std::string& content) {
    size_t lastSlash = filepath.find_last_of("/\\");
    std::string dir = (lastSlash != std::string::npos) ? filepath.substr(0, lastSlash) : ".";

    // 【关键修复 3】：不管用户把速度设得多快，终端前置命令强制固定 20ms 的保守速度
    TypeString("mkdir -p " + dir + "\n", false, 20);

    // 增加创建目录的等待时间，Windows 上执行 mkdir 非常慢
    Sleep(800);

    TypeString("vim " + filepath + "\n", false, 20);

    // 增加 vim 的加载等待时间
    Sleep(1000);

    SendVKey(VK_ESCAPE, 20);
    TypeString(":%d\n", false, 20);
    TypeString(":set paste\n", false, 20);
    TypeString("i", false, 20);

    // ==========================================
    // 命令写完，真正敲击代码正文时，解除速度封印！
    // 使用 -1 代表恢复用户在界面上填写的极速（如 5ms）
    TypeString(content, true, -1);
    // ==========================================

    if (state != EngineState::IDLE) {
        SendVKey(VK_ESCAPE, 20);
        TypeString(":wq\n", false, 20);
        Sleep(500); // 退出 vim 后稍微等一下，再处理下一个文件
    }
}