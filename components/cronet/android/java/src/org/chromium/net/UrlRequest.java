// Copyright 2014 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

package org.chromium.net;

import org.apache.http.conn.ConnectTimeoutException;
import org.chromium.base.CalledByNative;
import org.chromium.base.JNINamespace;

import java.io.IOException;
import java.net.MalformedURLException;
import java.net.URL;
import java.net.UnknownHostException;
import java.nio.ByteBuffer;
import java.nio.channels.ReadableByteChannel;
import java.nio.channels.WritableByteChannel;
import java.util.HashMap;
import java.util.Map;
import java.util.Map.Entry;
import java.util.concurrent.Semaphore;

/**
 * Network request using the native http stack implementation.
 */
@JNINamespace("cronet")
public class UrlRequest {
    private static final class ContextLock {
    }

    private static final int UPLOAD_BYTE_BUFFER_SIZE = 32768;

    private final UrlRequestContext mRequestContext;
    private final String mUrl;
    private final int mPriority;
    private final Map<String, String> mHeaders;
    private final WritableByteChannel mSink;
    private Map<String, String> mAdditionalHeaders;
    private boolean mPostBodySet;
    private ReadableByteChannel mPostBodyChannel;
    private WritableByteChannel mOutputChannel;
    private IOException mSinkException;
    private volatile boolean mStarted;
    private volatile boolean mCanceled;
    private volatile boolean mRecycled;
    private volatile boolean mFinished;
    private String mContentType;
    private long mContentLength;
    private Semaphore mAppendChunkSemaphore;
    private final ContextLock mLock;

    /**
     * Native peer object, owned by UrlRequest.
     */
    private long mUrlRequestPeer;

    /**
     * Constructor.
     *
     * @param requestContext The context.
     * @param url The URL.
     * @param priority Request priority, e.g. {@link #REQUEST_PRIORITY_MEDIUM}.
     * @param headers HTTP headers.
     * @param sink The output channel into which downloaded content will be
     *            written.
     */
    public UrlRequest(UrlRequestContext requestContext, String url,
            int priority, Map<String, String> headers,
            WritableByteChannel sink) {
        if (requestContext == null) {
            throw new NullPointerException("Context is required");
        }
        if (url == null) {
            throw new NullPointerException("URL is required");
        }
        mRequestContext = requestContext;
        mUrl = url;
        mPriority = priority;
        mHeaders = headers;
        mSink = sink;
        mLock = new ContextLock();
        mUrlRequestPeer = nativeCreateRequestPeer(
                mRequestContext.getUrlRequestContextPeer(), mUrl, mPriority);
        mPostBodySet = false;
    }

    /**
     * Adds a request header.
     */
    public void addHeader(String header, String value) {
        validateNotStarted();
        if (mAdditionalHeaders == null) {
            mAdditionalHeaders = new HashMap<String, String>();
        }
        mAdditionalHeaders.put(header, value);
    }

    /**
     * Sets data to upload as part of a POST request.
     *
     * @param contentType MIME type of the post content or null if this is not a
     *            POST.
     * @param data The content that needs to be uploaded if this is a POST
     *            request.
     */
    public void setUploadData(String contentType, byte[] data) {
        synchronized (mLock) {
            validateNotStarted();
            validatePostBodyNotSet();
            nativeSetPostData(mUrlRequestPeer, contentType, data);
            mPostBodySet = true;
        }
    }

    /**
     * Sets a readable byte channel to upload as part of a POST request.
     *
     * @param contentType MIME type of the post content or null if this is not a
     *            POST request.
     * @param channel The channel to read to read upload data from if this is a
     *            POST request.
     */
    public void setUploadChannel(String contentType,
            ReadableByteChannel channel) {
        synchronized (mLock) {
            validateNotStarted();
            validatePostBodyNotSet();
            nativeBeginChunkedUpload(mUrlRequestPeer, contentType);
            mPostBodyChannel = channel;
            mPostBodySet = true;
        }
        mAppendChunkSemaphore = new Semaphore(0);
    }

