# Acquisition Plus

**IMPORTANT! Please note that Acquisition Plus is a modified version of the original Acquisition, which can be found [here](https://github.com/xyzz/acquisition).**

Linux Build: ![Travis](https://travis-ci.org/Novynn/acquisitionplus.svg?branch=master)

Acquisition Plus is an inventory management tool for the online ARPG [Path of Exile](https://www.pathofexile.com/). It is written in C++, uses Qt widget toolkit and runs on Windows and Linux.

![alt tag](http://i.imgur.com/QFR0FCx.png)
You can find some more (slightly outdated) screenshots of it in action [here](http://imgur.com/a/QIPQJ).

## Compiling/developing Acquisition

### Windows

On Windows you can use either Visual Studio or MinGW version of Qt Creator. Alternatively you can also use Visual Studio with Qt Add-in. Note that only Visual Studio 2013 Update 3 is supported.

### Linux

Either open `acquisition.pro` in Qt Creator and build or do `qmake && make`.

## Command line arguments

`--data-dir <path>`: set the path where Acquisition should save its data. By default it's `%localappdata%\acquisitionplus` on Windows and `~/.local/share/acquisitionplus` on Linux.
