#include "app.h"
#include "audio_backend.h"
#include "dialog_device_selection.h"
#include "gui.h"
#include "midi_backend.h"
#include "settings.h"
#include "sound.h"

#include "client/manager.h"
#include "client/window.h"
#include "core/array.h"
#include "core/concurrency.h"
#include "core/debug.h"
#include "core/file.h"
#include "core/map.h"
#include "core/misc.h"
#include "core/string.h"
#include "core/timer.h"
#include "gpu/manager.h"
#include "job/manager.h"
#include "imgui/manager.h"
#include "plugin/manager.h"

#include "audio_stats_callback.h"
#include "audio_buffer_callback.h"
#include "audio_playback_callback.h"
#include "audio_recording_callback.h"

#include "dialog_device_selection.h"

#include <cstdlib>
#include <cstdio>
#include <utility>

namespace
{
	AudioBackend audioBackend_;
	MidiBackend midiBackend_;
	App::Settings settings_;

	Client::Window* window_ = nullptr;
	GPU::SwapChainDesc scDesc_;
	GPU::Handle scHandle_;
	GPU::Handle fbsHandle_;
	GPU::Handle cmdHandle_;

	GPU::CommandList* cmdList_ = nullptr;
	
	Callbacks::AudioStatsCallback* audioStatsCallback_ = nullptr;
	Callbacks::AudioRecordingCallback* audioRecordingCallback_ = nullptr;
	Callbacks::AudioBufferCallback* audioBufferCallback_ = nullptr;
	Callbacks::AudioPlaybackCallback* audioPlaybackCallback_ = nullptr;

	Gui::DialogDeviceSelection* dialogDeviceSelection_ = nullptr;
	Gui::DeviceSelectionStatus deviceSelectionStatus_ = Gui::DeviceSelectionStatus::NONE;
	
	GPU::SetupParams GetDefaultSetupParams()
	{
		GPU::SetupParams setupParams;
		setupParams.debuggerIntegration_ = GPU::DebuggerIntegrationFlags::NONE;
		return setupParams;
	}

	Core::Map<Core::String, IAudioCallback*> callbacks_;
}

namespace App
{
	int Manager::Run(int argc, char* const argv[])
	{
		// Change to executable path.
		char path[Core::MAX_PATH_LENGTH];
		if(Core::FileSplitPath(argv[0], path, Core::MAX_PATH_LENGTH, nullptr, 0, nullptr, 0))
		{
			Core::FileChangeDir(path);
		}

		settings_.Load();

		Client::Manager::Scoped clientManager;
		Plugin::Manager::Scoped pluginManager;
		Job::Manager::Scoped jobManager(4, 256, 256 * 1024);
		GPU::Manager::Scoped gpuManager(GetDefaultSetupParams());

		if(Initialize(argc, argv))
		{
			ImGui::Manager::Scoped imguiManager;
			while(Tick());
			Finalize();

			return 0;
		}

		return 1;
	}
	
