#!/usr/bin/env python3
"""Export the website's display cover, USB socket, and switch from the assembly.

Requires Python 3.12, cadquery-ocp==7.8.1.1, and vtk==9.3.1.
The printed shell STL exports remain authoritative and are not regenerated here.
"""
from pathlib import Path

from OCP.BRepMesh import BRepMesh_IncrementalMesh
from OCP.STEPCAFControl import STEPCAFControl_Reader
from OCP.StlAPI import StlAPI_Writer
from OCP.TCollection import TCollection_ExtendedString
from OCP.TDataStd import TDataStd_Name
from OCP.TDF import TDF_Label, TDF_LabelSequence
from OCP.TDocStd import TDocStd_Document
from OCP.TopLoc import TopLoc_Location
from OCP.XCAFDoc import XCAFDoc_DocumentTool

ROOT = Path(__file__).resolve().parents[1]
TARGETS = {
    "LCD-MIANBAN": "display-cover.stl",
    "TYPE_C_20241129": "usb-port.stl",
    "SPDT slide switch SS12D00": "power-switch.stl",
}


def main():
    document = TDocStd_Document(TCollection_ExtendedString("Bleep"))
    reader = STEPCAFControl_Reader()
    reader.SetNameMode(True)
    reader.ReadFile(str(ROOT / "hardware/Bleep Remote.step"))
    if not reader.Transfer(document):
        raise RuntimeError("Could not import the mechanical assembly")
    shape_tool = XCAFDoc_DocumentTool.ShapeTool_s(document.Main())
    roots = TDF_LabelSequence()
    shape_tool.GetFreeShapes(roots)
    exported = set()

    def walk(label, location):
        reference = TDF_Label()
        if shape_tool.GetReferredShape_s(label, reference):
            walk(reference, location.Multiplied(shape_tool.GetLocation_s(label)))
            return
        if shape_tool.IsAssembly_s(label):
            children = TDF_LabelSequence()
            shape_tool.GetComponents_s(label, children)
            for index in range(1, children.Length() + 1):
                walk(children.Value(index), location)
            return
        attribute = TDataStd_Name()
        if not label.FindAttribute(TDataStd_Name.GetID_s(), attribute):
            return
        name = attribute.Get().ToExtString()
        if name not in TARGETS:
            return
        if name in exported:
            raise RuntimeError(f"Ambiguous duplicate component: {name}")
        # Preserve nested placement transforms so the ports fit the shell cutouts.
        shape = shape_tool.GetShape_s(label).Moved(location)
        BRepMesh_IncrementalMesh(shape, 0.06, False, 0.25, True).Perform()
        destination = ROOT / "website/assets/models" / TARGETS[name]
        writer = StlAPI_Writer()
        writer.ASCIIMode = False
        if not writer.Write(shape, str(destination)):
            raise RuntimeError(f"Could not export {destination}")
        exported.add(name)
        print(destination.relative_to(ROOT))

    for index in range(1, roots.Length() + 1):
        walk(roots.Value(index), TopLoc_Location())
    if exported != set(TARGETS):
        raise RuntimeError(f"Missing components: {set(TARGETS) - exported}")


if __name__ == "__main__":
    main()
