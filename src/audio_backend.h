#pragma once

#include "core/types.h"
#include "core/uuid.h"
#include "serialization/serializer.h"

struct AudioDeviceInfo
{
	char name_[256] = {0};
	Core::UUID uuid_;
	i32 idx_ = 0;
	i32 maxIn_ = 0;
	i32 maxOut_ = 0;

	/// Portaudio specific.
	i32 deviceIdx_ = 0;
};

struct AudioDeviceSettings
{
	Core::UUID inputDevice_;
	Core::UUID outputDevice_;
	i32 bufferSize_ = 1024;
	i32 sampleRate_ = 48000;

	bool Serialize(Serialization::Serializer& ser)
	{
		ser.Serialize("inputDevice", inputDevice_);
		ser.Serialize("outputDevice", outputDevice_);
		ser.Serialize("bufferSize", bufferSize_);
		ser.Serialize("sampleRate", sampleRate_);
		return true;
	}
};


class IAudioCallback
{
public:
	virtual ~IAudioCallback() {}

	/**
	 * Called when there is audio data to process.
	 * @param numIn Number of input channels.
	 * @param numOut Number of output channels.
	 * @param in Array of requested input channels.
	 * @param out Array of requested output channels.
	 * @param numFrames Number of frames to process.
	 */
	virtual void OnAudioCallback(i32 numIn, i32 numOut, const f32** in, f32** out, i32 numFrames) = 0;
};

class AudioBackend
{
public:
	AudioBackend();
	~AudioBackend();
	void Enumerate();
	bool StartDevice(const AudioDeviceSettings& settings);

	i32 GetNumInputDevices() const;
	i32 GetNumOutputDevices() const;
	const AudioDeviceInfo& GetInputDeviceInfo(i32 idx);
	const AudioDeviceInfo& GetOutputDeviceInfo(i32 idx);
	const AudioDeviceInfo* GetInputDeviceInfo(const Core::UUID& uuid);
	const AudioDeviceInfo* GetOutputDeviceInfo(const Core::UUID& uuid);
	

	bool RegisterCallback(IAudioCallback* callback, u32 inMask, u32 outMask);
	void UnregisterCallback(IAudioCallback* callback);


private:
	struct AudioBackendImpl* impl_ = nullptr;
		
};

