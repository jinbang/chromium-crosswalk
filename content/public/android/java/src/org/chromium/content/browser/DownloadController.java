// Copyright (c) 2012 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

package org.chromium.content.browser;

import org.chromium.base.CalledByNative;
import org.chromium.base.JNINamespace;

import android.content.Context;

/**
 * Java counterpart of android DownloadController.
 *
 * Its a singleton class instantiated by the C++ DownloadController.
 */
@JNINamespace("content")
public class DownloadController {
    private static final String LOGTAG = "DownloadController";
    private static DownloadController sInstance;

    /**
     * Class for notifying the application that download has completed.
     */
    public interface DownloadNotificationService {
        /**
         * Notify the host application that a download is finished.
         * @param context Application context.
         * @param downloadInfo Information about the completed download.
         */
        void onDownloadCompleted(Context context, DownloadInfo downloadInfo);
    }

    private static DownloadNotificationService sDownloadNotificationService;

    @CalledByNative
    public static DownloadController getInstance() {
        if (sInstance == null) {
            sInstance = new DownloadController();
        }
        return sInstance;
    }

    private DownloadController() {
        nativeInit();
    }

    private static ContentViewDownloadDelegate downloadDelegateFromView(ContentViewCore view) {
        return view.getDownloadDelegate();
    }

    public static void setDownloadNotificationService(DownloadNotificationService service) {
        sDownloadNotificationService = service;
    }

    /**
     * Notifies the download delegate of a new GET download and passes all the information
     * needed to download the file.
     *
     * The download delegate is expected to handle the download.
     */
    @CalledByNative
    public void newHttpGetDownload(ContentViewCore view, String url,
            String userAgent, String contentDisposition, String mimeType,
            String cookie, String referer, long contentLength) {
        ContentViewDownloadDelegate downloadDelegate = downloadDelegateFromView(view);

        if (downloadDelegate != null) {
            DownloadInfo downloadInfo = new DownloadInfo.Builder()
                    .setUrl(url)
                    .setUserAgent(userAgent)
                    .setContentDisposition(contentDisposition)
                    .setMimeType(mimeType)
                    .setCookie(cookie)
                    .setReferer(referer)
                    .setContentLength(contentLength)
                    .setIsGETRequest(true)
                    .build();
            downloadDelegate.requestHttpGetDownload(downloadInfo);
        }
    }

    /**
     * Notifies the download delegate that a new download has started. This can
     * be either a POST download or a GET download with authentication.
     * @param view ContentViewCore associated with the download item.
     * @param filename File name of the downloaded file.
     * @param mimeType Mime of the downloaded item.
     */
    @CalledByNative
    public void onDownloadStarted(ContentViewCore view, String filename, String mimeType) {
        ContentViewDownloadDelegate downloadDelagate = downloadDelegateFromView(view);

        if (downloadDelagate != null) {
            downloadDelagate.onDownloadStarted(filename, mimeType);
        }
    }

    /**
     * Notifies the download delegate that a download completed and passes along info about the
     * download. This can be either a POST download or a GET download with authentication.
     */
    @CalledByNative
    public void onDownloadCompleted(Context context, String url, String mimeType,
            String filename, String path, long contentLength, boolean successful) {
        if (sDownloadNotificationService != null) {
            DownloadInfo downloadInfo = new DownloadInfo.Builder()
                    .setUrl(url)
                    .setMimeType(mimeType)
                    .setFileName(filename)
                    .setFilePath(path)
                    .setContentLength(contentLength)
                    .setIsSuccessful(successful)
                    .setDescription(filename)
                    .build();
            sDownloadNotificationService.onDownloadCompleted(context, downloadInfo);
        }
    }

    /**
     * Notifies the download delegate that a dangerous download started.
     */
    @CalledByNative
    public void onDangerousDownload(ContentViewCore view, String filename,
            int downloadId) {
        ContentViewDownloadDelegate downloadDelegate = downloadDelegateFromView(view);
        if (downloadDelegate != null) {
            downloadDelegate.onDangerousDownload(filename, downloadId);
        }
    }

    // native methods
    private native void nativeInit();
}
