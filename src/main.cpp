#include "audio_backend.h"
#include "dialog_device_selection.h"
#include "note.h"
#include "sound.h"

#include "ispc/acf_ispc.h"
#include "ispc/audio_stats_ispc.h"
#include "ispc/biquad_filter_ispc.h"

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

namespace
{
	AudioBackend audioBackend_;
	AudioDeviceSettings audioDeviceSettings_;

	GPU::SetupParams GetDefaultSetupParams()
	{
		GPU::SetupParams setupParams;
		setupParams.debuggerIntegration_ = GPU::DebuggerIntegrationFlags::NONE;
		return setupParams;
	}

	void SaveSoundAsync(const char* rawFilename, const char* outFilename, Sound::Format format, i32 numChannels, i32 sampleRate)
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


	class SoundBuffer
	{
	public:
		static const i32 FLUSH_SIZE = 1024 * 1024 * 1;
		static volatile i32 SoundBufferID;

		SoundBuffer()
		{
			soundBufferID_ = Core::AtomicInc(&SoundBufferID);
			sprintf_s(flushFileName_.data(), flushFileName_.size(), "temp_audio_out_%08u.raw", soundBufferID_);
			sprintf_s(saveFileName_.data(), saveFileName_.size(), "audio_out_%08u.wav", soundBufferID_);

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

			SaveSoundAsync(flushFileName_.data(), saveFileName_.data(), Sound::Format::F32, 1, audioDeviceSettings_.sampleRate_);
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

		u32 GetID() const { return soundBufferID_; }

	private:
		/// Current ID.
		u32 soundBufferID_ = 0;
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
	
	static const i32 AUDIO_DATA_SIZE = 2048;

	/// Gathers stats for input audio.
	class AudioStatsCallback : public IAudioCallback
	{
	public:
		AudioStatsCallback()
		{
		}

		void OnAudioCallback(i32 numIn, i32 numOut, const f32** in, f32** out, i32 numFrames) override
		{
			if(numIn > 0)
			{
				rms_ = ispc::audio_stats_rms(in[0], numFrames);
				max_ = ispc::audio_stats_max(in[0], numFrames);
				
				/// TODO: Lerp to 0.0 to scale for different buffer size/sample rate.
				rmsSmoothed_ *= 0.99f;
				maxSmoothed_ *= 0.99f;

				rmsSmoothed_ = Core::Max(rmsSmoothed_, rms_);
				maxSmoothed_ = Core::Max(maxSmoothed_, max_);
			}
		}


		f32 rms_ = 0.0f;
		f32 max_ = 0.0f;

		f32 rmsSmoothed_ = 0.0f;
		f32 maxSmoothed_ = 0.0f;
	};

	/// Handles automatic recording to disc.
	class AudioRecordingCallback : public IAudioCallback
	{
	public:
		AudioRecordingCallback(AudioStatsCallback& audioStats)
			: audioStats_(audioStats)
		{
		}

		virtual ~AudioRecordingCallback()
		{
			if(soundBufferCounter_)
			{
				Job::Manager::WaitForCounter(soundBufferCounter_, 0);
			}
			delete soundBuffer_;
		}

		void OnAudioCallback(i32 numIn, i32 numOut, const f32** in, f32** out, i32 numFrames) override
		{
			if(numIn > 0)
			{
				if(soundBuffer_ == nullptr && audioStats_.max_ > 0.01f)
				{
					soundBuffer_ = new SoundBuffer();
				}

				if(audioStats_.max_ > 0.1f)
				{
					lowMaxSamples_ = 0;
				}
				else
				{
					lowMaxSamples_ += numFrames;
				}

				if(soundBuffer_ != nullptr && lowMaxSamples_ > (2 * audioDeviceSettings_.sampleRate_))
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

						Core::ScopedMutex lock(recordingMutex_);
						recordingIds_.push_back(soundBuffer_->GetID());

						jobDesc.param_ = 0;
						jobDesc.data_ = soundBuffer_;
						jobDesc.name_ = "SoundBuffer save";
						Job::Manager::RunJobs(&jobDesc, 1, &soundBufferCounter_);

						soundBuffer_ = nullptr;

						lowMaxSamples_ = 0;
					}
				}
			}
		}

