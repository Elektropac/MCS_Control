#!/usr/bin/env python3
"""Ret 3D-visningen af MCS Control BottomBoard efter EasyEDA-importen.

Kør med KiCads egen Python fra repo-roden, med PCB'et LUKKET i KiCad:
  $KPY hardware/tools/fix_3d_models.py [projektmappe]

Problemer fra importen:
  - EasyEDA placerer en model efter dens midtpunkt; KiCad efter modellens nulpunkt.
    Modeller med nulpunkt ved pin 1 (relæer, klemrækker, headere) stod derfor forskudt,
    og z-offset sænkede dem ned i printet.
  - DC/DC (SIP-4 B2405S) pegede på en kopi af pin-header-modellen.
  - 18 PhotoMOS (SMD-8) havde fået model i biblioteket, men ikke på printet.

Justering: modellens udstrækning (fra STEP-filens hjørnepunkter) drejes 0/90/180/270 grader
og flyttes, så den dækker alle pads og passer bedst muligt med footprintets courtyard.
Ændrer kun 3D-model-felter - ingen pads, spor eller net.
"""
import collections
import math
import re
import sys
from pathlib import Path

import pcbnew

KICAD = Path(sys.argv[1]) if len(sys.argv) > 1 else Path(__file__).resolve().parent.parent / "kicad"
NAME = "MCS-Control_6fa4f290d14a4191b5bd4ad91b5ee33a"
PCB = KICAD / f"{NAME}.kicad_pcb"
KICAD_3D = Path("/Applications/KiCad/KiCad.app/Contents/SharedSupport/3dmodels")

PHOTOMOS_FP = "SMD-8_L9.8-W6.4-APW212G2EH"
PHOTOMOS_MODEL = "${KIPRJMOD}/EASYEDA_MODELS/SMD-8_L9.8-W6.4_APW212G2EH.step"
DCDC_FP = "SIP-4_L11.5-W6.0-P2.54-L_ROE-XXXXS"
DCDC_MODEL = "${KICAD10_3DMODEL_DIR}/Converter_DCDC.3dshapes/Converter_DCDC_Murata_CRE1xxxxxxSC_THT.step"
# EasyEDA-modellerne er forkerte her (relæ ufuldstændig ~10x20 mm, CN1/CN2 viser flerpolet klemme)
# og KiCad har ingen tilsvarende -> forenklet kasse i courtyard-mål. Højde = skøn.
BOX_FP = {"RELAY-TH_TE_T9GV1L14-9": ("relay_T9G_box", 20.0, (0.95, 0.95, 0.92)),
          "CONN-TH_2P-P7.62_1728853": ("terminal_2p_7.62_box", 12.0, (0.2, 0.6, 0.3))}
# THT-dele med EasyEDA-offsets der skal genberegnes
REALIGN_FP = ("CONN-TH_24P-P3.50_KEFA_KF2EDGSR-3.5-2X", "HDR-M-2.54_1X4", DCDC_FP)

if list(KICAD.glob(f"~{NAME}.kicad_pcb.lck")):
    raise SystemExit("PCB'et er åbent i KiCad - luk det først.")


def step_path(fn):
    return Path(fn.replace("${KIPRJMOD}", str(KICAD)).replace("${KICAD10_3DMODEL_DIR}", str(KICAD_3D)))


_box_cache = {}


def step_box(fn):
    """XY-udstrækning af STEP-modellens hjørnepunkter (VERTEX_POINT) i modellens koordinater (y op)."""
    if fn not in _box_cache:
        t = step_path(fn).read_text(errors="ignore")
        ents = dict(re.findall(r"#(\d+)\s*=\s*(.*?);\s*(?=#|ENDSEC)", t, re.S))
        pts = {}
        for k, v in ents.items():
            m = re.match(r"CARTESIAN_POINT\(\s*'[^']*'\s*,\s*\(\s*([-\d.Ee+]+)\s*,\s*([-\d.Ee+]+)\s*,", v)
            if m:
                pts[k] = (float(m.group(1)), float(m.group(2)))
        vp = [pts[m.group(1)] for v in ents.values()
              for m in [re.match(r"VERTEX_POINT\(\s*'[^']*'\s*,\s*#(\d+)", v)] if m and m.group(1) in pts]
        xs, ys = zip(*vp)
        _box_cache[fn] = (min(xs), min(ys), max(xs), max(ys))
    return _box_cache[fn]


