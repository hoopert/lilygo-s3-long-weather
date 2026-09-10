#include "pressure_logic.h"

#include <math.h>

namespace {

bool finite(float v) { return !isnan(v) && !isinf(v); }

RiskLevel level(bool high, bool medium) {
    if (high) return RiskLevel::High;
    if (medium) return RiskLevel::Medium;
    return RiskLevel::Low;
}

}  // namespace

PressureOutlook pressure_outlook(float delta_3h) {
    if (!finite(delta_3h)) return {PressureBand::Unknown, "Clear", "NO TREND YET", false};
    if (delta_3h <= -3.0f) return {PressureBand::StormRisk,    "Storm Risk",    "FALLING FAST", true};
    if (delta_3h <= -1.0f) return {PressureBand::ClearingSlow, "Clearing Slow", "FALLING",      false};
    if (delta_3h <   1.0f) return {PressureBand::Clear,        "Clear",         "STEADY",       false};
    if (delta_3h <   3.0f) return {PressureBand::Clearing,     "Clearing",      "RISING",       false};
    return {PressureBand::WindRisk, "Wind Risk", "RISING FAST", true};
}

PressureRisks pressure_risks(float delta_3h, float pressure_hpa,
                             float humidity_pct, float temp_c) {
    const float d  = finite(delta_3h) ? delta_3h : 0.0f;
    const bool  hp = finite(pressure_hpa);
    const bool  hh = finite(humidity_pct);
    const bool  ht = finite(temp_c);

    PressureRisks r;
    r.joint_pain = level(
        d <= -2.5f || (hp && hh && pressure_hpa < 1008.0f && humidity_pct > 80.0f),
        d <= -1.0f || (hp && pressure_hpa < 1005.0f));
    r.migraine = level(fabsf(d) >= 2.5f, fabsf(d) >= 1.2f);
    r.sinus_ears = level(d <= -3.0f, d <= -1.5f || d >= 3.0f);
    r.heart_strain = level(
        ht && hp && temp_c < 5.0f && pressure_hpa > 1022.0f,
        ht && hp && temp_c < 10.0f && pressure_hpa > 1018.0f);
    r.any_high = r.joint_pain == RiskLevel::High || r.migraine == RiskLevel::High ||
                 r.sinus_ears == RiskLevel::High || r.heart_strain == RiskLevel::High;
    return r;
}

float pressure_delta_3h(const float *history, size_t count, float now) {
    if (history == nullptr || count < 4 || !finite(now)) return NAN;
    const float then = history[count - 4];
    if (!finite(then)) return NAN;
    return now - then;
}

const char *risk_word(RiskLevel level) {
    switch (level) {
        case RiskLevel::High:   return "HIGH";
        case RiskLevel::Medium: return "MEDIUM";
        default:                return "LOW";
    }
}
