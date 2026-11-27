#!/usr/bin/env python3
"""Generates the factory preset recipes (presets/recipes/*.txt) for tools/presetnorm.

  - 128 Layer presets (one synth, every note): Bass, Keys, Lead, Pad, Pluck, Chord, Percussive.
  - 24 Multi presets that use all 12 synths, NOTE ASSIGN on: synth n plays note n of the chromatic scale
    (Synth 1 = C, Synth 2 = C#, ... Synth 12 = B). The 12 synths of a Multi share a family (e.g. all basses)
    but each has its own character (engine, waveform, filter, envelope). "01 Bass Multi" is the first one.

Deterministic (fixed seed): run `python3 tools/gen_recipes.py` and then `presetnorm build` (presets/README.md).
All names are original (no third-party trademarks).
"""
import os
import random

ROOT = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))
OUT = os.path.join(ROOT, "presets", "recipes")
NOTES = ["C", "Db", "D", "Eb", "E", "F", "Gb", "G", "Ab", "A", "Bb", "B"]   # flats: "#" starts a comment in recipes
ENGINES = ["Analog", "Additive", "FM6", "Wavetable", "PD"]


def fmt(v):
    if isinstance(v, float):
        s = f"{v:.4f}".rstrip("0").rstrip(".")
        return s if s not in ("", "-0") else "0"
    return str(v)


def clamp(x, lo, hi):
    return max(lo, min(hi, x))


# ------------------------------------------------------------------------------------------------ building blocks
def amp(a, d, s, r, vel=0.3):
    return {"env1_attack": a, "env1_decay": d, "env1_sustain": s, "env1_release": r, "env1_velocity": vel}


def filt(model, cutoff, res=0.15, env=0.0, kt=0.3, a=0.002, d=0.4, s=0.3, r=0.3, drive=0.0):
    p = {"filter_on": "On", "filter_model": model, "filter_cutoff": float(clamp(cutoff, 30, 18000)),
         "filter_resonance": res, "filter_keytrack": kt}
    if drive: p["filter_drive"] = drive
    if env:
        p.update({"filter_env_amount": env, "env2_attack": a, "env2_decay": d, "env2_sustain": s, "env2_release": r})
    return p


def lfo_mod(dest, amount, rate, slot=1, lfo=1, shape=None, free=True):
    p = {f"lfo{lfo}_rate": rate, f"mm{slot}_source": f"LFO {lfo}", f"mm{slot}_dest": dest, f"mm{slot}_amount": amount}
    if free: p[f"lfo{lfo}_mode"] = "Free"
    if shape: p[f"lfo{lfo}_shape"] = shape
    return p


def fx_chorus(mix=0.3):
    return {"fx_chorus_on": "On", "fx_chorus_mix": mix}


def fx_reverb(mix=0.25, size=0.7, decay=3.0):
    return {"fx_reverb_on": "On", "fx_reverb_mix": mix, "fx_reverb_size": size, "fx_reverb_decay": decay}


def fx_delay(mix=0.2, fb=0.3, l="1/8D", r="1/4"):
    return {"fx_delay_on": "On", "fx_delay_sync": "On", "fx_delay_div_l": l, "fx_delay_div_r": r,
            "fx_delay_feedback": fb, "fx_delay_mix": mix}


def fx_drive(t="Tube", drive=0.25, mix=0.5):
    return {"fx_dist_on": "On", "fx_dist_type": t, "fx_dist_drive": drive, "fx_dist_mix": mix}


def mono(glide=0.0, legato=False):
    p = {"voice_mode": "Legato" if legato else "Mono"}
    if glide: p.update({"glide_on": "On", "glide_time": glide})
    return p


# ------------------------------------------------------------------------------------------------ engine voices
WT_TABLES = ["Basic Shapes", "Sine Harmonics", "PWM", "Saw Sync", "Square Sync", "Vowels", "Formant Sweep",
             "Odd Harmonics", "Harmonic Sweep", "Spectral Comb", "Bell", "Drawbars", "FM 2:1", "FM 3:2",
             "PD Saw", "PD Reso", "Wavefolder", "Drive", "Chebyshev", "Saw Stack", "Choir", "Brass", "Strings",
             "Reed", "Metallic", "Hollow", "Windowed Sine", "Sine Pairs", "Triangle Fold", "Spectral Tilt"]


