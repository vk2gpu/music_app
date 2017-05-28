#pragma once

#include "core/types.h"
#include "core/uuid.h"
#include "serialization/serializer.h"

struct MidiDeviceInfo
{
	char name_[256] = {0};
	Core::UUID uuid_;
	i32 idx_;
	i32 maxIn_ = 0;
	i32 maxOut_ = 0;

	/// Portmidi specific.
	i32 deviceIdx_ = 0;
};

struct MidiDeviceSettings
{
	Core::UUID inputDevice_;
	Core::UUID outputDevice_;

	bool Serialize(Serialization::Serializer& ser)
	{
		ser.Serialize("inputDevice", inputDevice_);
		ser.Serialize("outputDevice", outputDevice_);
		return true;
	}
};


class IMidiCallback
{
public:
	virtual ~IMidiCallback() {}
};

class MidiBackend
{
public:
	MidiBackend();
	~MidiBackend();
	void Enumerate();
	bool StartDevice(const MidiDeviceSettings& settings);
	
	i32 GetNumInputDevices() const;
	i32 GetNumOutputDevices() const;
	const MidiDeviceInfo& GetInputDeviceInfo(i32 idx);
	const MidiDeviceInfo& GetOutputDeviceInfo(i32 idx);
	const MidiDeviceInfo* GetInputDeviceInfo(const Core::UUID& uuid);
	const MidiDeviceInfo* GetOutputDeviceInfo(const Core::UUID& uuid);
	
private:
	struct MidiBackendImpl* impl_ = nullptr;

};

