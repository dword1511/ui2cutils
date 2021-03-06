Userspace I2C Utilities for Linux
=================================

These utilities let you use I2C peripherals without kernel driver.
Devices such as SSD1306 have kernel drivers that are hotplug-allergic.
A userspace tool is way safer, although has lower efficiency.

**CURRENTLY UNDER HEAVY DEVELOPMENT.**

**See [STATUS](STATUS) for detailed progresses.**

**Since I have little spare time now, expect inactivities.**

**Patches and PRs are welcome!**


Usage
=====

General
-------

* Identify a usable I2C master. See [I2C Masters](#masters).
* Scan it with `i2cdetect` tool from `i2c-tools` to confirm the devices are connected properly (so they are present).
  e.g. `i2cdetect -l` then `i2cdetect 1` for bus `i2c-1`.
* Compile and use the corresponding utilites for each device. You can compile individually by using `make [target]`.


I2C Masters
===========
<a name="masters"></a>

I2C on VGA port (DDC)
---------------------

Pinout:

    ----------------------
    \   5  4  3  2  1    /
     \   10 9  8  7  6  /
      \ 15 14 13 12 11 /
       ----------------

*  5: GND
*  9: +5V (not always available)
* 12: SDA
* 15: SCL

Tested on:

* Acer Aspire 4741G (proprietary driver, "NVIDIA i2c adapter 0 at 2:00.0", external power required as VGA only supplies ~ 1V)
* Dell Latitude E5430 ("i915 gmbus vga")

I2C on Beaglebone Black
-----------------------

AM335x does not support 400kHz I2C bus, thus no fast writes.
For `i2c-tools`, you want to use the "-r" flag.

Not tested yet.

I2C on Raspberry Pi
-------------------

Not tested yet.


Devices that will NOT get supported (for now)
=============================================

24xx EEPROM
-----------

The `eeprom` kernel module is time-proven, hotplug-safe and easy to use.
To register a device, simply do something like the following:

    echo eeprom 0x50 > /sys/bus/i2c/devices/i2c-x/new_device
    cat /sys/bus/i2c/devices/x-0050/eeprom | hd

For more details see [the kernel document](https://www.kernel.org/doc/Documentation/i2c/instantiating-devices).
Besides, it is quite easy to use `i2cdump` from `i2c-tools` to read it from userspace.

PN532 NFC
---------

The `libnfc` project provides many userspace tools to communicate with the chip over UART/I2C/SPI.
See [`libnfc` on github](https://github.com/nfc-tools/libnfc) for repository and
[Lady Ada's instructions](https://learn.adafruit.com/adafruit-pn532-rfid-nfc/libnfc) for a general tutorial,
and also [Libnfc:configuration](http://nfc-tools.org/index.php?title=Libnfc:configuration) for how to specify devices.
When scanning for device, you might need the `-i` (intrusive) flag.
The bad thing is that you can not control which I2C bus it scans.

MPU-9150 MEMS-IMU
-----------------

There is a [userspace tool](https://github.com/kriswiner/MPU-9150) that can control the IMU over I2C.
Tested with Beaglebone Black. The DMP firmware is embedded.
You may want to fix some of its algorithm or force it to print some low-level compass heading.
