#include "client/manager.h"
#include "client/window.h"
#include "core/array.h"
#include "core/concurrency.h"
#include "core/debug.h"
#include "core/file.h"
#include "core/misc.h"
#include "core/timer.h"
#include "gpu/manager.h"
#include "job/manager.h"
#include "imgui/manager.h"
#include "plugin/manager.h"
#include "serialization/serializer.h"

#include <algorithm>
#include <utility>

#include "audio_backend.h"

#include "ispc/acf_ispc.h"
#include "note.h"
#include "sound.h"

namespace
{
	GPU::SetupParams GetDefaultSetupParams()
	{
		GPU::SetupParams setupParams;
		setupParams.debuggerIntegration_ = GPU::DebuggerIntegrationFlags::NONE;
		return setupParams;
	}


	class SoundBuffer
	{
	public:
		static const i32 FLUSH_SIZE = 1024 * 1024 * 1;
		static volatile i32 SoundBufferID;

		SoundBuffer()
		{
			u32 soundBufferID = Core::AtomicInc(&SoundBufferID);
			sprintf_s(flushFileName_.data(), flushFileName_.size(), "temp_audio_out_%08u.raw", soundBufferID);
			sprintf_s(saveFileName_.data(), saveFileName_.size(), "audio_out_%08u.wav", soundBufferID);

			buffer_.resize(FLUSH_SIZE);
			flushBuffer_.resize(FLUSH_SIZE);
			if(Core::FileExists(flushFileName_.data()))
			{
				Core::FileRemove(flushFileName_.data());
			}
			flushFile_ = Core::File(flushFileName_.data(), Core::FileFlags::CREATE | Core::FileFlags::WRITE);
		}

		~SoundBuffer()
		{
			FlushData();

			if(flushCounter_)
			{
				Job::Manager::WaitForCounter(flushCounter_, 0);
			}

			// Kick off save job in background after destruction.
			struct Params
			{
				Core::Array<char, Core::MAX_PATH_LENGTH> inFilename_;
				Core::Array<char, Core::MAX_PATH_LENGTH> outFilename_;
			};

			auto* params = new Params;
			params->inFilename_ = flushFileName_;
			params->outFilename_ = saveFileName_;

			Job::JobDesc jobDesc;
			jobDesc.func_ = [](i32 param, void* data) {
				Params* params = static_cast<Params*>(data);
				
				auto inFile = Core::File(params->inFilename_.data(), Core::FileFlags::READ);
				if(inFile)
				{
					Sound::SoundData soundData;
					soundData.numChannels_ = 1;
					soundData.sampleRate_ = 48000;
					soundData.format_ = Sound::Format::F32;
					soundData.numBytes_ = inFile.Size();
					soundData.rawData_ = new u8[soundData.numBytes_];
					inFile.Read(soundData.rawData_, soundData.numBytes_);
					soundData.numSamples_ = soundData.numBytes_ / (soundData.numChannels_ * sizeof(f32));

					if(Core::FileExists(params->outFilename_.data()))
					{
						Core::FileRemove(params->outFilename_.data());
					}

					auto outFile = Core::File(params->outFilename_.data(), Core::FileFlags::CREATE | Core::FileFlags::WRITE);
					Sound::Save(outFile, soundData);

					std::swap(inFile, Core::File());
					if(Core::FileExists(params->inFilename_.data()))
					{
						Core::FileRemove(params->inFilename_.data());
					}
				}				
			};
			jobDesc.data_ = params;
			jobDesc.name_ = "Save file to wav";
			Job::Manager::RunJobs(&jobDesc, 1);
		}

		void FlushData()
		{
			if(flushCounter_)
			{
				Job::Manager::WaitForCounter(flushCounter_, 0);
			}

			// Swap buffers.
			std::swap(flushBuffer_, buffer_);

			// Kick job to copy in background to avoid hitching on audio thread.
			Job::JobDesc jobDesc;
			jobDesc.func_ = [](i32 param, void* data) {
				SoundBuffer* soundBuffer = static_cast<SoundBuffer*>(data);
				soundBuffer->flushFile_.Write(soundBuffer->flushBuffer_.data(), param);
			};

			jobDesc.param_ = size_;
			jobDesc.data_ = this;
			jobDesc.name_ = "SoundBuffer flush";
			Job::Manager::RunJobs(&jobDesc, 1, &flushCounter_);

			size_ = 0;
		}
		
