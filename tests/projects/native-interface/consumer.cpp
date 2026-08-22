#include "sample_native.h"

static int32_t sampleMeasure(const sample_native_NativeContext *context) {
    return context->Value;
}

static int32_t sampleScale(const sample_native_NativeScale *scale) {
    return scale->Factor;
}

int main() {
    sample_native_NativePoint point{};
    point.Context.Value = 3;
    point.X = 13;
    point.Y = 21;
    point.Ready = true;
    point.Transform = sample_increment;
    point.Measure = sampleMeasure;
    point.Scale = sampleScale;
    sample_native_NativeScale scale{4};
    sample_native_NativePoint shifted{};
    sample_shift(&point, &shifted, 5);
    return sizeof(point) == 40 && point.Measure(&point.Context) == 3 &&
                   point.Scale(&scale) == 4 &&
                   shifted.X == 22 && shifted.Y == 16 && !shifted.Ready &&
                   sample_sine(0.0) == 0.0 &&
                   sample_apply(80) == 81 &&
                   sample_invoke(sample_increment, 100) == 101 &&
                   sample_increment(9) == 10 && sample_callback(5) == 17
               ? 0
               : 1;
}
