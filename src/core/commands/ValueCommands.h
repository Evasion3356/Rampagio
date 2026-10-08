/*
	Commands holding a value: IntCommand, FloatCommand, ListCommand,
	StringCommand and ColorCommand (HorseMenu's types of the same names;
	Vector3Command is left out until something needs it).

	Rampagio differences:
	- A value can live in the feature's own variable: pass `storage` and the
	  command reads and writes through it, so the menu files' globals and
	  their readers stay as they are. The command still saves it.
	- onChange is a std::function and runs directly (no FiberPool).
	- Out-of-range saved values are clamped (numbers to min/max, list
	  indices to the list).
	- Call() re-applies the current value through onChange (a row's select,
	  or a hotkey), so it's only Hotkeyable when there's an onChange.
*/

#pragma once

#include "Command.h"
#include "..\..\ColorRgba.h"

#include <functional>
#include <optional>
#include <string>
#include <vector>

namespace Rampagio
{
	template <typename T>
	class ValueCommand : public Command
	{
	protected:
		T m_Own;
		T* m_Storage;
		T m_Default;
		std::optional<T> m_Saved;
		std::function<void()> m_OnChange;

		T& Ref() { return m_Storage ? *m_Storage : m_Own; }
		virtual T Clamp(const T& value) const { return value; }
		void OnCall() override
		{
			if (m_OnChange)
				m_OnChange();
		}

	public:
		ValueCommand(std::string name, std::string label, std::string description, T defaultValue, T* storage, std::function<void()> onChange) :
		    Command(std::move(name), std::move(label), std::move(description)),
		    m_Own(defaultValue),
		    m_Storage(storage),
		    m_Default(defaultValue),
		    m_OnChange(std::move(onChange))
		{
			if (m_Storage)
				*m_Storage = defaultValue;
			SetHotkeyable(m_OnChange != nullptr);
		}

		const T& GetState() { return Ref(); }
		const T& GetDefault() const { return m_Default; }
		bool HasOnChange() const { return m_OnChange != nullptr; }

		// Clamps, runs onChange if the value changed and marks it for saving.
		void SetState(T value)
		{
			value = Clamp(value);
			if (value == Ref())
				return;
			Ref() = std::move(value);
			if (m_OnChange)
				m_OnChange();
			MarkDirty();
		}

		bool HasState() const override { return true; }
		void SaveState(nlohmann::json& value) override;
		void LoadState(const nlohmann::json& value) override;

		void ApplyLoaded(bool restoreFeatures) override
		{
			if (!m_Saved)
				return;
			T saved = std::move(*m_Saved);
			m_Saved.reset();
			// Values with a change hook act on the game: restoring them is a
			// feature state. Plain values are parameters and always load.
			if ((m_OnChange && !restoreFeatures) || saved == Ref())
				return;
			Ref() = std::move(saved);
			if (m_OnChange)
				m_OnChange();
		}

		void ResetToDefault() override { SetState(m_Default); }
	};

	template <typename T>
	class NumberCommand : public ValueCommand<T>
	{
		std::optional<T> m_Min;
		std::optional<T> m_Max;
		T m_Step;

	protected:
		T Clamp(const T& value) const override
		{
			T v = value;
			if (m_Min && v < *m_Min)
				v = *m_Min;
			if (m_Max && v > *m_Max)
				v = *m_Max;
			return v;
		}

	public:
		NumberCommand(std::string name, std::string label, std::string description, std::optional<T> min, std::optional<T> max, T step,
			T defaultValue, T* storage = nullptr, std::function<void()> onChange = nullptr) :
		    ValueCommand<T>(std::move(name), std::move(label), std::move(description), defaultValue, storage, std::move(onChange)),
		    m_Min(min),
		    m_Max(max),
		    m_Step(step)
		{
		}

		// One step up (direction > 0) or down.
		void Step(int direction) { this->SetState(this->GetState() + (direction > 0 ? m_Step : -m_Step)); }
		std::optional<T> GetMinimum() const { return m_Min; }
		std::optional<T> GetMaximum() const { return m_Max; }
		T GetStep() const { return m_Step; }
	};

	using IntCommand = NumberCommand<int>;
	using FloatCommand = NumberCommand<float>;

	// An index into a list of names.
	class ListCommand : public ValueCommand<int>
	{
		std::vector<std::string> m_List;

	protected:
		int Clamp(const int& value) const override;

	public:
		ListCommand(std::string name, std::string label, std::string description, std::vector<std::string> list,
			int defaultValue = 0, int* storage = nullptr, std::function<void()> onChange = nullptr);

		// Next (direction > 0) or previous entry, wrapping around.
		void Step(int direction);
		const std::vector<std::string>& GetList() const { return m_List; }
		// The current entry's name, or "" if the list is empty.
		std::string GetSelected();
		std::string StatusText() override { return GetLabel() + ": " + GetSelected(); }
	};

	class StringCommand : public ValueCommand<std::string>
	{
	public:
		StringCommand(std::string name, std::string label, std::string description, std::string defaultValue = {},
			std::string* storage = nullptr, std::function<void()> onChange = nullptr) :
		    ValueCommand<std::string>(std::move(name), std::move(label), std::move(description), std::move(defaultValue), storage, std::move(onChange))
		{
		}
	};

	// Saved as [r, g, b, a].
	class ColorCommand : public ValueCommand<ColorRgba>
	{
	public:
		ColorCommand(std::string name, std::string label, std::string description, ColorRgba defaultValue = { 255, 255, 255, 255 },
			ColorRgba* storage = nullptr, std::function<void()> onChange = nullptr) :
		    ValueCommand<ColorRgba>(std::move(name), std::move(label), std::move(description), defaultValue, storage, std::move(onChange))
		{
		}
	};

	extern template class ValueCommand<int>;
	extern template class ValueCommand<float>;
	extern template class ValueCommand<std::string>;
	extern template class ValueCommand<ColorRgba>;
}
