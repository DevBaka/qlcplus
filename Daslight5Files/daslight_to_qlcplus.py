#!/usr/bin/env python3
# -*- coding: utf-8 -*-
"""
daslight_to_qlcplus.py

Converts fixture personalities found in a Daslight 5 show file (.dvc) into
QLC+ fixture definitions (.qxf).

WHY THE .dvc FILE AND NOT THE .ssl2 FILES?
-------------------------------------------
Daslight's standalone .ssl2 fixture library files are stored in a proprietary
encrypted binary format with no documented/open algorithm, so they can't be
read directly. However, the .dvc save file embeds a full, UNENCRYPTED copy of
every fixture personality that is actually patched in the show, inside the
<PATCHS DATA="..."/> element (base64 + zlib deflate). This script extracts
that data and converts it to QLC+'s .qxf format.

USAGE
-----
    python daslight_to_qlcplus.py <path-to-savefile.dvc> [-o OUTPUT_DIR]

    python daslight_to_qlcplus.py Daslight5_savefile.dvc -o qlcplus_fixtures

It will create one .qxf file per unique fixture found in the show, sorted
into Manufacturer sub-folders (like QLC+'s own fixture library layout), e.g.:

    qlcplus_fixtures/Varytec/Varytec-Giga-Bar-240-LED-RGB.qxf

HOW TO IMPORT INTO QLC+
------------------------
Copy the generated manufacturer folders into your QLC+ user fixture
directory, then restart QLC+ (or use "Fixture Editor" > "Open" per file):

    Windows: %USERPROFILE%\\QLC+\\Fixtures
    Or system-wide (needs admin): C:\\Program Files\\QLC+\\Fixtures

LIMITATIONS / PLEASE REVIEW AFTER IMPORT
-----------------------------------------
This is a best-effort, heuristic conversion:
  - Fixture "Type" (Moving Head / LED Bar / Color Changer / ...) is guessed
    from Daslight's internal category codes. Check/adjust it in QLC+'s
    Fixture Editor if it looks wrong.
  - Physical dimensions are converted from Daslight's cm to QLC+'s mm; take
    them as a rough approximation.
  - Colour-wheel / gobo-wheel presets are imported as named capabilities but
    without real wheel colours - you may want to add those in the editor.
  - Any channel type this script doesn't recognize is imported with group
    "Nothing" and a "TODO" marker in its capability name/comment so you can
    find and fix it easily.
"""

import argparse
import base64
import os
import re
import sys
import zlib
import xml.etree.ElementTree as ET
from xml.sax.saxutils import escape as xml_escape

# ---------------------------------------------------------------------------
# Daslight SSLCHANNELTYPE -> QLC+ channel Group
# ---------------------------------------------------------------------------
TYPE_GROUP = {
    "0": "Effect",     # generic function / macro / music control
    "1": "Pan",
    "2": "Tilt",
    "5": "Colour",      # colour wheel
    "7": "Intensity",   # dimmer
    "8": "Gobo",
    "14": "Beam",       # zoom
    "15": "Shutter",    # strobe / shutter
    "18": "Speed",
    "25": "Colour",     # red
    "26": "Colour",     # green
    "27": "Colour",     # blue
    "31": "Colour",     # white / uv
    "37": "Effect",     # function / macro
    "46": "Colour",     # uv
}

# Daslight type -> simple QLC+ channel Preset, used only when the channel has
# exactly one capability spanning the whole 0-255 range.
def simple_preset(chan_type, name):
    lname = name.lower()
    if chan_type == "7":
        return "IntensityMasterDimmer" if "master" in lname else "IntensityDimmer"
    if chan_type == "25":
        return "IntensityRed"
    if chan_type == "26":
        return "IntensityGreen"
    if chan_type == "27":
        return "IntensityBlue"
    if chan_type in ("31", "46"):
        if "uv" in lname:
            return "IntensityUV"
        return "IntensityWhite"
    if chan_type == "1":
        return "PositionPan"
    if chan_type == "2":
        return "PositionTilt"
    if chan_type == "18":
        has_pan = "pan" in lname
        has_tilt = "tilt" in lname
        if has_pan and has_tilt:
            return "SpeedPanTiltSlowFast"
        if has_pan:
            return "SpeedPanSlowFast"
        if has_tilt:
            return "SpeedTiltSlowFast"
    return None

