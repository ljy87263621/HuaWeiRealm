# 第三方代码与资源

本仓库保留了运行和构建所依赖的第三方文件及其原有许可。下列说明不替代各组件目录内的许可、版权及资源归属信息。

| 组件 | 位置 | 来源与许可 |
| --- | --- | --- |
| LVGL | `Client/third_party/lvgl/` | [LVGL](https://github.com/lvgl/lvgl)，MIT，见 `LICENCE.txt`；随附子库、字体及示例资源适用其目录内的声明 |
| SDL2 开发依赖 | `Client/third_party/SDL2-2.28.5/` | [SDL](https://www.libsdl.org/)，zlib 风格许可，见 `COPYING.txt`；Linux 构建通过系统 SDL2 开发包链接 |
| Twemoji 表情 | `Server/Emojis/` | [Twemoji](https://twemoji.twitter.com/)，CC BY 4.0，见 `Server/EMOJI_ATTRIBUTION.txt` |
| SquareLine 导出界面 | `Client/mycode/squareline_ui/` | 保留导出文件原有头部声明；导出代码不代表包含 SquareLine Studio 编辑器 |

客户端背景、生成字体及其他应用资源按原工程保留。本次整理未为其新增授权声明，也未为项目自有代码指定统一开源许可证。公开仓库本身不替代各文件的授权条款。
