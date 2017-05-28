#include "gui.h"

namespace Gui
{
	bool ListBox(const char* label, int* current_item, const char* const* items, int items_count, int height_items)
	{
		Gui::ScopedID scopedId(label);
		ImGui::LabelText("", label);
		return ImGui::ListBox("", current_item, items, items_count, height_items);
	}

	bool Combo(const char* label, int* current_item, const char* const* items, int items_count, int height_in_items)
	{
		Gui::ScopedID scopedId(label);
		ImGui::LabelText("", label);
		return ImGui::Combo("", current_item, items, items_count, height_in_items);
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
