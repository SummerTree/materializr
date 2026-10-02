#include "MeshDiskCache.h"

#include <BinTools.hxx>
#include <BRep_Builder.hxx>
#include <BRep_Tool.hxx>
#include <Poly_PolygonOnTriangulation.hxx>
#include <Poly_Triangulation.hxx>
#include <TopExp.hxx>
#include <TopExp_Explorer.hxx>
#include <TopLoc_Location.hxx>
#include <TopTools_IndexedMapOfShape.hxx>
#include <TopoDS.hxx>
#include <TopoDS_Edge.hxx>
#include <TopoDS_Face.hxx>

#include <algorithm>
#include <atomic>
#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <filesystem>
#include <fstream>
#include <sstream>
#include <string>
#include <system_error>
#include <vector>

#ifdef __APPLE__
#include <TargetConditionals.h>
#endif

namespace materializr {
namespace meshcache {

namespace {
namespace fs = std::filesystem;

// Bump whenever the mesher's output changes (a fix like the ruled-wall strip
// or the sag repair), so meshes cached by an older build - including wrong
// ones - are never handed back.
constexpr int kMesherVersion = 1;
// Least-recently-used entries go once the cache grows past this.
constexpr std::uintmax_t kMaxCacheBytes = 1024ull * 1024ull * 1024ull;

std::atomic<int> g_depth{0};

#if defined(__ANDROID__) || (defined(__APPLE__) && TARGET_OS_IPHONE)
constexpr bool kSupported = false;   // no dependable cache directory wired up yet
#else
constexpr bool kSupported = true;
#endif

fs::path cacheDir() {
#ifdef _WIN32
    if (const char* la = std::getenv("LOCALAPPDATA"); la && *la)
        return fs::path(la) / "materializr" / "meshcache";
    return {};
#elif defined(__APPLE__)
    if (const char* home = std::getenv("HOME"); home && *home)
        return fs::path(home) / "Library" / "Caches" / "materializr" / "meshcache";
    return {};
#else
    if (const char* xdg = std::getenv("XDG_CACHE_HOME"); xdg && *xdg)
        return fs::path(xdg) / "materializr" / "meshcache";
    if (const char* home = std::getenv("HOME"); home && *home)
        return fs::path(home) / ".cache" / "materializr" / "meshcache";
    return {};
#endif
}

// FNV-1a over the body's own geometry and topology (no triangulation) plus
// the quality setting and mesher version.
std::string keyFor(const TopoDS_Shape& shape, double deflection, double angularDeflection) {
    std::ostringstream bin;
    BinTools::Write(shape, bin, Standard_False, Standard_False,
                    BinTools_FormatVersion_CURRENT);
    const std::string bytes = bin.str();
    std::uint64_t h = 1469598103934665603ull;
    auto mix = [&h](const char* p, std::size_t n) {
        for (std::size_t i = 0; i < n; ++i) { h ^= static_cast<unsigned char>(p[i]); h *= 1099511628211ull; }
    };
    mix(bytes.data(), bytes.size());
    char tail[96];
    const int n = std::snprintf(tail, sizeof(tail), "|%.9g|%.9g|v%d", deflection,
                                angularDeflection, kMesherVersion);
    mix(tail, static_cast<std::size_t>(n));
    char name[48];
    std::snprintf(name, sizeof(name), "%016llx-%zu.mesh",
                  static_cast<unsigned long long>(h), bytes.size());
    return name;
}

void pruneIfNeeded(const fs::path& dir) {
    std::error_code ec;
    std::vector<std::pair<fs::file_time_type, fs::path>> files;
    std::uintmax_t total = 0;
    for (const auto& e : fs::directory_iterator(dir, ec)) {
        if (!e.is_regular_file(ec)) continue;
        total += e.file_size(ec);
        files.push_back({e.last_write_time(ec), e.path()});
    }
    if (total <= kMaxCacheBytes) return;
    std::sort(files.begin(), files.end());   // oldest first
    for (const auto& f : files) {
        if (total <= kMaxCacheBytes) break;
        const auto sz = fs::file_size(f.second, ec);
        if (fs::remove(f.second, ec)) total -= std::min(total, sz);
    }
}

// Copies every face triangulation and edge polygon from `src` to `dst`. The
// two have identical topology (src was written from a shape that serialized
// to the same bytes), so the indexed maps line up one to one.
bool transfer(const TopoDS_Shape& src, const TopoDS_Shape& dst) {
    TopTools_IndexedMapOfShape sf, df, se, de;
    TopExp::MapShapes(src, TopAbs_FACE, sf);
    TopExp::MapShapes(dst, TopAbs_FACE, df);
    TopExp::MapShapes(src, TopAbs_EDGE, se);
    TopExp::MapShapes(dst, TopAbs_EDGE, de);
    if (sf.Extent() != df.Extent() || se.Extent() != de.Extent()) return false;
    BRep_Builder bb;
    for (int i = 1; i <= sf.Extent(); ++i) {
        const TopoDS_Face& cf = TopoDS::Face(sf(i));
        const TopoDS_Face& lf = TopoDS::Face(df(i));
        TopLoc_Location cl;
        const Handle(Poly_Triangulation) tri = BRep_Tool::Triangulation(cf, cl);
        if (tri.IsNull()) continue;   // was bare when cached: stays bare, as it would re-mesh
        bb.UpdateFace(lf, tri);
        TopLoc_Location ll;
        BRep_Tool::Triangulation(lf, ll);
        TopTools_IndexedMapOfShape faceEdges;
        TopExp::MapShapes(cf, TopAbs_EDGE, faceEdges);
        for (int k = 1; k <= faceEdges.Extent(); ++k) {
            const TopoDS_Edge& ce = TopoDS::Edge(faceEdges(k));
            const int idx = se.FindIndex(ce);
            if (idx <= 0) continue;
            const TopoDS_Edge& le = TopoDS::Edge(de(idx));
            if (BRep_Tool::IsClosed(ce, cf)) {   // seam: one polygon per side
                const Handle(Poly_PolygonOnTriangulation) pf = BRep_Tool::PolygonOnTriangulation(
                    TopoDS::Edge(ce.Oriented(TopAbs_FORWARD)), tri, cl);
                const Handle(Poly_PolygonOnTriangulation) pr = BRep_Tool::PolygonOnTriangulation(
                    TopoDS::Edge(ce.Oriented(TopAbs_REVERSED)), tri, cl);
                if (!pf.IsNull() && !pr.IsNull())
                    bb.UpdateEdge(TopoDS::Edge(le.Oriented(TopAbs_FORWARD)), pf, pr, tri, ll);
            } else {
                const Handle(Poly_PolygonOnTriangulation) p =
                    BRep_Tool::PolygonOnTriangulation(ce, tri, cl);
                if (!p.IsNull()) bb.UpdateEdge(le, p, tri, ll);
            }
        }
    }
    return true;
}
} // namespace

bool active() { return kSupported && g_depth.load() > 0; }

LoadScope::LoadScope() { ++g_depth; }
LoadScope::~LoadScope() { --g_depth; }

bool load(const TopoDS_Shape& shape, double deflection, double angularDeflection) {
    if (!active() || shape.IsNull()) return false;
    try {
        const fs::path dir = cacheDir();
        if (dir.empty()) return false;
        const fs::path file = dir / keyFor(shape, deflection, angularDeflection);
        std::error_code ec;
        if (!fs::exists(file, ec)) return false;
        std::ifstream in(file, std::ios::binary);
        if (!in) return false;
        TopoDS_Shape cached;
        BinTools::Read(cached, in);
        if (cached.IsNull() || !transfer(cached, shape)) return false;
        fs::last_write_time(file, fs::file_time_type::clock::now(), ec);   // LRU
        return true;
    } catch (...) {
        return false;
    }
}

void store(const TopoDS_Shape& shape, double deflection, double angularDeflection) {
    if (!active() || shape.IsNull()) return;
    try {
        const fs::path dir = cacheDir();
        if (dir.empty()) return;
        std::error_code ec;
        fs::create_directories(dir, ec);
        const fs::path file = dir / keyFor(shape, deflection, angularDeflection);
        const fs::path tmp = fs::path(file).concat(".tmp");
        {
            std::ofstream out(tmp, std::ios::binary | std::ios::trunc);
            if (!out) return;
            BinTools::Write(shape, out, Standard_True, Standard_False,
                            BinTools_FormatVersion_CURRENT);
            if (!out.good()) { out.close(); fs::remove(tmp, ec); return; }
        }
        fs::rename(tmp, file, ec);   // atomic: a reader never sees half a file
        if (ec) { fs::remove(tmp, ec); return; }
        pruneIfNeeded(dir);
    } catch (...) {
    }
}

} // namespace meshcache
} // namespace materializr
