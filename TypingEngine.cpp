#include "TypingEngine.h"

TypingEngine::TypingEngine() : state(EngineState::IDLE) {}

TypingEngine::~TypingEngine() {
    Reset();
    if (worker.joinable()) worker.join();
}

void TypingEngine::SetTasks(const std::vector<FileTask>& t) { tasks = t; }
void TypingEngine::SetConfig(int delay, int interval, InputMode m) { startDelaySec = delay; intervalMs = interval; mode = m; }
void TypingEngine::UpdateInterval(int ms) { intervalMs = ms; }

// 绑定回调
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

void TypingEngine::SendVKey(WORD vkey) {
    INPUT inputs[2] = {};
    inputs[0].type = INPUT_KEYBOARD; inputs[0].ki.wVk = vkey;
    inputs[1].type = INPUT_KEYBOARD; inputs[1].ki.wVk = vkey; inputs[1].ki.dwFlags = KEYEVENTF_KEYUP;
    SendInput(2, inputs, sizeof(INPUT));
    Sleep(intervalMs);
}

// 核心输入逻辑，新增 reportProgress 支持
void TypingEngine::TypeString(const std::string& str, bool reportProgress) {
    int len = MultiByteToWideChar(CP_UTF8, 0, str.c_str(), -1, NULL, 0);
    if (len <= 1) return;

    std::wstring wstr(len - 1, 0);
    MultiByteToWideChar(CP_UTF8, 0, str.c_str(), -1, &wstr[0], len - 1);

    long richEditOffset = 0; // 精确计算 UI 编辑器的选中偏移量

    for (size_t i = 0; i < wstr.length(); ++i) {
        while (state == EngineState::PAUSED || state == EngineState::COUNTDOWN) Sleep(100);
        if (state == EngineState::IDLE) return;

        // 触发进度回调 (传给UI前台高亮)
        if (reportProgress && onProgressCb) onProgressCb(richEditOffset);

        wchar_t c = wstr[i];

        if (c == L'\r') {
            continue; // 跳过 \r，避免 UI 进度计算错位
        } else if (c == L'\n') {
            SendVKey(VK_RETURN);
            richEditOffset++; // 富文本框把换行算作 1 个字符
        } else if (c == L'\t') {
            SendVKey(VK_TAB);
            richEditOffset++;
        } else if (c == L' ') {
            SendVKey(VK_SPACE);
            richEditOffset++;
        } else {
            INPUT inputs[2] = {};
            inputs[0].type = INPUT_KEYBOARD; inputs[0].ki.wVk = 0;
            inputs[0].ki.wScan = c; inputs[0].ki.dwFlags = KEYEVENTF_UNICODE;
            inputs[1] = inputs[0]; inputs[1].ki.dwFlags |= KEYEVENTF_KEYUP;
            SendInput(2, inputs, sizeof(INPUT));
            richEditOffset++;
        }
        Sleep(intervalMs);
    }
    // 扫尾更新最后一次进度
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

        // 【关键触发】：通知前台 UI 切换了新文件，自动展开树节点并加载代码
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
    TypeString(content, true); // true: 只有正文代码触发高亮
    if (state != EngineState::IDLE) TypeString("\n");
}

void TypingEngine::ExecuteVimCommand(const std::string& filepath, const std::string& content) {
    size_t lastSlash = filepath.find_last_of("/\\");
    std::string dir = (lastSlash != std::string::npos) ? filepath.substr(0, lastSlash) : ".";
    TypeString("mkdir -p " + dir + "\n");
    Sleep(200);
    TypeString("vim " + filepath + "\n");
    Sleep(500);
    SendVKey(VK_ESCAPE);
    TypeString(":%d\n");
    TypeString(":set paste\n");
    TypeString("i");

    TypeString(content, true); // true: 只有正文代码触发高亮

    if (state != EngineState::IDLE) {
        SendVKey(VK_ESCAPE);
        TypeString(":wq\n");
    }
}