def to_local(fp, x, y):
    """Board-mm -> footprint-lokale koordinater i model-retning (y op)."""
    px, py = pcbnew.ToMM(fp.GetPosition().x), pcbnew.ToMM(fp.GetPosition().y)
    a = math.radians(fp.GetOrientationDegrees())
    dx, dy = x - px, y - py
    # KiCad-orientering er mod uret set ovenfra; skærmens y peger ned
    lx = dx * math.cos(a) - dy * math.sin(a)
    ly = dx * math.sin(a) + dy * math.cos(a)
    return lx, -ly


def rot_box(box, deg):
    x1, y1, x2, y2 = box
    a = math.radians(deg)
    pts = [(x * math.cos(a) - y * math.sin(a), x * math.sin(a) + y * math.cos(a))
           for x, y in ((x1, y1), (x1, y2), (x2, y1), (x2, y2))]
    xs, ys = zip(*pts)
    return min(xs), min(ys), max(xs), max(ys)


def iou(a, b):
    ix = max(0, min(a[2], b[2]) - max(a[0], b[0]))
    iy = max(0, min(a[3], b[3]) - max(a[1], b[1]))
    inter = ix * iy
    area = lambda r: (r[2] - r[0]) * (r[3] - r[1])
    return inter / (area(a) + area(b) - inter)


def local_pads(fp):
    return [to_local(fp, pcbnew.ToMM(p.GetPosition().x), pcbnew.ToMM(p.GetPosition().y)) for p in fp.Pads()]


def local_courtyard(fp):
    pts = []
    for g in fp.GraphicalItems():
        if g.GetLayer() in (pcbnew.F_CrtYd, pcbnew.B_CrtYd):
            bb = g.GetBoundingBox()
            for x, y in ((bb.GetLeft(), bb.GetTop()), (bb.GetRight(), bb.GetBottom())):
                pts.append(to_local(fp, pcbnew.ToMM(x), pcbnew.ToMM(y)))
    xs, ys = zip(*(pts or local_pads(fp)))
    return min(xs), min(ys), max(xs), max(ys)


def local_silk_outline(fp):
    """Kroppens omrids fra silketrykkets streger (EasyEDA tegner huset her); ellers courtyard."""
    pts = []
    for g in fp.GraphicalItems():
        if g.GetClass() == "PCB_SHAPE" and g.GetLayer() in (pcbnew.F_SilkS, pcbnew.B_SilkS):
            bb = g.GetBoundingBox()
            for x, y in ((bb.GetLeft(), bb.GetTop()), (bb.GetRight(), bb.GetBottom())):
                pts.append(to_local(fp, pcbnew.ToMM(x), pcbnew.ToMM(y)))
    if not pts:
        return local_courtyard(fp)
    xs, ys = zip(*pts)
    return min(xs), min(ys), max(xs), max(ys)


def write_box(name, box, h, rgb):
    """VRML-kasse i mm (footprintet sætter scale 1/2,54), centreret om origo."""
    (OUT_3D := KICAD / "3d").mkdir(exist_ok=True)
    sx, sy = box[2] - box[0], box[3] - box[1]
    r, g, b = rgb
    (OUT_3D / f"{name}.wrl").write_text(
        "#VRML V2.0 utf8\n# Forenklet kasse (genereret af tools/fix_3d_models.py)\n"
        f"Transform {{ translation 0 0 {h / 2:.3f} children [ Shape {{ appearance Appearance {{ material "
        f"Material {{ diffuseColor {r} {g} {b} }} }} geometry Box {{ size {sx:.3f} {sy:.3f} {h:.3f} }} }} ] }}\n")
    return f"${{KIPRJMOD}}/3d/{name}.wrl"


