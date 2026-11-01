#pragma once
#include <cstdint>

namespace dx7clean
{
// function settings; defaults are the measurement rig's power-on state (INTERFACE.md 6)
struct Global
{
	struct Ctrl
	{
		uint8_t range = 99;
		uint8_t assign = 7;
	};

	bool mono = false;
	uint8_t bendRange = 0;
	uint8_t bendStep = 0;
	uint8_t portaMode = 0;
	bool glissando = false;
	uint8_t portaTime = 0;
	Ctrl wheel, foot, breath, aftertouch;
	int masterTune = 0;
};

// function parameter numbers as sent with F0 43 1n 08 pp vv F7
namespace fn
{
enum : uint8_t
{
	PolyMono = 64, BendRange, BendStep, PortaMode, Glissando, PortaTime,
	WheelRange, WheelAssign, FootRange, FootAssign, BreathRange, BreathAssign, AtRange, AtAssign
};
}
}
