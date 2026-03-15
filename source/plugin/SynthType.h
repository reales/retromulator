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
        OpenWurli = 8,  // Wurlitzer 200A (OpenWurli physical model)
        OPL3      = 9,  // Yamaha OPL3 / YMF262 (Nuked OPL3)

        Count
    };

    // Display order for the synth combo box (alphabetical).
    inline const SynthType* synthTypeDisplayOrder(int& count)
    {
        static const SynthType order[] = {
            SynthType::AkaiS1000,
            SynthType::DX7,
            SynthType::JE8086,
            SynthType::MicroQ,
            SynthType::NordN2X,
            SynthType::OPL3,
            SynthType::OpenWurli,
            SynthType::VirusABC,
            SynthType::VirusTI,
            SynthType::XT,
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
            case SynthType::OpenWurli: return "OpenWurli";
            case SynthType::OPL3:      return "OPL3";
            default:                   return "None";
        }
    }
}
