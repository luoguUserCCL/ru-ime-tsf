# RuIME TSF

一个基于 Windows TSF (Text Services Framework) 的俄语输入法，采用 ЙЦУКЕН 键位布局，并提供 x86 / x64 两个 DLL 版本。

它不是传统钩子型输入法，而是通过 COM / TSF 注册为系统输入法服务，由应用程序进程自身加载，适用于大部分支持 TSF 的 Windows 应用程序。

## 功能特点

- 支持俄语 ЙЦУКЕН 键盘布局
- 以 Windows TSF 方式接入系统输入法列表
- 可通过 Scroll Lock 键切换 俄语 / 英文
- 支持 x86 和 x64 双架构编译
- 默认以俄语模式启用
- 允许在 Windows 7 及以上版本中使用
- 适用于大部分现代 Windows 应用程序与编辑器

## 项目结构

```text
.
├── build.bat          # 编译脚本：生成 x64/x86 DLL
├── install.bat        # 安装脚本：注册 DLL 到系统
├── src/
│   ├── ruime_tsf.cpp # 输入法核心实现
│   ├── RuIME_x64.def # x64 DLL 导出定义
│   └── RuIME_x86.def # x86 DLL 导出定义
├── res/
│   ├── app.ico        # 程序图标
│   └── ruime_tsf.rc   # Windows 资源文件
└── README.md          # 项目说明
```

## 编译要求

在 Windows 环境下，需要先安装 MinGW-w64 工具链，并确认以下命令可用：

```bat
x86_64-w64-mingw32-g++
i686-w64-mingw32-g++
windres
```

如果未安装对应工具链，编译脚本会自动跳过相应平台的编译。

## 编译方法

在仓库根目录执行：

```bat
build.bat
```

脚本会依次：

1. 编译资源文件
2. 编译 `RuIME_x64.dll`
3. 编译 `RuIME_x86.dll`

编译完成后，若无错误，将在项目根目录生成：

- `RuIME_x64.dll`
- `RuIME_x86.dll`

## 安装方法

以管理员身份运行：

```bat
install.bat
```

脚本会：

- 复制 DLL 到 `C:\Program Files\RuIME` 或 `C:\Program Files (x86)\RuIME`
- 通过 `regsvr32.exe` 注册输入法组件
- 将其注册为 TSF 输入法服务

安装成功后，Windows 输入法列表中会出现对应的输入法项，通常显示为：

- `RuIME JCUKEN`

## 使用方法

- 默认启用俄语模式
- 按 `Scroll Lock` 可以在 俄语 / 英文 两种状态间切换
- 键位遵循 ЙЦУКЕН 布局映射规则
- 组合键（如 Ctrl / Alt / Win）会被放行，不干扰系统快捷键

## 注意事项

- 需要以管理员权限安装和注册 DLL
- 该项目依赖 COM / TSF 机制，因此需要在 Windows 系统中使用
- 编译和注册时，应确认系统中已有相应的 MINIGW / Windows SDK 环境
- 若重新更改 CLSID、语言配置或注册项，系统中可能存在重复/冲突的输入法配置

## 卸载方法

如果需要卸载，可在管理员权限的命令提示符中执行：

```bat
regsvr32 /u "C:\Program Files\RuIME\RuIME_x64.dll"
regsvr32 /u "C:\Program Files (x86)\RuIME\RuIME_x86.dll"
```

或者使用项目中注册逻辑对应的反注册方式。

## 说明

该项目是一个较小的 Windows 输入法原型，适合：

- 学习 TSF 输入法架构
- 研究 COM 组件注册流程
- 自定义俄语键盘映射
- 在 Windows 上实现自己的输入法服务

## 许可证

当前仓库中未提供显式许可证声明，使用前请根据实际需求自行确认授权方式。

## 相关说明

项目中的核心逻辑位于：

- `src/ruime_tsf.cpp`

其中包含：

- TSF 输入法注册逻辑
- 键盘事件拦截与字符转换
- 俄语/英文状态共享
- Scroll Lock 切换键支持
- COM DLL 导出函数

如果你希望，我还可以进一步为这个项目补一份更偏“GitHub 风格”的英文 README，或者增加一份中文使用截图说明和常见问题排查章节。
