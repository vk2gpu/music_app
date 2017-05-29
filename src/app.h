#pragma once

#include "settings.h"

namespace App
{
	class Manager
	{
	public:
		static int Run(int argc, char* const argv[]);

		static const Settings& GetSettings();
		static void SetSettings(const Settings& settings);

	private:
		static bool Initialize(int argc, char* const argv[]);
		static void Finalize();
		static bool Tick();

		static void MenuBar();
		static void MainUpdate();


		Manager() = delete;
		~Manager() = delete;
	};





} // namespace App
