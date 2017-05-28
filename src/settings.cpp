#include "settings.h"
#include "core/file.h"

namespace App
{
	void Settings::Save()
	{
		auto file = Core::File("settings.json", Core::FileFlags::WRITE);
		if(file)
		{
			Serialization::Serializer ser(file, Serialization::Flags::TEXT);
			ser.SerializeObject("settings", *this);
		}
	}

	void Settings::Load()
	{
		auto file = Core::File("settings.json", Core::FileFlags::READ);
		if(file)
		{
			Serialization::Serializer ser(file, Serialization::Flags::TEXT);
			ser.SerializeObject("settings", *this);
		}
	}

} // namespace App
