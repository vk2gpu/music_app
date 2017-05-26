#pragma once

#include "core/types.h"

/**
 * Get cent difference between 2 frequencies.
 */
f32 CentDifference(f32 a, f32);

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