		void Push(const void* data, i32 size)
		{
			// Always alloc double what's needed.
			i32 requiredSize = size_ + size;
			if(requiredSize > buffer_.size())
			{
				FlushData();
			}

			memcpy(buffer_.data() + size_, data, size);
			size_ += size;
			totalSize_ += size;
		}

	private:
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

	volatile i32 SoundBuffer::SoundBufferID = 0;
	
	static const i32 AUDIO_DATA_SIZE = 512;

	class TestAudioCallback : public IAudioCallback
	{
	public:
		void OnAudioCallback(i32 numIn, i32 numOut, const f32** in, f32** out, i32 numFrames) override
		{
			// If RMS is over a certain amount, create a sound buffer.
			if(numIn > 0)
			{
				f64 rms = 0.0f;
				const f32* inData = in[0];
				for(i32 idx = 0; idx < numFrames; ++idx)
				{
					rms += (inData[idx] * inData[idx]);
				}
				rms = sqrt((1.0 / numFrames) * rms);

				if(soundBuffer_ == nullptr && rms > 0.01f)
				{
					soundBuffer_ = new SoundBuffer();
				}


				if(rms > 0.01f)
				{
					lowRmsSamples_ = 0;
				}
				else
				{
					lowRmsSamples_ += numFrames;
				}

				if(soundBuffer_ != nullptr && lowRmsSamples_ > (2 * 48000))
				{
					Core::AtomicExchg(&saveBuffer_, 1);
				}

				// Create and push to sound buffer.
				if(soundBuffer_ != nullptr)
				{
					soundBuffer_->Push(in[0], sizeof(f32) * numFrames);

					// If we need to save, kick of a job to delete and finalize.
					if(Core::AtomicCmpExchg(&saveBuffer_, 0, 1) == 1)
					{
						if(soundBufferCounter_)
						{
							Job::Manager::WaitForCounter(soundBufferCounter_, 0);
						}

						Job::JobDesc jobDesc;
						jobDesc.func_ = [](i32 param, void* data) {
							SoundBuffer* soundBuffer = static_cast<SoundBuffer*>(data);
							delete soundBuffer;
						};

						jobDesc.param_ = 0;
						jobDesc.data_ = soundBuffer_;
						jobDesc.name_ = "SoundBuffer save";
						Job::Manager::RunJobs(&jobDesc, 1, &soundBufferCounter_);

						soundBuffer_ = nullptr;
					}
				}

				if(numOut > 0)
				{
					for(i32 idx = 0; idx < (i32)numFrames; ++idx)
					{
						for(i32 ch = 0; ch < numOut; ++ch)
						{
							out[ch][idx] = sin((f32)freqTick_);
						}

						freqTick_ += freq_ / (48000.0 / Core::F32_PIMUL2);
					}

					if(freqTick_ > Core::F32_PIMUL2)
						freqTick_ -= Core::F32_PIMUL2;
				}

				if((audioDataOffset_ + (i32)numFrames) < audioData_.size())
				{
					memcpy(audioData_.data() + audioDataOffset_, in[0], sizeof(f32) * numFrames);
					audioDataOffset_ += numFrames;
				}
				else
				{
					const i32 firstBlock = (audioData_.size() - audioDataOffset_);
					memcpy(audioData_.data() + audioDataOffset_, in[0], sizeof(f32) * firstBlock);
					audioDataOffset_ = 0;
					numFrames -= firstBlock;
				
					memcpy(audioData_.data() + audioDataOffset_, in[0], sizeof(f32) * numFrames);
					audioDataOffset_ += numFrames;
				}
			}
		}

		const f32* GetAudioData() const
		{
			return audioData_.data();
		}
		
		i32 GetAudioDataOffset() const
		{
			return audioDataOffset_;
		}

		void SetOutputFrequency(f32 freq)
		{
			freq_ = freq;
		}

		void SaveBuffer()
		{
			Core::AtomicExchg(&saveBuffer_, 1);
		}


	private:
		Core::Array<f32, AUDIO_DATA_SIZE> audioData_;
		i32 audioDataOffset_ = 0;

		f32 freq_ = 440.0f;
		f64 freqTick_ = 0.0f;

