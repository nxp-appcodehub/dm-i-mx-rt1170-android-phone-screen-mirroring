# NXP Application Code Hub
[<img src="https://mcuxpresso.nxp.com/static/icon/nxp-logo-color.svg" width="100"/>](https://www.nxp.com)

## i.MX RT1170 Mirror and control android phone using Zephyr

This demo demonstrates how to mirror the Android phone screen onto the RT1170 LCD screen. The video is captured first by the Android application scrcpy, compressed in MJPEG format, and then transmitted over Ethernet/Wifi to the RT1170 board. The MCU decodes the compressed stream and renders it on the display. The demo also supports multi-touch control of the Android phone through the RT1170 touchscreen.


![Geneneral schematic](images/EOSS_2025_PhoneMirror_v1.4.jpeg)

#### Boards: MIMXRT1170-EVKB
#### Categories: RTOS, Graphics, Touch Sensing
#### Peripherals: DISPLAY, I2C, VIDEO, Wi-Fi
#### Toolchains: VS Code, GCC

## Table of Contents
1. [Software](#step1)
2. [Hardware](#step2)
3. [Setup](#step3)
4. [Results](#step4)
5. [FAQs](#step5) 
6. [Support](#step6)
7. [Release Notes](#step7)

## 1. Software<a name="step1"></a>
- [Zephyr 4.3.0](https://github.com/zephyrproject-rtos/zephyr/releases/tag/v4.3.0) - Zephyr RTOS
- [scrcpy](https://github.com/Genymobile/scrcpy) - Android screen mirroring application


## 2. Hardware<a name="step2"></a>

### 2.1 Required Components
- **[MIMXRT1170-EVKB](https://www.nxp.com/design/design-center/development-boards/i-mx-evaluation-and-development-boards/i-mx-rt1170-evaluation-kit:MIMXRT1170-EVK)** - i.MX RT1170 Evaluation Kit (Rev B)
- **[RK055HDMIPI4MA0](https://www.nxp.com/part/RK055HDMIPI4MA0)** - 5.5" MIPI LCD display panel with touchscreen
- **Android Smartphone** - Android device with USB debugging enabled
- **USB Type-C Cable** - For connecting Android phone to PC/laptop (running scrcpy)

### 2.2 Additional Components (Choose One Option)

#### Option A: Ethernet (Wired Connection)
- **Ethernet Cable** - Standard RJ45 network cable
- **USB Type-C to Ethernet Adapter** - For connecting Android phone to network via Ethernet

#### Option B: Wi-Fi (Wireless Connection)
- **[IW612-MURATA-2EL-M.2](https://www.nxp.com/products/wireless-connectivity/wi-fi-plus-bluetooth-plus-802-15-4/2-4-5-ghz-dual-band-1x1-wi-fi-6-802-11ax-plus-bluetooth-5-4-plus-802-15-4-tri-radio-solution:IW612)** - Wi-Fi 6 + Bluetooth 5.4 M.2 module

> **Note**: The Ethernet option provides more stable performance, while the Wi-Fi option offers greater mobility and convenience.

## 3. Setup<a name="step3"></a>
### 3.1 Hardware Setup
#### 3.1.1 LCD Display
Attach the screen to the RT1170 board to the MIPI DSI connector (J48)
#### 3.1.2 Ethernet Connection or Wi-Fi Module
Connect the board ethernet to the Android phone using USB Type-C to ethernet adapter.
or
In the case of Wi-Fi, insert the IW612-MURATA-2EL-M.2 module into the M.2 slot (J23) on the RT1170 board.
#### 3.3 Debug Connection
Connect the board micro usb connector (J86) to the PC for the debugging using LinkServer and serial console output.

#### 3.4 Power Connection
Plug in the power adapter (5V, 2A) to the RT1170 board power connector (J43).

### 3.2 Software Setup
#### 3.2.1 Zephyr Side
#### 3.2.1.1 Install Zephyr and Dependencies
 - If you are not familar with Zephyr, follow this [guide] to first install the west tool and its dependencies as well as the Zephyr SDK.

 - Once the Zephyr SDK and west are installed and set up, follow the next steps (described [here](https://docs.zephyrproject.org/latest/develop/application/index.html#advanced-example-application-usage)) to obtain the source code and setup the Zephyr environment.
#### 3.2.1.2 Cloning and building with CLI
 ```bash
west init my-workspace -m https://github.com/nxp-appcodehub/dm-i-mx-rt1170-android-phone-screen-mirroring --mr main
cd my-workspace
setup environment variables:
        (for Windows) zephyr\zephyr-env.cmd
        (for Ubuntu) source zephyr/zephyr-env.sh
west update

west blobs fetch

 ```
 - After executing ```west update```, all of the application's core software blocks defined inside the west.yml manifest such as **hal_nxp, wifi middleware and drivers and the android application source code** will be downloaded and placed at their designated spots.

 - Build and flash the zephyr app with the following commands, the required bandwidth is only ~11 Mbps so you can use either Enet1G or Enet100M (remove the enet1g overlay)

```bash
west build -p -b mimxrt1170_evk@B/mimxrt1176/cm7 scrmirror/app -DOVERLAY_CONFIG=overlay-ethernet.conf -DEXTRA_DTC_OVERLAY_FILE=~/my-workspace/zephyr/boards/nxp/mimxrt1170_evk/dts/nxp,enet1g.overlay -DSHIELD='rk055hdmipi4ma0'

west flash
```

 - Ethernet over USB can be used instead of Ethernet connection by enabling the USB-ECM functionality on J20 of the RT1170 EVKB using the build command below. Note that this connection mode is currently less stable than the Ethernet:

 ```bash
west build -p -b mimxrt1170_evk@B/mimxrt1176/cm7 scrmirror/app -DOVERLAY_CONFIG=overlay-netusb.conf -DSHIELD='rk055hdmipi4ma0'

west flash
```

  - WiFi can also be used instead of Ethernet by enabling the WiFi IW612 module as an AP (Access Point) using the build command below. This connection is currently also less stable than Ethernet:

```bash
west build -p -b mimxrt1170_evk@B/mimxrt1176/cm7 scrmirror/app -DOVERLAY_CONFIG=overlay-wifi.conf -DSHIELD='rk055hdmipi4ma0'

west flash
```

[guide]: https://docs.zephyrproject.org/latest/develop/getting_started/index.html
[branch]: https://bitbucket.sw.nxp.com/projects/MMIOT/repos/zephyr/browse?at=refs%2Fheads%2Fscreen_mirror_demo
[repo]: https://bitbucket.sw.nxp.com/projects/MMIOT/repos/zephyr_ffmpeg/browse

### 3.2 Android side setup
 - Install Android SDK and adb tool, I recommmend installing Android [Studio] which contains both and other useful things.
 - Enable USB debugging mode on the phone.
 - Build and launch the Android app with adb and a (Linux) PC, just running this [script]:
 ```bash
 cd scrmirror/scrcpy/
./launch_scrcpy_server.sh
 ```
 - After launching the Android app, we need to do the follownig according to the selected network connection:
   - **Ethernet connection mode:** Plug the `USB Type-C Ethernet adapter` cable to the phone and the RT1170 to connect them via Ethernet.
   - **Ethernet over USB:** Connect the USB type C port of the phone to the USB J20 of RT1170 with a USB C to micro USB cable.
   - **WiFi:** Restart the board to ensure the correct initialization of the WiFi module and network. Then, refresh the available WiFi networks on the phone and connect it to the screen-mirror-wifi AP with following credential:

      ```
      SSID: screen-mirror-wifi
      Password: nxpdemo2025
      ```
    Finally, use mobile data for Internet connection.

## 4. Results<a name="step4"></a>
Expected console output on the Zephyr side:
```
[00:00:02.708,000] <inf> sd: Card does not support CMD8, assuming legacy card
[00:00:02.741,000] <inf> sd: Card switched to 1.8V signaling
STA MAC Address: 50:26:EF:B1:4A:FC
*** Booting Zephyr OS build v4.1.0-5028-gecdf66a55a78 ***
[00:00:04.697,000] <inf> main: Turning on AP Mode
[00:00:04.698,000] <inf> main: Protocol ua is selected

Display device: display-controller@40804000
- Capabilities:
  x_resolution = 720, y_resolution = 1280, supported_pixel_formats = 40
  current_pixel_format = 32, current_orientation = 0

Station connected: C6:8E:E7:C3:E6:3A
C6:8E:E7:C3:E6:3A C6:8E:E7:C3:E6:3A TCP: Waiting for client...
TCP: Accepted connection
[00:00:26.323,000] <inf> touch_report: Wait for control socket ...
[00:00:26.327,000] <inf> touch_report: TCP control socket is connected
[00:00:26.327,000] <inf> decode: Socket receiving started
[00:00:26.327,000] <inf> decode: Decoding thread started
```

Expected on the display and the mirrored screen:

![Expected on the display and the mirrored screen](images/demo_oss2025.png)

## 5. FAQs<a name="step5"></a>

### Q1: How long does it take to establish the connection?
**A:** It takes a few seconds to establish the connection between the Android phone and the RT1170 board. Please be patient during the initial handshake process.

### Q2: Why isn't my Android phone connecting via Ethernet?
**A:** When using the Ethernet option, you must temporarily disable other Internet modes (Wi-Fi, Cellular/5G) on your Android phone to ensure it connects to the RT1170 via Ethernet. Once the connection is established, you may re-enable these modes if needed.

### Q3: Why must I use `adb shell` to launch the application?
**A:** The Android application must be pushed and launched using `adb shell` to grant it the necessary privileged permissions to inject touch events into the Android system. Without these permissions, the touchscreen control feature will not function.

### Q4: What should I do if the connection is lost or the system hangs?
**A:** If the demo encounters issues (e.g., system hang, connection loss due to unplugged cable, phone sleep mode), you need to repush and relaunch the Android app by running the `build_launch_server` script (excluding the compilation section).

**Note:** While it's possible to implement automatic reconnection in the Android app, the proper solution would be for the Zephyr app to programmatically push, launch, and terminate the Android app via `adb` when the cable is connected/disconnected. However, since `adb` is not currently supported on MCU/Zephyr, manual intervention via PC is required.

### Q5: How do I properly terminate the demo?
**A:** After completing the demo, you must manually kill the Android application to prevent battery drain. This can be done by either:
- Restarting your Android phone, **or**
- Executing the first part of the `build_launch_server` script

### Q6: Can I use Wi-Fi and Ethernet simultaneously?
**A:** No, it's recommended to use only one connection method at a time for optimal performance and to avoid routing conflicts on the Android device.

### Q7: The screen is freezing what should I do ?
**A:** Sometimes, the Zephyr app could hang, therefore, the RTTT1170 board must be reset in this case. You can reset the board by pressing the reset button on the board or by power cycling it. After the reset, relaunch the Android app using the `build_launch_server` script.

### Q8: I cannot connect to the hotspot screen-mirror-demo, what should I do?
**A:** The wifi has some problem after flashing the board without resetting. You can reset the board by pressing the reset button on the board or by power cycling it. After the reset, you should be able to connect to the hotspot.

## 6. Support<a name="step6"></a>

#### Project Metadata

<!----- Boards ----->
[![Board badge](https://img.shields.io/badge/Board-MIMXRT1170&ndash;EVKB-blue)]()

<!----- Categories ----->
[![Category badge](https://img.shields.io/badge/Category-RTOS-yellowgreen)](https://mcuxpresso.nxp.com/appcodehub?category=rtos)
[![Category badge](https://img.shields.io/badge/Category-GRAPHICS-yellowgreen)](https://mcuxpresso.nxp.com/appcodehub?category=graphics)
[![Category badge](https://img.shields.io/badge/Category-TOUCH%20SENSING-yellowgreen)](https://mcuxpresso.nxp.com/appcodehub?category=touch_sensing)

<!----- Peripherals ----->
[![Peripheral badge](https://img.shields.io/badge/Peripheral-DISPLAY-yellow)](https://mcuxpresso.nxp.com/appcodehub?peripheral=display)
[![Peripheral badge](https://img.shields.io/badge/Peripheral-I2C-yellow)](https://mcuxpresso.nxp.com/appcodehub?peripheral=i2c)
[![Peripheral badge](https://img.shields.io/badge/Peripheral-VIDEO-yellow)](https://mcuxpresso.nxp.com/appcodehub?peripheral=video)
[![Peripheral badge](https://img.shields.io/badge/Peripheral-WI&ndash;FI-yellow)](https://mcuxpresso.nxp.com/appcodehub?peripheral=wifi)

<!----- Toolchains ----->
[![Toolchain badge](https://img.shields.io/badge/Toolchain-VS%20CODE-orange)](https://mcuxpresso.nxp.com/appcodehub?toolchain=vscode)
[![Toolchain badge](https://img.shields.io/badge/Toolchain-GCC-orange)](https://mcuxpresso.nxp.com/appcodehub?toolchain=gcc)

Questions regarding the content/correctness of this example can be entered as Issues within this GitHub repository.

>**Warning**: For more general technical questions regarding NXP Microcontrollers and the difference in expected functionality, enter your questions on the [NXP Community Forum](https://community.nxp.com/)

[![Follow us on Youtube](https://img.shields.io/badge/Youtube-Follow%20us%20on%20Youtube-red.svg)](https://www.youtube.com/NXP_Semiconductors)
[![Follow us on LinkedIn](https://img.shields.io/badge/LinkedIn-Follow%20us%20on%20LinkedIn-blue.svg)](https://www.linkedin.com/company/nxp-semiconductors)
[![Follow us on Facebook](https://img.shields.io/badge/Facebook-Follow%20us%20on%20Facebook-blue.svg)](https://www.facebook.com/nxpsemi/)
[![Follow us on Twitter](https://img.shields.io/badge/X-Follow%20us%20on%20X-black.svg)](https://x.com/NXP)

## 7. Release Notes<a name="step7"></a>
| Version | Description / Update                           | Date                        |
|:-------:|------------------------------------------------|----------------------------:|
| 1.0     | Initial release on Application Code Hub        | January 30<sup>th</sup> 2026 |

## Licensing
Apache 2.0 License - See LICENSE file for details.

## Origin
This project use following open source library:

[JPEGDEC](https://github.com/bitbank2/JPEGDEC) - JPEG decoder library by Larry Bank (Apache 2.0 License)


[Studio]: https://developer.android.com/studio
[Android]: https://bitbucket.sw.nxp.com/projects/MMIOT/repos/scrcpy/commits?until=refs%2Fheads%2Fscreen_mirror_demo
[script]: https://bitbucket.sw.nxp.com/projects/MMIOT/repos/scrcpy/browse/server/scripts/build_launch_server.sh?at=refs%2Fheads%2Fscreen_mirror_demo