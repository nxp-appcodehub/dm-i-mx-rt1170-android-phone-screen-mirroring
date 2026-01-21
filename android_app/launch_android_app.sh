#
# Copyright 2024-2026 NXP
#
# SPDX-License-Identifier: Apache-2.0
#

#!/bin/bash

echo -e "===> Killing all running Android app instances (if any)...\n"
PIDS=$(adb shell "ps -A | grep app_process" | awk '{print $2}')

if [ -n "$PIDS" ]; then
  for PID in $PIDS; do
    adb shell "kill -9 $PID"
    echo -e "===> Process app_process with PID $PID has been killed.\n"
  done
else
  echo -e "===> No process named app_process found.\n"
fi

echo -e "\n===> Pushing the Android app into Android phone...\n"
adb push scrcpy-server /data/local/tmp/scrcpy-server.jar

echo -e "\n===> Launching the Android app from Android shell...\n"
adb shell "CLASSPATH=/data/local/tmp/scrcpy-server.jar nohup app_process / com.genymobile.scrcpy.Server 2.6.1 video=true control=true audio=false cleanup=true show_touches=true video_source=display video_codec=mjpeg raw_stream=true max_fps=15 &"

adb shell "ps -A | grep app_process"
