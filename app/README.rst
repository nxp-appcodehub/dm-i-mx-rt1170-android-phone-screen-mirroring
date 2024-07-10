.. zephyr:code-sample:: video-scrmirror
   :name: screen mirror
   :relevant-api: display_interface video_interface bsd_sockets

   Mirror a PC screen onto an MCU LCD display

Description
***********

This sample application mirrors a (Windows/Linux) PC screen onto an MCU touchscreen

Requirements
************

This samples requires an LCD display and currently supports i.MX RT1170 and RT1064 EVKs

- :ref:`mimxrt1170_evk`

- :ref:`mimxrt1064_evk`


Wiring
******

On :ref:`mimxrt1170_evk`,

On :ref:`mimxrt1064_evk`,

Building and Running
********************

For :ref:`mimxrt1170_evk`, build this sample application with the following commands:

.. zephyr-app-commands::
   :zephyr-app: samples/subsys/video/scrmirror
   :board: mimxrt1170_evk/mimxrt1176/cm7
   :shield: rk055hdmipi4ma0
   :gen-args: -DEXTRA_DTC_OVERLAY_FILE=nxp,enet1g.overlay
   :goals: build
   :compact:

For :ref:`mimxrt1064_evk`, build this sample application with the following commands:

.. zephyr-app-commands::
   :zephyr-app: samples/subsys/video/scrmirror
   :board: mimxrt1064_evk
   :shield: rk043fn66hs_ctg
   :goals: build
   :compact:

Sample Output
=============

.. code-block:: console

    [00:00:00.268,000] <inf> net_config: Initializing network
    [00:00:00.269,000] <inf> net_config: IPv4 address: 192.0.2.1

    Display device: display-controller@40804000
    - Capabilities:
      x_resolution = 720, y_resolution = 1280, supported_pixel_formats = 40
      current_pixel_format = 32, current_orientation = 0

    TCP: Waiting for client...
    [00:00:03.957,000] <inf> phy_rt_rtl8211f: PHY 1 is up
    [00:00:03.957,000] <inf> phy_rt_rtl8211f: PHY (1) Link speed 1000 Mb, full duplex
    [00:00:03.957,000] <inf> eth_nxp_enet_mac: Link is up
    TCP: Accepted connection

    Receiving frame 0


Then from a peer on the same network you can connect and send screen capture frames.

Example with gstreamer on a Linux (ximagesrc) / Windows PC (d3d11screencapturesrc):

.. code-block:: console

    gst-launch-1.0 ximagesrc ! queue ! videoscale ! video/x-raw,width=1280,height=720 \
        ! videoflip method=counterclockwise ! tcpclientsink host=192.0.2.1 port=5000 \

References
**********
