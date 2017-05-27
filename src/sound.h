#include "core/types.h"

namespace Core
{
	class File;
} // namespace Core

namespace Sound
{
	enum class Format
	{
		UNKNOWN = 0,
		S16,
		F32
	};

	struct SoundData
	{
		SoundData() = default;
		SoundData(const SoundData&) = delete;
		SoundData& operator=(const SoundData& other) = delete;

		~SoundData();	
		SoundData(SoundData&& other);
		SoundData& operator=(SoundData&& other);
		void swap(SoundData& other);
		operator bool() const;

		i32 numChannels_ = 0;
		i32 sampleRate_ = 0;
		i32 numSamples_ = 0;
		Format format_ = Format::UNKNOWN;
		u32 numBytes_ = 0;
		u8* rawData_ = nullptr;
	};


	/**
	 * Load a sound from a given file.
	 */
	SoundData Load(Core::File& file);

	/**
	 * Save a sound to a given file.
	 */
	void Save(Core::File& file, const SoundData& soundData);

} // namespace Sound
