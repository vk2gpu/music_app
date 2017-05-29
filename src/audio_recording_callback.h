#pragma once

#include "audio_backend.h"
#include "core/concurrency.h"
#include "core/vector.h"

namespace Job
{
	struct Counter;
} // namespace Job

namespace Sound
{
	class OutputStream;
} // namespace Sound

namespace Callbacks
{
	class AudioStatsCallback;

	/// Handles automatic recording to disc.
	class AudioRecordingCallback : public IAudioCallback
	{
	public:
		AudioRecordingCallback(AudioStatsCallback& audioStats);
		virtual ~AudioRecordingCallback();
		void OnAudioCallback(i32 numIn, i32 numOut, const f32** in, f32** out, i32 numFrames) override;
		void Start();
		void Stop();

		Core::Vector<i32> GetRecordingIDs() const;

		bool IsRecording() const { return outputStream_ != nullptr; }
		f32 RecordingTimeLeft() const { return remainingTimeToStop_; }

		bool GetAutoStop() const { return autoStop_; }
		void SetAutoStop(bool autoStop) { autoStop_ = autoStop; }

		f32 GetTimeout() const { return timeout_; }
		void SetTimeout(f32 val) { timeout_ = val; }

		f32 GetThresholdStart() const { return thresholdStart_; }
		void SetThresholdStart(f32 val) { thresholdStart_ = val; }

		f32 GetThresholdStop() const { return thresholdStop_; }
		void SetThresholdStop(f32 val) { thresholdStop_ = val; }

	private:
		AudioStatsCallback& audioStats_;

		Sound::OutputStream* outputStream_ = nullptr;
		Job::Counter* outputStreamCounter_ = nullptr;

		/// Threshold volume to start recording.
		f32 thresholdStart_ = 0.1f;
		/// Threshold volume to stop recording.
		f32 thresholdStop_ = 0.1f;
		/// How long to remain under @a thresholdStop_ before stopping.
		f32 timeout_ = 2.0f;
		/// Enable automatic stopping.
		bool autoStop_ = true;

		/// Remaining time to stop.
		f32 remainingTimeToStop_ = 0.0f;

		volatile i32 startSignal_ = 0;
		volatile i32 stopSignal_ = 0;

		mutable Core::Mutex recordingMutex_;
		Core::Vector<i32> recordingIds_;
	};


} // namespace Callbacks
