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

        Count
    };

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
            case SynthType::DX7:      return "DX7";
            default:                  return "None";
        }
    }
}
