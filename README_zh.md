# Octave 编辑器背景图片补丁

给 [GNU Octave](https://octave.org) 代码编辑器添加背景图片，效果类似 IntelliJ IDEA 的编辑器背景图功能。

适用于 **Windows x86_64** + Octave 11.1.0（Qt6 + QScintilla 构建）。

[English →](README.md)

## 原理

通过 DLL 注入，在每个编辑器面板上方创建一个透明叠加层控件。叠加层以可配置的透明度绘制你选择的图片，同时所有鼠标和键盘输入直接穿透到底层编辑器。

```
OverlayWidget（背景图，鼠标穿透）
     |
QsciScintilla 编辑器（正常渲染，不受影响）
```

![this](Pictures/屏幕截图%202026-05-20%20182630.png)
![fullScreen](Pictures/屏幕截图%202026-05-20%20190125.png)

## 快速开始

1. 从 [Releases](../../releases) 下载 `patcher.exe` 和 `bgpatch.dll`
2. 把两个文件放在 Octave 安装目录下的**任意文件夹**里（例如 `octave-11.1.0-w64\home\octave-bg-patcher\`）
3. 双击运行 `patcher.exe`
4. 在弹出对话框中选择一张背景图片
5. 点击**是**启动 Octave
6. 打开任意 `.m` 文件 —— 背景图就会显示在代码后面

想换图片的话再运行一次 `patcher.exe` 就行。想跳过设置对话框直接启动用 `patcher.exe /silent`。

## 配置

编辑 `bgpatch.ini`（首次运行后自动生成）：

```ini
[Background]
Image=D:/图片/壁纸.png
Opacity=30       ; 0 = 完全透明, 100 = 完全不透明
Dimming=40       ; 0 = 不做暗化, 100 = 全黑
ScaleMode=1      ; 0=适配  1=裁剪填充  2=拉伸  3=居中  4=平铺
Scope=1          ; 1 = 仅编辑器, 2 = 整个窗口
DebugLog=0       ; 0 = 关闭日志, 1 = 写入 bgpatch.log 用于调试
```

修改后 3 秒内自动生效，不需要重启 Octave。

## 从源码编译

需要 Octave 自带的 MSYS2/MinGW 工具链。

```batch
cd octave-bg-patcher
set "MINGW=<octave根目录>\mingw64"
set "PATH=%MINGW%\bin;%PATH%"
set "QT6=%MINGW%\qt6"

g++ -std=c++17 -O2 -Wall -fPIC ^
  -DWIN32_LEAN_AND_MEAN -DNOMINMAX -DUNICODE -D_UNICODE ^
  -I"%QT6%\include" -I"%QT6%\include\QtCore" -I"%QT6%\include\QtGui" ^
  -I"%QT6%\include\QtWidgets" -I"%QT6%\include\Qsci" ^
  -shared -o bgpatch.dll src\bgpatch\main.cpp src\bgpatch\bgpatch.cpp ^
  -L"%QT6%\lib" -lQt6Core -lQt6Gui -lQt6Widgets -lqscintilla2_qt6

g++ -std=c++17 -O2 -Wall -DUNICODE -D_UNICODE -mwindows -municode ^
  -o patcher.exe src\patcher\main.cpp -lcomctl32 -lcomdlg32
```

或者直接双击 `build.bat`。

## 其他说明

- 使用 `WA_TransparentForMouseEvents` + `WA_NativeWindow` 实现鼠标穿透的叠加层
- 通过 Qt 元对象系统（`QApplication::allWidgets`）发现编辑器控件
- 叠加层挂在 viewport 上，确保显示在 QScintilla 原生渲染层之上
- 调试日志输出到 `<dll目录>\bgpatch.log`
- 已在 Octave 11.1.0（Qt 6.7.3, QScintilla 2, GCC 15.2.0）上测试通过

## 许可证

GPL-3.0-or-later
