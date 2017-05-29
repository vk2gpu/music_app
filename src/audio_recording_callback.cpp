#include "audio_recording_callback.h"
#include "audio_stats_callback.h"
#include "sound.h"
#include "app.h"

#include "job/manager.h"

#include <utility>

namespace Callbacks
{
	AudioRecordingCallback::AudioRecordingCallback(AudioStatsCallback& audioStats)
		: audioStats_(audioStats)
	{
	}

	AudioRecordingCallback::~AudioRecordingCallback()
	{
		if(outputStreamCounter_)
		{
			Job::Manager::WaitForCounter(outputStreamCounter_, 0);
		}
		delete outputStream_;
	}

	void AudioRecordingCallback::OnAudioCallback(i32 numIn, i32 numOut, const f32** in, f32** out, i32 numFrames)
	{
		i32 sampleRate = App::Manager::GetSettings().audioSettings_.sampleRate_;

		if(numIn > 0)
		{
			bool shouldStart = Core::AtomicCmpExchg(&startSignal_, 0, 1) == 1;
			if(outputStream_ == nullptr && (shouldStart || audioStats_.max_ > thresholdStart_))
			{
				outputStream_ = new Sound::OutputStream(sampleRate);
				remainingTimeToStop_ = timeout_;
			}

			// If automatic stopping is enabled, then count towards it.
			if(autoStop_)
			{
				if(audioStats_.max_ > thresholdStop_)
				{
					remainingTimeToStop_ = timeout_;
				}
				else if(remainingTimeToStop_ > 0.0f)
				{
					remainingTimeToStop_ -= (f32)numFrames / (f32)sampleRate;
				}

				if(outputStream_ != nullptr && remainingTimeToStop_ <= 0.0f )
				{
					Stop();
				}
			}

			// Create and push to sound buffer.
			if(outputStream_ != nullptr)
			{
				outputStream_->Push(in[0], sizeof(f32) * numFrames);

				// If we need to save, kick of a job to delete and finalize.
				if(Core::AtomicCmpExchg(&stopSignal_, 0, 1) == 1)
				{
					if(outputStreamCounter_)
					{
						Job::Manager::WaitForCounter(outputStreamCounter_, 0);
					}

					Job::JobDesc jobDesc;
					jobDesc.func_ = [](i32 param, void* data) {
						Sound::OutputStream* outputStream = static_cast<Sound::OutputStream*>(data);
						delete outputStream;
					};

					Core::ScopedMutex lock(recordingMutex_);
					recordingIds_.push_back(outputStream_->GetID());

					jobDesc.param_ = 0;
					jobDesc.data_ = outputStream_;
					jobDesc.name_ = "Sound::OutputStream save";
					Job::Manager::RunJobs(&jobDesc, 1, &outputStreamCounter_);

					outputStream_ = nullptr;

					remainingTimeToStop_ = timeout_;
				}
			}
		}
	}

	void AudioRecordingCallback::Start()
	{
		Core::AtomicExchg(&startSignal_, 1);
	}

	void AudioRecordingCallback::Stop()
	{
		Core::AtomicExchg(&stopSignal_, 1);
	}

	Core::Vector<i32> AudioRecordingCallback::GetRecordingIDs() const
	{
		Core::ScopedMutex lock(recordingMutex_);
		return recordingIds_;
	}

} // namespace Callbacks
