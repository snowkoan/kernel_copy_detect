# kernel_copy_detect
Playing with Windows 11 [kernel copy notifications](https://learn.microsoft.com/en-us/windows-hardware/drivers/ifs/km-file-copy).

Originally forked from @zoadiacon: https://github.com/zodiacon/windowskernelprogrammingbook2e/tree/master/Chapter12/KBackup2

# building
1. This should build against WDK 10.0.22621.0 and later. I'm not sure about earlier versions.
2. [ktl](https://github.com/snowkoan/ktl) is a submodule so you will have problems if you don't initialize it somehow. The easiest way is likely: `git clone https://github.com/snowkoan/kernel_copy_detect.git --recurse-submodules`

# installing & running
Do not run this on your dev machine. It's kernel code of the POC variety. Here's what I do:

1. Copy all binaries to a test VM with [test signing enabled](https://learn.microsoft.com/en-us/windows-hardware/drivers/install/the-testsigning-boot-configuration-option).
2. First time service creation: `sc create copydetect type= kernel binPath="%cd%\aamini.sys" start= demand`
3. Run `fltmc load copydetect`. This will set up the necessary minifilter registry values etc (thanks @zodiacon)
4. Run `aaminiexe.exe` to see messages from the driver.

ex:

```
cmd.exe (11980): Created SH context 0xFFFFAE0761D02B40 for \Device\HarddiskVolume3\temp\out.txt
        Opened with copy source flag
cmd.exe (11980): Created SH context 0xFFFFAE0761D02D80 for \Device\HarddiskVolume3\temp\out2.txt
        Opened with copy destination flag
System (4): Copy Notification (pos=0, len=1000000)
        Destination: \Device\HarddiskVolume3\temp\out2.txt (SH=FFFFAE0761D02D80)
        Source: \Device\HarddiskVolume3\temp\out.txt
```

# testing
So far, I've only played with this on Windows 11, since copy notifications are only supported in Win11 22000+. I run with Driver Verifier standard settings enabled, but that doesn't mean there are no bugs.

