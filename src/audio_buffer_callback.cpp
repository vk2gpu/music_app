#include "audio_buffer_callback.h"
#include "core/misc.h"

#include <cstring>

namespace Callbacks
{
	AudioBufferCallback::AudioBufferCallback()
	{
	}

	AudioBufferCallback::~AudioBufferCallback()
	{
	}
	
	void AudioBufferCallback::OnAudioCallback(i32 numIn, i32 numOut, const f32** in, f32** out, i32 numFrames)
	{
		if(numIn > 0)
		{
			if(numOut > 0)
			{	
				for(int i = 0; i < numOut; ++i)
				{
					memcpy(out[i], in[0], sizeof(float) * numFrames);
				}
			}

			i32 audioDataFrames = Core::Min(audioData_.size(), numFrames);
			if((audioDataOffset_ + (i32)numFrames) < audioData_.size())
			{
				memcpy(audioData_.data() + audioDataOffset_, out[0], sizeof(f32) * audioDataFrames);
				audioDataOffset_ += audioDataFrames;
			}
			else
			{
				const i32 firstBlock = (audioData_.size() - audioDataOffset_);
				memcpy(audioData_.data() + audioDataOffset_, out[0], sizeof(f32) * firstBlock);
				audioDataOffset_ = 0;
				audioDataFrames -= firstBlock;
				
				memcpy(audioData_.data() + audioDataOffset_, out[0], sizeof(f32) * audioDataFrames);
				audioDataOffset_ += audioDataFrames;
			}
		}
	}

} // namespace Callbacks
