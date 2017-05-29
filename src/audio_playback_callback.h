#pragma once

#include "audio_backend.h"
#include "core/concurrency.h"
#include "core/vector.h"
#include "sound.h"

namespace Callbacks
{
	/// Handles automatic recording to disc.
	class AudioPlaybackCallback : public IAudioCallback
	{
	public:
		AudioPlaybackCallback();
		virtual ~AudioPlaybackCallback();
		void OnAudioCallback(i32 numIn, i32 numOut, const f32** in, f32** out, i32 numFrames) override;
		void Play(const char* fileName);
		void Stop();

	private:
		mutable Core::Mutex soundDataMutex_;
		Sound::Data soundData_;
		i32 currSample_ = currSample_;
	};

} // namespace Callbacks
