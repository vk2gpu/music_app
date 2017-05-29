#include "audio_stats_callback.h"
#include "ispc/audio_stats_ispc.h"
#include "core/misc.h"

namespace Callbacks
{
	AudioStatsCallback::AudioStatsCallback()
	{
	}

	AudioStatsCallback::~AudioStatsCallback()
	{
	}

	void AudioStatsCallback::OnAudioCallback(i32 numIn, i32 numOut, const f32** in, f32** out, i32 numFrames)
	{
		if(numIn > 0)
		{
			rms_ = ispc::audio_stats_rms(in[0], numFrames);
			max_ = ispc::audio_stats_max(in[0], numFrames);
				
			/// TODO: Lerp to 0.0 to scale for different buffer size/sample rate.
			rmsSmoothed_ *= 0.99f;
			maxSmoothed_ *= 0.99f;

			rmsSmoothed_ = Core::Max(rmsSmoothed_, rms_);
			maxSmoothed_ = Core::Max(maxSmoothed_, max_);
		}
	}

} // namespace Callbacks
