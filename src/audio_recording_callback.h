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
		void SaveBuffer();
		Core::Vector<i32> GetRecordingIDs() const;

	private:
		AudioStatsCallback& audioStats_;

		Sound::OutputStream* outputStream_ = nullptr;
		Job::Counter* outputStreamCounter_ = nullptr;

		i32 lowMaxSamples_ = 0;

		volatile i32 saveBuffer_ = 0;

		mutable Core::Mutex recordingMutex_;
		Core::Vector<i32> recordingIds_;
	};


} // namespace Callbacks
