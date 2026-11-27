#!/usr/bin/env python3
"""Generate C++ parameter code, generic GUI layout and manual tables from params/*.yaml.

Outputs (in --out): ParamIDs.h, ParamLayout.cpp, GuiLayout.h
Manual table (--docs): Markdown, one table per section.
"""
import argparse
import pathlib
import re
import sys

import yaml

TYPES = {"float", "int", "bool", "choice"}
ID_RE = re.compile(r"^[a-z][a-z0-9_]*$")


def fail(msg):
    sys.exit(f"gen_params: error: {msg}")


def camel(pid):
    parts = pid.split("_")
    return parts[0] + "".join(p[:1].upper() + p[1:] for p in parts[1:])


def cstr(s):
    return '"' + str(s).replace("\\", "\\\\").replace('"', '\\"') + '"'


def fnum(v):
    return f"{float(v)!r}f"


def subst(text, env):
    text = str(text)
    for k, v in env.items():
        text = text.replace("{" + k + "}", str(v))
    return text


def expand(data, fname):
    """Yield (template, instance) pairs: plain params plus repeated blocks."""
    for p in data.get("params", []):
        yield p, dict(p), None
    for b in data.get("blocks", []):
        rep = b.get("repeat", {})
        count = int(rep.get("count", 1))
        vars_ = rep.get("vars", {})
        for k, v in vars_.items():
            if len(v) != count:
                fail(f"{fname}: block var '{k}' needs {count} values")
        for i in range(count):
            env = {"n": i + 1}
            env.update({k: v[i] for k, v in vars_.items()})
            for t in b.get("params", []):
                q = {**{k: v for k, v in b.items() if k not in ("repeat", "params")}, **t}
                for k in ("id", "name", "section", "description", "description_en"):
                    if k in q:
                        q[k] = subst(q[k], env)
                if "default_by_index" in q:
                    q["default"] = q["default_by_index"].get(i + 1, q["default"])
                if count > 1 and "array" not in q:
                    fail(f"{fname}:{t.get('id')}: repeated params need 'array'")
                q["_index"], q["_count"] = i, count
                doc = None
                if i == 0:
                    doc = dict(q)
                    docenv = {"n": "N" if count > 1 else 1}
                    docenv.update({k: f"{v[0]}…{v[-1]}" for k, v in vars_.items()})
                    for k in ("name", "section", "description", "description_en"):
                        doc[k] = subst({**b, **t}.get(k, ""), docenv)
                    if "default_by_index" in t:
                        doc["default"] = t["default"]
                        extra = ", ".join(f"{k}: {v}" for k, v in t["default_by_index"].items())
                        doc["description"] += f" Default per indice ({extra})."
                yield t, q, doc


