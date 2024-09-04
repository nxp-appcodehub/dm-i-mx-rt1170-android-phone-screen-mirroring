#
# Copyright 2024-2026 NXP
#
# SPDX-License-Identifier: Apache-2.0
#

#!/bin/bash

echo -e "===> Killing all running server app instances (if any)...\n"
PIDS=$(adb shell "ps -A | grep app_process" | awk '{print $2}')

if [ -n "$PIDS" ]; then
  for PID in $PIDS; do
    adb shell "kill -9 $PID"
    echo -e "===> Process app_process with PID $PID has been killed.\n"
  done
else
  echo -e "===> No process named app_process found.\n"
fi

echo -e "===> Cleaning build folder...\n"
rm -rf x/

echo -e "===> Compiling the server app...\n"
export ANDROID_SDK_ROOT=~/Android/Sdk
meson setup x --buildtype=release --strip -Db_lto=true
ninja -Cx

echo -e "\n===> Pushing the server app into Android phone...\n"
adb push x/server/scrcpy-server /data/local/tmp/scrcpy-server.jar

echo -e "\n===> Launching the server app from Android shell...\n"
adb shell "CLASSPATH=/data/local/tmp/scrcpy-server.jar nohup app_process / com.genymobile.scrcpy.Server 2.6.1 video=true control=true audio=false cleanup=true show_touches=true video_source=display video_codec=mjpeg raw_stream=true max_fps=11 &"

adb shell "ps -A | grep app_process"

