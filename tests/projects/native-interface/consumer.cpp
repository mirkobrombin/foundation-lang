#include "sample_native.h"

int main() {
    return sample_increment(9) == 10 && sample_callback(5) == 17 ? 0 : 1;
}