def load(params_dir):
    out, docs, seen, lists = [], [], set(), {}
    files = sorted(pathlib.Path(params_dir).glob("*.yaml"))
    if not files:
        fail(f"no yaml files in {params_dir}")
    raw = [(f, yaml.safe_load(f.read_text(encoding="utf-8")) or {}) for f in files]
    for f, data in raw:
        lists.update(data.get("lists", {}))
    # internal float params are modulation destinations unless mod_dest: false
    for f, data in raw:
        scope = data.get("scope", "host")
        for t, p, doc in expand(data, f.name):
            where = f"{f.name}:{p.get('id', '?')}"
            p["scope"] = scope
            for k in ("id", "name", "type", "default", "section", "description"):
                if k not in p:
                    fail(f"{where}: missing '{k}'")
            if not ID_RE.match(p["id"]):
                fail(f"{where}: id must be snake_case")
            if p["id"] in seen:
                fail(f"{where}: duplicate id")
            seen.add(p["id"])
            t_ = p["type"]
            if t_ not in TYPES:
                fail(f"{where}: unknown type '{t_}'")
            if t_ in ("float", "int"):
                if "min" not in p or "max" not in p:
                    fail(f"{where}: min/max required")
                if not (p["min"] <= p["default"] <= p["max"]):
                    fail(f"{where}: default out of range")
            p.setdefault("unit", "")
            p.setdefault("version", 1)
            p.setdefault("automatable", True)
            p.setdefault("gui", True)
            p.setdefault("mod_dest", scope == "layer" and t_ == "float")
            out.append(p)
            if doc is not None:
                doc.update({k: p[k] for k in ("scope", "unit")})
                docs.append(doc)
            elif "_index" not in p:
                docs.append(p)
    lists["mod_destinations"] = ["Off"] + [p["name"] for p in out if p["scope"] == "layer" and p["mod_dest"]]
    for p in out + docs:
        if p["type"] != "choice":
            continue
        if "choices_from" in p:
            if p["choices_from"] not in lists:
                fail(f"{p['id']}: unknown list '{p['choices_from']}'")
            p["choices"] = lists[p["choices_from"]]
        ch = [str(c) for c in p.get("choices", [])]
        if not ch:
            fail(f"{p['id']}: choices required")
        d = p["default"]
        idx = d if isinstance(d, int) and not isinstance(d, bool) else (ch.index(str(d)) if str(d) in ch else -1)
        if not (0 <= idx < len(ch)):
            fail(f"{p['id']}: invalid default")
        p["choices"], p["default_index"] = ch, idx
    return out, docs, lists


def arrays(params):
    groups = {}
    for p in params:
        if "array" in p:
            groups.setdefault(p["array"], []).append(p)
    return groups


def gen_ids(params):
    lines = ["// GENERATED by tools/gen_params.py - do not edit", "#pragma once", "#include <array>", "",
             "namespace chroma::ParamIDs", "{"]
    for p in params:
        if "_index" not in p or p.get("_count", 1) == 1:
            lines.append(f"    inline constexpr const char* {camel(p['id'])} = {cstr(p['id'])};")
    for name, g in arrays(params).items():
        lines.append(f"    inline constexpr std::array<const char*, {len(g)}> {name} {{ {', '.join(cstr(p['id']) for p in g)} }};")
    lines += [f"    inline constexpr int count = {len(params)};", "}", ""]
    return "\n".join(lines)


def gen_layout(params):
    L = ["// GENERATED by tools/gen_params.py - do not edit",
         "#include <juce_audio_processors/juce_audio_processors.h>",
         '#include "ParamIDs.h"', "",
         "namespace chroma", "{",
         "juce::AudioProcessorValueTreeState::ParameterLayout createParameterLayout()", "{",
         "    juce::AudioProcessorValueTreeState::ParameterLayout layout;",
         "    using namespace juce;"]
    for p in params:
        pid = f"ParameterID {{ {cstr(p['id'])}, {int(p['version'])} }}"
        attrs = f"AudioParameter{{t}}Attributes().withLabel({cstr(p['unit'])}).withAutomatable({'true' if p['automatable'] else 'false'})"
        t = p["type"]
        if t == "float":
            rng = f"NormalisableRange<float> ({fnum(p['min'])}, {fnum(p['max'])}, {fnum(p.get('step', 0.0))})"
            L.append("    {")
            L.append(f"        {rng.replace('NormalisableRange<float> ', 'NormalisableRange<float> r ')};")
            if "skew" in p:
                L.append(f"        r.setSkewForCentre ({fnum(p['skew'])});")
            L.append(f"        layout.add (std::make_unique<AudioParameterFloat> ({pid}, {cstr(p['name'])}, r, {fnum(p['default'])}, {attrs.format(t='Float')}));")
            L.append("    }")
        elif t == "int":
            L.append(f"    layout.add (std::make_unique<AudioParameterInt> ({pid}, {cstr(p['name'])}, {int(p['min'])}, {int(p['max'])}, {int(p['default'])}, {attrs.format(t='Int')}));")
        elif t == "bool":
            L.append(f"    layout.add (std::make_unique<AudioParameterBool> ({pid}, {cstr(p['name'])}, {'true' if p['default'] else 'false'}, {attrs.format(t='Bool')}));")
        else:
            ch = ", ".join(cstr(c) for c in p["choices"])
            L.append(f"    layout.add (std::make_unique<AudioParameterChoice> ({pid}, {cstr(p['name'])}, StringArray {{ {ch} }}, {p['default_index']}, {attrs.format(t='Choice')}));")
    L += ["    return layout;", "}", "} // namespace chroma", ""]
    return "\n".join(L)


