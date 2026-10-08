#include <cmath>
#include <cstdint>

#include "prx/libc/include/General.hpp"
#include "prx/libSceNgs2.native/include/Ngs2Types.hpp"

namespace {

float AngleDelta(float speakerAngle, float paramAngle, float unitAngle) {
    float delta = std::fmod(std::fabs(speakerAngle - paramAngle), unitAngle);
    if (delta > unitAngle * 0.5f) delta = unitAngle - delta;
    return delta;
}

}

#pragma GCC visibility push(default)

extern "C" {

int APS5_VABI sceNgs2PanInit(Ngs2PanWork* work, const float* speaker_angles, float unit_angle, uint32_t num_speakers) {
    if (work == nullptr) APS5_INVALID_ARG_EX;
    if (num_speakers > 8) APS5_INVALID_ARG_EX;
    if (!std::isfinite(unit_angle) || unit_angle <= 0.0f) APS5_INVALID_ARG_EX;
    for (uint32_t i = 0; i < num_speakers; i++) {
        if (speaker_angles != nullptr && !std::isfinite(speaker_angles[i])) APS5_INVALID_ARG_EX;
    }
    *work = {};
    for (uint32_t i = 0; i < num_speakers; i++) work->speakerAngle[i] = speaker_angles == nullptr ? 0.0f : speaker_angles[i];
    work->unitAngle = unit_angle;
    work->numSpeakers = num_speakers;
    return SCE_NGS2_OK;
}

int APS5_VABI sceNgs2PanGetVolumeMatrix(Ngs2PanWork* work, const Ngs2PanParam* params, uint32_t num_params, uint32_t matrix_format, float* out_volume_matrix) {
    if (work == nullptr || params == nullptr || out_volume_matrix == nullptr) APS5_INVALID_ARG_EX;
    if (num_params == 0 || work->numSpeakers == 0 || work->numSpeakers > 8) APS5_INVALID_ARG_EX;
    (void)matrix_format;
    for (uint32_t p = 0; p < num_params; p++) {
        const auto& param = params[p];
        if (!std::isfinite(param.angle) || !std::isfinite(param.distance) || !std::isfinite(param.fbwLevel) || !std::isfinite(param.lfeLevel)) APS5_INVALID_ARG_EX;
        if (param.distance <= 0.0f) APS5_INVALID_ARG_EX;
        float sumSquares = 0.0f;
        for (uint32_t i = 0; i < work->numSpeakers; i++) {
            const float gain = std::max(0.0f, std::cos(3.14159265358979323846f * AngleDelta(work->speakerAngle[i], param.angle, work->unitAngle) / work->unitAngle));
            out_volume_matrix[p * work->numSpeakers + i] = gain;
            sumSquares += gain * gain;
        }
        const float scale = sumSquares > 0.0f ? param.fbwLevel / param.distance / std::sqrt(sumSquares) : 0.0f;
        for (uint32_t i = 0; i < work->numSpeakers; i++) out_volume_matrix[p * work->numSpeakers + i] *= scale;
    }
    return SCE_NGS2_OK;
}

}

#pragma GCC visibility pop

