#pragma once

#include "audio_backend.h"
#include "core/array.h"

namespace Callbacks
{
	class AudioBufferCallback : public IAudioCallback
	{
	public:
		static const i32 AUDIO_DATA_SIZE = 2048;

		AudioBufferCallback();
		virtual ~AudioBufferCallback();
		void OnAudioCallback(i32 numIn, i32 numOut, const f32** in, f32** out, i32 numFrames) override;

		// NOTE: These should technically be synchronised, however this is mostly for just visualising.
		// Will create a synchronised interface if extending it further.
		const f32* GetAudioData() const { return audioData_.data(); }
		i32 GetAudioDataOffset() const { return audioDataOffset_; }

	private:
		Core::Array<f32, AUDIO_DATA_SIZE> audioData_;
		i32 audioDataOffset_ = 0;
	};
} // namespace Callbacks