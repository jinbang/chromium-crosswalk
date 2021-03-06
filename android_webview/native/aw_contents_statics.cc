// Copyright 2014 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "android_webview/native/aw_contents_statics.h"

#include "android_webview/browser/aw_browser_context.h"
#include "base/android/jni_string.h"
#include "base/android/scoped_java_ref.h"
#include "base/callback.h"
#include "components/data_reduction_proxy/browser/data_reduction_proxy_settings.h"
#include "content/public/browser/browser_thread.h"
#include "jni/AwContentsStatics_jni.h"
#include "net/cert/cert_database.h"

using base::android::AttachCurrentThread;
using base::android::ConvertJavaStringToUTF8;
using base::android::ScopedJavaGlobalRef;
using content::BrowserThread;
using data_reduction_proxy::DataReductionProxySettings;

namespace android_webview {

namespace {

void ClientCertificatesCleared(ScopedJavaGlobalRef<jobject>* callback) {
  DCHECK_CURRENTLY_ON(BrowserThread::UI);
  JNIEnv* env = AttachCurrentThread();
  Java_AwContentsStatics_clientCertificatesCleared(env, callback->obj());
}

void NotifyClientCertificatesChanged() {
  DCHECK_CURRENTLY_ON(BrowserThread::IO);
  net::CertDatabase::GetInstance()->OnAndroidKeyStoreChanged();
}

}  // namespace

// static
void ClearClientCertPreferences(JNIEnv* env, jclass, jobject callback) {
  DCHECK_CURRENTLY_ON(content::BrowserThread::UI);
  ScopedJavaGlobalRef<jobject>* j_callback = new ScopedJavaGlobalRef<jobject>();
  j_callback->Reset(env, callback);
  BrowserThread::PostTaskAndReply(
      BrowserThread::IO,
      FROM_HERE,
      base::Bind(&NotifyClientCertificatesChanged),
      base::Bind(&ClientCertificatesCleared, base::Owned(j_callback)));
}

// static
void SetDataReductionProxyKey(JNIEnv* env, jclass, jstring key) {
  AwBrowserContext* browser_context = AwBrowserContext::GetDefault();
  DCHECK(browser_context);
  DataReductionProxySettings* drp_settings =
      browser_context->GetDataReductionProxySettings();
  DCHECK(drp_settings);
  drp_settings->set_key(ConvertJavaStringToUTF8(env, key));
}

// static
void SetDataReductionProxyEnabled(JNIEnv* env, jclass, jboolean enabled) {
  AwBrowserContext::SetDataReductionProxyEnabled(enabled);
}

bool RegisterAwContentsStatics(JNIEnv* env) {
  return RegisterNativesImpl(env);
}

}  // namespace android_webview
