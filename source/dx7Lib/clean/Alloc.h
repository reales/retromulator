#pragma once
#include <array>
#include <cstdint>
#include <vector>

#include "Regs.h"

// Voice assignment, fitted from measurements/voice_alloc (see fits/voice_alloc/FIT.md)
namespace dx7clean
{
struct AllocAction
{
	enum Type : uint8_t { KeyOn, KeyOff, Legato };
	Type type;
	uint8_t voice;
	uint8_t note;
	uint8_t velocity;
};

class AllocPolicy
{
public:
	virtual ~AllocPolicy() = default;
	virtual void reset() = 0;
	virtual void noteOn(uint8_t note, uint8_t velocity, std::vector<AllocAction>& out) = 0;
	virtual void noteOff(uint8_t note, std::vector<AllocAction>& out) = 0;
	virtual void sustain(bool on, std::vector<AllocAction>& out) = 0;
	virtual void allNotesOff(std::vector<AllocAction>& out) = 0;
	virtual bool anyKeyHeld() const = 0;
};

// round robin from the last assigned voice, skipping voices whose key is held; sustained voices
// are reusable; no stealing when all 16 keys are held; note off frees the lowest voice with that note
class PolyAlloc final : public AllocPolicy
{
public:
	void reset() override;
	void noteOn(uint8_t note, uint8_t velocity, std::vector<AllocAction>& out) override;
	void noteOff(uint8_t note, std::vector<AllocAction>& out) override;
	void sustain(bool on, std::vector<AllocAction>& out) override;
	void allNotesOff(std::vector<AllocAction>& out) override;
	bool anyKeyHeld() const override;

private:
	static constexpr int kNone = -1;
	std::array<int, regs::kVoices> m_key{};
	std::array<bool, regs::kVoices> m_sustained{};
	int m_last = regs::kVoices - 1;
	bool m_pedal = false;

public:
	PolyAlloc() { reset(); }
};

// voice 0 only; up to 16 held keys; the sounding key is the highest or the lowest held one, the
// direction being set by the last two-key step (a new key is taken only past the current extreme)
class MonoAlloc final : public AllocPolicy
{
public:
	void reset() override;
	void noteOn(uint8_t note, uint8_t velocity, std::vector<AllocAction>& out) override;
	void noteOff(uint8_t note, std::vector<AllocAction>& out) override;
	void sustain(bool on, std::vector<AllocAction>& out) override;
	void allNotesOff(std::vector<AllocAction>& out) override;
	bool anyKeyHeld() const override { return !m_held.empty(); }

	static constexpr size_t kMaxKeys = 16;

private:
	void release(std::vector<AllocAction>& out);

	std::vector<uint8_t> m_held;
	int m_current = -1;
	bool m_up = true;
	bool m_sounding = false;
	bool m_pedal = false;
	uint8_t m_velocity = 0;
};
}