def gen_gui(params):
    params = [p for p in params if p["gui"]]
    sections = list(dict.fromkeys(p["section"] for p in params))
    L = ["// GENERATED by tools/gen_params.py - do not edit", "#pragma once", "#include <array>", "",
         "namespace chroma::gui", "{",
         "struct ParamEntry { const char* id; const char* name; const char* section; const char* type; int numChoices; const char* description; };", "",
         f"inline constexpr std::array<const char*, {len(sections)}> sections {{ {', '.join(cstr(s) for s in sections)} }};", "",
         f"inline constexpr std::array<ParamEntry, {len(params)}> params {{{{"]
    for p in params:
        L.append(f"    {{ {cstr(p['id'])}, {cstr(p['name'])}, {cstr(p['section'])}, {cstr(p['type'])}, {len(p.get('choices', []))}, {cstr(p.get('description', ''))} }},")
    L += ["}};", "}", ""]
    return "\n".join(L)


def gen_layer(params, lists):
    L = ["// GENERATED by tools/gen_params.py - do not edit", "#pragma once", "#include <array>", "",
         "namespace chroma::lp", "{",
         "enum Id : int", "{"]
    for p in params:
        L.append(f"    {camel(p['id'])},")
    L += ["    count", "};", "",
          "enum class Type { Float, Int, Bool, Choice };", "",
          "struct Desc",
          "{",
          "    const char* id; const char* name; const char* section; Type type;",
          "    float min, max, def, skewCentre; bool hasSkew; int numChoices; const char* const* choices; bool modDest;",
          "    const char* unit; const char* description;",
          "};", ""]
    for p in params:
        if p["type"] == "choice" and "choices_from" not in p:
            L.append(f"inline constexpr const char* const choices_{camel(p['id'])}[] {{ {', '.join(cstr(c) for c in p['choices'])} }};")
    for name, items in lists.items():
        L.append(f"inline constexpr const char* const list_{camel(name)}[] {{ {', '.join(cstr(c) for c in items)} }};")
        L.append(f"inline constexpr int list_{camel(name)}_size = {len(items)};")
    L += ["", "inline constexpr std::array<Desc, count> descs {{"]
    tmap = {"float": "Float", "int": "Int", "bool": "Bool", "choice": "Choice"}
    for p in params:
        t = p["type"]
        if t == "choice":
            mn, mx, df = 0, len(p["choices"]) - 1, p["default_index"]
            ch = f"list_{camel(p['choices_from'])}" if "choices_from" in p else f"choices_{camel(p['id'])}"
        elif t == "bool":
            mn, mx, df, ch = 0, 1, 1 if p["default"] else 0, "nullptr"
        else:
            mn, mx, df, ch = p["min"], p["max"], p["default"], "nullptr"
        sk = p.get("skew", 0.0)
        L.append(f"    Desc {{ {cstr(p['id'])}, {cstr(p['name'])}, {cstr(p['section'])}, Type::{tmap[t]}, {fnum(mn)}, {fnum(mx)}, {fnum(df)}, "
                 f"{fnum(sk)}, {'true' if 'skew' in p else 'false'}, {len(p.get('choices', []))}, {ch}, {'true' if p['mod_dest'] else 'false'}, "
                 f"{cstr(p.get('unit', ''))}, {cstr(p.get('description', ''))} }},")
    L += ["}};", ""]
    dests = [camel(p["id"]) for p in params if p["mod_dest"]]
    L.append(f"// index 0 of the destination list is 'Off'")
    L.append(f"inline constexpr std::array<Id, {len(dests)}> modDestinations {{ {', '.join(dests)} }};")
    for name, g in arrays(params).items():
        L.append(f"inline constexpr std::array<Id, {len(g)}> {name} {{ {', '.join(camel(p['id']) for p in g)} }};")
    L += ["} // namespace chroma::lp", ""]
    return "\n".join(L)


