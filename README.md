# pico-sd-card
SD card script for Raspberry Pi Pico. This is fundamentally the same script from [here](https://github.com/micropython/micropython/blob/a1bc32d8a8fbb09bc04c2ca07b10475f7ddde8c3/drivers/sdcard/sdcard.py) with numerous syntax and functional changes applied.

### The Changes include:

1) numerous unnecessary variables have been removed
2) `while` loops have been replaced with `range` loops where possible
3) all commands have been converted to constants
4) strings have been shortened to reflect the same meaning of their intended message in less characters
5) all unnecessary comparisons have been removed
6) command construction has been changed entirely
7) support for pyboard was removed
8) imports have been made more specific
9) main class has been renamed `SDObject`
10) `SDCard` is now a wrapper for `SDObject` which automatically mounts the card and adds it to `sys.path`
11) everything has been annotated


### Extra:

1) SDObject loads at roughly 300 bytes less than the original SDCard module did, without losing or compromising any functionality.
2) An `mpy-cross` compiled version is provided for those that don't intend to freeze `sdcard.py` into their firmware



### Test:

You can run scripts directly from the sdcard. The below example will write, import and run a simple test script from the SD Card. Note that `mount=True` and `drive='/sd'` are actually the defaults and it is unnecessary to define them if those are the values you want. I included them in the script below solely to give an examplle of all the constructor arguments.

```python
import sdcard

#init sd card
sd  = sdcard.SDCard(1, sck=10, mosi=11, miso=8, cs=9, baudrate=0x14<<20, mount=True, drive='/sd')

#write a script to the card
with open("{}/test.py".format(sd.drive), 'w') as f:
    f.write('class Test(object):\n\tdef __init__(self):\n\t\tprint("Hello From SD Card")')

#import the script
test = __import__('test', globals(), locals(), ['Test'], 0)

#call it's class
test.Test()

#eject card
sd.eject()
```
