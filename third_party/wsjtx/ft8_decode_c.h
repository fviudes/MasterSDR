#pragma once

#ifdef __cplusplus
extern "C" {
#endif

#include <stdint.h>

// Maximum decodes per call
#define FT8_MAX_DECODES 50
#define FT8_MSG_LEN 38

// C interface to the WSJT-X Fortran FT8 decoder
// This function signature matches what the Fortran decoder expects
// via its C_interface_module

int ft8_decode_c(
    const float* audio,           // Input: float32 PCM @ 12000 Hz, 15s = 180000 samples
    int samples,                  // Input: number of samples
    int sampleRate,               // Input: sample rate (should be 12000)
    double dialFreqHz,            // Input: dial frequency in Hz
    double freqLow,               // Input: lowest decode frequency offset
    double freqHigh,              // Input: highest decode frequency offset
    const char* myCall,           // Input: my callsign (12 chars max)
    const char* myGrid,           // Input: my grid (6 chars max)
    double* outFreqs,             // Output: decoded frequencies (offsets from dial)
    double* outSnrs,              // Output: SNR values in dB
    double* outDts,               // Output: delta-time offsets in seconds
    char* outMessages,            // Output: decoded messages (FT8_MSG_LEN * FT8_MAX_DECODES chars)
    int maxResults                // Input: max results to return
);

#ifdef __cplusplus
}
#endif
