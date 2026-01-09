<p align="center"><a href="#" target="_blank" rel="noopener noreferrer"><img width="100" src="logo.jpg" alt="LuatOS logo"></a></p>

本项目源于合宙LuatOS，专用于ESP32C3/S3系列芯片

LuatOS-SoC是一款实时操作系统,用户编写Lua代码就可完成各种功能, 仅需极少的内存和Flash空间

### 修改如下：
1. 修改系统分区表 844 555 布局
2. 添加FT6636U的驱动以及LVGL——PORTING

1. 基于Lua 5.3.x脚本编程,无需编译,把Lua文本文件下载到设备即可完成开发
2. 低内存需求, 最低32kb ram, 96kb flash空间
3. 硬件抽象层兼容M3/armv7/risc-v/win32/posix等等,具有强大的扩展性
4. 可测试,可模拟,可裁剪,可扩展, 提供高效的开发效率
5. 基于合宙深耕的Lua-Task编程模型,实现异步/同步无缝切换

----------------------------------------------------------------------------------

## 使用到的开源项目

* [lua](https://www.lua.org/) Lua官网
* [rt-thread](https://github.com/RT-Thread/rt-thread) 国产rtos, 非常好用
* [rtt-ds18b20](https://github.com/willianchanlovegithub/ds18b20) 在RT-Thread环境下读取ds18b20
* [LuaTask](https://github.com/openLuat/Luat_2G_RDA_8955) 合宙LuaTask
* [iRTU](https://github.com/hotdll/iRTU) 基于Luat的DTU, 稀饭大神
* [airkissOpen](https://github.com/heyuanjie87/airkissOpen) 参考其实现思路
* [minmea](https://github.com/kosma/minmea) 解析nmea
* [u8g2_wqy](https://github.com/larryli/u8g2_wqy) u8g2的中文字体
* [printf](https://github.com/mpaland/printf) A printf / sprintf Implementation for Embedded Systems
* [YMODEM for Python](https://github.com/alexwoo1900/ymodem) YMODEM 用于下载脚本
* [elua](http://www.eluaproject.net/) eLua 虽然已经停更多年,但精神犹在
* [FlashDB](https://gitee.com/Armink/FlashDB) 一款支持 KV 数据和时序数据的超轻量级数据库
* [cJSON](https://github.com/DaveGamble/cJSON) Ultralightweight JSON parser in ANSI C
* [coremark](https://github.com/eembc/coremark) MCU性能测试(跑分)
