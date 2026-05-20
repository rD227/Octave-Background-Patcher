# Octave Background Image Patcher

Add a customizable background image to the [GNU Octave](https://octave.org) code editor — similar to IntelliJ IDEA's background image feature.

Works on **Windows x86_64** with Octave 11.1.0 (Qt6 + QScintilla builds).

[中文说明 →](README_zh.md)

![this](Pictures/屏幕截图%202026-05-20%20182630.png)
![fullScreen](Pictures/屏幕截图%202026-05-20%190125.png)


## How it works

The patcher uses DLL injection to add a transparent overlay widget on top of each editor pane. The overlay draws your chosen image with configurable opacity, while all mouse and keyboard input passes straight through to the editor underneath.

```
OverlayWidget (background image, click-through)
     |
QsciScintilla editor (renders normally, unaffected)
```

## Quick start

1. Download `patcher.exe` and `bgpatch.dll` from [Releases](../../releases)
2. Place both files together in **any folder inside the Octave installation** (e.g. `octave-11.1.0-w64\home\octave-bg-patcher\`)
3. Run `patcher.exe`
4. Select a background image when prompted
5. Click **Yes** to launch Octave
6. Open any `.m` file — the image appears behind your code

To change the image later, run `patcher.exe` again.

To launch without the config dialog, use `patcher.exe /silent`.

## Configuration

Edit `bgpatch.ini` (created automatically after first run) to adjust:

```ini
[Background]
Image=D:/Pictures/wallpaper.png
Opacity=30       ; 0 = invisible, 100 = fully opaque
Dimming=40       ; 0 = no darkening, 100 = solid black
ScaleMode=1      ; 0=Fit  1=Fill  2=Stretch  3=Center  4=Tile
Scope=1          ; 1 = editor panes only, 2 = entire window
```

Changes take effect within 3 seconds — no restart needed.

## Build from source

Requires the MSYS2/MinGW toolchain included with Octave.

```batch
cd octave-bg-patcher
set "MINGW=<octave_root>\mingw64"
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

Or double-click `build.bat`.

## Technical notes

- Uses `WA_TransparentForMouseEvents` + `WA_NativeWindow` for click-through overlays
- Finds editors via Qt meta-object system (`QApplication::allWidgets`)
- Overlay is parented to the viewport (stays above QScintilla's native rendering)
- File-based logging at `<dll_directory>\bgpatch.log` for troubleshooting
- Tested with Octave 11.1.0 (Qt 6.7.3, QScintilla 2, GCC 15.2.0)

## License

GPL-3.0-or-later