def analog(w1="Saw", w2=None, oct2=0, semi2=0, fine2=0.0, lvl2=0.6, w3=None, oct3=0, semi3=0, lvl3=0.5,
           sub=0.0, noise=0.0, pw=None, ss=None, out=0.7):
    p = {"engine": "Analog", "an_osc1_wave": w1, "an_master_level": out}
    if pw is not None: p["an_osc1_pw"] = pw
    if ss: p.update({"an_osc1_ss_detune": ss[0], "an_osc1_ss_spread": ss[1]})
    if w2:
        p.update({"an_osc2_on": "On", "an_osc2_wave": w2, "an_osc2_octave": oct2, "an_osc2_semi": semi2,
                  "an_osc2_fine": fine2, "an_osc2_level": lvl2})
    if w3:
        p.update({"an_osc3_on": "On", "an_osc3_wave": w3, "an_osc3_octave": oct3, "an_osc3_semi": semi3,
                  "an_osc3_level": lvl3})
    if sub: p.update({"an_sub_on": "On", "an_sub_level": sub})
    if noise: p["an_noise_level"] = noise
    return p


def fm(alg, ops, out=0.7, fb=None):
    """ops: list of (ratio, level, decay, sustain, release[, detune]) for op 1..n; level = knob (amplitude = level²)."""
    p = {"engine": "FM6", "fm_algorithm": str(alg), "fm_output": out}
    for i, o in enumerate(ops, start=1):
        ratio, level, dec, sus, rel = o[:5]
        p.update({f"fm_op{i}_ratio": float(ratio), f"fm_op{i}_level": float(level), f"fm_op{i}_decay": float(dec),
                  f"fm_op{i}_sustain": float(sus), f"fm_op{i}_release": float(rel)})
        if len(o) > 5 and o[5]: p[f"fm_op{i}_detune"] = float(o[5])
    for i in range(len(ops) + 1, 7):
        p[f"fm_op{i}_on"] = "Off"
    if fb: p[f"fm_op{fb[0]}_feedback"] = fb[1]
    return p


def wt(t1, pos=0.0, t2=None, pos2=0.0, oct2=0, semi2=0, vol2=0.5, uni=1, det=15.0, sub=0.0, t3=None, semi3=0, vol3=0.4):
    p = {"engine": "Wavetable", "wt_osc1_table": t1, "wt_osc1_pos": pos}
    if uni > 1: p.update({"wt_osc1_unison": uni, "wt_osc1_uni_detune": det})   # no random phase: repeatable loudness
    if t2:
        p.update({"wt_osc2_on": "On", "wt_osc2_table": t2, "wt_osc2_pos": pos2, "wt_osc2_octave": oct2,
                  "wt_osc2_semi": semi2, "wt_osc2_volume": vol2})
    if t3:
        p.update({"wt_osc3_on": "On", "wt_osc3_table": t3, "wt_osc3_semi": semi3, "wt_osc3_volume": vol3})
    if sub: p.update({"wt_sub_on": "On", "wt_sub_level": sub})
    return p


def pd(w1="Saw", amt=0.4, env=0.5, ea=0.001, ed=0.4, es=0.2, w2=None, oct2=0, coarse2=0, lvl2=0.5, amt2=0.3, out=0.7):
    p = {"engine": "PD", "pd_l1_wave": w1, "pd_l1_amount": amt, "pd_l1_env_amount": env, "pd_l1_env_attack": ea,
         "pd_l1_env_decay": ed, "pd_l1_env_sustain": es, "pd_output": out}
    if w2:
        p.update({"pd_l2_on": "On", "pd_l2_wave": w2, "pd_l2_octave": oct2, "pd_l2_coarse": coarse2,
                  "pd_l2_level": lvl2, "pd_l2_amount": amt2})
    return p


def additive(a="Saw", b=None, morph=0.0, partials=48, bright=1.0, slope=12.0, odd=1.0, even=1.0, inh=0.0, out=0.7):
    p = {"engine": "Additive", "add_spectrum_a": a, "add_partials": partials, "add_brightness": bright,
         "add_slope": slope, "add_output": out}
    if b: p.update({"add_spectrum_b": b, "add_morph": morph})
    if odd != 1.0: p["add_odd"] = odd
    if even != 1.0: p["add_even"] = even
    if inh: p["add_inharm"] = inh
    return p