		SoundBuffer* soundBuffer_ = nullptr;
		Job::Counter* soundBufferCounter_ = nullptr;

		i32 lowRmsSamples_ = 0;

		volatile i32 saveBuffer_ = 0;
		
	};

	class MainWindow
	{
	public:
		MainWindow()
		{
			audioBackend_.Enumerate();

			LoadSettings();
		}

		void operator()()
		{
			if(ImGui::Begin("Main", nullptr))
			{
				if(ImGui::Button("Refresh Devices"))
				{
					audioBackend_.Enumerate();
				}

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

				settings_.inputDevice_ = Core::Min(settings_.inputDevice_, audioBackend_.GetNumInputDevices());
				settings_.outputDevice_ = Core::Min(settings_.outputDevice_, audioBackend_.GetNumOutputDevices());

				ImGui::ListBox("Inputs", &settings_.inputDevice_, inputDeviceNames.data(), audioBackend_.GetNumInputDevices());
				ImGui::ListBox("Outputs", &settings_.outputDevice_, outputDeviceNames.data(), audioBackend_.GetNumOutputDevices());

				if(settings_.inputDevice_ >= 0)
				{
					const auto& deviceInfo = audioBackend_.GetInputDeviceInfo(settings_.inputDevice_);
					ImGui::SliderInt("Channel:", &settings_.inputChannel_, 0, deviceInfo.maxIn_ - 1);
				}
				
				if(ImGui::Button("Start"))
				{
					audioBackend_.StartDevice(settings_.inputDevice_, settings_.outputDevice_, AUDIO_DATA_SIZE);
					audioBackend_.RegisterCallback(&audioCallback_, 1 << settings_.inputChannel_, 0xff);

					SaveSettings();
				}
				
				if(ImGui::Button("Save"))
				{
					audioCallback_.SaveBuffer();
				}

				ImGui::PlotLines("Input:", audioCallback_.GetAudioData(), AUDIO_DATA_SIZE, audioCallback_.GetAudioDataOffset(), nullptr, -1.0f, 1.0f, ImVec2(0.0f, 128.0f));

				f64 rms = 0.0f;
				const f32* inData = audioCallback_.GetAudioData();
				for(i32 idx = 0; idx < AUDIO_DATA_SIZE; ++idx)
				{
					rms += (inData[idx] * inData[idx]);
				}
				rms = sqrt((1.0 / AUDIO_DATA_SIZE) * rms);

				ImGui::Text("RMS: %f", rms);

				// Perform autocorrelation.
				Core::Array<f32, AUDIO_DATA_SIZE> acfData;
				ispc::acf_process(AUDIO_DATA_SIZE, audioCallback_.GetAudioData(), acfData.data());
				i32 largestPeriod = ispc::acf_largest_peak_period(AUDIO_DATA_SIZE, acfData.data());
				ImGui::PlotLines("ACF:", acfData.data(), AUDIO_DATA_SIZE, 0, nullptr, -2.0f, 2.0f, ImVec2(0.0f, 128.0f));

				static f64 freq = 0.0f;
				if(largestPeriod > 6 && rms > 0.01)
				{
					f64 newFreq = 48000.0 / (f64)largestPeriod;

					if(newFreq > 50.0 && newFreq < 2000.0)
						freq = newFreq;

					audioCallback_.SetOutputFrequency(freq);
				}

				freq_[freqIdx_] = (f32)freq;
				ImGui::PlotLines("Freq:", freq_.data(), freq_.size(), freqIdx_, nullptr, 0.0f, 880.0f, ImVec2(0.0f, 128.0f));


				Core::Array<f32, AUDIO_DATA_SIZE> fftOut;

				freqIdx_ = (freqIdx_ + 1) % freq_.size();

				ImGui::Text("Freq: %f hz",  freq);

				i32 midiNote = FreqToMidi((f32)freq);
				char note[8];
				MidiToString(midiNote, note, sizeof(note));
				ImGui::Text("Note: %s (%d)", note, midiNote);

				if(midiNote >= 0 && midiNote <= 127)
				{
					i32 singleOctaveNote = midiNote % 12;

					singleOctaveNote += 12 * 5;
					//audioCallback_.SetOutputFrequency(MidiToFreq(singleOctaveNote));

				}

			}
			ImGui::End();
		}

