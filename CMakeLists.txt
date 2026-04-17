cmake_minimum_required(VERSION 3.20)
project(AutoCoder)

set(CMAKE_CXX_STANDARD 17)

# 引入需要的 Windows 核心库
link_libraries(user32 gdi32 gdiplus comctl32 shell32 ole32)

add_executable(AutoCoder WIN32
        main.cpp
        TypingEngine.cpp
        TypingEngine.h
        OsdWindow.cpp
        OsdWindow.h
        # 已经去掉了 app.rc 和所有 RC 相关的 flag
        )