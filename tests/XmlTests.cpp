/*
	Tests for src/Xml (the spooner database reader/writer): a file shaped
	like Rampage's, entities, empty elements, comments and round trips.
	Exits 0 and prints ALL PASS on success.
*/

#include "..\src\Xml.h"

#include <cstdio>
#include <string>

namespace
{
	int g_failures = 0;

	void Check(bool condition, const char* name)
	{
		std::printf("[%s] %s\n", condition ? "PASS" : "FAIL", name);
		if (!condition)
			g_failures++;
	}

	const char* const kRampageFile = R"(<?xml version="1.0" encoding="ISO-8859-1"?>
<!-- saved by hand -->
<Map>
	<MapMeta>
		<Creator>Arthur</Creator>
		<RampageVersion>1.0</RampageVersion>
	</MapMeta>
	<Placement>
		<ModelHash>0x1A2B3C4D</ModelHash>
		<HashName>p_chair01x</HashName>
		<Dynamic>false</Dynamic>
		<PositionRotation>
			<X>-123.5</X>
			<Y>45.25</Y>
			<Z>100</Z>
			<Pitch>0</Pitch>
			<Roll>0</Roll>
			<Yaw>90.5</Yaw>
		</PositionRotation>
	</Placement>
	<Placement>
		<ModelHash>0x00000001</ModelHash>
		<HashName>a &amp; b &lt;c&gt;</HashName>
		<Texture/>
	</Placement>
	<Ped>
		<Health>250</Health>
		<Flags>
			<Invincible>true</Invincible>
		</Flags>
	</Ped>
</Map>
)";
}

int main()
{
	Xml::Node map;
	Check(Xml::Parse(kRampageFile, map), "parses a Rampage-shaped file");
	Check(map.name == "Map", "root is Map");
	Check(map.Child("MapMeta") && map.Child("MapMeta")->Text("Creator") == "Arthur", "nested text");
	const auto placements = map.Children("Placement");
	Check(placements.size() == 2, "two placements");
	if (placements.size() == 2)
	{
		const Xml::Node* pr = placements[0]->Child("PositionRotation");
		Check(pr && pr->Float("X") == -123.5f && pr->Float("Yaw") == 90.5f, "floats");
		Check(placements[0]->Int("ModelHash") == 0x1A2B3C4D, "hex int");
		Check(!placements[0]->Bool("Dynamic", true), "bool false");
		Check(placements[1]->Text("HashName") == "a & b <c>", "entities");
		Check(placements[1]->Child("Texture") && placements[1]->Text("Texture", "x").empty(), "empty element");
	}
	const Xml::Node* ped = map.Child("Ped");
	Check(ped && ped->Int("Health") == 250 && ped->Child("Flags")->Bool("Invincible"), "ped fields");
	Check(map.Text("Missing", "fallback") == "fallback", "missing child falls back");

	Xml::Node again;
	Check(Xml::Parse(Xml::Write(map), again), "round trip parses");
	Check(Xml::Write(again) == Xml::Write(map), "round trip is stable");
	Check(again.Children("Placement").size() == 2 && again.Children("Placement")[1]->Text("HashName") == "a & b <c>", "round trip keeps text");

	Xml::Node bad;
	Check(!Xml::Parse("<Map><Placement></Map", bad), "rejects a truncated file");
	Check(!Xml::Parse("not xml", bad), "rejects text without elements");

	std::printf(g_failures ? "%d FAILED\n" : "ALL PASS\n", g_failures);
	return g_failures ? 1 : 0;
}
