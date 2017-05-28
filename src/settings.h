#pragma once

#include "audio_backend.h"
#include "midi_backend.h"
#include "serialization/serializer.h"

namespace App
{
	struct Settings
	{
		AudioDeviceSettings audioSettings_;
		MidiDeviceSettings midiSettings_;

		void Save();
		void Load();

		bool Serialize(Serialization::Serializer& ser)
		{
			ser.SerializeObject("audioSettings", audioSettings_);
			ser.SerializeObject("midiSettings", midiSettings_);
			return true;
		}
	};
} // namespace App
