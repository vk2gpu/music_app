#include "gui.h"

namespace Gui
{
	bool ListBox(const char* label, int* current_item, const char* const* items, int items_count, int height_items)
	{
		Gui::ScopedID scopedId(label);
		ImGui::LabelText("", label);
		return ImGui::ListBox("", current_item, items, items_count, height_items);
	}

	bool ListBox(const char* label, int* current_item, bool (*items_getter)(void* data, int idx, const char** out_text), void* data, int items_count, int height_in_items)
	{
		Gui::ScopedID scopedId(label);
		ImGui::LabelText("", label);
		return ImGui::ListBox("", current_item, items_getter, data, items_count, height_in_items);
	}

	bool Combo(const char* label, int* current_item, const char* const* items, int items_count, int height_in_items)
	{
		Gui::ScopedID scopedId(label);
		ImGui::LabelText("", label);
		return ImGui::Combo("", current_item, items, items_count, height_in_items);
	}

	bool SliderFloat(const char* label, f32* value, f32 min, f32 max)
	{
		Gui::ScopedID scopedId(label);
		ImGui::LabelText("", label);
		return ImGui::SliderFloat("", value, min, max);
	}

	ScopedID::ScopedID(i32 id)
	{
		ImGui::PushID(id);
	}

	ScopedID::ScopedID(const char* id)
	{
		ImGui::PushID(id);
	}

	ScopedID::~ScopedID()
	{
		ImGui::PopID();
	}


	ScopedItemWidth::ScopedItemWidth(float w)
	{
		ImGui::PushItemWidth(w);
	}

	ScopedItemWidth::~ScopedItemWidth()
	{
		ImGui::PopItemWidth();
	}


} // namespace Gui