# ------------------------------------------------------------------------------------------------ category designers
# Each returns the layer keys of one patch; `k` = variant index, `e` = engine, `r` = random.Random (deterministic).
def bass(k, e, r):
    c = r.uniform(0.7, 1.4)
    if e == "Analog":
        w = ["Saw", "Square", "Pulse", "Saw", "Triangle"][k % 5]
        p = analog(w, w2=["Square", "Saw", "Sine"][k % 3], oct2=-1, lvl2=0.55, fine2=r.choice([0.0, 6.0, -5.0]),
                   sub=0.4 if k % 2 else 0.0, pw=r.uniform(20, 40) if w == "Pulse" else None)
        p.update(filt(["Ladder LP24", "Diode LP", "OTA LP24", "Classic LP24"][k % 4], 260 * c, 0.2 + 0.25 * (k % 3) / 2,
                      env=0.35 + 0.2 * r.random(), d=0.18 + 0.3 * r.random(), s=0.1, drive=0.2))
    elif e == "FM6":
        p = fm(5 if k % 2 else 1, [(1, 1.0, 1.2, 0.7, 0.15), (r.choice([1, 2, 0.5]), 0.42 + 0.12 * r.random(), 0.25 + 0.3 * r.random(), 0.15, 0.1),
                                   (1, 0.45, 1.0, 0.6, 0.15), (r.choice([1, 3]), 0.3, 0.4, 0.2, 0.1)], fb=(2, 0.3) if k % 3 == 0 else None)
    elif e == "Wavetable":
        p = wt(r.choice(["Saw Stack", "Drive", "Basic Shapes", "Wavefolder", "PWM"]), pos=r.uniform(0.1, 0.6), sub=0.5)
        p.update(filt("Ladder LP24", 500 * c, 0.2, env=0.35, d=0.35, s=0.2))
        p.update({"mm1_source": "Mod Env 1", "mm1_dest": "WT OSC 1 Position", "mm1_amount": 0.3})
    elif e == "PD":
        p = pd(r.choice(["Saw", "Square", "Saw-Pulse"]), amt=0.25, env=0.55 + 0.2 * r.random(), ed=0.25 + 0.25 * r.random(), es=0.15,
               w2="Square", oct2=-1, lvl2=0.45, amt2=0.2)
    else:
        p = additive("Saw" if k % 2 else "Square", partials=32, bright=0.7, slope=14.0)
        p.update(filt("Ladder LP24", 420 * c, 0.25, env=0.4, d=0.3, s=0.15))
    p.update(mono(0.03 if k % 3 == 0 else 0.0))
    p.update(amp(0.002, 0.6, 0.8, 0.12, 0.35))
    if k % 4 == 1: p.update(fx_drive("Tube", 0.25, 0.5))
    if k % 4 == 3: p.update(fx_drive("Op-Amp Clip", 0.2, 0.35))
    return p


def keys(k, e, r):
    if e == "FM6":
        p = fm(5, [(1, 1.0, 2.5 + r.random() * 2, 0.0, 0.5), (r.choice([14, 1, 7]), 0.3 + 0.1 * r.random(), 0.3, 0.0, 0.3),
                   (1, 0.8, 2.0, 0.0, 0.5, 3), (1, 0.45, 1.2, 0.1, 0.3)])
        p.update(fx_chorus(0.3))
    elif e == "Additive":
        p = additive(r.choice(["Organ", "Bell", "Glass", "Hollow"]), partials=48, bright=0.85)
        p.update({"add_penv_amount": 0.5, "add_penv_decay": 0.8, "add_penv_sustain": 0.3})
    elif e == "Analog":
        p = analog(r.choice(["Saw", "Square", "Triangle"]), w2="Triangle", oct2=1, lvl2=0.3, fine2=4.0)
        p.update(filt("SVF LP12", 900 + 600 * r.random(), 0.1, env=0.35, d=0.7, s=0.25, kt=0.5))
    elif e == "Wavetable":
        p = wt(r.choice(["Drawbars", "Bell", "Sine Harmonics", "Reed", "Windowed Sine"]), pos=r.uniform(0, 0.5),
               t2="Sine Harmonics", oct2=1, vol2=0.25)
        p.update(filt("SVF LP12", 2500 + 2000 * r.random(), 0.1, kt=0.5))
    else:
        p = pd(r.choice(["Double Sine", "Saw", "Pulse"]), amt=0.3, env=0.4, ed=0.9, es=0.2)
    p.update(amp(0.002, 1.8 + r.random(), 0.35 if k % 2 else 0.0, 0.5, 0.45))
    p.update(fx_reverb(0.18, 0.6, 2.2))
    if k % 3 == 2: p.update({"fx_phaser_on": "On", "fx_phaser_mix": 0.3})
    return p


