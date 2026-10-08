#include "ValueCommands.h"

#include <nlohmann/json.hpp>

#include <algorithm>
#include <stdexcept>

void to_json(nlohmann::json& j, const ColorRgba& c)
{
	j = nlohmann::json::array({ c.r, c.g, c.b, c.a });
}

void from_json(const nlohmann::json& j, ColorRgba& c)
{
	if (!j.is_array() || j.size() != 4)
		throw std::invalid_argument("expected [r, g, b, a]");
	auto channel = [&j](size_t i) { return static_cast<unsigned char>(std::clamp(j.at(i).get<int>(), 0, 255)); };
	c = { channel(0), channel(1), channel(2), channel(3) };
}

namespace Rampagio
{
	template <typename T>
	void ValueCommand<T>::SaveState(nlohmann::json& value)
	{
		value = Ref();
	}

	template <typename T>
	void ValueCommand<T>::LoadState(const nlohmann::json& value)
	{
		m_Saved = Clamp(value.get<T>());
	}

	template class ValueCommand<int>;
	template class ValueCommand<float>;
	template class ValueCommand<std::string>;
	template class ValueCommand<ColorRgba>;

	ListCommand::ListCommand(std::string name, std::string label, std::string description, std::vector<std::string> list,
		int defaultValue, int* storage, std::function<void()> onChange) :
	    ValueCommand<int>(std::move(name), std::move(label), std::move(description), defaultValue, storage, std::move(onChange)),
	    m_List(std::move(list))
	{
	}

	int ListCommand::Clamp(const int& value) const
	{
		if (m_List.empty())
			return 0;
		return std::clamp(value, 0, static_cast<int>(m_List.size()) - 1);
	}

	void ListCommand::Step(int direction)
	{
		if (m_List.empty())
			return;
		const int size = static_cast<int>(m_List.size());
		SetState((GetState() + (direction > 0 ? 1 : size - 1)) % size);
	}

	std::string ListCommand::GetSelected()
	{
		const int index = GetState();
		return index >= 0 && index < static_cast<int>(m_List.size()) ? m_List[index] : std::string();
	}
}
