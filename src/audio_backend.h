#pragma once

#include "core/types.h"

struct AudioDeviceInfo
{
	char name_[256] = {0};
	char backend_[256] = {0};
	i32 deviceIdx_ = 0;
	i32 maxIn_ = 0;
	i32 maxOut_ = 0;
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
	void StartDevice(i32 in, i32 out, i32 bufferSize);

	i32 GetNumInputDevices() const;
	i32 GetNumOutputDevices() const;
	const AudioDeviceInfo& GetInputDeviceInfo(i32 idx);
	const AudioDeviceInfo& GetOutputDeviceInfo(i32 idx);

	bool RegisterCallback(IAudioCallback* callback, u32 inMask, u32 outMask);
	void UnregisterCallback(IAudioCallback* callback);


private:
	struct AudioBackendImpl* impl_ = nullptr;
		
};