def fmt_range(p):
    t = p["type"]
    if t in ("float", "int"):
        return f"{p['min']} … {p['max']}"
    if t == "bool":
        return "Off / On"
    return " / ".join(p["choices"])


def fmt_default(p):
    t = p["type"]
    if t == "bool":
        return "On" if p["default"] else "Off"
    if t == "choice":
        return p["choices"][p["default_index"]]
    return str(p["default"])


def gen_docs(params):
    params = [p for p in params if p.get("gui", True) or p["scope"] == "layer" or p.get("automatable", True)]
    L = ["<!-- GENERATED by tools/gen_params.py from params/*.yaml - do not edit -->", "",
         "# Parametri", ""]
    for s in dict.fromkeys(p["section"] for p in params):
        L += [f"## {s}", "", "| Parametro | Range | Default | Unità | Descrizione |", "|---|---|---|---|---|"]
        for p in params:
            if p["section"] == s:
                rng = fmt_range(p) if len(p.get("choices", [])) <= 24 else f"{len(p['choices'])} voci"
                L.append(f"| {p['name']} | {rng} | {fmt_default(p)} | {p['unit']} | {p['description']} |")
        L.append("")
    return "\n".join(L)


def gen_docs_en(params):
    """English reference: names/ranges/defaults/units; description only where 'description_en' is given."""
    params = [p for p in params if p.get("gui", True) or p["scope"] == "layer" or p.get("automatable", True)]
    L = ["<!-- GENERATED by tools/gen_params.py from params/*.yaml - do not edit -->", "",
         "# Parameter reference", "",
         "Generated from the same source as the plug-in. Detailed descriptions of every parameter are shown as tooltips "
         "inside the plug-in and in the [Italian reference](../parameters.md); N = Layer / slot / operator index.", ""]
    for s in dict.fromkeys(p["section"] for p in params):
        L += [f"## {s}", "", "| Parameter | Range | Default | Unit | Notes |", "|---|---|---|---|---|"]
        for p in params:
            if p["section"] == s:
                rng = fmt_range(p) if len(p.get("choices", [])) <= 24 else f"{len(p['choices'])} items"
                L.append(f"| {p['name']} | {rng} | {fmt_default(p)} | {p['unit']} | {p.get('description_en', '')} |")
        L.append("")
    return "\n".join(L)


def write_if_changed(path, text):
    path = pathlib.Path(path)
    path.parent.mkdir(parents=True, exist_ok=True)
    if not path.exists() or path.read_text(encoding="utf-8") != text:
        path.write_text(text, encoding="utf-8", newline="\n")


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument("--params", required=True)
    ap.add_argument("--out", required=True)
    ap.add_argument("--docs")
    ap.add_argument("--docs-en")
    a = ap.parse_args()
    allp, docs, lists = load(a.params)
    host = [p for p in allp if p["scope"] == "host"]
    layer = [p for p in allp if p["scope"] == "layer"]
    out = pathlib.Path(a.out)
    write_if_changed(out / "ParamIDs.h", gen_ids(host))
    write_if_changed(out / "ParamLayout.cpp", gen_layout(host))
    write_if_changed(out / "GuiLayout.h", gen_gui(host))
    write_if_changed(out / "LayerParams.h", gen_layer(layer, {k: v for k, v in lists.items()}))
    if a.docs:
        write_if_changed(a.docs, gen_docs(docs))
    if a.docs_en:
        write_if_changed(a.docs_en, gen_docs_en(docs))


if __name__ == "__main__":
    main()