	bool Manager::Initialize(int argc, char* const argv[])
	{
		window_ = new Client::Window("Music Practice App", 100, 100, 1024, 768, true);

		i32 numAdapters = GPU::Manager::EnumerateAdapters(nullptr, 0);
		if(numAdapters == 0)
			return false;

		if(GPU::Manager::CreateAdapter(0) != GPU::ErrorCode::OK)
		{
			return false;
		}

		scDesc_.width_ = 1024;
		scDesc_.height_ = 768;
		scDesc_.format_ = GPU::Format::R8G8B8A8_UNORM;
		scDesc_.bufferCount_ = 2;
		scDesc_.outputWindow_ = window_->GetPlatformData().handle_;
		scHandle_ = GPU::Manager::CreateSwapChain(scDesc_, "App Swapchain");

		GPU::FrameBindingSetDesc fbDesc;
		fbDesc.rtvs_[0].resource_ = scHandle_;
		fbDesc.rtvs_[0].format_ = scDesc_.format_;
		fbDesc.rtvs_[0].dimension_ = GPU::ViewDimension::TEX2D;
			
		fbsHandle_ = GPU::Manager::CreateFrameBindingSet(fbDesc, "App Swapchain");
		cmdHandle_ = GPU::Manager::CreateCommandList("App Commandlist");

		cmdList_ = new GPU::CommandList(GPU::Manager::GetHandleAllocator());

		audioStatsCallback_ = new Callbacks::AudioStatsCallback();
		audioRecordingCallback_ = new Callbacks::AudioRecordingCallback(*audioStatsCallback_);
		audioBufferCallback_ = new Callbacks::AudioBufferCallback();
		audioPlaybackCallback_ = new Callbacks::AudioPlaybackCallback();
		
		audioBackend_.RegisterCallback(audioStatsCallback_, 0x1, 0x0);
		audioBackend_.RegisterCallback(audioRecordingCallback_, 0x1, 0x0);
		audioBackend_.RegisterCallback(audioBufferCallback_, 0x1, 0xf);
		audioBackend_.RegisterCallback(audioPlaybackCallback_, 0x0, 0xf);

		dialogDeviceSelection_ = new Gui::DialogDeviceSelection(audioBackend_, settings_.audioSettings_);

		// Attempt to start device.
		if(audioBackend_.StartDevice(settings_.audioSettings_))
		{
			deviceSelectionStatus_ = Gui::DeviceSelectionStatus::SELECTED;
		}

		return true;
	}

	void Manager::Finalize()
	{
		delete cmdList_;
		delete window_;

		audioBackend_.UnregisterCallback(audioPlaybackCallback_);
		audioBackend_.UnregisterCallback(audioStatsCallback_);
		audioBackend_.UnregisterCallback(audioRecordingCallback_);
		audioBackend_.UnregisterCallback(audioBufferCallback_);

		delete audioStatsCallback_;
		delete audioRecordingCallback_;
		delete audioBufferCallback_;
		delete dialogDeviceSelection_;

		GPU::Manager::DestroyResource(cmdHandle_);
		GPU::Manager::DestroyResource(fbsHandle_);
		GPU::Manager::DestroyResource(scHandle_);
	}
	
	bool Manager::Tick()
	{
		static const f32 color[4] = {0.1f, 0.1f, 0.2f, 1.0f};

		const Client::IInputProvider& input = window_->GetInputProvider();

		if(Client::Manager::Update())
		{
			// Reset command list to reuse.
			cmdList_->Reset();

			// Clear swapchain.
			cmdList_->ClearRTV(fbsHandle_, 0, color);

			ImGui::Manager::BeginFrame(input, scDesc_.width_, scDesc_.height_);

			MainUpdate();

			ImGui::Manager::EndFrame(fbsHandle_, *cmdList_);

			// Compile and submit.
			GPU::Manager::CompileCommandList(cmdHandle_, *cmdList_);
			GPU::Manager::SubmitCommandList(cmdHandle_);

			// Present.
			GPU::Manager::PresentSwapChain(scHandle_);

			// Next frame.
			GPU::Manager::NextFrame();

			// Force a sleep.
			Core::Sleep(1.0 / 60.0);

			return true;
		}

		return false;
	}

