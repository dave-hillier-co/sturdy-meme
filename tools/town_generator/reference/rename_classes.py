#!/usr/bin/env python3
"""
Rename obfuscated class names in MFCG JavaScript files.
Extracts mappings from g["..."] = XX; patterns and applies renames.
"""

import re
import os
import sys
from pathlib import Path

# Mapping of obfuscated name -> readable name
# Extracted from g["com.watabou..."] = XX; patterns
RENAMES = {
    # Coogee Framework
    "bb": "Game",
    "sb": "Main",
    "Pb": "Scene",
    "Hb": "ButtonBase",
    "fb": "Button",
    "gb": "VBox",
    "oc": "Form",
    "ic": "ButtonsForm",
    "ud": "CheckBox",
    "hg": "RadioGroup",
    "Hd": "Window",
    "ee": "Dialog",
    "Rc": "DropDown",
    "Ib": "Label",
    "dd": "Menu",
    "ff": "MenuItem",
    "ta": "SolidRect",
    "Je": "MenuSeparator",
    "fe": "MultiAction",
    "$d": "Slider",
    "Jb": "TextArea",
    "tc": "TextInput",
    # "q": "Toast",  # Too short, likely conflicts
    # "u": "UI",  # Too short, likely conflicts
    # "D": "UIStyle",  # Too short, likely conflicts
    "lb": "CloseButton",
    "Va": "DropDownButton",
    "eb": "FloatInput",
    "Ef": "FormButtons",
    "Od": "IntInput",
    "vd": "Swatch",
    "fd": "TextView",
    "xc": "EditForm",
    "Le": "ColorForm",
    "Id": "FontForm",
    "Qh": "FontPreview",
    "Wj": "Message",
    "Xj": "MultiColorForm",
    "Yj": "ColorItem",
    "Hc": "PaletteForm",
    "Rh": "ButtonColumn",
    "Pd": "Grid",
    "ed": "HBox",
    "bh": "SimpleBox",
    "ig": "Tabs",
    "ae": "TabButtons",
    "Uh": "TabMultiRow",
    "ld": "Text",

    # Formats
    "Wa": "GeoJSON",
    "Oa": "SVG",
    "Ja": "Sprite2SVG",

    # Geometry
    "Hf": "Chaikin",
    "Ea": "Circle",
    "Gc": "Color",
    "Me": "Cubic",
    "Ic": "DCEL",
    "kg": "HalfEdge",
    "ak": "Vertex",
    "Wh": "Face",
    "Ua": "EdgeChain",
    "qa": "GeomUtils",
    "bk": "Graph",
    "ck": "Node",
    "dk": "PoissonPattern",
    "Yh": "FillablePoly",
    "hf": "Segment",
    "jf": "SkeletonBuilder",
    "dh": "Rib",
    "ek": "SkeletonBuilder_Segment",
    "xe": "Triangulation",
    "fk": "Triangulation_Node",

    # Polygon classes
    "kf": "PolyAccess",
    "ye": "PolyBool",
    "Gb": "PolyBounds",
    "Sa": "PolyCore",
    "Qd": "PolyCreate",
    "gd": "PolyCut",
    "Yc": "PolyTransform",

    # MFCG Core
    "Wk": "Buffer",
    "kd": "Values",
    "ze": "Equator",
    "be": "Export",
    "lg": "JsonExporter",
    "Rd": "SvgExporter",
    "Ma": "Namer",

    # Mapping/Rendering
    "hd": "BuildingPainter",
    "Yk": "FarmPainter",
    "fh": "Focus",
    "ai": "FocusView",
    "bi": "FormalMap",
    "kc": "PatchView",
    "mg": "RiverView",
    "ie": "RoadsView",
    # "K": "Style",  # Too short
    "gc": "TreesLayer",
    "ng": "WallsView",

    # Model classes
    "Fd": "Blueprint",
    "Jd": "Building",
    "yb": "Canal",
    "ci": "Cell",
    "Ub": "City",
    "pc": "CurtainWall",
    "Pe": "District",
    "ik": "DistrictBuilder",
    "Re": "Grower",
    "ei": "DocksGrower",
    "fi": "ParkGrower",
    "Ae": "Forester",
    "di": "Landmark",
    "Bb": "ModelDispatcher",
    "gh": "Topology",
    "Db": "UnitSystem",

    # Utils
    "pg": "Noise",
    # "C": "Random",  # Too short, conflicts
    "Se": "Perlin",

    # Block classes
    "ii": "Block",
    "$k": "TwistedBlock",

    # Ward classes
    "Rb": "Ward",
    "Pc": "Alleys",
    "xd": "Castle",
    "Ne": "Cathedral",
    "yd": "Farm",
    "lf": "Harbour",
    "Te": "Mansion",
    "jk": "Mansion_Rect",
    "he": "Market",
    "ce": "Park",
    "Qe": "WardGroup",
    "og": "Wilderness",

    # Scenes
    "ia": "TownScene",
    "vc": "ToolForm",
    "rg": "TownForm",
    "ke": "StyleForm",
    "Ec": "ViewScene",
    "jd": "WarpScene",

    # Overlays
    "tb": "Compass",
    "Ca": "Overlay",
    "qg": "CompassOverlay",
    "tg": "CurvedLabel",
    "zi": "CurvedLabel_Letter",
    "ra": "Emblem",
    "nf": "EmblemOverlay",
    "Bi": "Frame",
    "ki": "GridOverlay",
    "oi": "LabelsOverlay",
    "Ci": "Legend",
    "ug": "Legend_Legendary",
    "kh": "Legend_LegendItem",
    "lh": "Legend_TitleView",
    "nh": "Legend_SeparatorView",
    "mh": "Legend_EmblemView",
    "Uc": "LegendOverlay",
    "Pf": "Marker",
    "li": "PinsOverlay",
    "Of": "Pin",
    "Lb": "ScaleBar",
    "of": "ScaleBarOld",
    "ni": "ScaleBarOverlay",
    "Di": "Title",
    "qi": "TitleOverlay",

    # Tools
    "me": "BloatTool",
    "Kf": "DisplaceTool",
    "xi": "EqualizeTool",
    "Lf": "LiquifyTool",
    "wi": "MeasureTool",
    "vi": "PinchTool",
    "Mf": "RelaxTool",
    "ui": "RotateTool",

    # UI
    "We": "EditInPlace",
    "jh": "Outline",
    "vb": "TextUI",  # Renamed to avoid conflict with Text
    "le": "Tooltip",
    "Nf": "EmblemForm",
    "pi": "TownInfo",
    "Jf": "URLForm",

    # Utils
    "ji": "Bisector",
    "mf": "Bloater",
    "Zk": "Cutter",
    "gk": "GraphicsUtils",
    "yi": "PathTracker",
    "uc": "PolyUtils",

    # NLP
    "de": "Markov",
    "nd": "Syllables",

    # Process/System
    "Ei": "Process",
    "ge": "Exporter",
    "ba": "State",
    "za": "URLState",

    # Tracery
    "wg": "RuleSelector",
    "$h": "DeckRuleSelector",
    "Rf": "Symbol",
    "Fi": "ExtSymbol",
    "hk": "Grammar",
    "jb": "ModsEngBasic",
    "qh": "NodeAction",
    "xg": "RuleSet",
    "Oe": "Tracery",
    "ph": "TraceryNode",

    # Array/Collection utils
    # "Z": "ArrayExtender",  # Too short
    "Sh": "ColorNames",
    "cl": "DisplayObjectExtender",
    "Kb": "GraphicsExtender",
    "Fc": "MathUtils",
    "Xc": "Palette",
    "wd": "PointExtender",
    "gf": "SetUtils",
    "eh": "StringUtils",
    "rb": "Updater",
    "yg": "RecurringEventDispatcher",
    "Gi": "TimerEventDispatcher",
    "Hi": "FrameEventDispatcher",
}


