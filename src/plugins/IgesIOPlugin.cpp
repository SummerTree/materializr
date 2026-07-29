#include "../plugin/PluginMacro.h"
#include "../core/Document.h"
#include "../io/IgesIO.h"
#include "../io/FileDialogs.h"
#include <cstdio>

REGISTER_PLUGIN(IgesIO, [](materializr::PluginContext& ctx) {
    ctx.registerIOFormat({"IGES", {"iges", "igs"}, true, true,
        [](materializr::PluginContext& ctx, const std::string&) {
            materializr::FileDialogs::openFile("Import IGES",
                {{"IGES Files", "*.iges *.igs *.IGES *.IGS"}},
                [&ctx](const std::string& path) {
                    if (path.empty()) return;
                    auto result = materializr::IgesIO::import(path, ctx.document());
                    if (result.success) {
                        ctx.markMeshesDirty();
                    }
                });
            return true;
        },
        [](materializr::PluginContext& ctx, const std::string&) {
            materializr::FileDialogs::exportFile("Export IGES", "export.iges",
                "application/octet-stream",
                {{"IGES Files", "*.iges *.igs"}},
                [&ctx](const std::string& path) {
                    return materializr::IgesIO::exportFile(path, ctx.document()).success;
                });
            return true;
        },
        // Export a caller-supplied document (the "export just these
        // bodies" path hands over a scratch doc holding copies).
        [](const Document& doc, const std::string& path) {
            return materializr::IgesIO::exportFile(path, doc).success;
        }});
})