def align(fp, fn):
    """Find drejning + offset så modellen dækker pads og bedst matcher courtyard."""
    pads = local_pads(fp)
    court = local_courtyard(fp)
    ccx, ccy = (court[0] + court[2]) / 2, (court[1] + court[3]) / 2
    best = None
    for deg in (0, 90, 180, 270):
        rb = rot_box(step_box(fn), deg)
        ox = ccx - (rb[0] + rb[2]) / 2
        oy = ccy - (rb[1] + rb[3]) / 2
        placed = (rb[0] + ox, rb[1] + oy, rb[2] + ox, rb[3] + oy)
        covered = all(placed[0] - 0.3 <= x <= placed[2] + 0.3 and placed[1] - 0.3 <= y <= placed[3] + 0.3
                      for x, y in pads)
        score = iou(placed, court) + (1 if covered else 0)
        if best is None or score > best[0]:
            best = (score, deg, ox, oy, covered)
    return best


board = pcbnew.LoadBoard(str(PCB))
added = swapped = 0
report = []
for fp in board.GetFootprints():
    name = fp.GetFPID().GetLibItemName().wx_str()
    models = fp.Models()
    if name == PHOTOMOS_FP and models.size() == 0:
        m = pcbnew.FP_3DMODEL()
        m.m_Filename = PHOTOMOS_MODEL
        models.push_back(m)
        added += 1
        continue
    if name in BOX_FP and models.size():
        box_name, h, rgb = BOX_FP[name]
        court = local_silk_outline(fp)
        m = models[0]
        m.m_Filename = write_box(box_name, court, h, rgb)
        m.m_Scale = pcbnew.VECTOR3D(1 / 2.54, 1 / 2.54, 1 / 2.54)
        m.m_Rotation = pcbnew.VECTOR3D(0, 0, 0)
        m.m_Offset = pcbnew.VECTOR3D(round((court[0] + court[2]) / 2, 3), round((court[1] + court[3]) / 2, 3), 0)
        report.append(f"  {fp.GetReference():4} {name[:34]:34} forenklet kasse {court[2] - court[0]:.1f}x{court[3] - court[1]:.1f}x{h:.0f} mm")
        continue
    if not any(name.startswith(p) for p in REALIGN_FP) or models.size() == 0:
        continue
    m = models[0]
    if name == DCDC_FP:
        m.m_Filename = DCDC_MODEL
        swapped += 1
    if fp.GetLayer() == pcbnew.B_Cu:
        report.append(f"  {fp.GetReference()}: bundside - ikke justeret")
        continue
    score, deg, ox, oy, covered = align(fp, m.m_Filename)
    # KiCads model-rotation regnes med uret set ovenfra (modsat matematisk positiv retning)
    m.m_Rotation = pcbnew.VECTOR3D(0, 0, (360 - deg) % 360)
    m.m_Offset = pcbnew.VECTOR3D(round(ox, 3), round(oy, 3), 0)
    report.append(f"  {fp.GetReference():4} {name[:34]:34} rot {deg:3}  offset ({ox:6.2f},{oy:6.2f})"
                  f"  {'dækker pads' if covered else 'DÆKKER IKKE ALLE PADS'}")

# --- SMD: importen har drejet mange modeller 90/180 grader forkert (fx alle R0603). Find den drejning
# (og evt. centrering) hvor modellens benfødder - de nederste hjørnepunkter i STEP-filen - lander på pads.
_feet_cache = {}


