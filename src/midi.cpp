#include "midi.h"

#include <cmath>
#include <cstdio>

namespace Midi
{
	f32 CentDifference(f32 a, f32 b)
	{
		return 1200.0f * log2f(a /  b);
	}
	
	i32 FreqToMidi(f32 freq)
	{
		return 69 + (i32)roundf(CentDifference(freq, 440.0f) / 100.0f);
	}

	f32 MidiToFreq(i32 note)
	{
		return powf(2.0f, (f32)(note - 69) / 12.0f) * 440.0f;
	}

	void MidiToString(i32 note, char* outStr, i32 strMax)
	{
		static const char* SEMITONES[12] = 
		{
			"C", "C#", "D", "D#", "E", "F", "F#", "G", "G#", "A", "A#", "B"
		};
	
		if(note >= 0 && note <= 127)
		{
			const char* semitone = SEMITONES[note % 12];
			const i32 octave = (note - 12) / 12;

			sprintf_s(outStr, strMax, "%s%d", semitone, octave);
		}
		else
		{
			sprintf_s(outStr, strMax, "-");
		}
	}

} // namespace Midi