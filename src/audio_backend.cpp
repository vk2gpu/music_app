#include "audio_backend.h"

#include "core/array.h"
#include "core/concurrency.h"
#include "core/file.h"
#include "core/misc.h"
#include "core/vector.h"

#include "ispc/clipping_ispc.h"

#include <portaudio.h>

#include <algorithm>

void AudioDeviceSettings::Save()
{
	auto file = Core::File("settings.json", Core::FileFlags::CREATE | Core::FileFlags::WRITE);
	if(file)
	{
		Serialization::Serializer ser(file, Serialization::Flags::TEXT);
		ser.SerializeObject("settings", *this);
	}
}

void AudioDeviceSettings::Load()
{
	auto file = Core::File("settings.json", Core::FileFlags::READ);
	if(file)
	{
		Serialization::Serializer ser(file, Serialization::Flags::TEXT);
		ser.SerializeObject("settings", *this);
	}
}


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

	// Perform clipping.
	for(i32 out = 0; out < outChannels; ++out)
	{
		ispc::clipping_hard(fout[out], fout[out], frameCount);
	}

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
			deviceInfo.uuid_ = Core::UUID(deviceInfo.name_, 0);
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

	i32 idx = 0;
	for(auto& device : impl_->inputDeviceInfos_)
		device.idx_ = idx++;
	idx = 0;
	for(auto& device : impl_->outputDeviceInfos_)
		device.idx_ = idx++;
}

bool AudioBackend::StartDevice(const AudioDeviceSettings& settings)
{
	if(impl_->stream_)
	{
		Pa_StopStream(impl_->stream_);
		Pa_CloseStream(impl_->stream_);
		impl_->stream_ = nullptr;
	}

	const auto* inputDevice = GetInputDeviceInfo(settings.inputDevice_);
	const auto* outputDevice = GetOutputDeviceInfo(settings.outputDevice_);
	if(!inputDevice || !outputDevice)
		return false;

	const auto* paDeviceInfoIn = Pa_GetDeviceInfo(inputDevice->deviceIdx_);
	const auto* paDeviceInfoOut = Pa_GetDeviceInfo(outputDevice->deviceIdx_);

	impl_->inChannels_ = inputDevice->maxIn_;
	impl_->outChannels_ = outputDevice->maxOut_;

	PaStreamParameters inParams;
	inParams.device = inputDevice->deviceIdx_;
	inParams.channelCount = inputDevice->maxIn_;
	inParams.sampleFormat = paFloat32 | paNonInterleaved;
	inParams.suggestedLatency = paDeviceInfoIn->defaultLowInputLatency;
	inParams.hostApiSpecificStreamInfo = nullptr;

	PaStreamParameters outParams;
	outParams.device = outputDevice->deviceIdx_;
	outParams.channelCount = outputDevice->maxOut_;
	outParams.sampleFormat = paFloat32 | paNonInterleaved;
	outParams.suggestedLatency = paDeviceInfoOut->defaultLowOutputLatency;
	outParams.hostApiSpecificStreamInfo = nullptr;

	impl_->inStreams_.resize(impl_->inChannels_);
	impl_->outStreams_.resize(impl_->outChannels_);

	PaError err;
	err = Pa_OpenStream(&impl_->stream_, &inParams, &outParams, settings.sampleRate_, settings.bufferSize_, paClipOff | paDitherOff, StaticStreamCallback, impl_);
	if(err)
		return false;
	err = Pa_StartStream(impl_->stream_);
	if(err)
	{
		Pa_CloseStream(impl_->stream_);
		impl_->stream_ = nullptr;
		return false;
	}

	return true;
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

const AudioDeviceInfo* AudioBackend::GetInputDeviceInfo(const Core::UUID& uuid)
{
	auto it = std::find_if(impl_->inputDeviceInfos_.begin(), impl_->inputDeviceInfos_.end(),
	[&uuid](const AudioDeviceInfo& deviceInfo)
	{
		return deviceInfo.uuid_ == uuid;
	});
	if(it == impl_->inputDeviceInfos_.end())
		return nullptr;
	return &(*it);
}

const AudioDeviceInfo* AudioBackend::GetOutputDeviceInfo(const Core::UUID& uuid)
{
	auto it = std::find_if(impl_->outputDeviceInfos_.begin(), impl_->outputDeviceInfos_.end(),
	[&uuid](const AudioDeviceInfo& deviceInfo)
	{
		return deviceInfo.uuid_ == uuid;
	});
	if(it == impl_->outputDeviceInfos_.end())
		return nullptr;
	return &(*it);
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


