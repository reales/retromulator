#include "freq_dial.h"

namespace ca72::freq_dial
{
const std::array<double, 5>& Pot::curve() const
{
    switch (value)
    {
        case Two: return OSC2_SEMITONES;
        case Three: return OSC3_SEMITONES;
    }
    return OSC2_SEMITONES;
}

const std::array<std::pair<double, double>, 5>& Pot::hardware() const
{
    switch (value)
    {
        case Two: return OSC2_HARDWARE;
        case Three: return OSC3_HARDWARE;
    }
    return OSC2_HARDWARE;
}

double Pot::semitones (double wiper_) const
{
    double x = ca72::clamp (wiper_, 0.0, 1.0) - 0.5;
    const auto& c = curve();
    double a = 0.0;
    for (size_t k = c.size(); k-- > 0;)
        a = a * x + c[k];
    return x * a;
}

double Pot::wiper (double semitones_) const
{
    if (semitones_ <= this->semitones (0.0))
        return 0.0;
    if (semitones_ >= this->semitones (1.0))
        return 1.0;
    double slope = curve()[0];
    double w = 0.5 + semitones_ / slope;
    for (int it = 0; it < 8; ++it)
    {
        double x = ca72::clamp (w, 0.0, 1.0) - 0.5;
        const auto& c = curve();
        double d = 0.0;
        for (size_t k = c.size(); k-- > 0;)
            d = d * x + (double) (k + 1) * c[k];
        w = ca72::clamp (w - (this->semitones (w) - semitones_) / d, 0.0, 1.0);
    }
    return w;
}

double Pot::wiper_at (Law law, double knob) const
{
    double k = ca72::clamp (knob, 0.0, 1.0);
    switch (law)
    {
        case Law::Semitones: return this->wiper ((2.0 * k - 1.0) * DIAL_END);
        case Law::Hardware: return along (hardware(), k);
    }
    return this->wiper ((2.0 * k - 1.0) * DIAL_END);
}

double Pot::knob_at (Law law, double wiper_) const
{
    double w = ca72::clamp (wiper_, 0.0, 1.0);
    switch (law)
    {
        case Law::Semitones: return ca72::clamp (0.5 + this->semitones (w) / (2.0 * DIAL_END), 0.0, 1.0);
        case Law::Hardware: return back_along (hardware(), w);
    }
    return ca72::clamp (0.5 + this->semitones (w) / (2.0 * DIAL_END), 0.0, 1.0);
}

double along (std::span<const std::pair<double, double>> points, double knob)
{
    for (size_t i = 0; i + 1 < points.size(); ++i)
    {
        auto [pa, ta] = points[i];
        auto [pb, tb] = points[i + 1];
        if (knob <= pb)
            return ta + (tb - ta) * (knob - pa) / (pb - pa);
    }
    return points[points.size() - 1].second;
}

double back_along (std::span<const std::pair<double, double>> points, double track)
{
    if (track <= points[0].second)
        return points[0].first;
    for (size_t i = 0; i + 1 < points.size(); ++i)
    {
        auto [pa, ta] = points[i];
        auto [pb, tb] = points[i + 1];
        if (track <= tb)
            return pa + (pb - pa) * (track - ta) / (tb - ta);
    }
    return points[points.size() - 1].first;
}
} // namespace ca72::freq_dial
