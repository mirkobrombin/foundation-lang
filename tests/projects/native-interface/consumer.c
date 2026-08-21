#include "sample_native.h"

int main(void) {
    return sample_increment(41) == 42 && sample_callback(4) == 15 ? 0 : 1;
}