		void SaveBuffer()
		{
			Core::AtomicExchg(&saveBuffer_, 1);
		}

		Core::Vector<i32> GetRecordingIDs() const
		{
			Core::ScopedMutex lock(recordingMutex_);
			return recordingIds_;
		}

	private:
		AudioStatsCallback& audioStats_;

		SoundBuffer* soundBuffer_ = nullptr;
		Job::Counter* soundBufferCounter_ = nullptr;

		i32 lowMaxSamples_ = 0;

		volatile i32 saveBuffer_ = 0;

		mutable Core::Mutex recordingMutex_;
		Core::Vector<i32> recordingIds_;
	};

	/// Audio callback for testing.
	class TestAudioCallback : public IAudioCallback
	{
	public:
		TestAudioCallback()
		{
			lp_ = ispc::biquad_filter_passthrough();
			hp_ = ispc::biquad_filter_passthrough();
			memset(lpb_.data(), 0, sizeof(lpb_));
			memset(hpb_.data(), 0, sizeof(hpb_));
		}

		void OnAudioCallback(i32 numIn, i32 numOut, const f32** in, f32** out, i32 numFrames) override
		{
			if(numIn > 0)
			{
				if(numOut > 0)
				{	
					for(int i = 0; i < numOut; ++i)
					{
						ispc::biquad_filter_process(&lp_, &lpb_[i], in[0], out[i], numFrames);
						ispc::biquad_filter_process(&hp_, &hpb_[i], out[i], out[i], numFrames);
					}
				}

				i32 audioDataFrames = Core::Min(audioData_.size(), numFrames);
				if((audioDataOffset_ + (i32)numFrames) < audioData_.size())
				{
					memcpy(audioData_.data() + audioDataOffset_, out[0], sizeof(f32) * audioDataFrames);
					audioDataOffset_ += audioDataFrames;
				}
				else
				{
					const i32 firstBlock = (audioData_.size() - audioDataOffset_);
					memcpy(audioData_.data() + audioDataOffset_, out[0], sizeof(f32) * firstBlock);
					audioDataOffset_ = 0;
					audioDataFrames -= firstBlock;
				
					memcpy(audioData_.data() + audioDataOffset_, out[0], sizeof(f32) * audioDataFrames);
					audioDataOffset_ += audioDataFrames;
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

		Core::Array<f32, AUDIO_DATA_SIZE> audioData_;
		i32 audioDataOffset_ = 0;

		f32 freq_ = 440.0f;
		f64 freqTick_ = 0.0f;

		ispc::BiquadCoeff lp_;
		ispc::BiquadCoeff hp_;

		Core::Array<ispc::BiquadBuffer, 8> lpb_;
		Core::Array<ispc::BiquadBuffer, 8> hpb_;	
	};

	class MainWindow
	{
	public:
		MainWindow()
			: dialogDeviceSelection_(audioBackend_, audioDeviceSettings_)
			, audioRecordingCallback_(audioStatsCallback_)
		{
			audioBackend_.RegisterCallback(&audioStatsCallback_, 0x1, 0x0);
			audioBackend_.RegisterCallback(&audioRecordingCallback_, 0x1, 0x0);
			audioBackend_.RegisterCallback(&audioCallback_, 0x1, 0xff);
			
			if(audioBackend_.StartDevice(audioDeviceSettings_))
			{
				deviceSelectionStatus_ = DeviceSelectionStatus::SELECTED;
			}
		}

		~MainWindow()
		{
			audioBackend_.UnregisterCallback(&audioStatsCallback_);
			audioBackend_.UnregisterCallback(&audioRecordingCallback_);
			audioBackend_.UnregisterCallback(&audioCallback_);
		}

		void MenuBar()
		{
			if(ImGui::BeginMainMenuBar())
			{
				if(ImGui::BeginMenu("File"))
				{
					if(ImGui::MenuItem("Quit"))
					{
						exit(0);
					}
					ImGui::EndMenu();
				}

				if(ImGui::BeginMenu("Settings"))
				{
					if(ImGui::MenuItem("Device"))
					{
						deviceSelectionStatus_ = DeviceSelectionStatus::NONE;
					}

					ImGui::EndMenu();
				}

				ImGui::EndMainMenuBar();
			}
		}

		void operator()(const Client::Window& window)
		{
			MenuBar();

			// Device selection.
			if(deviceSelectionStatus_ == DeviceSelectionStatus::NONE)
			{
				deviceSelectionStatus_ = dialogDeviceSelection_.Update();
				if(deviceSelectionStatus_ == DeviceSelectionStatus::SELECTED)
				{
					audioDeviceSettings_ = dialogDeviceSelection_.GetSettings();
					audioDeviceSettings_.Save();
				}
			}
			else
			{

				if(ImGui::Begin("Debug", nullptr))
				{
					ImGui::Text("Audio Input");
					ImGui::PlotLines("Input", audioCallback_.GetAudioData(), AUDIO_DATA_SIZE, audioCallback_.GetAudioDataOffset(), nullptr, -1.0f, 1.0f, ImVec2(0.0f, 128.0f));

					static f32 lp = 20000.0f;
					static f32 hp = 10.0f;
					ImGui::SliderFloat("LP", &lp, 10.0f, 20000.0f);
					ImGui::SliderFloat("HP", &hp, 10.0f, 20000.0f);
					audioCallback_.lp_ = ispc::biquad_filter_lowpass(audioDeviceSettings_.sampleRate_, lp, 0.0f);
					audioCallback_.hp_ = ispc::biquad_filter_highpass(audioDeviceSettings_.sampleRate_, hp, 0.0f);

					
					f32 rms = audioStatsCallback_.rms_;
					f32 rmsSmoothed = audioStatsCallback_.rmsSmoothed_;
					//ImGui::SliderFloat("RMS", &rms, 0.0f, 1.0f);
					ImGui::SliderFloat("RMS Smoothed", &rmsSmoothed, 0.0f, 1.0f);

					f32 max = audioStatsCallback_.max_;
					f32 maxSmoothed = audioStatsCallback_.maxSmoothed_;
					//ImGui::SliderFloat("Max", &max, 0.0f, 1.0f);
					ImGui::SliderFloat("Max Smoothed", &maxSmoothed, 0.0f, 1.0f);

					if(ImGui::Button("Save"))
					{
						audioRecordingCallback_.SaveBuffer();
					}

					ImGui::Separator();
					ImGui::Text("Recordings");

					Core::Array<char, Core::MAX_PATH_LENGTH> fileName;
					auto recordingIds = audioRecordingCallback_.GetRecordingIDs();
					for(auto id : recordingIds)
					{
						sprintf_s(fileName.data(), fileName.size(), "audio_out_%08u.wav", id);
						ImGui::BulletText("- %s", fileName.data());
					}
				}
				ImGui::End();
			}
		}

	private:		
		AudioStatsCallback audioStatsCallback_;
		AudioRecordingCallback audioRecordingCallback_;
		TestAudioCallback audioCallback_;

		DialogDeviceSelection dialogDeviceSelection_;
		DeviceSelectionStatus deviceSelectionStatus_ = DeviceSelectionStatus::NONE;

		Core::Array<f32, 1024> freq_;
		i32 freqIdx_ = 0;
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

	// Load settings.
	audioDeviceSettings_.Load();

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

		mainWindow(window);

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