def lead(k, e, r):
    if e == "Analog":
        w = ["Saw", "Square", "SuperSaw", "Pulse"][k % 4]
        p = analog(w, w2="Saw", fine2=r.choice([7.0, -9.0, 12.0]), lvl2=0.6, ss=(0.35, 0.6) if w == "SuperSaw" else None,
                   pw=30.0 if w == "Pulse" else None)
        p.update(filt(r.choice(["Ladder LP24", "Diode LP", "Cascade LP24"]), 1500 + 1500 * r.random(), 0.3, env=0.3, d=0.5, s=0.5))
    elif e == "FM6":
        p = fm(r.choice([1, 18, 16]), [(1, 1.0, 2, 0.8, 0.3), (1, 0.5, 1, 0.6, 0.3), (1, 0.8, 2, 0.8, 0.3, 4),
                                         (2, 0.4, 1, 0.5, 0.3), (1, 0.35, 1, 0.5, 0.3)], fb=(5, 0.4))
    elif e == "Wavetable":
        p = wt(r.choice(["Saw Sync", "Square Sync", "Brass", "Formant Sweep", "Harmonic Sweep"]), pos=r.uniform(0.1, 0.5),
               uni=3, det=12.0)
        p.update(lfo_mod("WT OSC 1 Position", 0.2, r.uniform(0.2, 0.8)))
    elif e == "PD":
        p = pd(r.choice(["Reso 1", "Reso 2", "Reso 3", "Saw"]), amt=0.45, env=0.4, ed=0.8, es=0.5)
    else:
        p = additive("Square" if k % 2 else "Saw", "Vocal A", morph=0.3, partials=64)
        p.update(lfo_mod("Morph", 0.3, 0.3))
    p.update(mono(0.05 + 0.05 * r.random(), legato=k % 2 == 0))
    p.update({"lfo2_rate": 5.0 + r.random(), "lfo2_delay": 0.4, "mm2_source": "LFO 2", "mm2_dest": "Pitch", "mm2_amount": 0.004})
    p.update(amp(0.005, 1.0, 0.9, 0.25, 0.25))
    p.update(fx_delay(0.18, 0.3))
    return p


def pad(k, e, r):
    if e == "Analog":
        p = analog("SuperSaw", ss=(0.3 + 0.2 * r.random(), 0.8), w2="Saw", oct2=-1, fine2=6.0, lvl2=0.4)
        p.update(filt("OTA LP24", 1200 + 1500 * r.random(), 0.15))
        p.update(lfo_mod("Cutoff", 0.15, 0.08 + 0.1 * r.random()))
    elif e == "Wavetable":
        p = wt(r.choice(["Vowels", "Choir", "Strings", "Hollow", "Spectral Comb", "Formant Sweep"]), pos=r.uniform(0.1, 0.7),
               uni=4, det=14.0, t2="Sine Harmonics", oct2=-1, vol2=0.35)
        p.update(lfo_mod("WT OSC 1 Position", 0.3, 0.07 + 0.1 * r.random()))
    elif e == "Additive":
        p = additive(r.choice(["Hollow", "Strings", "Vocal O", "Organ"]), r.choice(["Strings", "Glass", "Vocal A"]), partials=64)
        p.update({"add_width": 1.0, "add_spread": 0.6})
        p.update(lfo_mod("Morph", 0.45, 0.08 + 0.06 * r.random()))
    elif e == "FM6":
        p = fm(r.choice([5, 22, 31]), [(1, 0.95, 4, 0.9, 2.5), (1, 0.3, 4, 0.8, 2), (2, 0.8, 4, 0.9, 2.5, 5),
                                         (1, 0.28, 4, 0.8, 2), (0.5, 0.7, 4, 0.9, 2.5, -4)])
    else:
        p = pd("Double Sine", amt=0.35, env=0.3, ea=1.5, ed=2.0, es=0.6, w2="Saw", coarse2=7, lvl2=0.3)
    p.update(amp(0.6 + 1.2 * r.random(), 1.5, 0.85, 2.0 + 1.5 * r.random(), 0.15))
    p.update(fx_chorus(0.3))
    p.update(fx_reverb(0.32, 0.85, 4.5))
    return p


