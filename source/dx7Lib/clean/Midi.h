#pragma once
#include <cstddef>
#include <cstdint>
#include <vector>

namespace dx7clean
{
struct MidiHandler
{
	virtual ~MidiHandler() = default;
	virtual void onNoteOn(uint8_t ch, uint8_t note, uint8_t vel) = 0;
	virtual void onNoteOff(uint8_t ch, uint8_t note, uint8_t vel) = 0;
	virtual void onPolyPressure(uint8_t, uint8_t, uint8_t) {}
	virtual void onControlChange(uint8_t ch, uint8_t cc, uint8_t value) = 0;
	virtual void onProgramChange(uint8_t ch, uint8_t program) = 0;
	virtual void onChannelPressure(uint8_t ch, uint8_t value) = 0;
	virtual void onPitchBend(uint8_t ch, uint16_t value14) = 0;
	virtual void onSysex(const uint8_t* data, size_t len) = 0;
	virtual void onRealtime(uint8_t) {}
};

class MidiParser
{
public:
	explicit MidiParser(MidiHandler& h) : m_handler(h) {}
	void feed(const uint8_t* data, size_t len);
	void feed(uint8_t b);
	void reset();

	static constexpr size_t kMaxSysex = 4200;

private:
	void dispatch();

	MidiHandler& m_handler;
	uint8_t m_status = 0;
	uint8_t m_data[2] = {};
	int m_count = 0;
	bool m_inSysex = false;
	bool m_sysexOverflow = false;
	std::vector<uint8_t> m_sysex;
};
}