FINE_SUFFIX_OK = {
    "IntensityMasterDimmer", "IntensityDimmer", "IntensityRed", "IntensityGreen",
    "IntensityBlue", "IntensityUV", "IntensityWhite", "PositionPan", "PositionTilt",
}


def sanitize_filename(name):
    name = re.sub(r"[^A-Za-z0-9]+", "-", name).strip("-")
    return re.sub(r"-{2,}", "-", name)


def find_patch_blobs(text):
    """Return list of decompressed PATCHS DATA blocks (as str) found in the .dvc file."""
    blobs = []
    for m in re.finditer(r'<PATCHS[^>]*\bDATA="([^"]+)"', text):
        raw = base64.b64decode(m.group(1))
        idx = raw.find(b"\x78\x9c")
        if idx < 0:
            idx = raw.find(b"\x78\x01")
        if idx < 0:
            idx = raw.find(b"\x78\xda")
        if idx < 0:
            continue
        try:
            dec = zlib.decompress(raw[idx:])
        except zlib.error:
            continue
        blobs.append(dec.decode("utf-8", errors="replace"))
    return blobs


def parse_fixtures(blobs):
    """Parse all <SSLLIBRARY> elements from the decompressed patch blobs,
    deduplicated by SSLNAME (the library path Daslight uses internally)."""
    fixtures = {}
    for blob in blobs:
        # Multiple <FIXTURES>...</FIXTURES> siblings aren't valid as a single
        # XML doc, so wrap the whole thing in one root before parsing.
        wrapped = "<root>" + blob + "</root>"
        try:
            root = ET.fromstring(wrapped)
        except ET.ParseError:
            # try to recover by cutting at the last well-formed close tag
            cut = wrapped.rfind("</SSLLIBRARY>")
            if cut < 0:
                continue
            wrapped = wrapped[: cut + len("</SSLLIBRARY>")] + "</root>"
            try:
                root = ET.fromstring(wrapped)
            except ET.ParseError:
                continue
        for lib in root.iter("SSLLIBRARY"):
            name = lib.get("SSLNAME", "")
            if name and name not in fixtures:
                fixtures[name] = lib
    return fixtures


def guess_manufacturer_model(ssl_name, props):
    parts = [p for p in re.split(r"[\\/]", ssl_name) if p]
    filename = parts[-1] if parts else ssl_name
    model_guess = re.sub(r"\.ssl2$", "", filename, flags=re.IGNORECASE).strip()

    manufacturer = None
    if len(parts) >= 2:
        candidate = parts[-2]
        if candidate.lower() not in ("_cloud", "_varied"):
            manufacturer = candidate

    rdm_manuf = (props.get("SSLRDMMANUFACTURERNAME") or "").strip()
    rdm_model = (props.get("SSLRDMFIXTURENAME") or "").strip()

    if not manufacturer:
        manufacturer = rdm_manuf or "Generic"
    model = rdm_model or model_guess
    # avoid "Lixada Lixada ..." style duplication when the model name already
    # starts with the manufacturer name
    if model.lower().startswith(manufacturer.lower() + " "):
        model = model[len(manufacturer) + 1 :].strip() or model
    return manufacturer, model


def guess_type(props, has_pan, has_tilt, beam_count):
    family = props.get("SSLFIXTFAMILY", "0")
    ftype = props.get("SSLFIXTTYPE", "0")
    if family == "2":
        return "Laser"
    if has_pan and has_tilt:
        return "Moving Head"
    if ftype == "3":
        return "LED Bar (Pixels)" if beam_count > 8 else "LED Bar (Beams)"
    if ftype == "2":
        return "Color Changer"
    return "Other"