def pluck(k, e, r):
    if e == "Analog":
        p = analog(r.choice(["Saw", "Square", "Pulse"]), w2="Triangle", oct2=1, lvl2=0.3, pw=25.0)
        p.update(filt(r.choice(["Ladder LP24", "SVF LP12"]), 350, 0.35, env=0.55, d=0.18 + 0.15 * r.random(), s=0.0, kt=0.5))
    elif e == "FM6":
        p = fm(5, [(1, 1.0, 0.5 + 0.4 * r.random(), 0, 0.3), (r.choice([2, 3, 5]), 0.5, 0.12, 0, 0.1),
                   (2, 0.6, 0.6, 0, 0.3), (r.choice([1, 7]), 0.35, 0.2, 0, 0.1)])
    elif e == "Wavetable":
        p = wt(r.choice(["Bell", "Metallic", "FM 3:2", "Odd Harmonics", "PD Reso"]), pos=r.uniform(0.2, 0.8))
        p.update(filt("SVF LP12", 600, 0.2, env=0.5, d=0.25, s=0.0, kt=0.5))
    elif e == "PD":
        p = pd(r.choice(["Reso 2", "Saw", "Pulse"]), amt=0.15, env=0.7, ed=0.2 + 0.2 * r.random(), es=0.0)
    else:
        p = additive(r.choice(["Glass", "Bell", "Saw"]), partials=48)
        p.update({"add_penv_amount": 0.8, "add_penv_decay": 0.25, "add_penv_sustain": 0.0})
    p.update(amp(0.001, 0.5 + 0.5 * r.random(), 0.0, 0.35, 0.4))
    p.update(fx_delay(0.22, 0.35, r.choice(["1/8D", "1/16"]), "1/8"))
    p.update(fx_reverb(0.15, 0.5, 1.8))
    return p


CHORDS = [(4, 7), (3, 7), (7, 12), (5, 7), (3, 10), (4, 11), (2, 7), (7, 10)]   # intervals above the root


def chord(k, e, r):
    i1, i2 = CHORDS[k % len(CHORDS)]
    if e == "Analog":
        p = analog("Saw", w2="Saw", semi2=i1 if i1 <= 12 else 12, lvl2=0.7, w3="Saw", semi3=min(i2, 12), lvl3=0.65)
        p.update(filt("Ladder LP24", 700, 0.25, env=0.45, d=0.35, s=0.25))
    elif e == "Wavetable":
        t = r.choice(["Saw Stack", "Brass", "Strings", "PWM", "Basic Shapes"])
        p = wt(t, pos=r.uniform(0.1, 0.6), t2=t, semi2=i1, vol2=0.7, t3=t, semi3=min(i2, 12), vol3=0.65)
        p.update(filt("OTA LP24", 1400, 0.2, env=0.3, d=0.4, s=0.4))
    elif e == "FM6":
        ratios = [1, 2 ** (i1 / 12), 2 ** (min(i2, 12) / 12)]
        p = fm(32, [(ratios[0], 1.0, 1.5, 0.5, 0.4), (ratios[1], 0.95, 1.5, 0.5, 0.4), (ratios[2], 0.92, 1.5, 0.5, 0.4)], out=0.55)
    else:
        p = pd("Saw", amt=0.3, env=0.45, ed=0.45, es=0.3, w2="Saw", coarse2=i1, lvl2=0.75, amt2=0.3)
        p["engine"] = "PD"
    p.update(amp(0.003, 0.9, 0.5 if k % 2 else 0.2, 0.4, 0.35))
    p.update(fx_chorus(0.25))
    p.update(fx_reverb(0.22, 0.65, 2.5))
    if k % 3 == 0: p.update(fx_delay(0.15, 0.25))
    return p


def perc(k, e, r):
    if e == "FM6":
        p = fm(r.choice([5, 1, 7]), [(1, 1.0, 0.25 + 0.3 * r.random(), 0, 0.2), (r.choice([1.41, 3.5, 2.76, 5.19]), 0.55, 0.12, 0, 0.1),
                                     (r.choice([1, 2.5]), 0.7, 0.4, 0, 0.3), (r.choice([3.14, 7.1]), 0.4, 0.1, 0, 0.1)])
        p.update({"fm_penv_amount": r.choice([12.0, 24.0, 7.0]), "fm_penv_decay": 0.04 + 0.05 * r.random()})
    elif e == "Analog":
        p = analog(r.choice(["Sine", "Triangle"]), w2="Square", semi2=r.choice([5, 7, 11]), lvl2=0.3, noise=0.3 + 0.3 * r.random())
        p.update(filt("SVF LP12", 1500 + 2000 * r.random(), 0.35, env=0.5, d=0.08, s=0.0, kt=0.8))
    elif e == "Wavetable":
        p = wt(r.choice(["Metallic", "Band Noise", "Bitcrush", "Bell", "Chebyshev"]), pos=r.uniform(0, 1))
        p.update(filt("SVF LP12", 2500, 0.3, env=0.4, d=0.1, s=0.0, kt=0.7))
    elif e == "PD":
        p = pd(r.choice(["Reso 3", "Pulse", "Reso 1"]), amt=0.1, env=0.8, ed=0.08, es=0.0)
    else:
        p = additive(r.choice(["Bell", "Glass"]), partials=32, inh=0.3 + 0.4 * r.random())
        p.update({"add_penv_amount": 0.9, "add_penv_decay": 0.15, "add_penv_sustain": 0.0})
    p.update(amp(0.0005, 0.15 + 0.35 * r.random(), 0.0, 0.2, 0.6))
    if k % 2: p.update(fx_reverb(0.2, 0.4, 1.2))
    if k % 3 == 1: p.update(fx_drive("Hard", 0.2, 0.3))
    return p


