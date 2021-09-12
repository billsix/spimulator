## spimulator

spimulator is an text-based MIPS32 simulator. Works on Windows, Linux, and MacOS.

### Building from source

spimulator is built using CMake.



#### Windows, using Visual Studio

* Install Cmake, install Visual Studio.  I use community edition.
* Double click buildDebug.bat
* spimulator is now under buildInstall\bin\spimulator
* add to PATH on command line
    * set PATH=%PATH%;%CD%\buildInstall\bin
* run programs
    * batch
        * spimulator -f examples\01-helloworld\01-helloworld.asm
    * interactively
        * spimulator
            * load "examples\01-helloworld\01-helloworld.asm"
            * step
            * step
            * step
            * step

#### Linux

* Install cmake.
    * On Debian based system, apt install cmake gcc
* ./buildDebug.sh
* spimulator is now under buildInstall/bin/spimulator
* add to PATH on command line
    * export PATH=$(pwd)/buildInstall/bin
* run programs
    * batch
        * spimulator -f examples/01-helloworld/01-helloworld.asm
    * interactively
        * spimulator
            * load "examples/01-helloworld/01-helloworld.asm"
            * step
            * step
            * step
            * step

#### MacOS

* Install cmake.
    * I use brew. brew install cmake gcc
* If you just want to use the command line
    * ./buildDebug.sh
    * spimulator is now under buildInstall/bin/spimulator
* add to PATH on command line
    * export PATH=$(pwd)/buildInstall/bin
* run programs
    * batch
        * spimulator -f examples/01-helloworld/01-helloworld.asm
    * interactively
        * spimulator
            * load "examples/01-helloworld/01-helloworld.asm"
            * step
            * step
            * step
            * step
* If you want to use XCode
    * ./macBuildDebug.sh
    * make sure that you configure the project to use an external console, rather than XCode's embedded console.  Because otherwise you can't rum spimulator through XCode's debugger.
        * once XCode is open, click on "spimulator" at the top of the window, middle pane, on the left.
	* Click "Edit Scheme"
	* "Run" should be selected on the left.  On the right pane, scroll down to "Console", and check "Use Termimal".
	* Take note of where the console says the working directory is.  "load [assemblyfile]" will need for assembly file to be relative to the working directory


### Copyright

spimulator is Copyright (c) 2021, by William Emerison Six, starting from git commit e10b97408f6d2c405c36ab05cdffbf40828970fd
All rights reserved.

spimulator is distributed under a BSD license.  See LICENSE


### Original work

SPIM is Copyright (c) 1990-2020, by James R. Larus.
All rights reserved.


This project is derived from spim, https://sourceforge.net/projects/spimsimulator/.
