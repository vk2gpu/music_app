#include "audio_playback_callback.h"
#include "app.h"

#include "core/file.h"
#include "job/manager.h"

#include <utility>

namespace Callbacks
{
	AudioPlaybackCallback::AudioPlaybackCallback()
	{
	}

	AudioPlaybackCallback::~AudioPlaybackCallback()
	{
	}

	void AudioPlaybackCallback::OnAudioCallback(i32 numIn, i32 numOut, const f32** in, f32** out, i32 numFrames)
	{
		if(numOut > 0)
		{
			if(soundData_)
			{
				auto* sampleData = reinterpret_cast<f32*>(soundData_.rawData_);
				for(i32 i = 0; i < numFrames; ++i)
				{
					f32 sample = sampleData[currSample_];
					currSample_++;
					if(currSample_ > soundData_.numSamples_)
					{
						currSample_ = 0;
					}

					for(i32 j = 0; j < numOut; ++j)
					{
						out[j][i] += sample;
					}
				}
			}
		}
	}

	void AudioPlaybackCallback::Play(const char* fileName)
	{
		auto file = Core::File(fileName, Core::FileFlags::READ);
		if(file)
		{
			auto data = Sound::Load(file);
			if(data)
			{
				Core::ScopedMutex lock(soundDataMutex_);
				soundData_ = std::move(data);
				currSample_ = 0;
			}			
		}
	}

	void AudioPlaybackCallback::Stop()
	{
		Core::ScopedMutex lock(soundDataMutex_);
		std::swap(soundData_, Sound::Data());
		currSample_ = 0;
	}


} // namespace Callbacks