def build_channel(chan_el, prev_channels_by_beam):
    """Return (name, group, preset_or_none, capabilities) for one SSLCHANNEL."""
    ctype = chan_el.get("SSLCHANNELTYPE", "0")
    name = chan_el.get("SSLCHANNELNAME", "") or "Channel"
    # Daslight sometimes mangles fine-channel names with odd unicode bytes;
    # fall back to a generic name in that case.
    if not re.match(r"^[\x20-\x7eÀ-ÿ]+$", name):
        name = "Fine"
    msb = chan_el.get("SSLCHANNELMSB", "0") == "1"
    lsb = chan_el.get("SSLCHANNELLSB", "0") == "1"

    group = TYPE_GROUP.get(ctype, "Nothing")

    presets_el = chan_el.find("SSLPRESETS")
    presets = []
    if presets_el is not None:
        for p in presets_el.findall("SSLPRESET"):
            try:
                dstart = max(0, min(255, int(p.get("SSLPRESETDMXSTART", "0"))))
                dend = max(0, min(255, int(p.get("SSLPRESETDMXEND", "255"))))
            except ValueError:
                dstart, dend = 0, 255
            pname = p.get("SSLPRESETNAME", "").strip() or "Function"
            presets.append((dstart, dend, pname))
    presets.sort(key=lambda t: t[0])

    single_full_range = (
        len(presets) == 1 and presets[0][0] == 0 and presets[0][1] >= 254
    )

    preset = None
    if lsb and not msb:
        # fine channel: try to inherit a "...Fine" preset from context
        preset = None  # resolved by caller using the coarse sibling
    elif single_full_range or not presets:
        preset = simple_preset(ctype, name)

    if preset:
        return name, group, preset, []

    if not presets:
        # nothing to build a capability from - make a permissive full range
        presets = [(0, 255, name)]

    return name, group, None, presets


def unique_name(base, used):
    if base not in used:
        used.add(base)
        return base
    i = 2
    while f"{base} ({i})" in used:
        i += 1
    candidate = f"{base} ({i})"
    used.add(candidate)
    return candidate


