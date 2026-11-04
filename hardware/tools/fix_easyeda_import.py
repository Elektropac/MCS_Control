#!/usr/bin/env python3
"""Ryd op i importrester fra EasyEDA -> KiCad på MCS Control BottomBoard.

Kør med KiCads egen Python fra repo-roden, med PCB'et LUKKET i KiCad:
  KPY=/Applications/KiCad/KiCad.app/Contents/Frameworks/Python.framework/Versions/Current/bin/python3
  $KPY hardware/tools/fix_easyeda_import.py

Retter (ændrer ingen forbindelser):
  1. Design-regler i .kicad_pro: clearance 0,15 mm (layoutet er lavet med EasyEDA's 6 mil),
     kobber-til-kant 0,3 mm (typisk producent-minimum).
  2. Bundkomponenter havde courtyard på F.Courtyard -> flyttes til B.Courtyard.
  3. Pads med pasta-margin -100 mm (ingen stencil-åbning) -> margin nulstilles.
     Ramte kun passive komponenter (R/C/SD0805) - importfejl, ikke et bevidst valg.
  4. Spor med længde 0.
Verificerer til sidst med KiCads DRC at der ingen uforbundne items er.
"""
import json
import re
import subprocess
import sys
import tempfile
from pathlib import Path

import pcbnew

# valgfrit argument: anden projektmappe (fx en testkopi)
KICAD = Path(sys.argv[1]) if len(sys.argv) > 1 else Path(__file__).resolve().parent.parent / "kicad"
NAME = "MCS-Control_6fa4f290d14a4191b5bd4ad91b5ee33a"
PCB, PRO = KICAD / f"{NAME}.kicad_pcb", KICAD / f"{NAME}.kicad_pro"
CLI = "/Applications/KiCad/KiCad.app/Contents/MacOS/kicad-cli"
CLEARANCE, EDGE = 0.15, 0.3

if list(KICAD.glob(f"~{NAME}.kicad_pcb.lck")):
    raise SystemExit("PCB'et er åbent i KiCad - luk det først.")


# 1) design-regler
pro = json.loads(PRO.read_text())
for c in pro["net_settings"]["classes"]:
    if c["name"] == "Default":
        c["clearance"] = CLEARANCE
pro["board"]["design_settings"]["rules"]["min_copper_edge_clearance"] = EDGE
PRO.write_text(json.dumps(pro, indent=2) + "\n")
print(f"regler: clearance {CLEARANCE} mm, kobber-til-kant {EDGE} mm")

board = pcbnew.LoadBoard(str(PCB))

# 2) courtyard på bundkomponenter
moved = 0
for f in board.GetFootprints():
    if f.GetLayer() != pcbnew.B_Cu:
        continue
    for g in f.GraphicalItems():
        if g.GetLayer() == pcbnew.F_CrtYd:
            g.SetLayer(pcbnew.B_CrtYd)
            moved += 1
print(f"courtyard flyttet til bundsiden: {moved}")

# 3) pasta-margin
paste = 0
for f in board.GetFootprints():
    for p in f.Pads():
        m = p.GetLocalSolderPasteMargin()
        if m is not None and m < -pcbnew.FromMM(1):
            p.SetLocalSolderPasteMargin(0)
            paste += 1
print(f"pads med pasta-margin nulstillet: {paste}")

# 4a) nul-længde-spor
zero = [t for t in board.GetTracks() if t.GetClass() == "PCB_TRACK" and t.GetLength() == 0]
for t in zero:
    board.Remove(t)
print(f"nul-længde-spor fjernet: {len(zero)}")
pcbnew.SaveBoard(str(PCB), board)

# Løse spor-ender (track_dangling) fjernes IKKE automatisk: flere spor har samme net/lag/længde,
# så en automatisk match kan ramme det forkerte og bryde en forbindelse. Tages manuelt.

# verificér med KiCads DRC: ingen uforbundne items
with tempfile.NamedTemporaryFile(suffix=".rpt") as rpt:
    subprocess.run([CLI, "pcb", "drc", "--severity-all", "-o", rpt.name, str(PCB)], capture_output=True, check=True)
    txt = Path(rpt.name).read_text()
m = re.search(r"Found (\d+) unconnected", txt) or re.search(r"\*\* Found (\d+) unconnected pads", txt)
unc = len(re.findall(r"^\[unconnected_items\]", txt, re.M))
print("forbindelser OK (0 uforbundne)" if unc == 0 else f"ADVARSEL: {unc} uforbundne items efter oprydning")
