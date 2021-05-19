# pico-sd-card
SD card scripts for Raspberry Pi Pico. This began as a polishing of the `sdcard.py` script from [here](https://github.com/micropython/micropython/blob/a1bc32d8a8fbb09bc04c2ca07b10475f7ddde8c3/drivers/sdcard/sdcard.py), but has been expanded to include ports of that script with numerous changes. Even though this repo is titled **pico-sd-card** these scripts should work with every device the original script worked with, except PyBoard. PyBoard was taken out because it was the only device that had special exceptions. I made an executive decision that I don't want exceptions in my code that only 1 board needs.

**tested with micropython versions:**
- v1.14-137-g172fb5230-dirty
- v1.15-88-g7b923d6c7-dirty

### Community:

_To officially file a bug report or feature request you can use these templates:_   [bug report](https://github.com/OneMadGypsy/pico-sd-card/blob/main/.github/ISSUE_TEMPLATE/bug_report.md) | [feature request](https://github.com/OneMadGypsy/pico-sd-card/blob/main/.github/ISSUE_TEMPLATE/feature_request.md)

_To discus features, bugs or share your own project that utilize code in this repo:_   [join the discussion](https://github.com/OneMadGypsy/pico-sd-card/discussions/3)

<br />

- - - - 

<br />

## Changes:

1) numerous unnecessary variables have been removed
2) `while` loops have been replaced with `range` loops where possible
3) all commands have been converted to constants
4) strings have been shortened to reflect the same meaning of their intended message in less characters
5) all unnecessary comparisons have been removed
6) command construction has been changed entirely
7) support for pyboard was removed
8) imports have been made more specific
9) main class has been renamed `SDObject`
10) everything has been annotated
11) `SDCard` is now a wrapper for `SDObject`
12) ioctl has been fleshed out to include all cmds
13) automatically mount the `SDCard` and add it to `sys.path` option
14) indicator LED option
15) detection pin option
16) wait for `SDCard` detection option

<br />

-------

<br />

## Ports:

**All 3 ports have an identical interface from the front-end.**

