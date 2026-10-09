#!/usr/bin/env python3
"""Convert the old Grid Planner .stilib library into the SmartHomePlanner library format.

Usage: convert_stilib.py <old.stilib> <new.xml>
Adds per element: section, type (light/speaker/strip/generic), default note, beam/radius.
"""
import sys
import xml.etree.ElementTree as ET

SECTIONS = [
    ("lighting",   "Lighting",          "L",  "#F5A623"),
    ("switches",   "Switches",          "S",  "#4A90E2"),
    ("sensors",    "Sensors",           "SN", "#2EAD4B"),
    ("security",   "Security",          "A",  "#D0021B"),
    ("shading",    "Shading",           "J",  "#8B572A"),
    ("hvac",       "HVAC",              "H",  "#E94E77"),
    ("music",      "Music",             "M",  "#9013FE"),
    ("sockets",    "Sockets",           "P",  "#417505"),
    ("network",    "Network",           "N",  "#1BA39C"),
    ("electrical", "Electrical panel",  "E",  "#4A4A4A"),
    ("other",      "Other",             "X",  "#7B7B7B"),
]

SECTION_RO = {'lighting': 'Iluminat', 'switches': 'Întrerupătoare', 'sensors': 'Senzori', 'security': 'Securitate', 'shading': 'Umbrire', 'hvac': 'Climatizare (HVAC)', 'music': 'Muzică', 'sockets': 'Prize', 'network': 'Rețea', 'electrical': 'Tablou electric', 'other': 'Altele'}

CATEGORY_SECTION = {
    "Lighting": "lighting", "Pushbutton": "switches", "Switch": "switches",
    "Security": "security", "Shading": "shading", "HVAC": "hvac", "Music": "music",
    "Socket": "sockets", "IT": "network", "ETC": "other",
    "MCBs": "electrical", "DIFs": "electrical", "Fasteners": "electrical",
}

def has(name, *words):
    n = name.lower()
    return any(w.lower() in n for w in words)

def section_for(cat_path, name):
    if has(name, "senzor", "statie meteo"):
        return "sensors"
    if has(name, "priza"):
        return "sockets"
    if has(name, "tablou electric", "cabinet rack"):
        return "electrical"
    for c in reversed(cat_path):
        if c in CATEGORY_SECTION:
            return CATEGORY_SECTION[c]
    return "other"

def type_for(section, name):
    if section == "lighting":
        if has(name, "banda led", "stripe", "strip"):
            return "strip"
        return "light"
    if section == "music" and has(name, "difuzor", "speaker"):
        return "speaker"
    return "generic"

def beam_for(name):
    if has(name, "spot", "downlight"):
        return 36
    if has(name, "pendulum"):
        return 60
    if has(name, "aplica", "lampa de perete"):
        return 90
    return 120

def note_for(section, typ, name, conventional):
    if has(name, " tree", "tree "):
        return "Tree"
    if has(name, " air", "air "):
        return "Air"
    if typ == "strip":
        if has(name, "rgbw"):
            return "5x0.75 (24V)"
        if has(name, "rgb"):
            return "4x0.75 (24V)"
        return "2x0.75 (24V)"
    if section == "lighting":
        if has(name, "dual tone"):
            return "3x0.75 (24V)"
        if has(name, "pwm", "24v"):
            return "2x0.75 (24V)"
        if has(name, "0-10v"):
            return "3x1.5 + 2x0.75"
        if has(name, "dali"):
            return "5x1.5"
        if has(name, "dmx"):
            return "2x0.5 DMX"
        return "3x1.5"
    if section == "switches":
        if has(name, "nano"):
            return "Tree"
        return "3x1.5" if conventional else "Cat5"
    if section == "sockets":
        if has(name, "aragaz trifazat"):
            return "5x4"
        if has(name, "aragaz"):
            return "3x6"
        if has(name, "masina", "uscator", "cuptor"):
            return "3x2.5 (16A)"
        return "3x2.5"
    if section == "network":
        return "2x Cat6" if has(name, "dublu") else "Cat6"
    if section == "music":
        return "2x1.5 LS" if typ == "speaker" else "Cat5"
    if section == "shading":
        return "4x1.5"
    if section == "hvac":
        if has(name, "1-wire"):
            return "Cat5 (1-Wire)"
        if conventional:
            return "3x1.5"
        return "3x1.5"
    if section == "sensors":
        if has(name, "1-wire"):
            return "Cat5 (1-Wire)"
        return "Cat5"
    if has(name, "rs485", "modbus", "m-bus"):
        return "Cat5 (RS485)"
    if has(name, "trifaz", "3-faze"):
        return "5x2.5"
    if has(name, "consumator", "cazan", "iesire prin cablu"):
        return "3x2.5"
    return ""

def convert(src, dst):
    old = ET.parse(src).getroot()
    lib = ET.Element("Library", name=old.get("name", "Library"), icon=old.get("icon", ""), version="2")
    secs = ET.SubElement(lib, "Sections")
    for sid, sname, prefix, color in SECTIONS:
        ET.SubElement(secs, "Section", id=sid, name=sname, name_ro=SECTION_RO[sid], prefix=prefix, color=color)

    def walk(old_el, new_parent, cat_path):
        for child in old_el:
            if child.tag == "Category":
                path = cat_path + [child.get("name")]
                attrs = {"name": child.get("name"), "icon": child.get("icon", "")}
                if child.get("name") in CATEGORY_SECTION:
                    attrs["section"] = CATEGORY_SECTION[child.get("name")]
                cat = ET.SubElement(new_parent, "Category", attrs)
                walk(child, cat, path)
            elif child.tag == "Item":
                name = child.get("name")
                conventional = "Conventional" in cat_path
                sec = section_for(cat_path, name)
                typ = type_for(sec, name)
                attrs = {
                    "name": name, "icon": child.get("icon", ""),
                    "section": sec, "type": typ,
                    "size": child.get("width", "30"),
                    "note": note_for(sec, typ, name, conventional),
                    "doc": "", "shop": "",
                }
                if typ == "light":
                    attrs["beam"] = str(beam_for(name))
                    attrs["cct"] = "3000"
                if typ == "speaker":
                    attrs["radius"] = "300"
                if typ == "strip":
                    attrs["color"] = "#FFD27F" if has(name, "ww") else ("#FF4FD8" if has(name, "rgb") else "#FFF4E0")
                    attrs["glow"] = "35"
                item = ET.SubElement(new_parent, "Item", attrs)
                for v in child:
                    if v.tag == "Variant":
                        vattrs = {"name": v.get("name"), "icon": v.get("icon", "")}
                        vn = v.get("name", "")
                        if vn in ("Tree", "Air"):
                            vattrs["note"] = vn
                        elif vn in ("Relay", "Wire", "Generic"):
                            pass
                        ET.SubElement(item, "Variant", vattrs)
                    elif v.tag == "HotPoint":
                        ET.SubElement(item, "HotPoint", x=v.get("x"), y=v.get("y"))
    walk(old, lib, [])
    ET.indent(lib, space="    ")
    ET.ElementTree(lib).write(dst, encoding="utf-8", xml_declaration=True)

if __name__ == "__main__":
    convert(sys.argv[1], sys.argv[2])
