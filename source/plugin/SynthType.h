#pragma once

namespace retromulator
{
    enum class SynthType
    {
        None = -1,
        VirusABC = 0,   // Virus A / B / C
        VirusTI  = 1,   // Virus TI / TI2
        MicroQ   = 2,   // Waldorf MicroQ / Q
        XT       = 3,   // Waldorf XT / Microwave XT
        NordN2X  = 4,   // Nord Lead A1X / A2X (N2X)
        JE8086   = 5,   // Roland JD-800 / JD-990 (Ronaldo)
        DX7      = 6,   // Yamaha DX7 (VDX7)
        AkaiS1000 = 7,  // Akai S1000 (SFZero sample player)

        Count
    };

    // Display order for the synth combo box (Akai S1000 first).
    // Returns an array of SynthType values in the preferred combo order.
    inline const SynthType* synthTypeDisplayOrder(int& count)
    {
        static const SynthType order[] = {
            SynthType::AkaiS1000,
            SynthType::VirusABC,
            SynthType::VirusTI,
            SynthType::MicroQ,
            SynthType::XT,
            SynthType::NordN2X,
            SynthType::JE8086,
            SynthType::DX7,
        };
        count = static_cast<int>(sizeof(order) / sizeof(order[0]));
        return order;
    }

    inline const char* synthTypeName(SynthType t)
    {
        switch(t)
        {
            case SynthType::VirusABC: return "Virus ABC";
            case SynthType::VirusTI:  return "Virus TI";
            case SynthType::MicroQ:   return "MicroQ";
            case SynthType::XT:       return "XT";
            case SynthType::NordN2X:  return "Nord N2X";
            case SynthType::JE8086:   return "JE-8086";
            case SynthType::DX7:       return "DX7";
            case SynthType::AkaiS1000: return "Akai S1000";
            default:                   return "None";
        }
    }
}
