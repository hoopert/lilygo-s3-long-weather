// Host harness for tools/check_pressure_logic.py. Reads
// "delta pressure humidity temp_c" per line and prints the band index and
// the four risk levels (0 low, 1 medium, 2 high), one line per input.
#include <cstdio>
#include <cstring>
#include <cmath>

#include "ui/pressure_logic.h"

int main() {
    // Sanity on the history helper first: 4 samples, NaN below that.
    const float hist[5] = {1010.0f, 1011.0f, 1012.0f, 1013.0f, 1009.0f};
    if (std::fabs(pressure_delta_3h(hist, 5, 1009.0f) - (-2.0f)) > 1e-4f) return 2;
    if (!std::isnan(pressure_delta_3h(hist, 3, 1009.0f))) return 3;

    char line[128];
    while (std::fgets(line, sizeof(line), stdin)) {
        float d, p, h, t;
        if (std::sscanf(line, "%f %f %f %f", &d, &p, &h, &t) != 4) continue;
        const PressureOutlook o = pressure_outlook(d);
        const PressureRisks r = pressure_risks(d, p, h, t);
        std::printf("%d %d %d %d %d\n", int(o.band), int(r.joint_pain), int(r.migraine),
                    int(r.sinus_ears), int(r.heart_strain));
    }
    return 0;
}
