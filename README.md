# kernel_copy_detect
Playing with Windows 11 [kernel copy notifications](https://learn.microsoft.com/en-us/windows-hardware/drivers/ifs/km-file-copy).

Originally forked from @zoadiacon: https://github.com/zodiacon/windowskernelprogrammingbook2e/tree/master/Chapter12/KBackup2

# building
1. This should build against WDK 10.0.22621.0 and later. I'm not sure about earlier versions.
2. [ktl](https://github.com/snowkoan/ktl) is a submodule so you will have problems if you don't initialize it somehow. The easiest way is likely: `git clone https://github.com/snowkoan/kernel_copy_detect.git --recurse-submodules`

# installing & running
Do not run this on your dev machine. It's kernel code of the POC variety. Here's what I do:

1. Copy all binaries to a test VM with [test signing enabled](https://learn.microsoft.com/en-us/windows-hardware/drivers/install/the-testsigning-boot-configuration-option).
2. To install, just create a service entry for the driver: `sc create copydetect type= kernel binPath="%cd%\aamini.sys" start= demand`
3. Run `fltmc load copydetect`. This will set up the necessary minifilter registry values etc (thanks @zodiacon)
4. Run `aaminiexe.exe` to see messages from the driver and to tell the driver to block certain copy operations. If you try to copy a file via Explorer, xcopy, robocopy, cmd copy, Powershell copy etc. and the first characters of the file are ASCII `snowkoan-secret`, the copy will be blocked.

ex:

```cmd.exe (2388,4844): Created context SH=FFFFBD066FCD6840, FO=FFFFBD0672C5FBD0, \Device\HarddiskVolume3\temp\test.txt
Opened with copy source flag
Received section handle - file size 18 bytes, handle 200
73 6E 6F 77 6B 6F 61 6E  2D 73 65 63 72 65 74 20  |  snowkoan-secret
0D 0A                                             |  ..
Block #10: Secret data detected! Not allowing copy.
cmd.exe (2388,4844): Blocking copy to \Device\HarddiskVolume3\temp\test2.txt with code 0xc0000804
```

To uninstall:
1. Run `sc delete copydetect`

# testing
So far, I've only played with this on Windows 11, since copy notifications are only supported in Win11 22000+. I run with Driver Verifier standard settings enabled, but that doesn't mean there are no bugs.

