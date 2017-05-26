#include "audio_backend.h"

#include "core/array.h"
#include "core/concurrency.h"
#include "core/misc.h"
#include "core/vector.h"

#include <portaudio.h>

#include <algorithm>

struct AudioBackendImpl
{
	Core::Vector<AudioDeviceInfo> inputDeviceInfos_;
	Core::Vector<AudioDeviceInfo> outputDeviceInfos_;

	PaStream* stream_ = nullptr;

	i32 inChannels_ = 0;
	i32 outChannels_ = 0;

	struct Callback
	{
		IAudioCallback* callback_ = nullptr;
		u32 inMask_ = 0;
		u32 outMask_ = 0;
	};

	Core::Mutex callbackMutex_;
	Core::Vector<Callback> callbacks_;

	Core::Vector<const f32*> inStreams_;
	Core::Vector<f32*> outStreams_;
};

static int StaticStreamCallback(
	const void *input, void *output,
	unsigned long frameCount,
	const PaStreamCallbackTimeInfo* timeInfo,
	PaStreamCallbackFlags statusFlags,
	void *userData )
{
	AudioBackendImpl* impl_ = (AudioBackendImpl*)userData;
	const f32* const* fin = reinterpret_cast<const f32* const*>(input);
	f32** fout = reinterpret_cast<f32**>(output);

	const i32 inChannels = impl_->inChannels_;
	const i32 outChannels = impl_->outChannels_;

	Core::ScopedMutex lock(impl_->callbackMutex_);
	for(const auto& callback : impl_->callbacks_)
	{
		i32 callbackIn = 0;
		for(i32 in = 0; in < inChannels; ++in)
		{
			if(Core::ContainsAllFlags(callback.inMask_, (1 << in)))
			{
				impl_->inStreams_[callbackIn++] = fin[in];
			}
		}

		i32 callbackOut = 0;
		for(i32 out = 0; out < outChannels; ++out)
		{
			if(Core::ContainsAllFlags(callback.outMask_, (1 << out)))
			{
				impl_->outStreams_[callbackOut++] = fout[out];
			}
		}

		callback.callback_->OnAudioCallback(callbackIn, callbackOut, impl_->inStreams_.data(), impl_->outStreams_.data(), frameCount);
	}


#if 0
	// If RMS is over a certain amount, create a sound buffer.
	f64 rms = 0.0f;
	const f32* inData = fin[inChannel_];
	for(i32 idx = 0; idx < AUDIO_DATA_SIZE; ++idx)
	{
		rms += (inData[idx] * inData[idx]);
	}
	rms = sqrt((1.0 / AUDIO_DATA_SIZE) * rms);

	if(soundBuffer_ == nullptr && rms > 0.01f)
	{
		soundBuffer_ = new SoundBuffer();
	}


	if(rms > 0.01f)
	{
		lowRmsSamples_ = 0;
	}
	else
	{
		lowRmsSamples_ += frameCount;
	}

	if(soundBuffer_ != nullptr && lowRmsSamples_ > (2 * 48000))
	{
		Core::AtomicExchg(&saveBuffer_, 1);
	}

	// Create and push to sound buffer.
	if(soundBuffer_ != nullptr)
	{
		soundBuffer_->Push(fin[inChannel_], sizeof(f32) * frameCount);

		// If we need to save, kick of a job to delete and finalize.
		if(Core::AtomicCmpExchg(&saveBuffer_, 0, 1) == 1)
		{
			if(soundBufferCounter_)
			{
				Job::Manager::WaitForCounter(soundBufferCounter_, 0);
			}

			Job::JobDesc jobDesc;
			jobDesc.func_ = [](i32 param, void* data) {
				SoundBuffer* soundBuffer = static_cast<SoundBuffer*>(data);
				delete soundBuffer;
			};

			jobDesc.param_ = 0;
			jobDesc.data_ = soundBuffer_;
			jobDesc.name_ = "SoundBuffer save";
			Job::Manager::RunJobs(&jobDesc, 1, &soundBufferCounter_);

			soundBuffer_ = nullptr;
		}
	}


	if(fout)
	{
		for(i32 idx = 0; idx < (i32)frameCount; ++idx)
		{
			for(i32 ch = 0; ch < outChannels_; ++ch)
			{
				fout[ch][idx] = sin((f32)freqTick_);
			}

			freqTick_ += freq_ / (48000.0 / Core::F32_PIMUL2);
		}

		if(freqTick_ > Core::F32_PIMUL2)
			freqTick_ -= Core::F32_PIMUL2;
	}

	if((audioDataOffset_ + (i32)frameCount) < audioData_.size())
	{
		memcpy(audioData_.data() + audioDataOffset_, fin[inChannel_], sizeof(f32) * frameCount);
		audioDataOffset_ += frameCount;
	}
	else
	{
		const i32 firstBlock = (audioData_.size() - audioDataOffset_);
		memcpy(audioData_.data() + audioDataOffset_, fin[inChannel_], sizeof(f32) * firstBlock);
		audioDataOffset_ = 0;
		frameCount -= firstBlock;
				
		memcpy(audioData_.data() + audioDataOffset_, fin[inChannel_], sizeof(f32) * frameCount);
		audioDataOffset_ += frameCount;
	}
#endif

	return paContinue;
}


AudioBackend::AudioBackend()
{
	impl_ = new AudioBackendImpl;
	Pa_Initialize();
}