    public WritableByteChannel getSink() {
        return mSink;
    }

    public void start() {
        synchronized (mLock) {
            if (mCanceled) {
                return;
            }

            validateNotStarted();
            validateNotRecycled();

            mStarted = true;

            if (mHeaders != null && !mHeaders.isEmpty()) {
                for (Entry<String, String> entry : mHeaders.entrySet()) {
                    nativeAddHeader(mUrlRequestPeer, entry.getKey(),
                            entry.getValue());
                }
            }

            if (mAdditionalHeaders != null && !mAdditionalHeaders.isEmpty()) {
                for (Entry<String, String> entry :
                        mAdditionalHeaders.entrySet()) {
                    nativeAddHeader(mUrlRequestPeer, entry.getKey(),
                            entry.getValue());
                }
            }

            nativeStart(mUrlRequestPeer);
        }

        if (mPostBodyChannel != null) {
            uploadFromChannel(mPostBodyChannel);
        }
    }

    /**
     * Uploads data from a {@code ReadableByteChannel} using chunked transfer
     * encoding. The native call to append a chunk is asynchronous so a
     * semaphore is used to delay writing into the buffer again until chromium
     * is finished with it.
     *
     * @param channel the channel to read data from.
     */
    private void uploadFromChannel(ReadableByteChannel channel) {
        ByteBuffer buffer = ByteBuffer.allocateDirect(UPLOAD_BYTE_BUFFER_SIZE);

        // The chromium API requires us to specify in advance if a chunk is the
        // last one. This extra ByteBuffer is needed to peek ahead and check for
        // the end of the channel.
        ByteBuffer checkForEnd = ByteBuffer.allocate(1);

        try {
            boolean lastChunk;
            do {
                // First dump in the one byte we read to check for the end of
                // the channel. (The first time through the loop the checkForEnd
                // buffer will be empty).
                checkForEnd.flip();
                buffer.clear();
                buffer.put(checkForEnd);
                checkForEnd.clear();

                channel.read(buffer);
                lastChunk = channel.read(checkForEnd) <= 0;
                buffer.flip();
                nativeAppendChunk(mUrlRequestPeer, buffer, buffer.limit(),
                        lastChunk);

                if (lastChunk) {
                    break;
                }

                // Acquire permit before writing to the buffer again to ensure
                // chromium is done with it.
                mAppendChunkSemaphore.acquire();
            } while (!lastChunk && !mFinished);
        } catch (IOException e) {
            mSinkException = e;
            cancel();
        } catch (InterruptedException e) {
            mSinkException = new IOException(e);
            cancel();
        } finally {
            try {
                mPostBodyChannel.close();
            } catch (IOException ignore) {
                ;
            }
        }
    }

    public void cancel() {
        synchronized (mLock) {
            if (mCanceled) {
                return;
            }

            mCanceled = true;

            if (!mRecycled) {
                nativeCancel(mUrlRequestPeer);
            }
        }
    }

    public boolean isCanceled() {
        synchronized (mLock) {
            return mCanceled;
        }
    }

    public boolean isRecycled() {
        synchronized (mLock) {
            return mRecycled;
        }
    }

    /**
     * Returns an exception if any, or null if the request was completed
     * successfully.
     */
    public IOException getException() {
        if (mSinkException != null) {
            return mSinkException;
        }

        validateNotRecycled();

        int errorCode = nativeGetErrorCode(mUrlRequestPeer);
        switch (errorCode) {
            case UrlRequestError.SUCCESS:
                return null;
            case UrlRequestError.UNKNOWN:
                return new IOException(nativeGetErrorString(mUrlRequestPeer));
            case UrlRequestError.MALFORMED_URL:
                return new MalformedURLException("Malformed URL: " + mUrl);
            case UrlRequestError.CONNECTION_TIMED_OUT:
                return new ConnectTimeoutException("Connection timed out");
            case UrlRequestError.UNKNOWN_HOST:
                String host;
                try {
                    host = new URL(mUrl).getHost();
                } catch (MalformedURLException e) {
                    host = mUrl;
                }
                return new UnknownHostException("Unknown host: " + host);
            default:
                throw new IllegalStateException(
                        "Unrecognized error code: " + errorCode);
        }
    }

