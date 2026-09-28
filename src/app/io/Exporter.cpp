#include "io/Exporter.h"

#include "model/ModelView.h"

#include "base/KernelLock.h"
#include "doc/Document.h"
#include "geom/OcctUtil.h"

#include <set>

namespace cadjitsu {

ExportJob makeExportJob(const ModelView &view, const cad::Document &doc, ExportJob::Format format, bool selectedOnly) {
    ExportJob job;
    job.format = format;
    const cad::StatePtr st = view.state();
    if(!st) return job;
    std::set<cad::BodyId> picked;
    if(selectedOnly)
        for(const auto &it : view.selection().items())
            if(!it.body.empty()) picked.insert(it.body);
    const QColor c = ModelView::defaultBodyColor();
    for(const cad::Body *b : st->orderedBodies()) {
        if(selectedOnly ? !picked.count(b->id) : !doc.bodyVisible(b->id)) continue;
        cad::NamedSolid s;
        s.name = doc.bodyName(*b);
        s.shape = b->shape.shape();
        s.rgb[0] = c.redF();
        s.rgb[1] = c.greenF();
        s.rgb[2] = c.blueF();
        job.solids.push_back(std::move(s));
    }
    return job;
}

ExportResult runExport(const ExportJob &job, const std::string &path, bool writeInvalid) {
    ExportResult r;
    const cad::KernelLock kernel(cad::kernelMutex()); // the shapes are shared with the model
    if(job.solids.empty()) {
        r.error = "there are no bodies to export";
        return r;
    }
    for(const auto &s : job.solids) r.solidVolume += cad::volumeOf(s.shape);
    if(job.format == ExportJob::Format::Stl) {
        std::vector<TopoDS_Shape> shapes;
        for(const auto &s : job.solids) shapes.push_back(s.shape);
        cad::StlExport stl;
        if(!cad::buildStlMesh(shapes, job.stl, stl, r.error)) return r;
        r.meshChecked = true;
        r.report = stl.report;
        r.solidVolume = stl.solidVolume;
        if(!stl.report.ok && !writeInvalid) {
            r.error = "the mesh is not printable: " + stl.report.summary();
            return r;
        }
        const std::string name = job.solids.size() == 1 ? job.solids.front().name : std::string("Cadjitsu");
        r.ok = cad::writeStlFile(path, stl.mesh, job.stl.binary, name, r.error);
        return r;
    }
    if(!cad::writeStepFile(path, job.solids, job.schema, r.error)) return r;
    r.ok = true;
    // Read it back: the same solids, the same volume.
    std::vector<cad::NamedSolid> back;
    std::string err;
    if(cad::readStepFile(path, back, err)) {
        r.reimported = true;
        for(const auto &s : back) r.reimportedVolume += cad::volumeOf(s.shape);
    }
    return r;
}

} // namespace cadjitsu
