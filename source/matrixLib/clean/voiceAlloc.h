#pragma once

#include <array>
#include <cstdint>
#include <vector>

#include "types.h"

namespace matrixClean
{
	// keyboard modes (INTERFACE.md 4.9, measurements/voice). Reassign with rob behaves like
	// reassign: no voice is taken from a held note. The pedal does not hold notes (ctrl/).
	class VoiceAllocator
	{
	public:
		struct Voice
		{
			int note = -1;           // last assigned note, kept after release
			bool gate = false;
			uint32_t age = 0;        // assignment order
			uint8_t velocity = 0;
		};

		struct Event
		{
			enum class Type : uint8_t { On, Off, Retarget };
			Type type;
			uint32_t voice;
			uint8_t note;
			uint8_t velocity;        // note on velocity, or release velocity for Off
			bool legato;             // On/Retarget while other keys were held
			bool voiceGated;         // the voice was still gated (portamento legato)
		};

		void reset();
		void setMode(KeyboardMode _mode) { m_mode = _mode; }
		KeyboardMode getMode() const { return m_mode; }

		void noteOn(uint8_t _note, uint8_t _velocity, std::vector<Event>& _out);
		void noteOff(uint8_t _note, uint8_t _velocity, std::vector<Event>& _out);
		// MIDI mono mode: one voice per channel offset, no allocation
		void noteOnVoice(uint32_t _voice, uint8_t _note, uint8_t _velocity, std::vector<Event>& _out);
		void noteOffVoice(uint32_t _voice, uint8_t _note, uint8_t _velocity, std::vector<Event>& _out);
		void allNotesOff(std::vector<Event>& _out);

		const Voice& voice(uint32_t _v) const { return m_voices[_v]; }
		bool anyKeyHeld() const { return !m_held.empty(); }

	private:
		void unisonUpdate(bool _wasHeld, uint8_t _velocity, std::vector<Event>& _out);
		void release(uint32_t _v, uint8_t _velocity, std::vector<Event>& _out);
		int findFree() const;
		void assign(uint32_t _v, uint8_t _note, uint8_t _velocity, bool _legato, std::vector<Event>& _out);

		KeyboardMode m_mode = KeyboardMode::Reassign;
		std::array<Voice, VoiceCount> m_voices{};
		std::vector<uint8_t> m_held;      // keys down, in press order
		uint32_t m_next = 1;              // next voice; the first note after boot plays voice 1
		uint32_t m_ageCounter = 0;
		int m_unisonNote = -1;
	};
}