	void Manager::MenuBar()
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
					deviceSelectionStatus_ = Gui::DeviceSelectionStatus::NONE;
				}

				ImGui::EndMenu();
			}

			ImGui::EndMainMenuBar();
		}
	}

	void Manager::MainUpdate()
	{
		MenuBar();

		// Device selection.
		if(deviceSelectionStatus_ == Gui::DeviceSelectionStatus::NONE)
		{
			deviceSelectionStatus_ = dialogDeviceSelection_->Update();
			if(deviceSelectionStatus_ == Gui::DeviceSelectionStatus::SELECTED)
			{
				settings_.audioSettings_ = dialogDeviceSelection_->GetSettings();
				settings_.Save();
			}
		}
		else
		{

			if(ImGui::Begin("Debug", nullptr))
			{
				ImGui::Text("Audio Input");
				ImGui::PlotLines("Input", audioBufferCallback_->GetAudioData(), Callbacks::AudioBufferCallback::AUDIO_DATA_SIZE, audioBufferCallback_->GetAudioDataOffset(), nullptr, -1.0f, 1.0f, ImVec2(0.0f, 128.0f));
					
				f32 rms = audioStatsCallback_->rms_;
				f32 rmsSmoothed = audioStatsCallback_->rmsSmoothed_;
				ImGui::SliderFloat("RMS Smoothed", &rmsSmoothed, 0.0f, 1.0f);

				f32 max = audioStatsCallback_->max_;
				f32 maxSmoothed = audioStatsCallback_->maxSmoothed_;
				ImGui::SliderFloat("Max Smoothed", &maxSmoothed, 0.0f, 1.0f);

				ImGui::Separator();

				if(audioRecordingCallback_->IsRecording())
				{
					ImGui::TextColored(ImVec4(0.8f, 0.0f, 0.0f, 1.0f), "* RECORDING");
				}
				else
				{
					ImGui::Text("* Waiting...");
				}

				f32 stopTimeout = audioRecordingCallback_->GetTimeout();
				Gui::SliderFloat("Stop Timeout:", &stopTimeout, 0.0f, 30.0f);
				audioRecordingCallback_->SetTimeout(stopTimeout);

				f32 startThreshold = audioRecordingCallback_->GetThresholdStart();
				Gui::SliderFloat("Start Threshold:", &startThreshold, 0.0f, 1.0f);
				audioRecordingCallback_->SetThresholdStart(startThreshold);
				
				f32 stopThreshold = audioRecordingCallback_->GetThresholdStop();
				Gui::SliderFloat("Stop Threshhold:", &stopThreshold, 0.0f, 1.0f);
				audioRecordingCallback_->SetThresholdStop(stopThreshold);

				if(ImGui::Button("Start Recording"))
					audioRecordingCallback_->Start();
				ImGui::SameLine();
				if(ImGui::Button("Stop Recording"))
					audioRecordingCallback_->Stop();

				ImGui::Separator();


				f32 countDownTimer = audioRecordingCallback_->RecordingTimeLeft();
				ImGui::SliderFloat("", &countDownTimer, 0.0f, audioRecordingCallback_->GetTimeout());

				Core::Vector<Core::String> fileNames;
				auto recordingIds = audioRecordingCallback_->GetRecordingIDs();
				for(auto id : recordingIds)
				{
					Core::String fileName;
					fileName.Printf("audio_out_%08u.wav", id);
					fileNames.push_back(fileName);
				}

				if(fileNames.size() > 0)
				{
					ImGui::Columns(2);

					static int selectedRecording = 0;
					if(Gui::ListBox("Recordings:", &selectedRecording,
						[](void* data, int idx, const char** out_text)
					{
						const Core::Vector<Core::String>& fileNames = *static_cast<const Core::Vector<Core::String>*>(data);
						if(idx >= fileNames.size())
							return false;
						*out_text = fileNames[idx].c_str();
						return true;
					}, &fileNames, fileNames.size(), 16))
					{
						audioPlaybackCallback_->Play(fileNames[selectedRecording].c_str());
					}
				

					ImGui::NextColumn();

					if(ImGui::Button("Play"))
					{
						audioPlaybackCallback_->Play(fileNames[selectedRecording].c_str());
					}
					if(ImGui::Button("Stop"))
					{
						audioPlaybackCallback_->Stop();
					}

					ImGui::Columns(1);
				}

				
			}
			ImGui::End();
		}
	}

	const Settings& Manager::GetSettings()
	{
		return settings_;
	}

	void Manager::SetSettings(const Settings& settings)
	{
		settings_ = settings;
	}

} // namespace App

