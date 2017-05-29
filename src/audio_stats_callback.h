#pragma once

#include "audio_backend.h"

namespace Callbacks
{
	/// Gathers stats for input audio.
	class AudioStatsCallback : public IAudioCallback
	{
	public:
		AudioStatsCallback();
		virtual ~AudioStatsCallback();

		void OnAudioCallback(i32 numIn, i32 numOut, const f32** in, f32** out, i32 numFrames) override;

		f32 rms_ = 0.0f;
		f32 max_ = 0.0f;

		f32 rmsSmoothed_ = 0.0f;
		f32 maxSmoothed_ = 0.0f;
	};
} // namespace Callbacks
