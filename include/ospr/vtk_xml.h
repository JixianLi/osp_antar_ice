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

// Reads VTK XML ImageData (.vti) and StructuredGrid (.vts). Supports the subset
// the singn pipeline emits: appended raw data, little endian, UInt32 or UInt64
// headers, optional vtkZLibDataCompressor, identity Direction. Anything else
// throws std::runtime_error naming what was unsupported rather than failing
// subtly.
ImageData read_vti(const std::string& path);
StructuredGrid read_vts(const std::string& path);

} // namespace ospr