def build_qxf(ssl_name, lib_el):
    props_el = lib_el.find("SSLPROPERTIES")
    props = dict(props_el.attrib) if props_el is not None else {}
    manufacturer, model = guess_manufacturer_model(ssl_name, props)

    beams_el = props_el.find("SSLBEAMS") if props_el is not None else None
    beam_count = int(beams_el.get("SSLNBBEAM", "1")) if beams_el is not None else 1

    modes_el = lib_el.find("SSLMODES")
    modes = modes_el.findall("SSLMODE") if modes_el is not None else []

    # channel_key -> (name, group, preset, capabilities)
    channels = {}
    used_names = set()
    mode_channel_refs = []  # list of (mode_name, [channel_name,...], beam_of_each_channel)

    has_pan_any = False
    has_tilt_any = False

    for mi, mode_el in enumerate(modes):
        mode_name = f"{mode_el.get('SSLNBCHANNEL', '?')} Channel"
        chan_els = mode_el.findall("SSLCHANNEL")
        refs = []
        beams_of = []
        last_by_type = {}
        used_in_this_mode = set()
        for ci, chan_el in enumerate(chan_els):
            ctype = chan_el.get("SSLCHANNELTYPE", "0")
            name, group, preset, caps = build_channel(chan_el, last_by_type)
            lsb = chan_el.get("SSLCHANNELLSB", "0") == "1"
            msb = chan_el.get("SSLCHANNELMSB", "0") == "1"

            if ctype == "1":
                has_pan_any = True
            if ctype == "2":
                has_tilt_any = True

            if lsb and not msb:
                base_preset = last_by_type.get(ctype)
                if base_preset and base_preset in FINE_SUFFIX_OK:
                    preset = base_preset + "Fine"
                    base_name = last_by_type.get(ctype + "_name", name)
                    name = base_name + " fine"

            if not (lsb and not msb):
                if preset:
                    last_by_type[ctype] = preset
                    last_by_type[ctype + "_name"] = name

            sslbeams = chan_el.find("SSLBEAMS")
            beam_idx = None
            if sslbeams is not None:
                first_beam = sslbeams.find("SSLBEAM")
                if first_beam is not None and first_beam.get("SSLBEAMINDEX") is not None:
                    beam_idx = int(first_beam.get("SSLBEAMINDEX"))

            content = (group, preset, tuple(caps))

            # Reuse a channel definition already registered under this exact
            # name (typical when several Modes share the same channel), but
            # only if it's not already used earlier in *this* Mode - QLC+
            # fixtures never reference the same channel name twice within a
            # single Mode, even when two DMX slots happen to be identical.
            if name in channels and channels[name] == content and name not in used_in_this_mode:
                cname = name
            else:
                cname = name
                if cname in used_in_this_mode or (cname in channels and channels[cname] != content):
                    cname = unique_name(name, used_names)
                elif cname not in used_names:
                    used_names.add(cname)
                channels[cname] = content

            used_in_this_mode.add(cname)
            refs.append(cname)
            beams_of.append(beam_idx)

        mode_channel_refs.append((mode_name, refs, beams_of))

    fixture_type = guess_type(props, has_pan_any, has_tilt_any, beam_count)

    # ---- build XML ----
    lines = []
    lines.append('<?xml version="1.0" encoding="UTF-8"?>')
    lines.append("<!DOCTYPE FixtureDefinition>")
    lines.append('<FixtureDefinition xmlns="http://www.qlcplus.org/FixtureDefinition">')
    lines.append(" <Creator>")
    lines.append("  <Name>daslight_to_qlcplus.py</Name>")
    lines.append("  <Version>1.0</Version>")
    lines.append("  <Author>Converted from Daslight 5</Author>")
    lines.append(" </Creator>")
    lines.append(f" <Manufacturer>{xml_escape(manufacturer)}</Manufacturer>")
    lines.append(f" <Model>{xml_escape(model)}</Model>")
    lines.append(f" <Type>{xml_escape(fixture_type)}</Type>")

    for cname, (group, preset, caps) in channels.items():
        if preset:
            lines.append(f' <Channel Name="{xml_escape(cname)}" Preset="{preset}"/>')
        else:
            lines.append(f' <Channel Name="{xml_escape(cname)}">')
            lines.append(f'  <Group Byte="0">{xml_escape(group)}</Group>')
            for (dstart, dend, pname) in caps:
                lines.append(
                    f'  <Capability Min="{dstart}" Max="{dend}">{xml_escape(pname)}</Capability>'
                )
            lines.append(" </Channel>")

    for mode_name, refs, beams_of in mode_channel_refs:
        lines.append(f' <Mode Name="{xml_escape(mode_name)}">')
        for i, cname in enumerate(refs):
            lines.append(f'  <Channel Number="{i}">{xml_escape(cname)}</Channel>')
        lines.append(" </Mode>")

    # ---- physical block (best effort, values are approximate) ----
    def fnum(key, default="0"):
        try:
            return float(props.get(key, default))
        except (TypeError, ValueError):
            return float(default)

    width_mm = int(fnum("SSLSIZEX") * 10)
    height_mm = int(fnum("SSLSIZEY") * 10)
    depth_mm = int(fnum("SSLSIZEZ") * 10)
    weight_kg = fnum("SSLWEIGHT")
    power_w = int(fnum("SSLLAMPPOWER"))
    lumens = int(fnum("SSLLAMPLUX", "-1"))
    if lumens < 0:
        lumens = 0
    colour_temp = int(fnum("SSLLAMPTEMP"))
    pan_max = int(fnum("SSLAMPLIPAN"))
    tilt_max = int(fnum("SSLAMPLITILT"))
    beam_open1 = fnum("SSLBEAMOPENING")
    beam_open2 = fnum("SSLBEAMOPENING2")
    degrees_min = int(min(beam_open1, beam_open2))
    degrees_max = int(max(beam_open1, beam_open2))
    focus_type = "Head" if (has_pan_any and has_tilt_any) else "Fixed"

    lines.append(" <Physical>")
    lines.append(f'  <Bulb Type="LED" Lumens="{lumens}" ColourTemperature="{colour_temp}"/>')
    lines.append(
        f'  <Dimensions Weight="{weight_kg:g}" Width="{width_mm}" Height="{height_mm}" Depth="{depth_mm}"/>'
    )
    lines.append(f'  <Lens Name="Other" DegreesMin="{degrees_min}" DegreesMax="{degrees_max}"/>')
    lines.append(f'  <Focus Type="{focus_type}" PanMax="{pan_max}" TiltMax="{tilt_max}"/>')
    lines.append(f'  <Technical PowerConsumption="{power_w}" DmxConnector="3-pin"/>')
    lines.append(" </Physical>")

    lines.append("</FixtureDefinition>")
    lines.append("")

    modes_info = [(m_name, len(refs)) for (m_name, refs, _beams) in mode_channel_refs]
    return manufacturer, model, "\n".join(lines), modes_info


