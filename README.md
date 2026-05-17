# sunshine-privatescreen
一款能实现sunshine或其他远程软件/屏幕捕获工具物理屏隐私保护的程序
# 工作原理

在当前屏幕下创建一个保持置顶的全屏无边框黑屏窗口，物理屏能正常显示该黑屏窗口，但包括sunshine在内的屏幕捕获软件无法捕获到该黑屏窗口，并且允许键鼠输入透传到无边框窗口下方的其他窗口中，从而实现远程控制时物理屏保持黑屏，但远程画面能正常显示的目的，和市面上主流远控软件的隐私屏功能效果类似。

目前已实现黑屏显示，理论上可以通过修改代码的方式实现自定义屏保的显示。

# 使用方法
## 1.直接运行（手动模式）

双击程序直接运行。即可实现上述效果。注意：不建议在非远程连接期间运行该程序，否则将可能只能通过远程连接退出。可在系统托盘右键选择退出，或通过任务管理器直接停止。

## 2.Sunshine创建应用程序配置文件（自动模式）

以下截图使用[foundation-sunshine](https://github.com/AlkaidLab/foundation-sunshine)进行演示，其他版本（包括官方版sunshine）方法是基本相同的。

进入Sunshine控制台，点击“应用程序”界面中的“添加新应用”，“应用名称”“图片”等可自定义，并在“主程序”中填入隐私屏程序的路径即可。建议在高级设置中将“退出超时”设置为1秒。

<img width="2732" height="1912" alt="image" src="https://github.com/user-attachments/assets/f133a580-395d-43b5-b369-8ec1fe0b7e99" />
<img width="2721" height="1913" alt="image" src="https://github.com/user-attachments/assets/6b3def34-0914-4cc3-a168-1e98f6b02671" />
<img width="2722" height="1904" alt="image" src="https://github.com/user-attachments/assets/924f3022-348d-4090-863d-f8e2a007caef" />

完成后在主控端打开Moonlight，选择刚创建的带隐私屏保护的那一项，此时可发现被控端物理显示器黑屏。但串流画面可正常显示桌面内容。
<img width="2560" height="1600" alt="Screenshot_2026-05-17-09-26-03-655_com limelight unofficialA-edit" src="https://github.com/user-attachments/assets/60cdec7c-5577-49a8-9fa4-ba284ca2e60d" />
<img width="2304" height="4096" alt="b390b2964d2b88e7089de84fa54f69a5" src="https://github.com/user-attachments/assets/53deab4f-168a-4021-98c3-0c613b09d874" />

# 自行编译

可使用mingw64 g++进行编译：
```powershell
g++ main.cpp -o DesktopPrivate.exe -mwindows -static -std=c++17 -lgdi32 -luser32 -lgdiplus -ldwmapi
```

# 已知问题

1.目前不支持隐藏鼠标指针、触控点；

2.由于Win11的限制，目前暂时无法实现开始菜单、控制中心、窗口预览图等界面的隐藏。（如确实有方法解决，欢迎提交issue或Pull Request）

如介意上述问题，建议改用虚拟屏驱动创建虚拟屏幕的方式实现隐私屏，但该方案可能导致部分应用程序存在兼容性问题。