def bell(k, e, r):
    p = fm(5, [(1, 1.0, 4 + 2 * r.random(), 0, 2.5), (r.choice([3.5, 1.41, 5.19, 2.76]), 0.55, 2.5, 0, 1.5),
               (r.choice([2, 4]), 0.8, 3, 0, 2, 3), (r.choice([5.19, 7.0, 9.0]), 0.4, 1.5, 0, 1)])
    p.update(amp(0.001, 5.0, 0.0, 2.5, 0.5))
    p.update(fx_reverb(0.3, 0.8, 4.0))
    return p


def organ(k, e, r):
    if e == "Additive":
        p = additive("Organ", partials=r.choice([16, 24, 32]), odd=r.uniform(0.6, 1.0), even=r.uniform(0.6, 1.0))
    elif e == "Wavetable":
        p = wt("Drawbars", pos=r.uniform(0, 1))
    else:
        p = fm(32, [(0.5, 0.8, 9, 1, 0.1), (1, 1.0, 9, 1, 0.1), (1.5, 0.7, 9, 1, 0.1), (2, 0.75, 9, 1, 0.1), (3, 0.6, 9, 1, 0.1),
                    (4, r.uniform(0.3, 0.6), 9, 1, 0.1)], out=0.4)
    p.update(amp(0.008, 1.0, 1.0, 0.08, 0.0))
    p.update({"fx_chorus_on": "On", "fx_chorus_rate": 6.0, "fx_chorus_mix": 0.25})
    p.update(fx_reverb(0.18, 0.6, 2.0))
    return p


def strings(k, e, r):
    if e == "Analog":
        p = analog("Saw", w2="Saw", fine2=8.0, lvl2=0.8, w3="Saw", oct3=1, lvl3=0.3)
        p.update(filt("SVF LP12", 2200 + 1000 * r.random(), 0.1))
    elif e == "Wavetable":
        p = wt("Strings", pos=r.uniform(0.2, 0.8), uni=5, det=12.0)
    else:
        p = additive("Strings", "Saw", morph=r.uniform(0, 0.4), partials=64)
        p.update({"add_width": 1.0, "add_spread": 0.5})
    p.update({"lfo1_rate": 5.5, "lfo1_delay": 0.5, "mm1_source": "LFO 1", "mm1_dest": "Pitch", "mm1_amount": 0.003})
    p.update(amp(0.25 + 0.2 * r.random(), 1.0, 0.9, 0.8, 0.3))
    p.update(fx_chorus(0.35))
    p.update(fx_reverb(0.28, 0.8, 3.5))
    return p


def brass(k, e, r):
    if e == "Analog":
        p = analog("Saw", w2="Saw", fine2=-6.0, lvl2=0.7)
        p.update(filt("Ladder LP24", 600, 0.1, env=0.45, a=0.06, d=0.5, s=0.5))
    elif e == "Wavetable":
        p = wt("Brass", pos=0.3, uni=2, det=8.0)
        p.update(filt("OTA LP24", 900, 0.1, env=0.35, a=0.05, d=0.5, s=0.6))
    else:
        p = fm(22, [(1, 1.0, 2, 0.85, 0.2), (1, 0.55, 0.6, 0.6, 0.2), (1, 0.9, 2, 0.85, 0.2, 5), (1, 0.85, 2, 0.85, 0.2, -5),
                    (1, 0.85, 2, 0.8, 0.2, 2), (1, 0.5, 0.6, 0.6, 0.2)], out=0.5)
    p.update(amp(0.04 + 0.04 * r.random(), 1.0, 0.85, 0.2, 0.45))
    p.update(fx_reverb(0.2, 0.6, 2.0))
    return p


def synth(k, e, r):
    d = [bass, lead, pluck, keys][k % 4]
    p = d(k, e, r)
    p.pop("voice_mode", None); p.pop("glide_on", None); p.pop("glide_time", None)
    return p


