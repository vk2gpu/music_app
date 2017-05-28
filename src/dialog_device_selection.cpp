#include "dialog_device_selection.h"

#include "audio_backend.h"

#include "core/array.h"
#include "core/file.h"
#include "core/misc.h"
#include "gui.h"

namespace
{
	const char* sampleRateStrs[] =
	{
		"44100",
		"48000",
		"96000"
	};

	i32 sampleRates[] = 
	{
		44100,
		48000,
		96000
	};

	i32 GetSampleRateIdx(i32 value)
	{
		for(i32 i = 0; i < 3; ++i)
		{
			if(sampleRates[i] == value)
				return i;
		}
		return 0;
	}

	const char* bufferSizeStrs[] = 
	{
		"512",
		"1024",
		"2048",
		"4096"
	};

	i32 bufferSizes[] = 
	{
		512,
		1024,
		2048,
		4096,
	};

	i32 GetBufferSizeIdx(i32 value)
	{
		for(i32 i = 0; i < 4; ++i)
		{
			if(bufferSizes[i] == value)
				return i;
		}
		return 0;
	}
}

DialogDeviceSelection::DialogDeviceSelection(AudioBackend& audioBackend, const AudioDeviceSettings& settings)
	: audioBackend_(audioBackend)
	, settings_(settings)
{
	audioBackend_.Enumerate();

	auto* inputDevice = audioBackend_.GetInputDeviceInfo(settings_.inputDevice_);
	if(inputDevice)
		inputDeviceIdx_ = inputDevice->idx_;

	auto* outputDevice = audioBackend_.GetOutputDeviceInfo(settings_.outputDevice_);
	if(outputDevice)
		outputDeviceIdx_ = outputDevice->idx_;

	sampleRateIdx_ = GetSampleRateIdx(settings_.sampleRate_);
	bufferSizeIdx_ = GetBufferSizeIdx(settings_.bufferSize_);
	
}

DialogDeviceSelection::~DialogDeviceSelection()
{
}

DeviceSelectionStatus DialogDeviceSelection::Update()
{
	ImGui::SetNextWindowPosCenter();
	ImGui::SetNextWindowSize(ImVec2(500.0f, 500.0f));

	if(ImGui::Begin("Device Selection", nullptr, ImGuiWindowFlags_NoResize | ImGuiWindowFlags_NoMove))
	{
		Core::Array<const char*, 256> inputDeviceNames;
		Core::Array<const char*, 256> outputDeviceNames;
		for(i32 idx = 0; idx < audioBackend_.GetNumInputDevices(); ++idx)
		{
			inputDeviceNames[idx] = audioBackend_.GetInputDeviceInfo(idx).name_;
		}
		for(i32 idx = 0; idx < audioBackend_.GetNumOutputDevices(); ++idx)
		{
			outputDeviceNames[idx] = audioBackend_.GetOutputDeviceInfo(idx).name_;
		}

		inputDeviceIdx_ = Core::Min(inputDeviceIdx_, audioBackend_.GetNumInputDevices());
		outputDeviceIdx_ = Core::Min(outputDeviceIdx_, audioBackend_.GetNumOutputDevices());

		{
			Gui::ScopedItemWidth scopedItemWidth(ImGui::GetWindowSize().x - 6.0f);

			Gui::ListBox("Inputs:", &inputDeviceIdx_, inputDeviceNames.data(), audioBackend_.GetNumInputDevices(), 8);
			Gui::ListBox("Outputs:", &outputDeviceIdx_, outputDeviceNames.data(), audioBackend_.GetNumOutputDevices(), 8);

			Gui::Combo("Sample Rate:", &sampleRateIdx_, sampleRateStrs, 3);				
			Gui::Combo("Buffer Size:", &bufferSizeIdx_, bufferSizeStrs, 4);
		}

		if(ImGui::Button("Start"))
		{
			settings_.inputDevice_ = audioBackend_.GetInputDeviceInfo(inputDeviceIdx_).uuid_;
			settings_.outputDevice_ = audioBackend_.GetOutputDeviceInfo(outputDeviceIdx_).uuid_;
			settings_.sampleRate_ = sampleRates[sampleRateIdx_];
			settings_.bufferSize_ = bufferSizes[bufferSizeIdx_];
			if(audioBackend_.StartDevice(settings_))
			{
				ImGui::End();
				return DeviceSelectionStatus::SELECTED;
			}
		}
	}

	ImGui::End();
	return DeviceSelectionStatus::NONE;
}