def model_feet(fn):
    if fn not in _feet_cache:
        t = step_path(fn).read_text(errors="ignore")
        ents = dict(re.findall(r"#(\d+)\s*=\s*(.*?);\s*(?=#|ENDSEC)", t, re.S))
        pts = {k: tuple(map(float, m.groups())) for k, v in ents.items()
               for m in [re.match(r"CARTESIAN_POINT\(\s*'[^']*'\s*,\s*\(\s*([-\d.Ee+]+)\s*,\s*([-\d.Ee+]+)\s*,\s*([-\d.Ee+]+)", v)] if m}
        vp = [pts[m.group(1)] for v in ents.values()
              for m in [re.match(r"VERTEX_POINT\(\s*'[^']*'\s*,\s*#(\d+)", v)] if m and m.group(1) in pts]
        zmin = min(p[2] for p in vp)
        _feet_cache[fn] = [(x, y) for x, y, z in vp if z <= zmin + 0.15]
    return _feet_cache[fn]


def local_pad_boxes(fp):
    mirror = fp.GetLayer() == pcbnew.B_Cu      # bundside: footprintet er spejlet
    boxes = []
    for p in fp.Pads():
        bb = p.GetBoundingBox()
        (x1, y1), (x2, y2) = (to_local(fp, pcbnew.ToMM(bb.GetLeft()), pcbnew.ToMM(bb.GetTop())),
                              to_local(fp, pcbnew.ToMM(bb.GetRight()), pcbnew.ToMM(bb.GetBottom())))
        if mirror:
            x1, x2 = -x1, -x2
        boxes.append((min(x1, x2), min(y1, y2), max(x1, x2), max(y1, y2)))
    return boxes


def feet_score(feet, kicad_rot, ox, oy, pads):
    r = -math.radians(kicad_rot)
    hit = 0
    for x, y in feet:
        X, Y = x * math.cos(r) - y * math.sin(r) + ox, x * math.sin(r) + y * math.cos(r) + oy
        hit += any(a - 0.2 <= X <= c + 0.2 and b - 0.2 <= Y <= d + 0.2 for a, b, c, d in pads)
    return hit / max(1, len(feet))


smd_fixed = collections.Counter()
for fp in board.GetFootprints():
    models = fp.Models()
    if models.size() == 0 or models[0].m_Filename.endswith(".wrl") or not fp.Pads() \
            or any(p.GetAttribute() != pcbnew.PAD_ATTRIB_SMD for p in fp.Pads()):
        continue
    m = models[0]
    feet = model_feet(m.m_Filename)
    pads = local_pad_boxes(fp)
    px = ((min(p[0] for p in pads) + max(p[2] for p in pads)) / 2, (min(p[1] for p in pads) + max(p[3] for p in pads)) / 2)
    orig = round(m.m_Rotation.z) % 360
    cand = []
    for kr in (orig, (orig + 90) % 360, (orig + 180) % 360, (orig + 270) % 360):
        r = -math.radians(kr)
        rf = [(x * math.cos(r) - y * math.sin(r), x * math.sin(r) + y * math.cos(r)) for x, y in feet]
        fx = ((min(p[0] for p in rf) + max(p[0] for p in rf)) / 2, (min(p[1] for p in rf) + max(p[1] for p in rf)) / 2)
        for ox, oy, centered in ((m.m_Offset.x, m.m_Offset.y, False), (px[0] - fx[0], px[1] - fx[1], True)):
            # nuværende løsning foretrækkes ved (næsten) lige score
            bonus = 0.02 if (kr == orig and not centered) else 0
            cand.append((feet_score(feet, kr, ox, oy, pads) + bonus, kr, ox, oy, centered))
    best = max(cand)
    if best[1] != orig or best[4]:
        m.m_Rotation = pcbnew.VECTOR3D(0, 0, best[1])
        m.m_Offset = pcbnew.VECTOR3D(round(best[2], 3), round(best[3], 3), 0)
        smd_fixed[fp.GetFPID().GetLibItemName().wx_str()[:30] + f" +{(best[1] - orig) % 360}"
                  + (" centreret" if best[4] else "")] += 1