DESIGNERS = {"Bass": bass, "Keys": keys, "Lead": lead, "Pad": pad, "Pluck": pluck, "Chord": chord, "Percussive": perc,
             "Bell": bell, "Organ": organ, "Strings": strings, "Brass": brass, "Synth": synth}
ENGINES_FOR = {"Bass": ENGINES, "Keys": ENGINES, "Lead": ENGINES, "Pad": ENGINES, "Pluck": ENGINES,
               "Chord": ["Analog", "Wavetable", "FM6", "PD"], "Percussive": ENGINES, "Bell": ["FM6"],
               "Organ": ["Additive", "Wavetable", "FM6"], "Strings": ["Analog", "Wavetable", "Additive"],
               "Brass": ["Analog", "Wavetable", "FM6"], "Synth": ENGINES}

# ------------------------------------------------------------------------------------------------ names
ADJ = ["Amber", "Velvet", "Copper", "Silver", "Neon", "Hollow", "Frozen", "Solar", "Lunar", "Dusty", "Molten", "Glassy",
       "Rusty", "Misty", "Electric", "Paper", "Iron", "Golden", "Crimson", "Cobalt", "Quiet", "Wild", "Soft", "Deep",
       "Bright", "Warm", "Cold", "Polar", "Hazy", "Sharp", "Liquid", "Static", "Distant", "Tidal", "Prism", "Ember"]
NOUN = {"Bass": ["Rumble", "Floor", "Engine", "Thump", "Low End", "Pulse", "Undertow", "Bedrock", "Growl", "Root"],
        "Keys": ["Tines", "Keys", "Piano", "Hammer", "Parlour", "Felt", "Console", "Lounge", "Clav", "Ivory"],
        "Lead": ["Lead", "Whistle", "Laser", "Solo", "Siren", "Line", "Hero", "Voice", "Comet", "Blade"],
        "Pad": ["Horizon", "Cloud", "Field", "Haze", "Aurora", "Drift", "Choir", "Veil", "Ocean", "Halo"],
        "Pluck": ["Pluck", "Drop", "Harp", "Pick", "Kalimba", "Pizz", "Spark", "Ping", "Seed", "Ripple"],
        "Chord": ["Stab", "Chord", "Memory", "Hit", "Stack", "Accord", "Block", "Swell", "Fifths", "Triad"],
        "Percussive": ["Mallet", "Knock", "Gong", "Tom", "Click", "Wood", "Metal", "Drum", "Clang", "Tap"]}

LAYER_PLAN = [("Bass", 20), ("Keys", 18), ("Lead", 20), ("Pad", 20), ("Pluck", 18), ("Chord", 16), ("Percussive", 16)]
CAT_OUT = {"Percussive": "Percussive"}

MULTI_PLAN = [  # (name, family, category, tags)
    ("01 Bass Multi", "Bass", "Bass", "multi, bass, note assign"),
    ("02 Pad Multi", "Pad", "Pad", "multi, pad, note assign"),
    ("03 Synth Multi", "Synth", "Other", "multi, synth, note assign"),
    ("04 Lead Multi", "Lead", "Lead", "multi, lead, note assign"),
    ("05 Keys Multi", "Keys", "Keys", "multi, keys, note assign"),
    ("06 Pluck Multi", "Pluck", "Pluck", "multi, pluck, note assign"),
    ("07 Chord Multi", "Chord", "Chord", "multi, chord, note assign"),
    ("08 Percussive Multi", "Percussive", "Percussive", "multi, percussive, note assign"),
    ("09 Bell Multi", "Bell", "Bell", "multi, bell, fm, note assign"),
    ("10 Organ Multi", "Organ", "Organ", "multi, organ, note assign"),
    ("11 Strings Multi", "Strings", "Strings", "multi, strings, note assign"),
    ("12 Brass Multi", "Brass", "Brass", "multi, brass, note assign"),
    ("13 Chromatic Drift", "Pad", "Texture", "multi, pad, evolving, note assign"),
    ("14 Twelve Echoes", "Pluck", "Sequence", "multi, pluck, delay, note assign"),
    ("15 Glass Spectrum", "Bell", "Bell", "multi, glass, note assign"),
    ("16 Dark Matter", "Bass", "Bass", "multi, dark, distorted, note assign"),
    ("17 Sunrise Ensemble", "Strings", "Strings", "multi, ensemble, note assign"),
    ("18 Tape Keys", "Keys", "Keys", "multi, lofi, keys, note assign"),
    ("19 Metal Garden", "Percussive", "Percussive", "multi, metallic, note assign"),
    ("20 Neon Arcade", "Lead", "Lead", "multi, chip, lead, note assign"),
    ("21 Velvet Stabs", "Chord", "Chord", "multi, stabs, note assign"),
    ("22 Prism Mallets", "Percussive", "Percussive", "multi, mallets, note assign"),
    ("23 Hybrid Engines", "Synth", "Other", "multi, all engines, note assign"),
    ("24 Slow Motion", "Pad", "Pad", "multi, slow, ambient, note assign"),
]

