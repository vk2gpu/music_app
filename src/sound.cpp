#include "sound.h"
#include "core/array.h"
#include "core/concurrency.h"
#include "core/file.h"
#include "core/vector.h"
#include "job/manager.h"

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

		void ReadChunks(Core::File& file, Data& data)
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
						data.sampleRate_ = fmtChunk.sampleRate_;
						data.numChannels_ = fmtChunk.numChannels_;
						if(fmtChunk.audioFormat_ == 1 && fmtChunk.bitsPerSample_ == 16)
							data.format_ = Format::S16;
						else if(fmtChunk.audioFormat_ == 3 && fmtChunk.bitsPerSample_ == 32)
							data.format_ = Format::F32;
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
						data.numBytes_ = chunk.size_;
						data.numSamples_ = chunk.size_ / ((fmtChunk.bitsPerSample_ * fmtChunk.numChannels_) / 8);

						data.rawData_ = new u8[data.numBytes_];
						file.Read(data.rawData_, data.numBytes_);
					}
					break;
				}

				// Skip to end of chunk, regardless of reads that occurred.
				file.Seek(chunkEnd);
			}
		}

		Data Load(Core::File& file)
		{
			Data data;
			if(ReadHeader(file))
			{
				ReadChunks(file, data);
			}
			return std::move(data);
		}


		void Save(Core::File& file, const Data& data)
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
				sizeof(Chunk) + data.numBytes_;

			riffChunk.format_ = WAVE_ID;
			file.Write(&chunk, sizeof(chunk));
			file.Write(&riffChunk, sizeof(riffChunk));

			// Setup format chunk.
			switch(data.format_)
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

			fmtChunk.numChannels_ = (u16)data.numChannels_;
			fmtChunk.sampleRate_ = data.sampleRate_;
			fmtChunk.byteRate_ = (fmtChunk.numChannels_ * fmtChunk.bitsPerSample_ * fmtChunk.sampleRate_) / 8;

			chunk.id_ = FmtChunk::ID;
			chunk.size_ = sizeof(FmtChunk);
			file.Write(&chunk, sizeof(chunk));
			file.Write(&fmtChunk, sizeof(fmtChunk));

			// Setup fact chunk.
			factChunk.fileSize_ = data.numSamples_;

			chunk.id_ = FACTChunk::ID;
			chunk.size_ = sizeof(FACTChunk);
			file.Write(&chunk, sizeof(chunk));
			file.Write(&factChunk, sizeof(factChunk));

			// Write data chunk.
			chunk.id_ = DataChunk::ID;
			chunk.size_ = data.numBytes_;
			file.Write(&chunk, sizeof(chunk));
			file.Write(data.rawData_, data.numBytes_);

		}
	}


	namespace Ogg
	{
		static const u32 TAG = 'SggO';

		Data Load(Core::File& file)
		{
			Data data;
			Core::Vector<u8> fileData;
			fileData.resize((i32)file.Size());
			file.Read(fileData.data(), fileData.size());

			int error = 0;
			stb_vorbis* vorbis = stb_vorbis_open_memory(fileData.data(), fileData.size(), &error, nullptr);
			if(vorbis)
			{
				stb_vorbis_info vorbisInfo = stb_vorbis_get_info(vorbis);

				data.numChannels_ = vorbisInfo.channels;
				data.sampleRate_ = vorbisInfo.sample_rate;
				data.numSamples_ = stb_vorbis_stream_length_in_samples(vorbis);
				data.format_ = Format::F32;
				data.numBytes_ = sizeof(f32) * data.numSamples_ * data.numChannels_;
				data.rawData_ = new u8[data.numBytes_];

				f32* outData = reinterpret_cast<f32*>(data.rawData_);

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
						for(i32 ch = 0; ch < data.numChannels_; ++ch)
						{
							*outData++ = outputs[ch][sample];
						}
					}
				}
				stb_vorbis_close(vorbis);
			}
			return std::move(data);
		}
	} // namespace Ogg

	Data::~Data()
	{
		delete [] rawData_;
	}

	Data::Data(Data&& other)
	{
		swap(other);
	}

	Data& Data::operator=(Data&& other)
	{
		swap(other);
		return *this;
	}

	void Data::swap(Data& other)
	{
		using std::swap;
		swap(numChannels_, other.numChannels_);
		swap(sampleRate_, other.sampleRate_);
		swap(numSamples_, other.numSamples_);
		swap(format_, other.format_);
		swap(numBytes_, other.numBytes_);
		swap(rawData_, other.rawData_);
	}

	Data::operator bool() const
	{
		return format_ != Format::UNKNOWN && !!rawData_;
	}


	Data Load(Core::File& file)
	{
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

		return Data();
	}

	void Save(Core::File& file, const Data& data)
	{
		Wav::Save(file, data);
	}

	void Save(Core::File& rawFile, Core::File& outFile, Format format, i32 numChannels, i32 sampleRate)
	{
		Sound::Data data;
		data.numChannels_ = 1;
		data.sampleRate_ = 48000;
		data.format_ = Sound::Format::F32;
		data.numBytes_ = (u32)rawFile.Size();
		data.rawData_ = new u8[data.numBytes_];
		rawFile.Read(data.rawData_, data.numBytes_);
		data.numSamples_ = data.numBytes_ / (data.numChannels_ * sizeof(f32));
		Sound::Save(outFile, data);
	}

	void SaveSoundAsync(const char* rawFilename, const char* outFilename, Format format, i32 numChannels, i32 sampleRate)
	{
		struct Params
		{
			Core::Array<char, Core::MAX_PATH_LENGTH> inFilename_;
			Core::Array<char, Core::MAX_PATH_LENGTH> outFilename_;
			Sound::Format format_;
			i32 numChannels_;
			i32 sampleRate_;
		};

		auto* params = new Params;
		strcpy_s(params->inFilename_.data(), params->inFilename_.size(), rawFilename);
		strcpy_s(params->outFilename_.data(), params->outFilename_.size(), outFilename);
		params->format_ = format;
		params->numChannels_ = numChannels;
		params->sampleRate_ = sampleRate;

		Job::JobDesc jobDesc;
		jobDesc.func_ = [](i32 param, void* data) {
			Params* params = static_cast<Params*>(data);
				
			auto inFile = Core::File(params->inFilename_.data(), Core::FileFlags::READ);
			if(inFile)
			{
				if(Core::FileExists(params->outFilename_.data()))
				{
					Core::FileRemove(params->outFilename_.data());
				}

				auto outFile = Core::File(params->outFilename_.data(), Core::FileFlags::CREATE | Core::FileFlags::WRITE);
				Sound::Save(inFile, outFile, params->format_, params->numChannels_, params->sampleRate_);

				std::swap(inFile, Core::File());
				if(Core::FileExists(params->inFilename_.data()))
				{
					Core::FileRemove(params->inFilename_.data());
				}
			}

			delete params;
		};
		jobDesc.data_ = params;
		jobDesc.name_ = "Save file to wav";
		Job::Manager::RunJobs(&jobDesc, 1);
	}

	struct OutputStreamImpl
	{
		/// Current ID.
		u32 soundBufferID_ = 0;
		/// Sample rate to save with.
		i32 sampleRate_ = 0;
		/// Current size of buffer.
		i32 size_ = 0;
		/// Total size of buffer (inc. flushed)
		i32 totalSize_ = 0;
		/// Buffer we're writing into currently.
		Core::Vector<u8> buffer_;
		/// Buffer to flush to disk.
		Core::Vector<u8> flushBuffer_;
		/// File to flush out to incrementally.
		Core::File flushFile_;
		/// Flush file name.
		Core::Array<char, Core::MAX_PATH_LENGTH> flushFileName_;
		/// Save file name.
		Core::Array<char, Core::MAX_PATH_LENGTH> saveFileName_;
		/// Flush job counter to wait until flushing to disk has completed.
		Job::Counter* flushCounter_ = nullptr; 
	};

	volatile i32 OutputStream::SoundBufferID = 0;

	OutputStream::OutputStream(i32 sampleRate)
	{
		impl_ = new OutputStreamImpl();

		impl_->soundBufferID_ = Core::AtomicInc(&SoundBufferID);
		sprintf_s(impl_->flushFileName_.data(), impl_->flushFileName_.size(), "temp_audio_out_%08u.raw", impl_->soundBufferID_);
		sprintf_s(impl_->saveFileName_.data(), impl_->saveFileName_.size(), "audio_out_%08u.wav", impl_->soundBufferID_);
		impl_->sampleRate_ = sampleRate;
		impl_->buffer_.resize(FLUSH_SIZE);
		impl_->flushBuffer_.resize(FLUSH_SIZE);
		if(Core::FileExists(impl_->flushFileName_.data()))
		{
			Core::FileRemove(impl_->flushFileName_.data());
		}
		impl_->flushFile_ = Core::File(impl_->flushFileName_.data(), Core::FileFlags::CREATE | Core::FileFlags::WRITE);
	}

	OutputStream::~OutputStream()
	{
		FlushData();

		if(impl_->flushCounter_)
		{
			Job::Manager::WaitForCounter(impl_->flushCounter_, 0);
		}

		SaveSoundAsync(impl_->flushFileName_.data(), impl_->saveFileName_.data(), Sound::Format::F32, 1, impl_->sampleRate_);

		delete impl_;
	}

	void OutputStream::FlushData()
	{
		if(impl_->flushCounter_)
		{
			Job::Manager::WaitForCounter(impl_->flushCounter_, 0);
		}

		// Swap buffers.
		std::swap(impl_->flushBuffer_, impl_->buffer_);

		// Kick job to copy in background to avoid hitching on audio thread.
		Job::JobDesc jobDesc;
		jobDesc.func_ = [](i32 param, void* data) {
			OutputStreamImpl* impl = static_cast<OutputStreamImpl*>(data);
			impl->flushFile_.Write(impl->flushBuffer_.data(), param);
		};

		jobDesc.param_ = impl_->size_;
		jobDesc.data_ = impl_;
		jobDesc.name_ = "SoundBuffer flush";
		Job::Manager::RunJobs(&jobDesc, 1, &impl_->flushCounter_);

		impl_->size_ = 0;
	}
		
	void OutputStream::Push(const void* data, i32 size)
	{
		// Always alloc double what's needed.
		i32 requiredSize = impl_->size_ + size;
		if(requiredSize > impl_->buffer_.size())
		{
			FlushData();
		}

		memcpy(impl_->buffer_.data() + impl_->size_, data, size);
		impl_->size_ += size;
		impl_->totalSize_ += size;
	}

	u32 OutputStream::GetID() const
	{
		return impl_->soundBufferID_;
	}

} // namespace Sound
