// Copyright 2013 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef MEDIA_BASE_ANDROID_AUDIO_DECODER_JOB_H_
#define MEDIA_BASE_ANDROID_AUDIO_DECODER_JOB_H_

#include <jni.h>

#include "media/base/android/media_decoder_job.h"

namespace media {

class AudioCodecBridge;
class AudioTimestampHelper;

// Class for managing audio decoding jobs.
class AudioDecoderJob : public MediaDecoderJob {
 public:
  virtual ~AudioDecoderJob();

  // Creates a new AudioDecoderJob instance for decoding audio.
  // |audio_codec| - The audio format the object needs to decode.
  // |sample_rate| - The sample rate of the decoded output.
  // |channel_count| - The number of channels in the decoded output.
  // |extra_data|, |extra_data_size| - Extra data buffer needed for initializing
  // the decoder.
  // |media_crypto| - Handle to a Java object that handles the encryption for
  // the audio data.
  // |request_data_cb| - Callback used to request more data for the decoder.
  static AudioDecoderJob* Create(
      const AudioCodec audio_codec, int sample_rate, int channel_count,
      const uint8* extra_data, size_t extra_data_size, jobject media_crypto,
      const base::Closure& request_data_cb);

  void SetVolume(double volume);

  // Sets the base timestamp for |audio_timestamp_helper_|.
  void SetBaseTimestamp(base::TimeDelta base_timestamp);

 private:
  AudioDecoderJob(scoped_ptr<AudioTimestampHelper> audio_timestamp_helper,
                  scoped_ptr<AudioCodecBridge> audio_decoder_bridge,
                  int bytes_per_frame,
                  const base::Closure& request_data_cb);

  // MediaDecoderJob implementation.
  virtual void ReleaseOutputBuffer(
      int output_buffer_index,
      size_t size,
      bool render_output,
      base::TimeDelta current_presentation_timestamp,
      const ReleaseOutputCompletionCallback& callback) OVERRIDE;

  virtual bool ComputeTimeToRender() const OVERRIDE;

  // number of bytes per audio frame;
  int bytes_per_frame_;

  scoped_ptr<AudioCodecBridge> audio_codec_bridge_;

  // Object to calculate the current audio timestamp for A/V sync.
  scoped_ptr<AudioTimestampHelper> audio_timestamp_helper_;
};

}  // namespace media

#endif  // MEDIA_BASE_ANDROID_AUDIO_DECODER_JOB_H_
