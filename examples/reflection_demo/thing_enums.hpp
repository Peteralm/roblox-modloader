#pragma once

namespace rml
{
	class InitContext;
}

enum class ThingMode
{
	Idle = 1,
	Patrol = 2,
	Chase = 3,
	Flee = 4,
	Legacy = 9
};

enum class Face
{
	Right = 0,
	Top = 1,
	Back = 2,
	Left = 3,
	Bottom = 4,
	Front = 5
};

void define_thing_enums(rml::InitContext& context);