AudioBackend::~AudioBackend()
{
	Pa_Terminate();
}

void AudioBackend::Enumerate()
{
	impl_->inputDeviceInfos_.clear();
	impl_->outputDeviceInfos_.clear();
	i32 numDevices = Pa_GetDeviceCount();
	impl_->inputDeviceInfos_.reserve(numDevices);
	impl_->outputDeviceInfos_.reserve(numDevices);

	for(i32 idx = 0; idx < numDevices; ++idx)
	{
		const auto* paDeviceInfo = Pa_GetDeviceInfo(idx);
		if(paDeviceInfo)
		{
			AudioDeviceInfo deviceInfo;
			const auto* paHostAPIInfo = Pa_GetHostApiInfo(paDeviceInfo->hostApi);
			sprintf_s(deviceInfo.name_, sizeof(deviceInfo.name_), "[%s] - %s", paHostAPIInfo->name, paDeviceInfo->name);
			deviceInfo.deviceIdx_ = idx;
			deviceInfo.maxIn_ = paDeviceInfo->maxInputChannels;
			deviceInfo.maxOut_ = paDeviceInfo->maxOutputChannels;
					
					
			if(deviceInfo.maxIn_ > 0)
				impl_->inputDeviceInfos_.push_back(deviceInfo);
			if(deviceInfo.maxOut_ > 0)
				impl_->outputDeviceInfos_.push_back(deviceInfo);
		}
	}

	std::sort(impl_->inputDeviceInfos_.begin(), impl_->inputDeviceInfos_.end(), 
		[](const AudioDeviceInfo& a, const AudioDeviceInfo& b)
		{
			return strcmp(a.name_, b.name_) < 0;
		});
	std::sort(impl_->outputDeviceInfos_.begin(), impl_->outputDeviceInfos_.end(), 
		[](const AudioDeviceInfo& a, const AudioDeviceInfo& b)
		{
			return strcmp(a.name_, b.name_) < 0;
		});
}

void AudioBackend::StartDevice(i32 in, i32 out, i32 bufferSize)
{
	if(impl_->stream_)
	{
		Pa_StopStream(impl_->stream_);
		Pa_CloseStream(impl_->stream_);
		impl_->stream_ = nullptr;
	}
	const auto& inputDevice = impl_->inputDeviceInfos_[in];
	const auto& outputDevice = impl_->outputDeviceInfos_[out];
	const auto* paDeviceInfoIn = Pa_GetDeviceInfo(inputDevice.deviceIdx_);
	const auto* paDeviceInfoOut = Pa_GetDeviceInfo(outputDevice.deviceIdx_);

	impl_->inChannels_ = inputDevice.maxIn_;
	impl_->outChannels_ = outputDevice.maxOut_;

	PaStreamParameters inParams;
	inParams.device = inputDevice.deviceIdx_;
	inParams.channelCount = inputDevice.maxIn_;
	inParams.sampleFormat = paFloat32 | paNonInterleaved;
	inParams.suggestedLatency = paDeviceInfoIn->defaultLowInputLatency;
	inParams.hostApiSpecificStreamInfo = nullptr;

	PaStreamParameters outParams;
	outParams.device = outputDevice.deviceIdx_;
	outParams.channelCount = outputDevice.maxOut_;
	outParams.sampleFormat = paFloat32 | paNonInterleaved;
	outParams.suggestedLatency = paDeviceInfoOut->defaultLowOutputLatency;
	outParams.hostApiSpecificStreamInfo = nullptr;

	impl_->inStreams_.resize(impl_->inChannels_);
	impl_->outStreams_.resize(impl_->outChannels_);

	Pa_OpenStream(&impl_->stream_, &inParams, &outParams, 48000.0, bufferSize, paNoFlag, StaticStreamCallback, impl_);
	Pa_StartStream(impl_->stream_);
}

i32 AudioBackend::GetNumInputDevices() const
{
	return impl_->inputDeviceInfos_.size();
}

i32 AudioBackend::GetNumOutputDevices() const
{
	return impl_->outputDeviceInfos_.size();
}

const AudioDeviceInfo& AudioBackend::GetInputDeviceInfo(i32 idx)
{
	return impl_->inputDeviceInfos_[idx];
}

const AudioDeviceInfo& AudioBackend::GetOutputDeviceInfo(i32 idx)
{
	return impl_->outputDeviceInfos_[idx];
}

bool AudioBackend::RegisterCallback(IAudioCallback* callback, u32 inMask, u32 outMask)
{
	UnregisterCallback(callback);

	AudioBackendImpl::Callback callbackObj;
	callbackObj.callback_ = callback;
	callbackObj.inMask_ = inMask;
	callbackObj.outMask_ = outMask;

	Core::ScopedMutex lock(impl_->callbackMutex_);
	impl_->callbacks_.push_back(callbackObj);

	return true;
}

void AudioBackend::UnregisterCallback(IAudioCallback* callback)
{
	Core::ScopedMutex lock(impl_->callbackMutex_);
	auto it = std::find_if(impl_->callbacks_.begin(), impl_->callbacks_.end(),
		[callback](const AudioBackendImpl::Callback& callbackObj)
		{
			return callbackObj.callback_ == callback;
		});
	if(it != impl_->callbacks_.end())
	{
		impl_->callbacks_.erase(it);
	}
}


