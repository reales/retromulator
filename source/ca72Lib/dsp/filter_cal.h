#pragma once

#include "common.h"
#include "expo.h"
#include "vcf.h"

namespace ca72::filter_cal
{
struct FilterTrims
{
    double r39 = 0.0;
    double r49 = 0.0;
    double r73_pos = 0.0;

    bool operator== (const FilterTrims&) const = default;

    ca72::vcf::FilterExpo expo() const;
    ca72::vcf::VcfCircuit circuit() const;
};

inline constexpr FilterTrims FACTORY {
    .r39 = 0.632274,
    .r49 = 327.0304,
    .r73_pos = 0.782635,
};

inline constexpr FilterTrims CALIBRATED {
    .r39 = 0.559,
    .r49 = FACTORY.r49,
    .r73_pos = FACTORY.r73_pos,
};

inline constexpr double G_BUS_OFF = 4.0 / 33e3 + 1.0 / 11e3;

std::array<ca72::expo::Input, 6> inputs (double cutoff, bool kb1, bool kb2, double v_kbd);
double oscillation (double rate, const FilterTrims& trims, std::span<const ca72::expo::Input> ins);
bool regenerates (double rate, const FilterTrims& trims, std::span<const ca72::expo::Input> ins, double emphasis);
FilterTrims folkman (double rate);
} // namespace ca72::filter_cal
