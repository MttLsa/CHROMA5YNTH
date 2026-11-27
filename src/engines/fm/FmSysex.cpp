#include "FmSysex.h"
#include "FmEngine.h"
#include "LayerParams.h"
#include <algorithm>
#include <array>
#include <cmath>

namespace chroma
{
float fmRateToSeconds (int rate) noexcept
{
    return std::clamp (38.0f * std::exp2 (-(float) std::clamp (rate, 0, 99) / 6.0f), 0.0005f, 10.0f);
}

namespace
{
float ampOf (int level) noexcept { return FmEngine::levelToAmp ((float) std::clamp (level, 0, 99) / 99.0f); }
} // namespace

FmPatch convertFmVoice (const uint8_t* d)
{
    FmPatch p;
    auto set = [&p] (int id, float v)
    {
        const auto& desc = lp::descs[(size_t) id];
        p.values.emplace_back (id, std::clamp (v, desc.min, desc.max));
    };

    const int alg = d[110] & 31;
    const int fb = d[111] & 7;
    const bool keySync = (d[111] >> 3) & 1;
    set (lp::fmAlgorithm, (float) alg);
    set (lp::fmAlgorithmB, (float) alg);
    set (lp::fmMorph, 0.0f);
    set (lp::fmModIndex, 1.0f);
    const int fbOp = FmEngine::classic (alg).feedbackOp;

    float maxRelease = 0.05f;
    for (int k = 0; k < FmEngine::numOps; ++k)
    {
        const auto i = (size_t) k;
        const uint8_t* o = d + (FmEngine::numOps - 1 - k) * 17;   // stored op 6 first
        const int r1 = o[0], r2 = o[1], r3 = o[2], r4 = o[3];
        const int l1 = o[4], l3 = o[6];
        set (lp::fmOpOn[i], 1.0f);
        set (lp::fmOpKsBreak[i], (float) (o[8] + 21));
        set (lp::fmOpKsLeftDepth[i], (float) o[9] / 99.0f);
        set (lp::fmOpKsRightDepth[i], (float) o[10] / 99.0f);
        set (lp::fmOpKsLeftCurve[i], (float) (o[11] & 3));
        set (lp::fmOpKsRightCurve[i], (float) ((o[11] >> 2) & 3));
        set (lp::fmOpRateScale[i], (float) (o[12] & 7) / 7.0f);
        set (lp::fmOpDetune[i], (float) (((o[12] >> 3) & 15) - 7));
        set (lp::fmOpAmSens[i], (float) (o[13] & 3) / 3.0f);
        set (lp::fmOpVelSens[i], (float) ((o[13] >> 2) & 7) / 7.0f);
        set (lp::fmOpLevel[i], FmEngine::levelToKnob ((float) std::min<int> (o[14], 99) / 99.0f));
        const bool fixed = o[15] & 1;
        const int coarse = (o[15] >> 1) & 31, fine = std::min<int> (o[16], 99);
        set (lp::fmOpMode[i], fixed ? 1.0f : 0.0f);
        set (lp::fmOpCoarse[i], 0.0f);
        set (lp::fmOpFine[i], 0.0f);
        if (fixed)
            set (lp::fmOpFixed[i], std::pow (10.0f, (float) (coarse & 3)) * std::pow (10.0f, (float) fine / 100.0f));
        else
            set (lp::fmOpRatio[i], (coarse == 0 ? 0.5f : (float) coarse) * (1.0f + (float) fine / 100.0f));
        set (lp::fmOpWave[i], 0.0f);
        set (lp::fmOpReset[i], keySync ? 1.0f : 0.0f);
        set (lp::fmOpPhase[i], 0.0f);
        set (lp::fmOpSync[i], 0.0f);
        set (lp::fmOpPan[i], 0.0f);
        set (lp::fmOpFeedback[i], k == fbOp ? (float) fb / 7.0f : 0.0f);

        // EG: R1 → attack to the L1 peak, R2/R3 → decay to L3 (sustain, relative to the peak), R4 → release.
        const float peak = ampOf (l1);
        set (lp::fmOpAttack[i], fmRateToSeconds (r1));
        set (lp::fmOpHold[i], 0.0f);
        set (lp::fmOpDecay[i], 0.5f * (fmRateToSeconds (r2) + fmRateToSeconds (r3)));
        set (lp::fmOpSustain[i], peak > 0.0f ? std::min (1.0f, ampOf (l3) / peak) : 0.0f);
        const float rel = fmRateToSeconds (r4);
        set (lp::fmOpRelease[i], rel);
        set (lp::fmOpLoop[i], 0.0f);
        set (lp::fmOpTimeScale[i], 1.0f);
        if (FmEngine::classic (alg).carrier[i] > 0.0f)
            maxRelease = std::max (maxRelease, rel);
    }

    // Pitch EG (level 50 = no offset, ±50 = ±4 octaves), resting level L4.
    const int pr1 = d[102], pr2 = d[103], pr3 = d[104], pr4 = d[105];
    const int pl1 = d[106], pl3 = d[108], pl4 = d[109];
    const bool peakMoves = pl1 != pl4;
    const float amount = (float) ((peakMoves ? pl1 : pl3) - pl4) / 50.0f * 48.0f;
    set (lp::fmPenvAmount, amount);
    set (lp::fmPenvAttack, fmRateToSeconds (peakMoves ? pr1 : pr2));
    set (lp::fmPenvDecay, 0.5f * (fmRateToSeconds (pr2) + fmRateToSeconds (pr3)));
    set (lp::fmPenvSustain, peakMoves ? std::clamp ((float) (pl3 - pl4) / (float) (pl1 - pl4), 0.0f, 1.0f) : 1.0f);
    set (lp::fmPenvRelease, fmRateToSeconds (pr4));

    // LFO → layer LFO 1 + FM LFO 1 depths
    const int speed = d[112], delay = d[113], pmd = d[114], amd = d[115];
    const int lfoWave = (d[116] >> 1) & 7, pms = (d[116] >> 4) & 7;
    const bool lfoSync = d[116] & 1;
    static constexpr int shapes[6] { 1, 4, 3, 2, 0, 5 };   // tri, saw down, saw up, square, sine, S&H
    static constexpr float pmsSemis[8] { 0.0f, 0.1f, 0.2f, 0.35f, 0.6f, 1.0f, 3.0f, 12.0f };
    set (lp::lfoShape[0], (float) shapes[std::min (lfoWave, 5)]);
    set (lp::lfoRate[0], 0.065f * std::exp ((float) speed * 0.068f));
    set (lp::lfoSync[0], 0.0f);
    set (lp::lfoDelay[0], 4.0f * ((float) delay / 99.0f) * ((float) delay / 99.0f));
    set (lp::lfoMode[0], lfoSync ? 1.0f : 0.0f);
    set (lp::lfoAmount[0], 1.0f);
    set (lp::lfoUnipolar[0], 0.0f);
    set (lp::fmLfoPitch[0], (float) pmd / 99.0f * pmsSemis[pms]);
    set (lp::fmLfoAmp[0], (float) amd / 99.0f);
    set (lp::fmLfoOp[0], 0.0f);

    // Transpose (24 = C3) and a neutral Amp Env (the operators shape the sound).
    set (lp::pitch, (float) ((int) d[117] - 24));
    set (lp::envAttack[0], 0.0f);
    set (lp::envHold[0], 0.0f);
    set (lp::envDecay[0], 0.0f);
    set (lp::envSustain[0], 1.0f);
    set (lp::envRelease[0], maxRelease);
    set (lp::envVelocity[0], 0.0f);

    for (int c = 0; c < 10; ++c)
    {
        const char ch = (char) (d[118 + c] & 0x7f);
        p.name += (ch >= 32 && ch < 127) ? ch : ' ';
    }
    while (! p.name.empty() && p.name.back() == ' ')
        p.name.pop_back();
    return p;
}

bool parseFmBank (const uint8_t* data, size_t size, std::vector<FmPatch>& out)
{
    out.clear();
    const uint8_t* voices = nullptr;
    if (size >= 4104 && data[0] == 0xF0 && data[1] == 0x43 && data[3] == 0x09 && data[4] == 0x20 && data[5] == 0x00)
    {
        voices = data + 6;
        int sum = 0;
        for (int i = 0; i < 4096; ++i) sum += voices[i];
        if (((-sum) & 0x7f) != (data[4102] & 0x7f) || data[4103] != 0xF7)
            return false;
    }
    else if (size == 4096)
        voices = data;
    else
        return false;

    out.reserve (32);
    for (int v = 0; v < 32; ++v)
        out.push_back (convertFmVoice (voices + v * 128));
    return true;
}
} // namespace chroma
