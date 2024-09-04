/*
 * Copyright 2024-2026 NXP
 *
 * SPDX-License-Identifier: Apache-2.0
 */

package com.genymobile.scrcpy.device;

import android.os.Handler;
import android.os.HandlerThread;
import android.os.SystemClock;

import com.genymobile.scrcpy.control.ControlChannel;
import com.genymobile.scrcpy.util.Codec;
import com.genymobile.scrcpy.util.Ln;
import com.genymobile.scrcpy.util.StringUtils;

import java.io.Closeable;
import java.io.FileDescriptor;
import java.io.IOException;
import java.io.InputStream;
import java.io.OutputStream;
import java.net.InetSocketAddress;
import java.net.Socket;
import java.nio.ByteBuffer;
import java.nio.charset.StandardCharsets;

public final class RemoteDirectConnection implements Closeable {
    private static final int DEVICE_NAME_FIELD_LENGTH = 64;
    // TODO : Move these at a better place
    private static final String SERVER_IP = "192.0.2.1";
    private static final int SERVER_PORT = 5000;
    private static final int CONNECTION_TIMEOUT = 3000;

    // TODO : Do not use static variables
    private static Socket videoSocket;
    private static Socket controlSocket;
    private static OutputStream videoOutputStream;
    private static InputStream controlInputStream;
    private static OutputStream controlOutputStream;
    private static ControlChannel controlChannel;

    private static HandlerThread handlerThread;
    private static Handler clientHandler;
    private static boolean isRunning = false;

    private final Codec codec;
    private final boolean sendCodecMeta;
    private final boolean sendFrameMeta;

    private RemoteDirectConnection(Codec codec, boolean sendCodecMeta, boolean sendFrameMeta) {
        this.codec = codec;
        this.sendCodecMeta = sendCodecMeta;
        this.sendFrameMeta = sendFrameMeta;

        if (handlerThread == null || !handlerThread.isAlive()) {
            handlerThread = new HandlerThread("RemoteDirectConnectionThread");
            handlerThread.start();
            clientHandler = new Handler(handlerThread.getLooper());
            isRunning = true;
        }
    }

    public static RemoteDirectConnection open(boolean video, boolean audio, boolean control, boolean sendDummyByte, Codec codec, boolean sendCodecMeta, boolean sendFrameMeta) throws IOException {
        while (true) {
            try {
                Ln.i("Trying to connect to remote host ...");
                if (video) {
                    videoSocket = new Socket();
                    videoSocket.connect(new InetSocketAddress(SERVER_IP, SERVER_PORT), CONNECTION_TIMEOUT);
                    videoOutputStream = videoSocket.getOutputStream();

                    if (sendDummyByte) {
                        // send one byte so the client may read() to detect a connection error
                        videoOutputStream.write(0);
                        sendDummyByte = false;
                    }
                    Ln.i("Video socket is connected to remote host");
                }
                if (control) {
                    controlSocket = new Socket();
                    controlSocket.connect(new InetSocketAddress(SERVER_IP, SERVER_PORT), CONNECTION_TIMEOUT);
                    controlInputStream = controlSocket.getInputStream();
                    controlOutputStream = controlSocket.getOutputStream();

                    controlChannel = new ControlChannel(controlInputStream, controlOutputStream);

                    if (sendDummyByte) {
                        // send one byte so the client may read() to detect a connection error
                        controlOutputStream.write(0);
                    }
                    Ln.i("Control socket is connected to remote host");
                }
                break;
            } catch (IOException e) {
                Ln.e("Failed to connect to remote host, will retry", e);
                if (controlInputStream != null) {
                    controlInputStream.close();
                }
                if (controlOutputStream != null) {
                    controlOutputStream.close();
                }
                if (controlSocket != null) {
                    controlSocket.close();
                }
                if (videoOutputStream != null) {
                    videoOutputStream.close();
                }
                if (videoSocket != null) {
                    videoSocket.close();
                }
                SystemClock.sleep(1000);
            }
        }
        return new RemoteDirectConnection(codec, sendCodecMeta, sendFrameMeta);
    }

    public void sendDeviceMeta(String deviceName) {
        byte[] buffer = new byte[DEVICE_NAME_FIELD_LENGTH];

        byte[] deviceNameBytes = deviceName.getBytes(StandardCharsets.UTF_8);
        int len = StringUtils.getUtf8TruncationIndex(deviceNameBytes, DEVICE_NAME_FIELD_LENGTH - 1);
        System.arraycopy(deviceNameBytes, 0, buffer, 0, len);
        // byte[] are always 0-initialized in java, no need to set '\0' explicitly

        sendRawBytes(buffer);
    }

    public void sendVideoHeader(Size videoSize) {
        if (sendCodecMeta) {
            ByteBuffer buffer = ByteBuffer.allocate(12);
            buffer.putInt(codec.getId());
            buffer.putInt(videoSize.getWidth());
            buffer.putInt(videoSize.getHeight());
            buffer.flip();

            sendRawBytes(buffer.array());
        }
    }

    public void sendRawBytes(final byte[] bytes) {
        if (clientHandler != null && isRunning) {
            clientHandler.post(() -> {
                try {
                    if (videoOutputStream != null) {
                        videoOutputStream.write(bytes);
                        videoOutputStream.flush();
                    }
                } catch (IOException e) {
                    Ln.e("Failed to send raw bytes", e);
                    close();
                }
            });
        } else {
            Ln.i("Client handler is not running or null");
        }
    }

    public ControlChannel getControlChannel() {
        return controlChannel;
    }
    // Not used
    public FileDescriptor getVideoFd() {
        return null;
    }
    // Not used
    public FileDescriptor getAudioFd() {
        return null;
    }

    public void shutdown() throws IOException {
        if (videoSocket != null) {
            videoSocket.shutdownInput();
            videoSocket.shutdownOutput();
        }

        if (controlSocket != null) {
            controlSocket.shutdownInput();
            controlSocket.shutdownOutput();
        }
    }

    public void close() {
        isRunning = false;
        try {
            if (controlInputStream != null) {
                controlInputStream.close();
            }
            if (controlOutputStream != null) {
                controlOutputStream.close();
            }
            if (videoOutputStream != null) {
                videoOutputStream.close();
            }
            if (controlSocket != null) {
                controlSocket.close();
            }
            if (videoSocket != null) {
                videoSocket.close();
            }
        } catch (IOException e) {
            Ln.e("Failed to close the remote connection", e);
        } finally {
            handlerThread.quitSafely();
        }
    }
}
