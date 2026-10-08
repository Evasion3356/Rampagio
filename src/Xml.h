/*
	A minimal XML DOM for the files Rampage writes (spooner databases):
	elements with text content and children. Attributes, comments, CDATA
	and the <?xml?> declaration are skipped on read; entities are the five
	predefined ones.

		Xml::Node map;
		if (Xml::Parse(text, map) && map.name == "Map")
			for (const Xml::Node& p : map.Children("Placement"))
				Hash model = GameUtil::ParseHash(p.Text("ModelHash"));

		Xml::Node& placement = map.Add("Placement");
		placement.Add("ModelHash", "0x1234ABCD");
		std::string out = Xml::Write(map);
*/

#pragma once

#include <string>
#include <string_view>
#include <vector>

namespace Xml
{
	struct Node
	{
		std::string name;
		std::string text;
		std::vector<Node> children;

		// The first child with that name, or nullptr.
		const Node* Child(std::string_view childName) const;
		// Every child with that name.
		std::vector<const Node*> Children(std::string_view childName) const;
		// The trimmed text of the first child with that name, else fallback.
		std::string Text(std::string_view childName, std::string_view fallback = {}) const;
		float Float(std::string_view childName, float fallback = 0.0f) const;
		int Int(std::string_view childName, int fallback = 0) const;
		bool Bool(std::string_view childName, bool fallback = false) const;

		Node& Add(std::string childName, std::string childText = {});
	};

	// Parses the document's root element into root. False on malformed input.
	bool Parse(std::string_view text, Node& root);
	// The document with an ISO-8859-1 declaration (what Rampage writes) and
	// tab indentation.
	std::string Write(const Node& root);
}