MULTI_TWEAK = {   # extra character applied to every synth of some free multis
    "16 Dark Matter": {"fx_dist_on": "On", "fx_dist_type": "Fuzz", "fx_dist_drive": 0.35, "fx_dist_mix": 0.4},
    "18 Tape Keys": {"fx_chorus_on": "On", "fx_chorus_rate": 0.4, "fx_chorus_depth": 0.6, "fx_chorus_mix": 0.4,
                     "fx_eq_on": "On", "fx_eq_high_gain": -6.0},
    "20 Neon Arcade": {"fx_dist_on": "On", "fx_dist_type": "Bitcrush", "fx_dist_drive": 0.3, "fx_dist_mix": 0.35},
    "14 Twelve Echoes": {"fx_delay_on": "On", "fx_delay_sync": "On", "fx_delay_feedback": 0.45, "fx_delay_mix": 0.3},
    "24 Slow Motion": {"env1_attack": 2.5, "env1_release": 5.0, "fx_reverb_on": "On", "fx_reverb_decay": 8.0, "fx_reverb_mix": 0.4},
}


def write(path, lines):
    with open(path, "w", encoding="utf-8") as f:
        f.write("\n".join(lines) + "\n")


def main():
    for f in os.listdir(OUT):
        if f.endswith(".txt"):
            os.remove(os.path.join(OUT, f))
    used = set()
    r = random.Random(5)

    def name_for(cat):
        for _ in range(500):
            n = f"{r.choice(ADJ)} {r.choice(NOUN[cat])}"
            if n not in used:
                used.add(n)
                return n
        raise RuntimeError("names exhausted")

    # ---- 128 Layer presets
    lines = ["# GENERATED by tools/gen_recipes.py - 128 Layer presets (one synth, all notes). Syntax: tools/presetnorm/main.cpp"]
    total = 0
    for cat, count in LAYER_PLAN:
        engines = ENGINES_FOR[cat]
        for k in range(count):
            e = engines[k % len(engines)]
            p = DESIGNERS[cat](k, e, random.Random(hash((cat, k)) & 0xffff if False else 1000 * total + 17))
            lines += ["", f"[{name_for(cat)}]", "kind = layer", f"category = {cat}",
                      f"tags = {e.lower()}, {cat.lower()}"]
            lines += [f"{key} = {fmt(v)}" for key, v in p.items()]
            total += 1
    write(os.path.join(OUT, "1_layers.txt"), lines)
    assert total == 128, total

    # ---- 24 Multi presets: synth n -> note n
    lines = ["# GENERATED by tools/gen_recipes.py - 24 Multi presets: all 12 synths, NOTE ASSIGN synth n = note n (1 = C ... 12 = B)."]
    for mi, (name, fam, cat, tags) in enumerate(MULTI_PLAN):
        engines = ENGINES_FOR[fam]
        lines += ["", f"[{name}]", "kind = multi", f"category = {cat}", f"tags = {tags}"]
        for n in range(12):
            e = engines[(n + mi) % len(engines)]
            p = DESIGNERS[fam](n, e, random.Random(7919 * (mi + 1) + n))
            # character variation across the 12 synths: brightness, stereo position, envelope
            p = dict(p)
            if "filter_cutoff" in p:
                p["filter_cutoff"] = float(clamp(p["filter_cutoff"] * (0.75 + 0.05 * n), 30, 18000))
            p["pan"] = round(-0.35 + 0.7 * ((n * 5) % 12) / 11.0, 3)
            p.update(MULTI_TWEAK.get(name, {}))
            lines.append(f"L{n + 1}.on = On")
            lines += [f"L{n + 1}.{key} = {fmt(v)}" for key, v in p.items()]
            lines.append(f"scale.L{n + 1} = {NOTES[n]}")
    write(os.path.join(OUT, "2_multi_note_assign.txt"), lines)
    print(f"128 layer + {len(MULTI_PLAN)} multi recipes written to {OUT}")


if __name__ == "__main__":
    main()