def iter_patch_instances(blobs):
    """Yield (SSLLIBRARY element, FIXTURE instance element) for every patched
    fixture, in the show's original order (needed to assign stable QLC+
    fixture IDs)."""
    for blob in blobs:
        wrapped = "<root>" + blob + "</root>"
        try:
            root = ET.fromstring(wrapped)
        except ET.ParseError:
            cut = wrapped.rfind("</FIXTURES>")
            if cut < 0:
                continue
            wrapped = wrapped[: cut + len("</FIXTURES>")] + "</root>"
            try:
                root = ET.fromstring(wrapped)
            except ET.ParseError:
                continue
        for fx in root.iter("FIXTURES"):
            lib = fx.find("SSLLIBRARY")
            if lib is None:
                continue
            # a <FIXTURES> wrapper holds one shared SSLLIBRARY personality
            # followed by *all* patched instances of that personality
            for inst in fx.findall("FIXTURE"):
                yield lib, inst


def clean_instance_name(raw_name, model, index):
    name = re.sub(r"\.\d+$", "", raw_name or "").strip()
    if not name:
        name = model
    name = " ".join(w if w.isupper() else w.capitalize() for w in name.split())
    return f"{name} #{index}"


def decode_daslight_blob(b64data):
    """Base64+zlib decode one Daslight internal data blob. Daslight never
    writes the final zlib checksum trailer, so a plain zlib.decompress()
    always fails with 'incomplete or truncated stream' - decompressobj's
    flush() recovers the full, correct payload regardless (verified: this
    happens on literally every blob in the file, including ones that
    decompress into obviously complete, well-formed XML)."""
    raw = base64.b64decode(b64data)
    dobj = zlib.decompressobj()
    out = dobj.decompress(raw)
    out += dobj.flush()
    return out


def decode_simple_scene_values(raw, expected_channels):
    """Decode one <FIXTUREDATA DATA="..."/> payload into {channelIndex: dmxValue}.

    Reverse-engineered and verified against unambiguous cases (e.g. a scene
    named "All On" sets exactly the Dimmer channel to 255). Confirmed layout
    for *single-group* fixtures (anything that isn't a multi-beam/segmented
    LED bar - PARs, moving heads, lasers, wall washers, ...):

        byte[0:2]   header, byte[1] = N = number of real DMX channels
        byte[2:2+2N] N pairs (flag, value):
                       flag == 0x00 -> channel is explicitly set to `value`
                       flag == 0xff -> channel untouched by this scene/step
        byte[2+2N:]  additional Daslight-internal data (curve/animation
                     parameters) that has no equivalent in QLC+ and is
                     not needed for a flat scene value - ignored.

    Multi-beam/segmented fixtures (e.g. an 8-segment LED bar) use a longer,
    differently structured record that this function does NOT understand;
    it detects that case via the length/header mismatch and returns None
    rather than guessing.
    """
    if len(raw) < 2:
        return None
    n = raw[1]
    if n != expected_channels:
        return None
    if len(raw) < 2 + 2 * n:
        return None
    values = {}
    for i in range(n):
        flag = raw[2 + 2 * i]
        val = raw[3 + 2 * i]
        if flag == 0x00:
            values[i] = val
    return values


