#pragma once

#include "core/types.h"
#include "serialization/serializer.h"

#include "audio_backend.h"

enum class DeviceSelectionStatus
{
	NONE,
	SELECTED,
};

class DialogDeviceSelection
{
public:
	DialogDeviceSelection(AudioBackend& audioBackend, const AudioDeviceSettings& settings);
	~DialogDeviceSelection();

	DeviceSelectionStatus Update();
	const AudioDeviceSettings& GetSettings() const { return settings_; }

private:
	AudioBackend& audioBackend_;

	i32 inputDeviceIdx_ = 0;
	i32 outputDeviceIdx_ = 0;
	i32 sampleRateIdx_ = 0;
	i32 bufferSizeIdx_ = 0;

	AudioDeviceSettings settings_;
};
