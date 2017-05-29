#pragma once

#include "imgui/manager.h"

namespace Gui
{
	/// @see ImGui::ListBox
	bool ListBox(const char* label, int* current_item, const char* const* items, int items_count, int height_items = -1);
	bool ListBox(const char* label, int* current_item, bool (*items_getter)(void* data, int idx, const char** out_text), void* data, int items_count, int height_in_items = -1);

	/// @see ImGui::Combo
	bool Combo(const char* label, int* current_item, const char* const* items, int items_count, int height_in_items = -1);
	/// @see ImGui::SliderFloat
	bool SliderFloat(const char* label, f32* value, f32 min, f32 max);

	/// Scoped ID to avoid users having to manually push/pop.
	struct ScopedID
	{
		ScopedID(i32 id);
		ScopedID(const char* id);
		~ScopedID();
	};

	struct ScopedItemWidth
	{
		ScopedItemWidth(float w);
		~ScopedItemWidth();
	};

} // namespace Gui