def build_scenes_and_chasers(scenes_root, dasuid_to_fixinfo, start_function_id=0):
    """Convert Daslight's plain "Steps" scenes (RACK TYPE="9") into QLC+
    Scene/Chaser Function XML. Every other rack type (built-in Daslight
    generator effects like random/movement/color-cycle, and "Super Scenes"
    which combine other scenes) is intentionally skipped - those have no
    static DMX snapshot to convert and would need Daslight's runtime effect
    engine to be reimplemented, which is out of scope here.
    """
    lines = []
    fid = start_function_id
    stats = {"scenes_ok": 0, "scenes_skipped_type": 0, "scenes_skipped_nodata": 0,
              "fixtures_skipped_multibeam": set()}

    for bank in scenes_root.findall("BANK"):
        for scene in bank.findall("SCENE"):
            racks = scene.find("RACKS")
            rack = racks.find("RACK") if racks is not None else None
            if rack is None or rack.get("TYPE") != "9":
                stats["scenes_skipped_type"] += 1
                continue
            steps_el = rack.find("STEPS")
            step_els = steps_el.findall("STEP") if steps_el is not None else []
            if not step_els:
                stats["scenes_skipped_type"] += 1
                continue

            scene_name = f"{bank.get('NAME', '')} - {scene.get('NAME', '')}".strip(" -")
            step_function_ids = []

            for si, step in enumerate(step_els):
                fds = step.find("FIXTUREDATAS")
                fixture_vals = {}  # fixture_id -> {channel: value}
                if fds is not None:
                    for fd in fds:
                        dasuid = fd.get("FIXTURE")
                        info = dasuid_to_fixinfo.get(dasuid)
                        if info is None:
                            continue
                        fixture_id, expected_channels = info
                        try:
                            raw = decode_daslight_blob(fd.get("DATA", ""))
                        except Exception:
                            continue
                        values = decode_simple_scene_values(raw, expected_channels)
                        if values is None:
                            stats["fixtures_skipped_multibeam"].add(dasuid)
                            continue
                        if values:
                            fixture_vals[fixture_id] = values

                if not fixture_vals:
                    continue

                step_name = scene_name if len(step_els) == 1 else f"{scene_name} - Step {si + 1}"
                lines.append(f'  <Function Type="Scene" Name="{xml_escape(step_name)}" ID="{fid}">')
                lines.append('   <Speed Duration="0" FadeOut="0" FadeIn="0"/>')
                for fixture_id in sorted(fixture_vals):
                    pairs = fixture_vals[fixture_id]
                    text = ",".join(f"{ch},{val}" for ch, val in sorted(pairs.items()))
                    lines.append(f'   <FixtureVal ID="{fixture_id}">{text}</FixtureVal>')
                lines.append('  </Function>')
                step_function_ids.append(fid)
                fid += 1

            if not step_function_ids:
                stats["scenes_skipped_nodata"] += 1
                continue

            if len(step_function_ids) > 1:
                # Daslight WAITTIME/FADETIME units aren't documented; assumed
                # to be ticks at a 25/s engine rate (WAITTIME=25 -> 1000ms),
                # which is by far the most common value in this show and
                # lines up with a plausible "one step per second" default.
                # Treat this as an approximation to check/adjust in QLC+.
                lines.append(f'  <Function Type="Chaser" Name="{xml_escape(scene_name)}" ID="{fid}">')
                lines.append('   <Speed Duration="0" FadeOut="0" FadeIn="0"/>')
                lines.append('   <Direction>Forward</Direction>')
                lines.append('   <RunOrder>Loop</RunOrder>')
                lines.append('   <SpeedModes Duration="PerStep" FadeOut="PerStep" FadeIn="PerStep"/>')
                for i, (step, child_fid) in enumerate(zip(step_els, step_function_ids)):
                    try:
                        wait_ms = int(step.get("WAITTIME", "25")) * 40
                    except ValueError:
                        wait_ms = 1000
                    try:
                        fade_ms = int(step.get("FADETIME", "0")) * 40
                    except ValueError:
                        fade_ms = 0
                    lines.append(
                        f'   <Step Duration="{wait_ms}" Number="{i}" FadeOut="0" FadeIn="{fade_ms}">{child_fid}</Step>'
                    )
                lines.append('  </Function>')
                fid += 1

            stats["scenes_ok"] += 1

    return lines, fid, stats


