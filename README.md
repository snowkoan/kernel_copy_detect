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
4. Run `aaminiexe.exe` to see messages from the driver.

ex:

```
svchost.exe (4640,7076): Created SH context 0xFFFFD18674F020B0 for \Device\HarddiskVolume3\ProgramData\Microsoft\Diagnosis\DownloadedSettings\utc.allow.json
        Opened with copy source flag
svchost.exe (4640,7076): Created SH context 0xFFFFD18674F02180 for \Device\HarddiskVolume3\ProgramData\Microsoft\Diagnosis\DownloadedSettings\utc.allow.json.bk
        Opened with copy destination flag
System (4, 8264): Copy Notification (pos=0, len=1048576)
        Destination: \Device\HarddiskVolume3\ProgramData\Microsoft\Diagnosis\DownloadedSettings\utc.allow.json.bk (SH=FFFFD18674F02180)
        Source: \Device\HarddiskVolume3\ProgramData\Microsoft\Diagnosis\DownloadedSettings\utc.allow.json
System (4, 2524): Copy Notification (pos=1048576, len=213632)
        Destination: \Device\HarddiskVolume3\ProgramData\Microsoft\Diagnosis\DownloadedSettings\utc.allow.json.bk (SH=FFFFD18674F02180)
        Source: \Device\HarddiskVolume3\ProgramData\Microsoft\Diagnosis\DownloadedSettings\utc.allow.json
Received section handle - file size 1262208 bytes, handle 188
7B 22 71 75 65 72 79 55  72 6C 22 3A 22 2F 73 65  |  {"queryUrl":"/se
74 74 69 6E 67 73 2F 76  33 2E 30 2F 75 74 63 2F  |  ttings/v3.0/utc/
...
3A 46 46 46 42 43 45 37  32 33 33 43 35 42 37 35  |  :FFFBCE7233C5B75
41 22 2C 22 76 61 6C 75  65 22 3A 33 7D 5D 7D 7D  |  A","value":3}]}}```

# testing
So far, I've only played with this on Windows 11, since copy notifications are only supported in Win11 22000+. I run with Driver Verifier standard settings enabled, but that doesn't mean there are no bugs.

