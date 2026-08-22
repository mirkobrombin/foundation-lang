#include "sample_native.h"

static int32_t sample_measure(const sample_native_NativeContext *context) {
    return context->Value;
}

static int32_t sample_scale(const sample_native_NativeScale *scale) {
    return scale->Factor;
}

int main(void) {
    sample_native_NativePoint point = {0};
    point.Context.Value = 2;
    point.X = 7;
    point.Y = 11;
    point.Ready = false;
    point.Transform = sample_increment;
    point.Measure = sample_measure;
    point.Scale = sample_scale;
    sample_native_NativeScale scale = {.Factor = 4};
    sample_native_NativePoint shifted = {0};
    sample_shift(&point, &shifted, 3);
    return sizeof(point) == 40 && point.Measure(&point.Context) == 2 &&
                   point.Scale(&scale) == 4 &&
                   point.X == 7 && point.Y == 11 && !point.Ready &&
                   shifted.X == 13 && shifted.Y == 8 && shifted.Ready &&
                   sample_sine(0.0) == 0.0 &&
                   sample_apply(40) == 41 &&
                   sample_invoke(sample_increment, 60) == 61 &&
                   sample_increment(41) == 42 && sample_callback(4) == 15 &&
                   sample_round_trip(73) == 73
               ? 0
               : 1;
}
