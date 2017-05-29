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

	struct Data
	{
		Data() = default;
		Data(const Data&) = delete;
		Data& operator=(const Data& other) = delete;

		~Data();	
		Data(Data&& other);
		Data& operator=(Data&& other);
		void swap(Data& other);
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
	Data Load(Core::File& file);

	/**
	 * Save a sound to a given file.
	 */
	void Save(Core::File& file, const Data& soundData);

	/**
	 * Save a sound from raw.
	 */
	void Save(Core::File& rawFile, Core::File& outFile, Format format, i32 numChannels, i32 sampleRate);


	/**
	 * Output stream.
	 */
	class OutputStream
	{
	public:
		static const i32 FLUSH_SIZE = 1024 * 1024 * 1;
		static volatile i32 SoundBufferID;

		OutputStream(i32 sampleRate);
		~OutputStream();
		void FlushData();
		void Push(const void* data, i32 size);
		u32 GetID() const;

	private:
		struct OutputStreamImpl* impl_ = nullptr;
	};

} // namespace Sound
