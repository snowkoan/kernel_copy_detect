# kernel_copy_detect
Playing with Windows 11 [kernel copy notifications](https://learn.microsoft.com/en-us/windows-hardware/drivers/ifs/km-file-copy).

Originally forked from @zoadiacon: https://github.com/zodiacon/windowskernelprogrammingbook2e/tree/master/Chapter12/KBackup2

I've added ktl as a submodule, you might want to use the `--recurse-submodules` flag.
ex: `git clone https://github.com/snowkoan/kernel_copy_detect.git --recurse-submodules`

# building
This should build against WDK 10.0.22621.0 and later. I'm less sure about earlier versions.

# installing & running
Do not run this on your dev machine. It's kernel code of the POC variety. Here's what I do:

1. Copy all binaries to a test VM with [tes signing enabled](https://learn.microsoft.com/en-us/windows-hardware/drivers/install/the-testsigning-boot-configuration-option).
2. Run `fltmc load aamini`. This will set up the necessary minifilter registry values etc (thanks @zodiacon)
3. Run `aaminiexe.exe` to see messages from the driver.

ex:

```
Robocopy.exe: Created SH context 0xFFFF850C4D7020B0 for \Device\HarddiskVolume3\Users\alnoor\test\test.txt
Robocopy.exe: Created SH context 0xFFFF850C4D702180 for \Device\Mup\127.0.0.1\c$\temp\copy\test.txt
Copy Notification (pos=0, len=1000000)
        Destination: \Device\Mup\127.0.0.1\c$\temp\copy\test.txt (SH=FFFF850C4D702180)
        Source: \Device\HarddiskVolume3\Users\alnoor\test\test.txt
```

# testing
So far, I've only played with this on Windows 11, since copy notifications are only supported in Win11 22000+. I run with Driver Verifier standard settings enabled, but that doesn't mean there are no bugs.