def rename_in_file(filepath: Path, renames: dict, dry_run: bool = False) -> int:
    """
    Apply renames to a single file.
    Returns the number of replacements made.
    """
    with open(filepath, 'r', encoding='utf-8') as f:
        content = f.read()

    original = content
    total_replacements = 0

    # Sort by length (longest first) to avoid partial replacements
    # Also prioritize names with special chars like $
    sorted_renames = sorted(renames.items(), key=lambda x: (-len(x[0]), x[0]))

    for old_name, new_name in sorted_renames:
        # Use word boundaries to avoid partial matches
        # Handle special chars in name (like $)
        escaped = re.escape(old_name)
        # Pattern: word boundary, then the name, then word boundary
        # But we need to be careful with $ which is valid in JS identifiers
        pattern = rf'\b{escaped}\b'

        count = len(re.findall(pattern, content))
        if count > 0:
            content = re.sub(pattern, new_name, content)
            total_replacements += count
            if count > 0:
                print(f"  {old_name} -> {new_name}: {count} replacements")

    if content != original and not dry_run:
        with open(filepath, 'w', encoding='utf-8') as f:
            f.write(content)

    return total_replacements


def main():
    dry_run = '--dry-run' in sys.argv

    base_dir = Path(__file__).parent

    # Files to process
    targets = [
        base_dir / "mfcg.js",
    ]

    # Add all files in mfcg/ folder
    mfcg_dir = base_dir / "mfcg"
    if mfcg_dir.exists():
        for js_file in mfcg_dir.rglob("*.js"):
            targets.append(js_file)

    print(f"Processing {len(targets)} files...")
    if dry_run:
        print("(DRY RUN - no files will be modified)")
    print()

    total = 0
    for filepath in targets:
        if filepath.exists():
            print(f"\nProcessing {filepath.name}...")
            count = rename_in_file(filepath, RENAMES, dry_run)
            total += count
            print(f"  Total: {count} replacements")

    print(f"\n{'Would make' if dry_run else 'Made'} {total} total replacements")


if __name__ == "__main__":
    main()
