/*
 * Copyright 2024-2026 NXP
 *
 * SPDX-License-Identifier: Apache-2.0
 */

package com.genymobile.scrcpy.video;

import android.graphics.Bitmap;
import android.graphics.PixelFormat;
import android.media.Image;
import android.media.ImageReader;
import android.os.Handler;
import android.os.HandlerThread;
import android.view.Surface;

import com.genymobile.scrcpy.AsyncProcessor;
import com.genymobile.scrcpy.device.RemoteDirectConnection;
import com.genymobile.scrcpy.util.IO;
import com.genymobile.scrcpy.util.Ln;

import java.io.ByteArrayOutputStream;
import java.io.IOException;
import java.nio.ByteBuffer;
import java.util.concurrent.atomic.AtomicBoolean;

public class JpegCompressor implements AsyncProcessor {
    // This should be set according to the remote receiver's decoding capacity
    private final int maxFps;
    private long lastFrameTime = 0;

    // TODO: Get these from MCU client via UDP
    private static final int DESIRED_WIDTH = 720;
    private static final int DESIRED_HEIGHT = 1280;

    private final SurfaceCapture capture;
    private final RemoteDirectConnection remoteDirectConnection;

    private ImageReader imageReader;
    private HandlerThread handlerThread;

    private final AtomicBoolean stopped = new AtomicBoolean();

    public JpegCompressor(SurfaceCapture capture, RemoteDirectConnection remoteDirectConnection, int maxFps) {
        this.capture = capture;
        this.remoteDirectConnection = remoteDirectConnection;
        this.maxFps = maxFps;

        this.handlerThread = new HandlerThread("JpegCompressor");
        handlerThread.start();
    }

    private void streamCapture() throws IOException {
        int captureWidth = capture.getSize().getWidth();
        int captureHeight = capture.getSize().getHeight();
        imageReader = ImageReader.newInstance(captureWidth, captureHeight, PixelFormat.RGBA_8888, 2);
        imageReader.setOnImageAvailableListener(listener -> {
            Image image = null;
            ByteArrayOutputStream baos = null;
            try {
                image = imageReader.acquireLatestImage();
                if (image != null) {
                    baos = new ByteArrayOutputStream();
                    // Skip frames that exceed the MCU client frame rate
                    long currentTime = System.currentTimeMillis();
                    if ((currentTime - lastFrameTime) < (1000 / maxFps)) {
                        image.close();
                        return;
                    }

                    Image.Plane[] planes = image.getPlanes();
                    ByteBuffer buffer = planes[0].getBuffer();
                    int pixelStride = planes[0].getPixelStride();
                    int rowStride = planes[0].getRowStride();
                    int rowPadding = rowStride - pixelStride * captureWidth;

                    Bitmap bitmap = Bitmap.createBitmap(captureWidth + rowPadding / pixelStride, captureHeight, Bitmap.Config.ARGB_8888);
                    bitmap.copyPixelsFromBuffer(buffer);

                    Bitmap scaled_bitmap = Bitmap.createScaledBitmap(bitmap, DESIRED_WIDTH, DESIRED_HEIGHT, true);

                    scaled_bitmap.compress(Bitmap.CompressFormat.JPEG, 80, baos);

                    remoteDirectConnection.sendRawBytes(baos.toByteArray());
                    lastFrameTime = currentTime;
                }
            } finally {
                if (image != null) {
                    image.close();
                }
                if (baos != null) {
                    try {
                        baos.close();
                    } catch (IOException e) {
                        Ln.e("Failed to close ByteArrayOutputStream", e);
                    }
                }
            }
        }, new Handler(handlerThread.getLooper()));

        capture.init();

        try {
            remoteDirectConnection.sendVideoHeader(capture.getSize());

            boolean alive = true;
            do {
                Surface surface = null;
                try {
                    surface = imageReader.getSurface();
                    capture.start(surface);

                    while (!capture.consumeReset()) {
                        if (stopped.get()) {
                            alive = false;
                            break;
                        }
                        if (capture.consumeReset()) {
                            // must restart encoding with new size
                            break;
                        }
                    }
                    if (capture.isClosed()) {
                        // The capture might have been closed internally (for example if the camera is disconnected)
                        alive = false;
                    }
                } finally {
                    if (surface != null) {
                        surface.release();
                    }
                }
            } while (alive);
        } finally {
            imageReader.close();
            capture.release();
        }
    }

    @Override
    public void start(TerminationListener listener) {
        try {
            streamCapture();
        } catch (IOException e) {
            // Broken pipe is expected on close, because the socket is closed by the client
            if (!IO.isBrokenPipe(e)) {
                Ln.e("Video encoding error", e);
            }
        } finally {
            Ln.d("Screen streaming stopped");
            listener.onTerminated(true);
        }
    }

    @Override
    public void stop() {
        if (handlerThread != null) {
            stopped.set(true);
        }
    }

    @Override
    public void join() throws InterruptedException {
        if (handlerThread != null) {
            handlerThread.join();
        }
    }
}