def build_workspace(fixture_root, blobs, fixture_meta):
    """Build a QLC+ .qxw workspace XML string containing the fixture patch
    (universe/address, taken verbatim from the show), fixture groups, and
    (best-effort, see build_scenes_and_chasers) Scene/Chaser functions
    converted from Daslight's plain "Steps" scenes.
    """
    lines = []
    lines.append('<!DOCTYPE Workspace>')
    lines.append('<Workspace CurrentWindow="FunctionManager">')
    lines.append(' <Creator>')
    lines.append('  <Name>daslight_to_qlcplus.py</Name>')
    lines.append('  <Version>1.0</Version>')
    lines.append('  <Author>Converted from Daslight 5</Author>')
    lines.append(' </Creator>')
    lines.append(' <Engine>')

    dasuid_to_id = {}
    dasuid_to_fixinfo = {}  # dasuid -> (fixture_id, expected_channel_count)
    per_model_counter = {}
    fixture_id = 0
    for lib_el, inst_el in iter_patch_instances(blobs):
        ssl_name = lib_el.get("SSLNAME", "")
        meta = fixture_meta.get(ssl_name)
        if meta is None:
            continue
        manufacturer, model, modes_info = meta
        mode_name, channels = modes_info[0] if modes_info else ("Unknown", 1)

        dasuid = inst_el.get("DASUID", "")
        try:
            address = max(0, int(inst_el.get("ADDRESS", "1")) - 1)
        except ValueError:
            address = 0
        try:
            universe = max(0, int(inst_el.get("UNIVERS", "1")) - 1)
        except ValueError:
            universe = 0

        per_model_counter[ssl_name] = per_model_counter.get(ssl_name, 0) + 1
        name = clean_instance_name(inst_el.get("NAME", ""), model, per_model_counter[ssl_name])

        lines.append('  <Fixture>')
        lines.append(f'   <Manufacturer>{xml_escape(manufacturer)}</Manufacturer>')
        lines.append(f'   <Model>{xml_escape(model)}</Model>')
        lines.append(f'   <Mode>{xml_escape(mode_name)}</Mode>')
        lines.append(f'   <ID>{fixture_id}</ID>')
        lines.append(f'   <Name>{xml_escape(name)}</Name>')
        lines.append(f'   <Universe>{universe}</Universe>')
        lines.append(f'   <Address>{address}</Address>')
        lines.append(f'   <Channels>{channels}</Channels>')
        lines.append('  </Fixture>')

        dasuid_to_id[dasuid] = fixture_id
        dasuid_to_fixinfo[dasuid] = (fixture_id, channels)
        fixture_id += 1

    # Fixture groups (plaintext in the outer .dvc XML, not part of the
    # compressed PATCHS blob - reliable to carry over as-is).
    group_id = 0
    fgs = fixture_root.find("FIXTUREGROUPS")
    if fgs is not None:
        for fg in fgs.findall("FIXTUREGROUP"):
            fixtures_el = fg.find("FIXTURES")
            member_uids = [f.get("DASUID") for f in fixtures_el.findall("FIXTURE")] if fixtures_el is not None else []
            member_ids = [dasuid_to_id[u] for u in member_uids if u in dasuid_to_id]
            if not member_ids:
                continue
            lines.append(f'  <FixtureGroup ID="{group_id}">')
            lines.append(f'   <Name>{xml_escape(fg.get("NAME", "Group"))}</Name>')
            lines.append(f'   <Size X="{len(member_ids)}" Y="1"/>')
            for i, fid in enumerate(member_ids):
                lines.append(f'   <Head X="{i}" Y="0" Fixture="{fid}">0</Head>')
            lines.append('  </FixtureGroup>')
            group_id += 1

    scenes_root = fixture_root.find("SCENES")
    scene_stats = {"scenes_ok": 0, "scenes_skipped_type": 0, "scenes_skipped_nodata": 0,
                   "fixtures_skipped_multibeam": set()}
    if scenes_root is not None:
        scene_lines, _next_fid, scene_stats = build_scenes_and_chasers(scenes_root, dasuid_to_fixinfo)
        lines.extend(scene_lines)

    lines.append(' </Engine>')
    lines.append('</Workspace>')
    lines.append('')
    return "\n".join(lines), fixture_id, group_id, scene_stats