		void SaveSettings()
		{
			auto file = Core::File("settings.json", Core::FileFlags::CREATE | Core::FileFlags::WRITE);
			if(file)
			{
				Serialization::Serializer ser(file, Serialization::Flags::TEXT);
				ser.SerializeObject("settings", settings_);
			}
		}

		void LoadSettings()
		{
			auto file = Core::File("settings.json", Core::FileFlags::READ);
			if(file)
			{
				Serialization::Serializer ser(file, Serialization::Flags::TEXT);
				ser.SerializeObject("settings", settings_);
			}
		}

	private:
		AudioBackend audioBackend_;

		TestAudioCallback audioCallback_;

		Core::Array<f32, 1024> freq_;
		i32 freqIdx_ = 0;
		
		struct Settings
		{
			i32 inputDevice_ = 0;
			i32 outputDevice_ = 0;
			i32 inputChannel_ = 0;

			bool Serialize(Serialization::Serializer& ser)
			{
				ser.Serialize("inputDevice", inputDevice_);
				ser.Serialize("outputDevice", outputDevice_);
				ser.Serialize("inputChannel", inputChannel_);
				return true;
			}
		};

		Settings settings_;
	};

}


int main(int argc, char* const argv[])
{
	// Change to executable path.
	char path[Core::MAX_PATH_LENGTH];
	if(Core::FileSplitPath(argv[0], path, Core::MAX_PATH_LENGTH, nullptr, 0, nullptr, 0))
	{
		Core::FileChangeDir(path);
	}


	Client::Manager::Scoped clientManager;
	Client::Window window("Music Practice App", 100, 100, 1024, 768, true);

	Plugin::Manager::Scoped pluginManager;
	Job::Manager::Scoped jobManager(4, 256, 256 * 1024);
	GPU::Manager::Scoped gpuManager(GetDefaultSetupParams());

	i32 numAdapters = GPU::Manager::EnumerateAdapters(nullptr, 0);
	if(numAdapters == 0)
		return 1;

	if(GPU::Manager::CreateAdapter(0) != GPU::ErrorCode::OK)
	{
		return 1;
	}

	ImGui::Manager::Scoped imguiManager;

	GPU::SwapChainDesc scDesc;
	scDesc.width_ = 1024;
	scDesc.height_ = 768;
	scDesc.format_ = GPU::Format::R8G8B8A8_UNORM;
	scDesc.bufferCount_ = 2;
	scDesc.outputWindow_ = window.GetPlatformData().handle_;

	GPU::Handle scHandle;
	scHandle = GPU::Manager::CreateSwapChain(scDesc, "sandbox_app");

	GPU::FrameBindingSetDesc fbDesc;
	fbDesc.rtvs_[0].resource_ = scHandle;
	fbDesc.rtvs_[0].format_ = scDesc.format_;
	fbDesc.rtvs_[0].dimension_ = GPU::ViewDimension::TEX2D;

	GPU::Handle fbsHandle = GPU::Manager::CreateFrameBindingSet(fbDesc, "sandbox_app");
	GPU::Handle cmdHandle = GPU::Manager::CreateCommandList("sandbox_app");
	GPU::CommandList cmdList(GPU::Manager::GetHandleAllocator());

	f32 color[] = {0.1f, 0.1f, 0.2f, 1.0f};

	const Client::IInputProvider& input = window.GetInputProvider();
	
	MainWindow mainWindow;

	while(Client::Manager::Update())
	{
		// Reset command list to reuse.
		cmdList.Reset();

		// Clear swapchain.
		cmdList.ClearRTV(fbsHandle, 0, color);

		ImGui::Manager::BeginFrame(input, scDesc.width_, scDesc.height_);

		mainWindow();

		ImGui::Manager::EndFrame(fbsHandle, cmdList);

		// Compile and submit.
		GPU::Manager::CompileCommandList(cmdHandle, cmdList);
		GPU::Manager::SubmitCommandList(cmdHandle);

		// Present.
		GPU::Manager::PresentSwapChain(scHandle);

		// Next frame.
		GPU::Manager::NextFrame();

		// Force a sleep.
		Core::Sleep(1.0 / 60.0);
	}

	GPU::Manager::DestroyResource(cmdHandle);
	GPU::Manager::DestroyResource(fbsHandle);
	GPU::Manager::DestroyResource(scHandle);

	return 0;
}
