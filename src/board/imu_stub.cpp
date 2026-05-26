#include "board.h"

// The FNK0104B has no IMU. We synthesise just enough of an accelerometer
// reading to keep src/main.cpp:isFaceDown() (and checkShake()) working
// without touching their bodies. A 2-finger continuous touch reported by
// the touch driver flips the z-axis to "face down".

void ImuShim::getAccelData(float* ax, float* ay, float* az) {
  *ax = 0.0f;
  *ay = 0.0f;
  *az = fnk::isCovered() ? -1.0f : 1.0f;
}