def main():
    ap = argparse.ArgumentParser(description="Convert Daslight 5 fixtures/patch (from a .dvc show file) to QLC+ .qxf/.qxw")
    ap.add_argument("dvc_file", help="Path to the Daslight 5 .dvc save file")
    ap.add_argument("-o", "--output", default="qlcplus_fixtures", help="Output directory (default: qlcplus_fixtures)")
    ap.add_argument("--no-workspace", action="store_true", help="Only write .qxf fixture files, skip the .qxw workspace/patch")
    args = ap.parse_args()

    with open(args.dvc_file, "r", encoding="utf-8", errors="replace") as f:
        text = f.read()
    fixture_root = ET.fromstring(text)

    blobs = find_patch_blobs(text)
    if not blobs:
        print("Keine <PATCHS DATA=...> Bloecke gefunden - ist das eine gueltige Daslight 5 .dvc Datei?", file=sys.stderr)
        sys.exit(1)

    fixtures = parse_fixtures(blobs)
    if not fixtures:
        print("Keine Fixture-Definitionen im Patch gefunden.", file=sys.stderr)
        sys.exit(1)

    os.makedirs(args.output, exist_ok=True)
    print(f"Gefundene Fixtures: {len(fixtures)}\n")

    fixture_meta = {}  # ssl_name -> (manufacturer, model, modes_info)
    for ssl_name, lib_el in fixtures.items():
        manufacturer, model, qxf, modes_info = build_qxf(ssl_name, lib_el)
        fixture_meta[ssl_name] = (manufacturer, model, modes_info)
        man_dir = os.path.join(args.output, sanitize_filename(manufacturer))
        os.makedirs(man_dir, exist_ok=True)
        fname = f"{sanitize_filename(manufacturer)}-{sanitize_filename(model)}.qxf"
        out_path = os.path.join(man_dir, fname)
        with open(out_path, "w", encoding="utf-8") as f:
            f.write(qxf)
        print(f"  {ssl_name}\n    -> {out_path}")

    print(f"\nFertig. {len(fixtures)} .qxf-Dateien in '{args.output}' geschrieben.")

    if not args.no_workspace:
        qxw, nfixtures, ngroups, scene_stats = build_workspace(fixture_root, blobs, fixture_meta)
        qxw_path = os.path.join(args.output, "daslight_patch.qxw")
        with open(qxw_path, "w", encoding="utf-8") as f:
            f.write(qxw)
        print(f"Workspace geschrieben: {qxw_path}")
        print(f"  {nfixtures} Fixtures, {ngroups} Fixture-Gruppen")
        print(f"  {scene_stats['scenes_ok']} Szenen/Chaser konvertiert (nur RACK TYPE=9 'Steps'-Szenen)")
        print(f"  {scene_stats['scenes_skipped_type']} Szenen uebersprungen (Daslight-Effekt-Generator oder Super Scene, kein statischer Wertesatz)")
        if scene_stats['scenes_skipped_nodata']:
            print(f"  {scene_stats['scenes_skipped_nodata']} Szenen uebersprungen (keine dekodierbaren Fixture-Werte gefunden)")
        if scene_stats['fixtures_skipped_multibeam']:
            print(f"  {len(scene_stats['fixtures_skipped_multibeam'])} mehrsegmentige Fixture-Instanzen (z.B. LED-Baelken) in Szenen NICHT konvertiert -")
            print("    ihr internes Datenformat unterscheidet sich und wurde nicht sicher genug entschluesselt.")
        print("  Bitte JEDE konvertierte Szene in QLC+ stichprobenartig gegen Daslight pruefen (siehe README.md).")

    print("Bitte in QLC+ importieren und pruefen (siehe Kommentar am Dateianfang dieses Skripts).")


if __name__ == "__main__":
    main()
