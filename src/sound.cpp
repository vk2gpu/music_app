#include "sound.h"
#include "core/file.h"
#include "core/vector.h"

#pragma warning(push)
#pragma warning(disable:4244)
#pragma warning(disable:4245)
#pragma warning(disable:4456)
#pragma warning(disable:4457)
#pragma warning(disable:4701)
#define STB_VORBIS_NO_STDIO
#include "stb_vorbis.c"
#pragma warning(pop)

#include <utility>

namespace Sound
{
	namespace Wav
	{
		static const u32 TAG = 'FFIR';

		struct Chunk
		{
			u32 id_ = 0;
			u32 size_ = 0;
		};

		struct RIFFChunk
		{
			static const u32 ID = 'FFIR';
			u32 format_ = 0;
		};

		static const u32 WAVE_ID = 'EVAW';

		struct FmtChunk
		{
			static const u32 ID = ' tmf';

			u16 audioFormat_ = 0;
			u16 numChannels_ = 0;
			u32 sampleRate_ = 0;
			u32 byteRate_ = 0;
			u16 blockAlign_ = 0;
			u16 bitsPerSample_ = 0;
		};

		struct FACTChunk
		{
			static const u32 ID = 'tcaf';

			u32 fileSize_ = 0;
		};

		struct PEAKChunk
		{
			static const u32 ID = 'KAEP';

			u32 version_ = 0;
			u32 timestamp_ = 0;
		};

		struct DataChunk
		{
			static const u32 ID = 'atad';
		};

		bool ReadHeader(Core::File& file)
		{
			Chunk chunk;
			file.Read(&chunk, sizeof(chunk));
			if(chunk.id_== RIFFChunk::ID)
			{
				RIFFChunk riffChunk;

				file.Read(&riffChunk, sizeof(riffChunk));
				if(riffChunk.format_ == WAVE_ID)
				{
					return true;
				}
			}
			return false;
		}

		void ReadChunks(Core::File& file, SoundData& soundData)
		{
			i64 fileSize = file.Size();
			(void)fileSize;

			Chunk chunk;
			FmtChunk fmtChunk;
			FACTChunk factChunk;
			PEAKChunk peakChunk;
			while(file.Read(&chunk, sizeof(chunk)) == sizeof(chunk))
			{
				i64 chunkEnd = file.Tell() + chunk.size_;

				switch(chunk.id_)
				{
				case FmtChunk::ID:
					{
						file.Read(&fmtChunk, sizeof(fmtChunk));
						soundData.sampleRate_ = fmtChunk.sampleRate_;
						soundData.numChannels_ = fmtChunk.numChannels_;
						if(fmtChunk.audioFormat_ == 1 && fmtChunk.bitsPerSample_ == 16)
							soundData.format_ = Format::S16;
						else if(fmtChunk.audioFormat_ == 3 && fmtChunk.bitsPerSample_ == 32)
							soundData.format_ = Format::F32;
					}
					break;

				case FACTChunk::ID:
					{
						file.Read(&factChunk, sizeof(factChunk));
					}
					break;

				case PEAKChunk::ID:
					{
						file.Read(&peakChunk, sizeof(peakChunk));
					}
					break;

				case DataChunk::ID:
					{
						soundData.numBytes_ = chunk.size_;
						soundData.numSamples_ = chunk.size_ / ((fmtChunk.bitsPerSample_ * fmtChunk.numChannels_) / 8);

						soundData.rawData_ = new u8[soundData.numBytes_];
						file.Read(soundData.rawData_, soundData.numBytes_);
					}
					break;
				}

				// Skip to end of chunk, regardless of reads that occurred.
				file.Seek(chunkEnd);
			}
		}

		SoundData Load(Core::File& file)
		{
			SoundData soundData;
			if(ReadHeader(file))
			{
				ReadChunks(file, soundData);
			}
			return std::move(soundData);
		}