### sdcard.py
>This can be uploaded directly to the board, but is intended to be used as a frozen module. For information regarding how to setup the sdk and freeze a module you can refer to [this post](https://www.raspberrypi.org/forums/viewtopic.php?f=146&t=306449#p1862108) on the Raspberry Pi forum.


### sdcard.mpy
>This is a cross-compiled version of `sdcard.py`. It is intended to be uploaded to your board as you would any normal `.py` script.


### modules/
>This contains a pure C port of `sdcard.py` packaged as a `USER_C_MODULE`. If you have the sdk setup it can be compiled into firware by navigating to `./ports/rp2/` and running the following command:

`make USER_C_MODULES=/path/to/modules/micropython.cmake all`

<br />

-------

<br />

## Docs:


**SDCard(`spi`, `sck`, `mosi`, `miso`, `cs`, `baudrate`, `automount`, `drive`, `led`, `detect`, `wait`)**
> Main SDCard interface

| Args          | Type | Description                                                    | Default     |
| ------------- |------|----------------------------------------------------------------|-------------|
| **spi**       | int  | which SPI to use (0 or 1)                                      | **REQUIRED**|
| **sck**       | int  | SPI clock pin id                                               | **REQUIRED**|
| **mosi**      | int  | SPI mosi pin id                                                | **REQUIRED**|
| **miso**      | int  | SPI miso pin id                                                | **REQUIRED**|
| **cs**        | int  | SPI chip select pin id                                         | **REQUIRED**|
| **baudrate**  | int  | the desired speed to read/write                                | 5mhz        |
| **automount** | bool | whether to automatically mount the drive                       | True        |
| **drive**     | str  | drive-name to represent the drive                              | "/sd"       |
| **led**       | int  | pin id for a connected LED. LED is on during read/write        | -1 (no pin) |
| **detect**    | int  | pin id for a detect feature                                    | -1 (no pin) |
| **wait**      | bool | whether to wait for card insertion. Used with detect (blocks)  | False       |

<br />

**.setup(`automount`, `wait`)**
> Manual setup option for use with the `detect` option. This is done automatically if a card was present or `wait` was `True` when `SDCard` was instantiated 

| Args          | Type | Description                                                    | Default     |
| ------------- |------|----------------------------------------------------------------|-------------|
| **automount** | bool | whether to automatically mount the drive                       | True        |
| **wait**      | bool | whether to wait for card insertion. Used with detect (blocks)  | False       |

<br />

**.mount()**
> Manually mount a card. If a card is already mounted nothing happens.

<br />

**.eject()**
> Eject a mounted card. If no card is mounted nothing happens.

<br />

**.type**
> Returns the type of sdcard that is inserted (ver. 1 or ver. 2)

<br />

**.sectors**
> Returns the number of sectors on the insterted sdcard. Divide by 2048 to get volume size in mb.

<br />

**.drive**
> Returns the drive letter that is assigned to the card.

<br />

------

<br />

## Usage:
>*The below scripts reflect a basic `SPI1` setup. The pin ids you use will depend on the pins your card is connected to. If you are unsure of your options, you can use this [pinout](https://hackaday.com/wp-content/uploads/2021/01/pico_pinout.png) as a reference.*

<br />

**basic**


This is an example of the absolute bare minimum it takes to connect and mount an sdcard. All omitted arguments will default to the values in the above table.
```python
import sdcard

sd = sdcard.SDCard(1, 10, 11, 8, 9)
```
<br />

**detection and waiting**

In this example we use a `baudrate` of 16mhz, and the on-board LED. It is implied that our sdcard reader has a `detect` feature and it is connected to `pin 15`. Since `wait` is `True` the script will sit in an infinite loop waiting for an sdcard to be inserted (if one is not already), and will automatically connect once one is. Then our `json` file will load. This feature may be handy if it is mandatory for an sdcard to be inserted in order for the system to proceed. In this case, it is implied that our system depends on log information to continue.
```python
import sdcard, ujson

sd = sdcard.SDCard(1, 10, 11, 8, 9, baudrate=0x10<<20, led=25, detect=15, wait=True)

log = ujson.load(open("{}/log.json".format(sd.drive), 'r'))
```
<br />

**mount() / eject()**

You can turn off `automount`, and `mount` the sdcard manually at a later time. You can also `eject` the sdcard whenever you like. Conditions are in place that wont run the `mount` feature if the sdcard is already mounted, and wont run the `eject` feature if the card is already ejected.
```python
import sdcard, ujson

sd = sdcard.SDCard(1, 10, 11, 8, 9, mount=False)

# this line represents your operations between initializing and mountiing the sdcard

sd.mount()

#this line represents an itermittent need for the sdcard

sd.eject()
```
<br />

**setup(`automount`, `wait`)**

If you use the `detect` option, did not `wait`, and did not have an sdcard inserted, you can manually call the card setup at a later time to establish a connection. You can also designate whether you want to `automount` the card and/or `wait` for a card to be inserted. This feature can be used anywhere that it is mandatory for an sdcard to be connected and mounted. If an sdcard is already connected and mounted when this is called the request is simply ignored.
```python
import sdcard

#init sd card
sd = sdcard.SDCard(1, 10, 11, 8, 9, baudrate=0x10<<20, led=25, detect=15, wait=False)

# this line represents your operations between instantiating and initializing the sdcard

sd.setup(wait=True)

# this line represents your operations once a card is inserted and detected 
# if wait is True this will not be reached until a card is inserted
```
<br />

**loading external scripts**

When a card has been successfully connected and mounted it is also automatically added to the system path. Below is a simple example of running a `pyhon` script from the sdcard. So the test is easy to perform, this will write a simple script to the sdcard first.
```python
import sdcard

sd = sdcard.SDCard(1, 10, 11, 8, 9)

#write a simple test script to the card
with open("{}/test.py".format(sd.drive), 'w') as f:
    f.write('class Test(object):\n\tdef __init__(self):\n\t\tprint("Hello From SD Card")')

#import the script
test = __import__('test', globals(), locals(), ['Test'], 0)

#call it's class
test.Test()
```
<br />

- - - - 

<br />

## Tips:

If you have a card reader that automatically converts 5v to 3.3v apparently the `miso` line will spit out 5v to the Pico, which is not tolerant of 5v. A solution to this is to completely remove the level shifter from the card and short `Vin` to `Vout` where the level shifter was, as illustrated below. This will allow you to connect the reader directly to 3.3v.

![example image](https://i.imgur.com/cGMl2l3.jpg "level shifter removed and shorted")

Another solution that utilizes a micro sd card adapter can be found [here](https://www.raspberrypi.org/forums/viewtopic.php?f=146&t=307275#p1838662)

If you are using a Pimoroni Pico Explorer and the type of card reader depicted in the image above, using the Explorer's `SPI` breakout pins will be futile. Your display will stop working while the reader is plugged in. The complications are multiplied because the Explorer uses all of the `SPI1` capable pins for the motor drivers and it's buttons. My solution to this requires some hacking. By soldering female headers across pins 8 through 11 you can bypass the motor drivers and use `SPI1` for the card reader. It's not illustrated in the below image but I also broke off the male header pins just to make sure that there was no connection at all to the motor drivers. I soldered a female header onto the 3.3v pin because I'm already using the Explorer's breakout for something else. I just unplugged it all to simplify the image.

![example image](https://i.imgur.com/YR19ubJ.jpg "hacked")
