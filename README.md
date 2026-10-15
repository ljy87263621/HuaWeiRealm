# HuaWeiRealm · Embedded ChatRoom

基于 C99、LVGL 和 POSIX socket 的局域网聊天原型，包含 Linux 服务端、SDL2 桌面客户端及 AArch64 Linux 触摸屏客户端。主流程包括注册登录、在线用户、私聊、文本历史、表情同步和小文件传输。

项目面向课程实践和局域网演示；名称不表示使用 HarmonyOS 或华为云服务。

## 目录

```text
Client/                  客户端入口、业务模块、界面、资源和测试
  mycode/                网络、传输、前端与 SquareLine 导出界面
  Assets/                登录背景等运行资源
  third_party/lvgl/      LVGL 源码与许可
  third_party/SDL2-2.28.5/ SDL2 开发依赖与许可
Server/                  服务端、表情资源、测试及 Windows LAN 转发工具
cmake/toolchains/        AArch64 交叉编译配置
tools/test.sh            基础回归测试入口
docs/STATUS.md           能力边界、审计结果和已知问题
THIRD_PARTY_NOTICES.md   第三方来源及许可说明
```

本仓库从已整理的 ChatRoom 源码包迁入，不包含账户数据、聊天记录、附件、日志、历史构建产物或本机编辑器设置。后续开发以本仓库为统一版本管理入口。

## 技术栈

| 部分 | 技术 |
| --- | --- |
| 客户端 | C99、LVGL、SquareLine 导出 C、POSIX socket/pthread |
| 桌面显示 | SDL2，窗口尺寸 1024 × 600 |
| 板端显示与触摸 | Linux framebuffer `/dev/fb0`、evdev `/dev/input/event6` |
| 服务端 | C99、TCP、select、pthread |
| 存储 | 本地文本账户、会话历史、附件和日志文件 |
| 附件 | 客户端 SHA-256 指纹、服务端缓存，单文件上限 8 MiB |

实际随附 LVGL 版本由 `Client/third_party/lvgl/lv_version.h` 标记为 **9.2.3 dev**。界面导出文件标注 SquareLine Studio 1.6.2 / LVGL 9.5，两者存在版本口径差异，当前组合已通过构建。

## Linux / WSL 构建

Ubuntu / Debian 示例依赖：

```sh
sudo apt update
sudo apt install build-essential cmake libsdl2-dev python3
```

### 服务端

```sh
gcc -std=c99 -Wall -Wextra -pthread Server/server_gui.c -o Server/chat_server
cd Server
./chat_server
```

服务端监听 TCP `8888`。从 `Server/` 启动以正确读取 `Emojis/`；首次运行会创建账户及运行数据目录。空账户文件会生成默认账户 `admin` / `admin`，仅用于本地演示，运行数据不应提交到仓库。

### SDL2 桌面客户端

在仓库根目录构建：

```sh
cmake -S Client -B build/desktop -DCMAKE_BUILD_TYPE=Release
cmake --build build/desktop --target main --parallel
cd Client
../build/desktop/bin/main
```

客户端必须以 `Client/` 为工作目录，才能读取 `Assets/login_background.png`。登录页填写服务端 IPv4 地址；同一 Linux/WSL 环境可用 `127.0.0.1`。

目前可靠构建路径为 Linux/WSL 与 AArch64 Linux。项目保留 SDL2 的 Windows 开发文件，但业务代码依赖 POSIX API，尚未验收 Windows 原生发行版。

### AArch64 触摸板客户端

```sh
sudo apt install gcc-aarch64-linux-gnu g++-aarch64-linux-gnu
cmake -S Client -B build/aarch64 \
  -DCMAKE_BUILD_TYPE=Release \
  -DCHAT_BOARD_FBDEV=ON \
  -DCMAKE_TOOLCHAIN_FILE="$PWD/cmake/toolchains/aarch64-linux-gnu.cmake"
cmake --build build/aarch64 --target main --parallel
```

将 `build/aarch64/bin/main` 与 `Client/Assets/` 放入板端同一运行目录，然后在该目录运行 `./main`。目标 Linux 需要兼容的动态链接器与 C 运行库，以及 framebuffer/输入设备访问权限。当前代码固定使用 `/dev/input/event6`，迁移板卡前需核对输入节点；本仓库尚未提供通用板端安装包。

### 板端访问 Windows 上的 WSL 服务端

如果 WSL localhost 转发已启用，但 LAN 设备无法连接，可在 Windows 的管理员 PowerShell 中执行：

```powershell
.\Server\tools\Enable-ChatLanAccess.ps1 `
  -ListenAddress '<Windows的局域网IPv4地址>' `
  -AllowedRemoteAddress '<板端IPv4地址或允许的CIDR网段>'
```

先启动服务端并确认 Windows 可连接 `127.0.0.1:8888`。脚本会创建指定地址的 TCP 端口转发和 Private 网络防火墙规则；登录页填写 Windows 的局域网地址。脚本不处理其他 WSL 网络模式，也不会自动发现板端地址。

## 测试

在 Linux/WSL 中，从仓库根目录运行：

```sh
sh tools/test.sh
```

入口执行客户端 Python 源码回归，以及服务端辅助函数、协议解析、SHA-256、流式接收、上传通知和认证超时测试。测试运行数据写入系统临时目录，不使用真实账户或服务端数据；超时测试使用本机 TCP `18888`，应保证该端口空闲。

`test_chat_network_io.c` 需要独立准备的服务端或 `protocol_stub.py`，使用测试账户 `alice/secret`，不包含在上述基础测试入口中。真实板端触摸、显示、双端媒体操作及长时间稳定性仍需人工验收。

## 当前边界

- 支持单服务端、局域网、最多 128 个本地账户；容量常量不代表并发性能承诺。
- TCP 为完成的业务通道；UDP 仅有端口登记与接收预留，没有完整发送链路。
- 设置页语言、主题、退出登录和退出程序有实际逻辑；分类标签及通知开关仍是界面占位。
- 暂无群组、离线消息投递、消息撤回、可靠投递确认、TLS、账号管理后台或自动升级。
- 密码明文存储，空配置含默认演示账户；附件内容未在服务端重新计算 SHA-256。当前版本不适合不可信网络。

详见 [项目状态与审计](docs/STATUS.md)。

## 许可与资源

第三方代码和表情继续遵循各自许可，详见 [第三方说明](THIRD_PARTY_NOTICES.md)。本次整理未替项目自有代码另行指定开源许可证。