		void Save(Core::File& file, const SoundData& soundData)
		{
			Chunk chunk;
			RIFFChunk riffChunk;
			FmtChunk fmtChunk;
			FACTChunk factChunk;
			PEAKChunk peakChunk;

			// Write out header chunk.
			chunk.id_ = RIFFChunk::ID;
			chunk.size_ = sizeof(RIFFChunk) + 
				sizeof(Chunk) + sizeof(FmtChunk) + 
				sizeof(Chunk) + sizeof(FACTChunk) +
				sizeof(Chunk) + soundData.numBytes_;

			riffChunk.format_ = WAVE_ID;
			file.Write(&chunk, sizeof(chunk));
			file.Write(&riffChunk, sizeof(riffChunk));

			// Setup format chunk.
			switch(soundData.format_)
			{
			case Format::S16:
				fmtChunk.audioFormat_ = 1;
				fmtChunk.bitsPerSample_ = 16;
				fmtChunk.blockAlign_ = 2;
				break;
			case Format::F32:
				fmtChunk.audioFormat_ = 3;
				fmtChunk.bitsPerSample_ = 32;
				fmtChunk.blockAlign_ = 4;
				break;
			}

			fmtChunk.numChannels_ = (u16)soundData.numChannels_;
			fmtChunk.sampleRate_ = soundData.sampleRate_;
			fmtChunk.byteRate_ = (fmtChunk.numChannels_ * fmtChunk.bitsPerSample_ * fmtChunk.sampleRate_) / 8;

			chunk.id_ = FmtChunk::ID;
			chunk.size_ = sizeof(FmtChunk);
			file.Write(&chunk, sizeof(chunk));
			file.Write(&fmtChunk, sizeof(fmtChunk));

			// Setup fact chunk.
			factChunk.fileSize_ = soundData.numSamples_;

			chunk.id_ = FACTChunk::ID;
			chunk.size_ = sizeof(FACTChunk);
			file.Write(&chunk, sizeof(chunk));
			file.Write(&factChunk, sizeof(factChunk));

			// Write data chunk.
			chunk.id_ = DataChunk::ID;
			chunk.size_ = soundData.numBytes_;
			file.Write(&chunk, sizeof(chunk));
			file.Write(soundData.rawData_, soundData.numBytes_);

		}
	}


	namespace Ogg
	{
		static const u32 TAG = 'SggO';

		SoundData Load(Core::File& file)
		{
			SoundData soundData;
			Core::Vector<u8> fileData;
			fileData.resize((i32)file.Size());
			file.Read(fileData.data(), fileData.size());

			int error = 0;
			stb_vorbis* vorbis = stb_vorbis_open_memory(fileData.data(), fileData.size(), &error, nullptr);
			if(vorbis)
			{
				stb_vorbis_info vorbisInfo = stb_vorbis_get_info(vorbis);

				soundData.numChannels_ = vorbisInfo.channels;
				soundData.sampleRate_ = vorbisInfo.sample_rate;
				soundData.numSamples_ = stb_vorbis_stream_length_in_samples(vorbis);
				soundData.format_ = Format::F32;
				soundData.numBytes_ = sizeof(f32) * soundData.numSamples_ * soundData.numChannels_;
				soundData.rawData_ = new u8[soundData.numBytes_];

				f32* outData = reinterpret_cast<f32*>(soundData.rawData_);

				for(;;)	
				{
					float **outputs = nullptr;
					int n = stb_vorbis_get_frame_float(vorbis, nullptr, &outputs);
					if (n == 0)
					{
						break;
					}

					for(i32 sample = 0; sample < n; ++sample)
					{
						for(i32 ch = 0; ch < soundData.numChannels_; ++ch)
						{
							*outData++ = outputs[ch][sample];
						}
					}
				}
				stb_vorbis_close(vorbis);
			}
			return std::move(soundData);
		}
	} // namespace Ogg

	SoundData::~SoundData()
	{
		delete [] rawData_;
	}

	SoundData::SoundData(SoundData&& other)
	{
		swap(other);
	}

	SoundData& SoundData::operator=(SoundData&& other)
	{
		swap(other);
		return *this;
	}

	void SoundData::swap(SoundData& other)
	{
		using std::swap;
		swap(numChannels_, other.numChannels_);
		swap(sampleRate_, other.sampleRate_);
		swap(numSamples_, other.numSamples_);
		swap(format_, other.format_);
		swap(numBytes_, other.numBytes_);
		swap(rawData_, other.rawData_);
	}

	SoundData::operator bool() const
	{
		return format_ != Format::UNKNOWN && !!rawData_;
	}


	SoundData Load(Core::File& file)
	{
		SoundData soundData;

		u32 tag = 0;
		file.Read(&tag, sizeof(tag));
		file.Seek(0);

		if(tag == Wav::TAG)
		{
			return Wav::Load(file);
		}
		else if(tag == Ogg::TAG)
		{
			return Ogg::Load(file);
		}

		return SoundData();
	}

	void Save(Core::File& file, const SoundData& soundData)
	{
		Wav::Save(file, soundData);
	}
} // namespace Sound