#pragma once

#include <string>
#include <vector>

#include "ospr/math.h"

namespace ospr {

// Every numeric type is widened to float on read: OSPRay volumes and transfer
// functions want float, and the pipeline emits Float32 for everything except
// the Int8/UInt8 mask arrays. Float64 sources would lose precision.
struct DataArray
{
    std::string name;
    int components{1};
    std::vector<float> values;
};

struct ImageData
{
    int dims[3]{0, 0, 0}; // point counts, not cell counts
    double origin[3]{0.0, 0.0, 0.0};
    double spacing[3]{1.0, 1.0, 1.0};
    std::vector<DataArray> point_arrays;

    const DataArray* find(const std::string& name) const;
    std::size_t point_count() const;
};

// The layer surfaces are single-slice curvilinear grids: same container as
// ImageData, but the point coordinates are stored explicitly.
struct StructuredGrid
{
    int dims[3]{0, 0, 0};
    std::vector<Vec3> points;
    std::vector<DataArray> point_arrays;

    const DataArray* find(const std::string& name) const;
    std::size_t point_count() const;
};

// PolyData holding only lines: the contour and pole tubes are baked as polylines
// (the renderer draws them as round curves). Each entry of lines is one
// polyline's vertex indices into points.
struct PolyLines
{
    std::vector<Vec3> points;
    std::vector<std::vector<unsigned int>> lines;
    std::vector<DataArray> point_arrays;

    const DataArray* find(const std::string& name) const;
    std::size_t point_count() const { return points.size(); }
};

// Reads VTK XML ImageData (.vti), StructuredGrid (.vts) and PolyData (.vtp).
// Supports the subset the singn pipeline emits: appended raw data, little endian,
// UInt32 or UInt64 headers, optional vtkZLibDataCompressor, identity Direction.
// read_vtp reads only the <Lines> cells; <Polys>/<Verts>/<Strips> are ignored.
// Anything else throws std::runtime_error naming what was unsupported rather
// than failing subtly.
ImageData read_vti(const std::string& path);
StructuredGrid read_vts(const std::string& path);
PolyLines read_vtp(const std::string& path);

} // namespace ospr
