#include "midi_backend.h"

#include "core/concurrency.h"
#include "core/file.h"
#include "core/vector.h"

#include "portmidi.h"

#include <algorithm>


void MidiDeviceSettings::Save()
{
	auto file = Core::File("midi_settings.json", Core::FileFlags::CREATE | Core::FileFlags::WRITE);
	if(file)
	{
		Serialization::Serializer ser(file, Serialization::Flags::TEXT);
		ser.SerializeObject("midi_settings", *this);
	}
}

void MidiDeviceSettings::Load()
{
	auto file = Core::File("midi_settings.json", Core::FileFlags::READ);
	if(file)
	{
		Serialization::Serializer ser(file, Serialization::Flags::TEXT);
		ser.SerializeObject("midi_settings", *this);
	}
}

struct MidiBackendImpl
{
	Core::Vector<MidiDeviceInfo> inputDeviceInfos_;
	Core::Vector<MidiDeviceInfo> outputDeviceInfos_;

	PmStream* inStream_;
	PmStream* outStream_;
};

MidiBackend::MidiBackend()
{
	impl_ = new MidiBackendImpl();
	Pm_Initialize();
}

MidiBackend::~MidiBackend()
{
	Pm_Terminate();
	delete impl_;
}

void MidiBackend::Enumerate()
{
	i32 numDevices = Pm_CountDevices();
	for(i32 idx = 0; idx < numDevices; ++idx)
	{
		auto* pmDeviceInfo = Pm_GetDeviceInfo(idx);
		if(pmDeviceInfo)
		{
			MidiDeviceInfo deviceInfo;
			sprintf_s(deviceInfo.name_, sizeof(deviceInfo.name_), "[%s] - %s" , pmDeviceInfo->interf, pmDeviceInfo->name);
			deviceInfo.uuid_ = Core::UUID(deviceInfo.name_, 0);
			deviceInfo.deviceIdx_ = idx;
			deviceInfo.maxIn_ = pmDeviceInfo->input;
			deviceInfo.maxOut_ = pmDeviceInfo->output;
					
			if(deviceInfo.maxIn_ > 0)
				impl_->inputDeviceInfos_.push_back(deviceInfo);
			if(deviceInfo.maxOut_ > 0)
				impl_->outputDeviceInfos_.push_back(deviceInfo);
		}
	}

	std::sort(impl_->inputDeviceInfos_.begin(), impl_->inputDeviceInfos_.end(), 
		[](const MidiDeviceInfo& a, const MidiDeviceInfo& b)
		{
			return strcmp(a.name_, b.name_) < 0;
		});
	std::sort(impl_->outputDeviceInfos_.begin(), impl_->outputDeviceInfos_.end(), 
		[](const MidiDeviceInfo& a, const MidiDeviceInfo& b)
		{
			return strcmp(a.name_, b.name_) < 0;
		});

	i32 idx = 0;
	for(auto& device : impl_->inputDeviceInfos_)
		device.idx_ = idx++;
	idx = 0;
	for(auto& device : impl_->outputDeviceInfos_)
		device.idx_ = idx++;

#if 0
	MidiDeviceSettings settings;
	settings.inputDevice_ = impl_->inputDeviceInfos_[2].uuid_;

	StartDevice(settings);

	PmEvent event;
	for(;;)
	{
		if(Pm_Read(impl_->inStream_, &event, 1) > 0)
		{
			int status = Pm_MessageStatus(event.message);
			int data1 = Pm_MessageData1(event.message);
			int data2 = Pm_MessageData2(event.message);
			Core::Log("Event: %x, %u, %u\n", status, data1, data2);
		}
		Core::Sleep(0.001);
	}
#endif
}

bool MidiBackend::StartDevice(const MidiDeviceSettings& settings)
{
	if(impl_->inStream_)
	{
		Pm_Close(impl_->inStream_);
		impl_->inStream_ = nullptr;
	}

	if(impl_->outStream_)
	{
		Pm_Close(impl_->outStream_);
		impl_->outStream_ = nullptr;
	}

	const auto* inputDevice = GetInputDeviceInfo(settings.inputDevice_);
	if(inputDevice)
	{
		const auto* paDeviceInfoIn = Pm_GetDeviceInfo(inputDevice->deviceIdx_);
		Pm_OpenInput(&impl_->inStream_, inputDevice->deviceIdx_, nullptr, 128, [](void *time_info){ return 0; }, this);
	}

	const auto* outputDevice = GetOutputDeviceInfo(settings.outputDevice_);
	if(outputDevice)
	{
		const auto* paDeviceInfoOut = Pm_GetDeviceInfo(outputDevice->deviceIdx_);
		Pm_OpenOutput(&impl_->outStream_, outputDevice->deviceIdx_, nullptr, 128, [](void *time_info){ return 0; }, this, 0);
	}

	return true;
}


i32 MidiBackend::GetNumInputDevices() const
{
	return impl_->inputDeviceInfos_.size();
}

i32 MidiBackend::GetNumOutputDevices() const
{
	return impl_->outputDeviceInfos_.size();
}

const MidiDeviceInfo& MidiBackend::GetInputDeviceInfo(i32 idx)
{
	return impl_->inputDeviceInfos_[idx];
}

const MidiDeviceInfo& MidiBackend::GetOutputDeviceInfo(i32 idx)
{
	return impl_->outputDeviceInfos_[idx];
}

const MidiDeviceInfo* MidiBackend::GetInputDeviceInfo(const Core::UUID& uuid)
{
	auto it = std::find_if(impl_->inputDeviceInfos_.begin(), impl_->inputDeviceInfos_.end(),
	[&uuid](const MidiDeviceInfo& deviceInfo)
	{
		return deviceInfo.uuid_ == uuid;
	});
	if(it == impl_->inputDeviceInfos_.end())
		return nullptr;
	return &(*it);
}

const MidiDeviceInfo* MidiBackend::GetOutputDeviceInfo(const Core::UUID& uuid)
{
	auto it = std::find_if(impl_->outputDeviceInfos_.begin(), impl_->outputDeviceInfos_.end(),
	[&uuid](const MidiDeviceInfo& deviceInfo)
	{
		return deviceInfo.uuid_ == uuid;
	});
	if(it == impl_->outputDeviceInfos_.end())
		return nullptr;
	return &(*it);
}

