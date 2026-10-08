/*
	An 8-bit RGBA color. Its own header so src/core (ColorCommand, the
	settings tests) can use it without the natives scriptmenu.h pulls in.
*/

#pragma once

struct ColorRgba
{
	unsigned char	r, g, b, a;

	bool operator==(const ColorRgba&) const = default;
};
