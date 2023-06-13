# OmniStack

This project is a refactored version of Project [OmniStack C Version](https://github.com/JeremyGuo/CQStack)

OmniStack是一个完全模块化的运行在用户态的网络协议栈

## System Design

整个系统分成三个部分：

1. 共享库
2. 动态库
3. 协议栈

### 共享库

使用者可以通过`LD_PRELOAD`实现对原可执行程序的透明迁移。

注意，本系统只实现了对如下符号的替换，如果有其他内核态函数调用会产生如创建、关闭文件描述符的操作可能导致程序无法正常执行。

共享库主要实现了如下内容：

1. 一个全新的文件描述符系统（进程独立），其底层为新的协议栈的node系统（进程间共享）或Linux原有的文件描述符系统。
    > 注意每一个socket的底层是两个不同的node，一个用于接收报文，另一个用于发送报文
2. 提供了新的node系统和协议栈通信的通道，每一个node的底层是一个无锁的和协议栈进程通信的通道。

### 动态库

### 协议栈

## Development

本项目基于C++20开发，代码规范参照 [Google 代码规范](https://zh-google-styleguide.readthedocs.io/en/latest/google-cpp-styleguide/naming/#section-7)