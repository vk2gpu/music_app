#pragma once

#include "core/types.h"

namespace Midi
{
	enum class Status
	{
		// Channel voice message (4 LSB = channel)
		VOICE_NOTE_OFF = 0b10000000,
		VOICE_NOTE_ON = 0b10010000,
		VOICE_POLY_KEY_PRESS = 0b10100000,
		VOICE_CONTROL_CHANGE = 0b10110000,
		VOICE_PROGRAM_CHANGE = 0b11000000,
		VOICE_CHANNEL_PRESSURE = 0b11010000,
		VOICE_PITCH_WHEEL_CHANGE = 0b11100000,

		// Channel control messages. (4 LSB = channel)
		CHANNEL_MODE_MESSAGE = 0b10110000,

		// System common.
		COMMON_SYSTEM_EXCLUSIVE = 0b11110000,
		COMMON_MIDI_TIMEC_ODE_QRT = 0b11110001,
		COMMON_SONG_POSITION_PTR = 0b11110010,
		COMMON_SONG_SELECT = 0b11110011,
		COMMON_TUNE_REQUEST = 0b11110110,

		// System real-time.
		REALTIME_TIMING_CLOCK = 0b11111000,
		REALTIME_START = 0b11111010,
		REALTIME_CONTINUE = 0b11111011,
		REALTIME_STOP = 0b11111100,
		REALTIME_ACTIVE_SENSING = 0b11111110,
		REALTIME_RESET = 0b11111111,
	};

	struct Message
	{
		u8 status_;
		u8 data1_;
		u8 data2_;

		bool HasChannel() const { return (status_ & 0xf0) != 0xf0 && (status_ & 0xf0) != 0x00; }
		i32 GetChannel() const { return status_ & 0xf; }
	};

	/**
	 * Convert frequency to MIDI note.
	 */
	i32 FreqToMidi(f32 freq);

	/**
	 * Convert MIDI note to frequency.
	 */
	f32 MidiToFreq(i32 note);

	/**
	 * Convert MIDI note to string.
	 */
	void MidiToString(i32 note, char* outStr, i32 strMax);

} // namespace Midi