    public int getHttpStatusCode() {
        return nativeGetHttpStatusCode(mUrlRequestPeer);
    }

    /**
     * Content length as reported by the server. May be -1 or incorrect if the
     * server returns the wrong number, which happens even with Google servers.
     */
    public long getContentLength() {
        return mContentLength;
    }

    public String getContentType() {
        return mContentType;
    }

    /**
     * A callback invoked when appending a chunk to the request has completed.
     */
    @CalledByNative
    protected void onAppendChunkCompleted() {
        mAppendChunkSemaphore.release();
    }

    /**
     * A callback invoked when the first chunk of the response has arrived.
     */
    @CalledByNative
    protected void onResponseStarted() {
        mContentType = nativeGetContentType(mUrlRequestPeer);
        mContentLength = nativeGetContentLength(mUrlRequestPeer);
    }

    /**
     * A callback invoked when the response has been fully consumed.
     */
    protected void onRequestComplete() {
    }

    /**
     * Consumes a portion of the response.
     *
     * @param byteBuffer The ByteBuffer to append. Must be a direct buffer, and
     *            no references to it may be retained after the method ends, as
     *            it wraps code managed on the native heap.
     */
    @CalledByNative
    protected void onBytesRead(ByteBuffer byteBuffer) {
        try {
            while (byteBuffer.hasRemaining()) {
                mSink.write(byteBuffer);
            }
        } catch (IOException e) {
            mSinkException = e;
            cancel();
        }
    }

    /**
     * Notifies the listener, releases native data structures.
     */
    @SuppressWarnings("unused")
    @CalledByNative
    private void finish() {
        synchronized (mLock) {
            mFinished = true;
            if (mAppendChunkSemaphore != null) {
                mAppendChunkSemaphore.release();
            }

            if (mRecycled) {
                return;
            }
            try {
                mSink.close();
            } catch (IOException e) {
                // Ignore
            }
            onRequestComplete();
            nativeDestroyRequestPeer(mUrlRequestPeer);
            mUrlRequestPeer = 0;
            mRecycled = true;
        }
    }

    private void validateNotRecycled() {
        if (mRecycled) {
            throw new IllegalStateException("Accessing recycled request");
        }
    }

    private void validateNotStarted() {
        if (mStarted) {
            throw new IllegalStateException("Request already started");
        }
    }

    private void validatePostBodyNotSet() {
        if (mPostBodySet) {
            throw new IllegalStateException("Post Body already set");
        }
    }

    public String getUrl() {
        return mUrl;
    }

    private native long nativeCreateRequestPeer(long urlRequestContextPeer,
            String url, int priority);

    private native void nativeAddHeader(long urlRequestPeer, String name,
            String value);

    private native void nativeSetPostData(long urlRequestPeer,
            String contentType, byte[] content);

    private native void nativeBeginChunkedUpload(long urlRequestPeer,
            String contentType);

    private native void nativeAppendChunk(long urlRequestPeer, ByteBuffer chunk,
            int chunkSize, boolean isLastChunk);

    private native void nativeStart(long urlRequestPeer);

    private native void nativeCancel(long urlRequestPeer);

    private native void nativeDestroyRequestPeer(long urlRequestPeer);

    private native int nativeGetErrorCode(long urlRequestPeer);

    private native int nativeGetHttpStatusCode(long urlRequestPeer);

    private native String nativeGetErrorString(long urlRequestPeer);

    private native String nativeGetContentType(long urlRequestPeer);

    private native long nativeGetContentLength(long urlRequestPeer);
}
