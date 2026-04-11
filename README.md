# AsmVsZombies

AvZ (Assembly vs. Zombies) 是一套使用 C++ 语言编写的高精度植物大战僵尸键控框架，理论由 yuchenxi0_0 提出，框架底层由 yuchenxi0_0 实现，和其他框架相似的接口由 vector-wlc 编写。

AvZ 操作精度为理论上的绝对精准，使用这套框架将再也不用担心精度的问题，可在一定程度上减少录制视频次数，有效地完成视频制作。

本项目使用 [VSCode](https://code.visualstudio.com/) + LLVM-MinGW 进行代码编辑、编译和注入。

## 使用

请转到教程的目录：[GitLab](https://gitlab.com/vector-wlc/AsmVsZombies/blob/master/tutorial/00_catalogue.md) / [GitHub](https://github.com/vector-wlc/AsmVsZombies/blob/master/tutorial/00_catalogue.md) / [Gitee](https://gitee.com/vector-wlc/AsmVsZombies/blob/master/tutorial/00_catalogue.md)

## 友情链接

**注意：以下存储库的作者不是 AsmVsZombies 的作者，因此出现任何问题请咨询存储库中的相关作者。**

AvZ 扩展功能库：[AvZLib](https://github.com/qrmd0/AvZLib)

AvZ 脚本库：[AvZScript](https://github.com/qrmd0/AvZScript)

## 原理

在游戏主循环函数前面注入键控代码，使得键控脚本在每一帧都被调用，从而实现真正意义上 100 %精确的键控。

## 对比

从原理可以明显看出此套框架在理论实现上与传统框架截然不同，传统框架使用一个程序向 PvZ 窗口发送点击消息，而此套框架使用代码注入，直接入侵到程序内部，让游戏程序运行我们编写的脚本！其优缺点大致如下：

> 缺点
>
> * 编写不慎可能会导致游戏崩溃
>
> 优点
>
> * 精度极高
> * 脚本出现错误时提示更加人性化
> * 对硬件配置 (CPU) 的要求低
> * 对操作时间顺序不做严格要求

## AvZ加载器

> by yuchenxi2000

`loader`目录下实现了类似Minecraft游戏的Fabric/Forge模组加载器，只需要把编译成动态库的AvZ脚本放在PvZ游戏所在目录的`mods`文件夹下，启动游戏时即可自动加载脚本。

AvZLoader能够：

* 启动游戏自动加载

* 同时加载多个脚本

* 方便分享脚本/插件

* 热加载，可以在游戏运行时加载/卸载脚本

* 限定脚本在指定用户、指定关卡运行，通过toml格式的配置文件

> 关于热加载：Windows不允许覆盖被加载的dll，因此卸载脚本只有两种方式：
> 1. 重命名（必须改后缀，比如a.dll改成a.dll.disabled，不然会被重新加载回来）
> 2. 移出mods目录

使用方式：

1. 编译完，把`bin`目录下的`avzloader.dll`和`avzinstaller.exe`拷贝到PvZ游戏所在目录下

2. 运行`avzinstaller.exe`，它生成一个`PlantsVsZombies_modded.exe`，这个就是安装了AvZLoader的版本

3. 编译脚本有两种方式：1、从头编译，具体可参考`CMakeLists.txt`中编译示例模组部分；2、链接`libavz.a`（必须是本仓库fork版本编译出的`libavz.a`）

4. 在PvZ游戏所在目录新建一个`mods`文件夹，把编译好的动态库放在`mods`文件夹下，启动游戏

> 注意第三条：dll是统一的，但目前只有两种选择
>
> 1. 脚本编译成的dll作为模组放入mods文件夹，启动`PlantsVsZombies_modded.exe`
>
> 2. 启动`PlantsVsZombies.exe`，用`injector.exe`注入（目前只能注入`PlantsVsZombies.exe`，不能注入`PlantsVsZombies_modded.exe`）
>
> 原因比较复杂。AvZLoader采用加载器统一维护钩子、脚本不挂钩子的方式。为了兼容`injector.exe`注入游戏方式，脚本如果检测到没有钩子存在，就会挂钩子。然而`PlantsVsZombies_modded.exe`加载的AvZLoader会先挂上钩子，导致注入的脚本发现钩子已经存在便不挂钩子，但AvZLoader又不知道脚本的存在。
>
> 接下来会写一个callback，在注入脚本时将其注册为mod（TODO）

脚本配置文件示例：（配置文件也支持热加载）
```toml
[[run]]
[run.user]
include = "Ycx"  # 限制只在用户名为Ycx的存档运行（PvZ用户名包括大小写！）
# exclude = "233"  # 排除模式，注意你只能选择include或者exclude
[run.level]
include = 13  # 限制只在泳池无尽运行
# exclude = [20, 24]  # 限制不在宝石迷阵系列关卡运行。这些选项都能用数组，包括上面的用户名也能用数组

# 如果要对另一个用户名或者关卡配置，可以新写一个表
[[run]]
[run.user]
include = "666"
[run.level]
include = 13
```
如果有一个脚本叫`script.dll`，那么对应的配置文件为`script.toml`，放到`mods`目录下。

## 致谢

* [yuchenxi2000/AssemblyVsZombies](https://github.com/yuchenxi2000/AssemblyVsZombies)
* [lmintlcx/pvzscript](https://github.com/lmintlcx/pvzscripts)
* [TsudaKageyu/minhook](https://github.com/TsudaKageyu/minhook)
* [失控的指令(bilibili)](https://space.bilibili.com/147204150/)
* [Power_tile(bilibili)](https://space.bilibili.com/367385512)
* [六三enjoy(bilibili)](https://space.bilibili.com/660622963)
* [alumkal(github)](https://github.com/alumkal)
* 以及所有对此项目提出建议的使用者和开发人员
