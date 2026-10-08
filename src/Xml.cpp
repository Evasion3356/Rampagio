#include "Xml.h"

#include <cctype>
#include <cstdlib>

namespace Xml
{
	namespace
	{
		std::string_view Trim(std::string_view s)
		{
			const size_t first = s.find_first_not_of(" \t\r\n");
			if (first == std::string_view::npos)
				return {};
			return s.substr(first, s.find_last_not_of(" \t\r\n") - first + 1);
		}

		std::string Unescape(std::string_view s)
		{
			static constexpr std::pair<std::string_view, char> kEntities[] = {
				{ "&lt;", '<' }, { "&gt;", '>' }, { "&amp;", '&' }, { "&quot;", '"' }, { "&apos;", '\'' } };
			std::string out;
			for (size_t i = 0; i < s.size(); i++)
			{
				bool replaced = false;
				if (s[i] == '&')
					for (const auto& [entity, c] : kEntities)
						if (s.substr(i, entity.size()) == entity)
						{
							out += c;
							i += entity.size() - 1;
							replaced = true;
							break;
						}
				if (!replaced)
					out += s[i];
			}
			return out;
		}

		std::string Escape(std::string_view s)
		{
			std::string out;
			for (char c : s)
				switch (c)
				{
				case '<': out += "&lt;"; break;
				case '>': out += "&gt;"; break;
				case '&': out += "&amp;"; break;
				case '"': out += "&quot;"; break;
				default: out += c;
				}
			return out;
		}

		class Parser
		{
		public:
			explicit Parser(std::string_view text) : m_text(text) {}

			bool Root(Node& root)
			{
				SkipMisc();
				return Element(root);
			}

		private:
			std::string_view m_text;
			size_t m_pos = 0;

			bool StartsWith(std::string_view s) const { return m_text.substr(m_pos, s.size()) == s; }

			bool SkipPast(std::string_view end)
			{
				const size_t at = m_text.find(end, m_pos);
				if (at == std::string_view::npos)
					return false;
				m_pos = at + end.size();
				return true;
			}

			// Whitespace, comments, declarations and processing instructions.
			void SkipMisc()
			{
				while (m_pos < m_text.size())
				{
					if (isspace(static_cast<unsigned char>(m_text[m_pos])))
						m_pos++;
					else if (StartsWith("<!--"))
						SkipPast("-->");
					else if (StartsWith("<?"))
						SkipPast("?>");
					else if (StartsWith("<!"))
						SkipPast(">");
					else
						break;
				}
			}

			bool Element(Node& node)
			{
				if (!StartsWith("<"))
					return false;
				m_pos++;
				const size_t nameEnd = m_text.find_first_of(" \t\r\n/>", m_pos);
				if (nameEnd == std::string_view::npos)
					return false;
				node.name = m_text.substr(m_pos, nameEnd - m_pos);
				const size_t tagEnd = m_text.find('>', nameEnd);
				if (tagEnd == std::string_view::npos)
					return false;
				const bool empty = m_text[tagEnd - 1] == '/';
				m_pos = tagEnd + 1;
				if (empty)
					return true;
				std::string text;
				while (m_pos < m_text.size())
				{
					if (StartsWith("</"))
					{
						if (!SkipPast(">"))
							return false;
						node.text = Trim(Unescape(text));
						return true;
					}
					if (StartsWith("<![CDATA["))
					{
						const size_t start = m_pos + 9;
						if (!SkipPast("]]>"))
							return false;
						text += m_text.substr(start, m_pos - 3 - start);
					}
					else if (StartsWith("<!--") || StartsWith("<?"))
						SkipMisc();
					else if (StartsWith("<"))
					{
						if (!Element(node.children.emplace_back()))
							return false;
					}
					else
					{
						const size_t next = m_text.find('<', m_pos);
						if (next == std::string_view::npos)
							return false;
						text += m_text.substr(m_pos, next - m_pos);
						m_pos = next;
					}
				}
				return false;
			}
		};

		void WriteNode(const Node& node, int depth, std::string& out)
		{
			const std::string indent(depth, '\t');
			if (node.children.empty())
			{
				out += indent + "<" + node.name + ">" + Escape(node.text) + "</" + node.name + ">\n";
				return;
			}
			out += indent + "<" + node.name + ">\n";
			for (const Node& child : node.children)
				WriteNode(child, depth + 1, out);
			out += indent + "</" + node.name + ">\n";
		}
	}

	const Node* Node::Child(std::string_view childName) const
	{
		for (const Node& c : children)
			if (c.name == childName)
				return &c;
		return nullptr;
	}

	std::vector<const Node*> Node::Children(std::string_view childName) const
	{
		std::vector<const Node*> out;
		for (const Node& c : children)
			if (c.name == childName)
				out.push_back(&c);
		return out;
	}

	std::string Node::Text(std::string_view childName, std::string_view fallback) const
	{
		const Node* c = Child(childName);
		return c ? c->text : std::string(fallback);
	}

	float Node::Float(std::string_view childName, float fallback) const
	{
		const Node* c = Child(childName);
		return c && !c->text.empty() ? static_cast<float>(std::strtod(c->text.c_str(), nullptr)) : fallback;
	}

	int Node::Int(std::string_view childName, int fallback) const
	{
		const Node* c = Child(childName);
		return c && !c->text.empty() ? static_cast<int>(std::strtoll(c->text.c_str(), nullptr, 0)) : fallback;
	}

	bool Node::Bool(std::string_view childName, bool fallback) const
	{
		const Node* c = Child(childName);
		if (!c || c->text.empty())
			return fallback;
		return c->text == "true" || c->text == "1";
	}

	Node& Node::Add(std::string childName, std::string childText)
	{
		Node& c = children.emplace_back();
		c.name = std::move(childName);
		c.text = std::move(childText);
		return c;
	}

	bool Parse(std::string_view text, Node& root)
	{
		root = {};
		return Parser(text).Root(root);
	}

	std::string Write(const Node& root)
	{
		std::string out = "<?xml version=\"1.0\" encoding=\"ISO-8859-1\"?>\n";
		WriteNode(root, 0, out);
		return out;
	}
}
