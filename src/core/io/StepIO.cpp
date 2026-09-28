#include "io/StepIO.h"

#include "base/KernelLock.h"
#include "geom/OcctUtil.h"

#include <IFSelect_ReturnStatus.hxx>
#include <Interface_Static.hxx>
#include <Quantity_Color.hxx>
#include <STEPCAFControl_Reader.hxx>
#include <STEPCAFControl_Writer.hxx>
#include <STEPControl_StepModelType.hxx>
#include <TDF_ChildIterator.hxx>
#include <TDF_Label.hxx>
#include <TDF_LabelSequence.hxx>
#include <TDataStd_Name.hxx>
#include <TDocStd_Document.hxx>
#include <TCollection_ExtendedString.hxx>
#include <TCollection_AsciiString.hxx>
#include <UnitsMethods_LengthUnit.hxx>
#include <XCAFApp_Application.hxx>
#include <XCAFDoc_ColorTool.hxx>
#include <XCAFDoc_DocumentTool.hxx>
#include <XCAFDoc_ShapeTool.hxx>

#include <mutex>

namespace cad {

namespace {

// STEP translation uses process-wide static parameters.
std::mutex &stepMutex() {
    static std::mutex m;
    return m;
}

Handle(TDocStd_Document) newXcafDocument() {
    Handle(XCAFApp_Application) app = XCAFApp_Application::GetApplication();
    Handle(TDocStd_Document) doc;
    app->NewDocument("MDTV-XCAF", doc);
    return doc;
}

} // namespace

bool writeStepFile(const std::string &path, const std::vector<NamedSolid> &solids, StepSchema schema,
                   std::string &error) {
    const KernelLock kernel(kernelMutex());
    if(solids.empty()) {
        error = "there are no bodies to export";
        return false;
    }
    quietKernelMessages();
    std::lock_guard<std::mutex> lock(stepMutex());
    Handle(TDocStd_Document) doc = newXcafDocument();
    XCAFDoc_DocumentTool::SetLengthUnit(doc, 1.0, UnitsMethods_LengthUnit_Millimeter);
    Handle(XCAFDoc_ShapeTool) shapes = XCAFDoc_DocumentTool::ShapeTool(doc->Main());
    Handle(XCAFDoc_ColorTool) colors = XCAFDoc_DocumentTool::ColorTool(doc->Main());
    for(const auto &s : solids) {
        const TDF_Label label = shapes->AddShape(s.shape, Standard_False);
        TDataStd_Name::Set(label, TCollection_ExtendedString(s.name.c_str(), Standard_True));
        colors->SetColor(label, Quantity_Color(s.rgb[0], s.rgb[1], s.rgb[2], Quantity_TOC_RGB), XCAFDoc_ColorSurf);
    }

    Interface_Static::SetCVal("write.step.schema", schema == StepSchema::AP242 ? "AP242DIS" : "AP214IS");
    Interface_Static::SetCVal("write.step.unit", "MM");
    Interface_Static::SetCVal("write.step.product.name", "Cadjitsu part");

    STEPCAFControl_Writer writer;
    writer.SetNameMode(Standard_True);
    writer.SetColorMode(Standard_True);
    if(!writer.Transfer(doc, STEPControl_AsIs)) {
        error = "STEP translation failed";
        return false;
    }
    if(writer.Write(path.c_str()) != IFSelect_RetDone) {
        error = "cannot write " + path;
        return false;
    }
    return true;
}

bool readStepFile(const std::string &path, std::vector<NamedSolid> &solids, std::string &error) {
    const KernelLock kernel(kernelMutex());
    solids.clear();
    quietKernelMessages();
    std::lock_guard<std::mutex> lock(stepMutex());
    Handle(TDocStd_Document) doc = newXcafDocument();
    STEPCAFControl_Reader reader;
    reader.SetNameMode(Standard_True);
    reader.SetColorMode(Standard_True);
    if(reader.ReadFile(path.c_str()) != IFSelect_RetDone) {
        error = "cannot read " + path;
        return false;
    }
    if(!reader.Transfer(doc)) {
        error = "STEP translation failed";
        return false;
    }
    Handle(XCAFDoc_ShapeTool) shapes = XCAFDoc_DocumentTool::ShapeTool(doc->Main());
    TDF_LabelSequence free;
    shapes->GetFreeShapes(free);
    for(int i = 1; i <= free.Length(); ++i) {
        const TDF_Label label = free.Value(i);
        NamedSolid s;
        s.shape = XCAFDoc_ShapeTool::GetShape(label);
        Handle(TDataStd_Name) name;
        if(label.FindAttribute(TDataStd_Name::GetID(), name))
            s.name = TCollection_AsciiString(name->Get()).ToCString();
        solids.push_back(s);
    }
    return true;
}

} // namespace cad
