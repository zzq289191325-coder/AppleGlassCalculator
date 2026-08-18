# Apple Glass Calculator

一款为 Windows 10/11 打造的 Apple / macOS 风格毛玻璃计算器。

![Apple Glass Calculator 运行截图](assets/screenshot.png)

## 项目简介

这是一个使用 C# + WPF 开发的 Windows 桌面计算器，采用 Apple / macOS 风格的半透明毛玻璃 UI，支持基础计算、历史记录、键盘输入和 Windows 高 DPI，并可发布为无需额外安装运行环境的独立 EXE。

项目强调轻盈、克制的桌面体验：冷灰蓝玻璃表面与暖橙色运算键形成清晰层级，圆角外壳、渐变高光和细描边共同营造玻璃折射感。窗口使用透明圆角区域，四角不会出现多余的矩形底色。

## UI 风格

- Apple / macOS 风格的圆角窗口与交通灯控制按钮
- 冷蓝左上高光、暖橙右下光晕的玻璃渐变
- 半透明面板、折射描边和轻量阴影
- 红、橙、绿窗口按钮依次用于关闭、最小化和拖动
- 自适应高 DPI 显示，适合 Windows 10/11 桌面
- 专属透明圆角计算器图标

## 功能介绍

- 加、减、乘、除基础运算
- 小数输入、连续运算和重复等号运算
- 退格、清除当前输入、全部清除
- 最多保留 50 条本地历史记录
- 横向历史列表、鼠标滚轮和可拖动滚动条
- 数字键盘与主键盘快捷键
- 可拖动、最小化、关闭及双击标题区域最大化
- 历史记录自动保存到 `%LocalAppData%\AppleGlassCalculator\history.json`

## Windows 系统要求

### GitHub Release 独立版

- Windows 10 或 Windows 11，64 位
- 无需预装 .NET 运行环境

### 本地轻量版

- Windows 10 或 Windows 11，64 位
- [.NET 8 Desktop Runtime](https://dotnet.microsoft.com/download/dotnet/8.0)

## 使用方法

1. 从 GitHub Releases 下载 `Calculator.exe`。
2. 双击运行，无需安装。
3. 使用界面按钮或键盘完成计算。

常用键盘操作：

| 功能 | 按键 |
| --- | --- |
| 数字与小数点 | `0-9`、`.` |
| 运算 | `+`、`-`、`*`、`/` |
| 计算结果 | `Enter` |
| 退格 | `Backspace` |
| 清除当前输入 | `Delete` |
| 全部清除 | `Esc` |

## 开发技术栈

- C# 12
- .NET 8
- WPF / XAML
- Windows 原生窗口消息与高 DPI 支持
- `System.Text.Json` 本地历史记录持久化

## 本地编译

安装 [.NET 8 SDK](https://dotnet.microsoft.com/download/dotnet/8.0)，在仓库根目录执行：

```powershell
dotnet restore
dotnet build
dotnet build -c Release
```

## Release 发布

发布无需额外运行环境的 `win-x64`、Self-contained、Single-file EXE：

```powershell
dotnet publish src/AppleGlassCalculator/Calculator.csproj `
  -c Release `
  -p:PublishProfile=Standalone `
  -o artifacts/win-x64
```

输出文件为：

```text
artifacts/win-x64/Calculator.exe
```

默认项目配置也可生成约数百 KB 的轻量单文件版本，但目标电脑必须已经安装 .NET 8 Desktop Runtime。

## 项目目录结构

```text
AppleGlassCalculator/
├─ assets/
│  └─ screenshot.png
├─ src/
│  └─ AppleGlassCalculator/
│     ├─ Helpers/
│     ├─ Models/
│     ├─ Properties/PublishProfiles/
│     ├─ Resources/
│     ├─ Services/
│     ├─ App.xaml
│     ├─ MainWindow.xaml
│     └─ Calculator.csproj
├─ .gitignore
├─ AppleGlassCalculator.sln
├─ LICENSE
└─ README.md
```

## License

本项目采用 [MIT License](LICENSE)。

