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

#include <algorithm>
#include <utility>

#include <portaudio.h>

#include "ispc/acf_ispc.h"
#include "note.h"

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
		/// Flush job counter to wait until flushing to disk has completed.
		Job::Counter* flushCounter_ = nullptr; 

	};

	volatile i32 SoundBuffer::SoundBufferID = 0;
	
	static const i32 AUDIO_DATA_SIZE = 2048;

	class PortAudio
	{
	public:
		struct DeviceInfo
		{
			char name_[256] = {0};
			char backend_[256] = {0};
			PaDeviceIndex deviceIdx_ = 0;
			i32 maxIn_ = 0;
			i32 maxOut_ = 0;
		};

		PortAudio()
		{
			Pa_Initialize();
			audioData_.fill(0.0f);
		}

		~PortAudio()
		{
			if(soundBufferCounter_)
			{
				Job::Manager::WaitForCounter(soundBufferCounter_, 0);
			}

			Pa_Terminate();
		}

		void Enumerate()
		{
			inputDeviceInfos_.clear();
			outputDeviceInfos_.clear();
			i32 numDevices = Pa_GetDeviceCount();
			inputDeviceInfos_.reserve(numDevices);
			outputDeviceInfos_.reserve(numDevices);

			for(i32 idx = 0; idx < numDevices; ++idx)
			{
				const auto* paDeviceInfo = Pa_GetDeviceInfo(idx);
				if(paDeviceInfo)
				{
					DeviceInfo deviceInfo;
					const auto* paHostAPIInfo = Pa_GetHostApiInfo(paDeviceInfo->hostApi);
					sprintf_s(deviceInfo.name_, sizeof(deviceInfo.name_), "[%s] - %s", paHostAPIInfo->name, paDeviceInfo->name);
					deviceInfo.deviceIdx_ = idx;
					deviceInfo.maxIn_ = paDeviceInfo->maxInputChannels;
					deviceInfo.maxOut_ = paDeviceInfo->maxOutputChannels;
					
					
					if(deviceInfo.maxIn_ > 0)
						inputDeviceInfos_.push_back(deviceInfo);
					if(deviceInfo.maxOut_ > 0)
						outputDeviceInfos_.push_back(deviceInfo);
				}
			}

			std::sort(inputDeviceInfos_.begin(), inputDeviceInfos_.end(), 
				[](const DeviceInfo& a, const DeviceInfo& b)
				{
					return strcmp(a.name_, b.name_) < 0;
				});
			std::sort(outputDeviceInfos_.begin(), outputDeviceInfos_.end(), 
				[](const DeviceInfo& a, const DeviceInfo& b)
				{
					return strcmp(a.name_, b.name_) < 0;
				});
		}

		void StartDevice(i32 in, i32 out, i32 inChannel)
		{
			if(stream_)
			{
				Pa_StopStream(stream_);
				Pa_CloseStream(stream_);
				stream_ = nullptr;
				audioData_.fill(0.0f);
			}
			const auto& inputDevice = inputDeviceInfos_[in];
			const auto& outputDevice = outputDeviceInfos_[out];
			const auto* paDeviceInfoIn = Pa_GetDeviceInfo(inputDevice.deviceIdx_);
			const auto* paDeviceInfoOut = Pa_GetDeviceInfo(outputDevice.deviceIdx_);

			inChannel_ = inChannel;
			outChannels_ = outputDevice.maxOut_;

			PaStreamParameters inParams;
			inParams.device = inputDevice.deviceIdx_;
			inParams.channelCount = inputDevice.maxIn_;
			inParams.sampleFormat = paFloat32 | paNonInterleaved;
			inParams.suggestedLatency = paDeviceInfoIn->defaultLowInputLatency;
			inParams.hostApiSpecificStreamInfo = nullptr;

			PaStreamParameters outParams;
			outParams.device = outputDevice.deviceIdx_;
			outParams.channelCount = outputDevice.maxOut_;
			outParams.sampleFormat = paFloat32 | paNonInterleaved;
			outParams.suggestedLatency = paDeviceInfoOut->defaultLowOutputLatency;
			outParams.hostApiSpecificStreamInfo = nullptr;

			Pa_OpenStream(&stream_, &inParams, &outParams, 48000.0, AUDIO_DATA_SIZE, paNoFlag, StaticStreamCallback, this);
			Pa_StartStream(stream_);
		}

		inline int StreamCallback(const void *input, void *output,
			unsigned long frameCount,
			const PaStreamCallbackTimeInfo* timeInfo,
			PaStreamCallbackFlags statusFlags)
		{
			const f32* const* fin = reinterpret_cast<const f32* const*>(input);
			f32** fout = reinterpret_cast<f32**>(output);

			// If RMS is over a certain amount, create a sound buffer.
			f64 rms = 0.0f;
			const f32* inData = fin[inChannel_];
			for(i32 idx = 0; idx < AUDIO_DATA_SIZE; ++idx)
			{
				rms += (inData[idx] * inData[idx]);
			}
			rms = sqrt((1.0 / AUDIO_DATA_SIZE) * rms);

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
				lowRmsSamples_ += frameCount;
			}

			if(soundBuffer_ != nullptr && lowRmsSamples_ > (2 * 48000))
			{
				Core::AtomicExchg(&saveBuffer_, 1);
			}

			// Create and push to sound buffer.
			if(soundBuffer_ != nullptr)
			{
				soundBuffer_->Push(fin[inChannel_], sizeof(f32) * frameCount);

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


			if(statusFlags != 0x0)
			{
				Core::Log("Status flags: 0x%x\n", statusFlags);
			}

			if(fout)
			{
				for(i32 idx = 0; idx < (i32)frameCount; ++idx)
				{
					for(i32 ch = 0; ch < outChannels_; ++ch)
					{
						fout[ch][idx] = sin((f32)freqTick_);
					}

					freqTick_ += freq_ / (48000.0 / Core::F32_PIMUL2);
				}

				if(freqTick_ > Core::F32_PIMUL2)
					freqTick_ -= Core::F32_PIMUL2;
			}

			if((audioDataOffset_ + (i32)frameCount) < audioData_.size())
			{
				memcpy(audioData_.data() + audioDataOffset_, fin[inChannel_], sizeof(f32) * frameCount);
				audioDataOffset_ += frameCount;
			}
			else
			{
				const i32 firstBlock = (audioData_.size() - audioDataOffset_);
				memcpy(audioData_.data() + audioDataOffset_, fin[inChannel_], sizeof(f32) * firstBlock);
				audioDataOffset_ = 0;
				frameCount -= firstBlock;
				
				memcpy(audioData_.data() + audioDataOffset_, fin[inChannel_], sizeof(f32) * frameCount);
				audioDataOffset_ += frameCount;
			}

			return paContinue;
		}

		static int StaticStreamCallback(
		    const void *input, void *output,
			unsigned long frameCount,
			const PaStreamCallbackTimeInfo* timeInfo,
			PaStreamCallbackFlags statusFlags,
			void *userData )
		{
			PortAudio* _this = (PortAudio*)userData;
			return _this->StreamCallback(input, output, frameCount, timeInfo, statusFlags);
		}



		i32 GetNumInputDevices() const
		{
			return inputDeviceInfos_.size();
		}

		i32 GetNumOutputDevices() const
		{
			return outputDeviceInfos_.size();
		}

		const DeviceInfo& GetInputDeviceInfo(i32 idx)
		{
			return inputDeviceInfos_[idx];
		}

		const DeviceInfo& GetOutputDeviceInfo(i32 idx)
		{
			return outputDeviceInfos_[idx];
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
		Core::Vector<DeviceInfo> inputDeviceInfos_;
		Core::Vector<DeviceInfo> outputDeviceInfos_;

		PaStream* stream_ = nullptr;

		i32 inChannel_ = 0;
		i32 outChannels_ = 0;

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
			portAudio_.Enumerate();
		}

		void operator()()
		{
			if(ImGui::Begin("Main", nullptr))
			{
				if(ImGui::Button("Refresh Devices"))
				{
					portAudio_.Enumerate();
				}

				Core::Array<const char*, 256> inputDeviceNames;
				Core::Array<const char*, 256> outputDeviceNames;
				for(i32 idx = 0; idx < portAudio_.GetNumInputDevices(); ++idx)
				{
					inputDeviceNames[idx] = portAudio_.GetInputDeviceInfo(idx).name_;
				}
				for(i32 idx = 0; idx < portAudio_.GetNumOutputDevices(); ++idx)
				{
					outputDeviceNames[idx] = portAudio_.GetOutputDeviceInfo(idx).name_;
				}

				selectedInputDeviceIdx_ = Core::Min(selectedInputDeviceIdx_, portAudio_.GetNumInputDevices());
				selectedOutputDeviceIdx_ = Core::Min(selectedOutputDeviceIdx_, portAudio_.GetNumOutputDevices());

				ImGui::ListBox("Inputs", &selectedInputDeviceIdx_, inputDeviceNames.data(), portAudio_.GetNumInputDevices());
				ImGui::ListBox("Outputs", &selectedOutputDeviceIdx_, outputDeviceNames.data(), portAudio_.GetNumOutputDevices());

				if(selectedInputDeviceIdx_ >= 0)
				{
					const auto& deviceInfo = portAudio_.GetInputDeviceInfo(selectedInputDeviceIdx_);
					ImGui::SliderInt("Channel:", &selectedInputChannel_, 0, deviceInfo.maxIn_ - 1);
				}
				
				if(ImGui::Button("Start"))
				{
					portAudio_.StartDevice(selectedInputDeviceIdx_, selectedOutputDeviceIdx_, selectedInputChannel_);
				}
				
				if(ImGui::Button("Save"))
				{
					portAudio_.SaveBuffer();
				}

				ImGui::PlotLines("Input:", portAudio_.GetAudioData(), AUDIO_DATA_SIZE, portAudio_.GetAudioDataOffset(), nullptr, -1.0f, 1.0f, ImVec2(0.0f, 128.0f));

				f64 rms = 0.0f;
				const f32* inData = portAudio_.GetAudioData();
				for(i32 idx = 0; idx < AUDIO_DATA_SIZE; ++idx)
				{
					rms += (inData[idx] * inData[idx]);
				}
				rms = sqrt((1.0 / AUDIO_DATA_SIZE) * rms);

				ImGui::Text("RMS: %f", rms);

				// Perform autocorrelation.
				Core::Array<f32, AUDIO_DATA_SIZE> acfData;
				ispc::acf_process(AUDIO_DATA_SIZE, portAudio_.GetAudioData(), acfData.data());
				i32 largestPeriod = ispc::acf_largest_peak_period(AUDIO_DATA_SIZE, acfData.data());
				ImGui::PlotLines("ACF:", acfData.data(), AUDIO_DATA_SIZE, 0, nullptr, -2.0f, 2.0f, ImVec2(0.0f, 128.0f));

				static f64 freq = 0.0f;
				if(largestPeriod > 6 && rms > 0.01)
				{
					f64 newFreq = 48000.0 / (f64)largestPeriod;

					if(newFreq > 50.0 && newFreq < 2000.0)
						freq = newFreq;

					
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
					portAudio_.SetOutputFrequency(MidiToFreq(singleOctaveNote));

				}

			}
			ImGui::End();
		}


	private:
		PortAudio portAudio_;

		Core::Array<f32, 1024> freq_;
		i32 freqIdx_ = 0;
		
		int selectedInputDeviceIdx_ = 0;
		int selectedInputChannel_ = 0;
		int selectedOutputDeviceIdx_ = 0;
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
	Client::Window window("sandbox_app", 100, 100, 1024, 768, true);

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
