#pragma once
#include <windows.h>
#include <string>
#include <vector>
#include <atomic>
#include <thread>
#include <functional>

enum class InputMode { CONTINUOUS, VIM };
enum class EngineState { IDLE, COUNTDOWN, TYPING, PAUSED };

struct FileTask {
    std::string filePath;
    std::string content;
    void* treeItem; // 【新增】保存目录树的节点句柄，用于自动选中和展开
};

class TypingEngine {
public:
    TypingEngine();
    ~TypingEngine();

    void SetTasks(const std::vector<FileTask>& tasks);
    void SetConfig(int startDelaySec, int intervalMs, InputMode mode);
    void UpdateInterval(int ms);

    // 【新增】设置 UI 联动回调函数
    using FileStartCallback = std::function<void(void*)>;
    using ProgressCallback = std::function<void(size_t)>;
    void SetCallbacks(FileStartCallback onFile, ProgressCallback onProg);

    void Start(std::function<void(int)> onCountdown, std::function<void()> onTypingStart);
    void Resume(std::function<void(int)> onCountdown, std::function<void()> onTypingStart);
    void Pause();
    void Reset();

    EngineState GetState() const { return state; }

private:
    void WorkerThread(std::function<void(int)> onCountdown, std::function<void()> onTypingStart);

    // 【新增】reportProgress 参数，只在打核心代码时高亮，打 vim 命令时不乱高亮
    void TypeString(const std::string& str, bool reportProgress = false);

    void SendVKey(WORD vkey);
    void ExecuteVimCommand(const std::string& filepath, const std::string& content);
    void ExecuteContinuousCommand(const std::string& filepath, const std::string& content);

    std::vector<FileTask> tasks;
    int startDelaySec = 3;
    std::atomic<int> intervalMs{50};
    InputMode mode = InputMode::CONTINUOUS;

    std::atomic<EngineState> state;
    std::thread worker;

    size_t currentFileIndex = 0;

    // 回调函数存储
    FileStartCallback onFileStartCb;
    ProgressCallback onProgressCb;
};