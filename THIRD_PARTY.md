# 第三方依赖

直接源码依赖固定在 `cmake/Dependencies.cmake`，由 FetchContent 获取，未修改上游源文件。

| 依赖 | 版本/提交 | 许可与来源 |
|---|---|---|
| Cordis C++ | 8eba297c535894ebe9b924ecb5e1046c0963d960 | [README 声明 MIT](https://github.com/luoyebai/cordis-cpp/tree/8eba297c535894ebe9b924ecb5e1046c0963d960)，该版本未包含独立 LICENSE 文件 |
| raylib | 5.5 / c1ab645ca298a2801097931d1079b10ff7eb9df8 | [zlib](https://github.com/raysan5/raylib/blob/5.5/LICENSE) |
| raygui | 4.0 / 25c8c65a6e5f0f4d4b564a0343861898c6f2778b | [zlib](https://github.com/raysan5/raygui/blob/4.0/LICENSE) |
| nlohmann/json | 3.12.0 / 55f93686c01528224f448c19128836e7df245f72 | [MIT](https://github.com/nlohmann/json/blob/v3.12.0/LICENSE.MIT) |

raylib 自带 GLFW、glad、stb 图片/字体代码、miniaudio 及其他格式解码器，桌面构建使用其内置 GLFW。
这些间接组件随固定 raylib 源码一同获取，各自声明位于 `src/external` 的文件头或 LICENSE；
发行包应保留所用组件的声明。本仓库 `licenses` 保存直接依赖已有的许可原文及 GLFW 许可。
不需要额外安装 SDL、Qt、Boost、ECS、脚本引擎或状态机库。

CMake、Git、编译器、Ninja（预设使用）属于开发工具，CTest 来自 CMake。
GitHub Actions 的 actions/checkout@v4 仅用于 CI。
二次元角色资源独立记录 artist/license，当前接入后藤一里官方图片，来源与权利说明见 assets/SOURCES.md。

中文字体使用 Noto Sans CJK SC Regular（SIL OFL 1.1），许可原文见 licenses/NotoSansSC-OFL.txt。