# --- Manglende modeller
# U24 = Waveshare ESP32-S3-ETH (samme montering som pumpesimulatoren: ben loddet, 2,5 mm afstand,
# RJ45-siden op). Modellen (control-emulator/hardware/tools/gen_ws_model.py) har origo i USB-hjørnet
# på H2-siden; her vender USB-enden ned, så den drejes 180 grader og pin 1 lægges på pad 1.
WS_PIN1 = (19.87, 1.61)    # pin 1 i modellen: afstand fra USB-enden, afstand fra H2-kanten (mm)
for fp in board.GetFootprints():
    if fp.GetValue() == "ESP32-S3 ETH" and fp.Models().size() == 0:
        p1 = next(p for p in fp.Pads() if p.GetNumber() == "1")
        lx, ly = to_local(fp, pcbnew.ToMM(p1.GetPosition().x), pcbnew.ToMM(p1.GetPosition().y))
        m = pcbnew.FP_3DMODEL()
        m.m_Filename = "${KIPRJMOD}/3d/waveshare_esp32s3eth.wrl"
        m.m_Scale = pcbnew.VECTOR3D(1 / 2.54, 1 / 2.54, 1 / 2.54)
        m.m_Rotation = pcbnew.VECTOR3D(0, 0, 180)
        # efter 180 grader ligger modellens pin 1 i (-1,61, +19,87)
        m.m_Offset = pcbnew.VECTOR3D(round(lx + WS_PIN1[1], 3), round(ly - WS_PIN1[0], 3), 0)
        fp.Models().push_back(m)
        report.append(f"  {fp.GetReference():4} Waveshare-model tilføjet")
    if fp.GetValue() == "CPT-1667-85T" and fp.Models().size() == 0:
        # piezo-buzzer: forenklet cylinder i silketryk-cirklens størrelse, højde = skøn
        sx1, sy1, sx2, sy2 = local_silk_outline(fp)
        d, h = max(sx2 - sx1, sy2 - sy1), 8.0
        n = 32
        ring = [(d / 2 * math.cos(2 * math.pi * i / n), d / 2 * math.sin(2 * math.pi * i / n)) for i in range(n)]
        coords = ", ".join(f"{x:.3f} {y:.3f} {z}" for z in (0, h) for x, y in ring)
        faces = [f"{i}, {(i + 1) % n}, {n + (i + 1) % n}, {n + i}, -1" for i in range(n)]
        faces += [", ".join(str(n + i) for i in range(n)) + ", -1", ", ".join(str(i) for i in reversed(range(n))) + ", -1"]
        (KICAD / "3d").mkdir(exist_ok=True)
        (KICAD / "3d" / "buzzer_cpt1667_cyl.wrl").write_text(
            "#VRML V2.0 utf8\n# Forenklet buzzer-cylinder (genereret af tools/fix_3d_models.py)\n"
            "Shape { appearance Appearance { material Material { diffuseColor 0.1 0.1 0.1 } }\n"
            f"  geometry IndexedFaceSet {{ solid FALSE coord Coordinate {{ point [ {coords} ] }}\n"
            f"    coordIndex [ {' '.join(faces)} ] }} }}\n")
        m = pcbnew.FP_3DMODEL()
        m.m_Filename = "${KIPRJMOD}/3d/buzzer_cpt1667_cyl.wrl"
        m.m_Scale = pcbnew.VECTOR3D(1 / 2.54, 1 / 2.54, 1 / 2.54)
        m.m_Offset = pcbnew.VECTOR3D(round((sx1 + sx2) / 2, 3), round((sy1 + sy2) / 2, 3), 0)
        fp.Models().push_back(m)
        report.append(f"  {fp.GetReference():4} buzzer-cylinder Ø{d:.1f} x {h:.0f} mm tilføjet")

pcbnew.SaveBoard(str(PCB), board)
print(f"PhotoMOS-model tilføjet: {added}, DC/DC-model skiftet: {swapped}")
print("SMD-modeller rettet:", dict(smd_fixed) or "ingen")
print("\n".join(report))
