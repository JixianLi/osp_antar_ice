#include "ospr/vtk_xml.h"

#include <cstring>
#include <fstream>
#include <sstream>
#include <stdexcept>

#include <miniz.h>
#include <pugixml.hpp>

namespace ospr {
namespace {

// The appended data section holds raw binary that is not valid XML, so pugixml
// is only ever shown the part of the file before it, with the root element
// closed off by hand.
constexpr const char* APPENDED_TAG = "<AppendedData";

std::string read_file(const std::string& path)
{
    std::ifstream stream(path, std::ios::binary);
    if (!stream)
        throw std::runtime_error("cannot open " + path);
    std::ostringstream buffer;
    buffer << stream.rdbuf();
    return buffer.str();
}

void require(bool condition, const std::string& message)
{
    if (!condition)
        throw std::runtime_error(message);
}

uint64_t read_header_value(const char* bytes, bool wide_header)
{
    if (wide_header) {
        uint64_t value = 0;
        std::memcpy(&value, bytes, sizeof(value));
        return value;
    }
    uint32_t value = 0;
    std::memcpy(&value, bytes, sizeof(value));
    return value;
}

template <typename T>
void widen_to_float(const char* bytes, std::size_t count, std::vector<float>& out)
{
    out.resize(count);
    for (std::size_t index = 0; index < count; ++index) {
        T value;
        std::memcpy(&value, bytes + index * sizeof(T), sizeof(T));
        out[index] = static_cast<float>(value);
    }
}

std::size_t size_of_vtk_type(const std::string& type)
{
    if (type == "Int8" || type == "UInt8")
        return 1;
    if (type == "Int16" || type == "UInt16")
        return 2;
    if (type == "Int32" || type == "UInt32" || type == "Float32")
        return 4;
    if (type == "Int64" || type == "UInt64" || type == "Float64")
        return 8;
    throw std::runtime_error("unsupported DataArray type: " + type);
}

void widen_by_type(const std::string& type,
    const char* bytes,
    std::size_t count,
    std::vector<float>& out)
{
    if (type == "Int8")
        widen_to_float<int8_t>(bytes, count, out);
    else if (type == "UInt8")
        widen_to_float<uint8_t>(bytes, count, out);
    else if (type == "Int16")
        widen_to_float<int16_t>(bytes, count, out);
    else if (type == "UInt16")
        widen_to_float<uint16_t>(bytes, count, out);
    else if (type == "Int32")
        widen_to_float<int32_t>(bytes, count, out);
    else if (type == "UInt32")
        widen_to_float<uint32_t>(bytes, count, out);
    else if (type == "Int64")
        widen_to_float<int64_t>(bytes, count, out);
    else if (type == "UInt64")
        widen_to_float<uint64_t>(bytes, count, out);
    else if (type == "Float32")
        widen_to_float<float>(bytes, count, out);
    else if (type == "Float64")
        widen_to_float<double>(bytes, count, out);
    else
        throw std::runtime_error("unsupported DataArray type: " + type);
}

// A compressed appended block is laid out as
//   [num_blocks][uncompressed_block_size][last_block_size][compressed_size]*n
//   followed by the n deflate streams back to back.
// An uncompressed one is just [num_bytes] followed by the bytes.
std::vector<char> decode_block(const std::string& file,
    std::size_t appended_start,
    uint64_t offset,
    bool compressed,
    bool wide_header,
    const std::string& array_name)
{
    const std::size_t header_size = wide_header ? 8 : 4;
    const std::size_t block_start = appended_start + offset;
    require(block_start + header_size <= file.size(),
        "appended offset past end of file for array " + array_name);

    if (!compressed) {
        const uint64_t num_bytes = read_header_value(file.data() + block_start, wide_header);
        const std::size_t data_start = block_start + header_size;
        require(data_start + num_bytes <= file.size(),
            "uncompressed block runs past end of file for array " + array_name);
        return std::vector<char>(
            file.begin() + data_start, file.begin() + data_start + num_bytes);
    }

    const uint64_t num_blocks = read_header_value(file.data() + block_start, wide_header);
    const uint64_t block_size
        = read_header_value(file.data() + block_start + header_size, wide_header);
    const uint64_t last_size
        = read_header_value(file.data() + block_start + 2 * header_size, wide_header);

    std::vector<uint64_t> compressed_sizes(num_blocks);
    const std::size_t sizes_start = block_start + 3 * header_size;
    require(sizes_start + num_blocks * header_size <= file.size(),
        "compressed block table runs past end of file for array " + array_name);
    for (uint64_t index = 0; index < num_blocks; ++index)
        compressed_sizes[index]
            = read_header_value(file.data() + sizes_start + index * header_size, wide_header);

    // A last_size of 0 means the final block is full rather than partial.
    const uint64_t total = num_blocks == 0
        ? 0
        : (num_blocks - 1) * block_size + (last_size == 0 ? block_size : last_size);

    std::vector<char> out(total);
    std::size_t source = sizes_start + num_blocks * header_size;
    std::size_t destination = 0;
    for (uint64_t index = 0; index < num_blocks; ++index) {
        const bool is_last = index + 1 == num_blocks;
        mz_ulong expanded = static_cast<mz_ulong>(
            is_last && last_size != 0 ? last_size : block_size);
        require(source + compressed_sizes[index] <= file.size(),
            "compressed data runs past end of file for array " + array_name);
        const int status = mz_uncompress(
            reinterpret_cast<unsigned char*>(out.data()) + destination,
            &expanded,
            reinterpret_cast<const unsigned char*>(file.data()) + source,
            static_cast<mz_ulong>(compressed_sizes[index]));
        if (status != MZ_OK)
            throw std::runtime_error("zlib inflate failed on array " + array_name
                + " block " + std::to_string(index) + " (miniz status "
                + std::to_string(status) + ")");
        source += compressed_sizes[index];
        destination += expanded;
    }
    out.resize(destination);
    return out;
}

} // namespace

const DataArray* ImageData::find(const std::string& name) const
{
    for (const DataArray& array : point_arrays)
        if (array.name == name)
            return &array;
    return nullptr;
}

std::size_t ImageData::point_count() const
{
    return static_cast<std::size_t>(dims[0]) * dims[1] * dims[2];
}

ImageData read_vti(const std::string& path)
{
    const std::string file = read_file(path);

    const std::size_t appended_tag = file.find(APPENDED_TAG);
    require(appended_tag != std::string::npos,
        path + ": no <AppendedData> section; only appended format is supported");
    const std::size_t underscore = file.find('_', appended_tag);
    require(underscore != std::string::npos, path + ": malformed <AppendedData> section");
    const std::size_t appended_start = underscore + 1;

    std::string header = file.substr(0, appended_tag);
    header += "</VTKFile>";

    pugi::xml_document document;
    const pugi::xml_parse_result parsed = document.load_buffer(header.data(), header.size());
    require(parsed, path + ": XML parse error: " + parsed.description());

    const pugi::xml_node root = document.child("VTKFile");
    require(root, path + ": no <VTKFile> root");
    require(std::string(root.attribute("type").value()) == "ImageData",
        path + ": expected type=\"ImageData\", got \""
            + root.attribute("type").value() + "\"");
    require(std::string(root.attribute("byte_order").value()) == "LittleEndian",
        path + ": only LittleEndian is supported");

    const std::string header_type = root.attribute("header_type").value();
    require(header_type.empty() || header_type == "UInt32" || header_type == "UInt64",
        path + ": unsupported header_type " + header_type);
    const bool wide_header = header_type == "UInt64";

    const std::string compressor = root.attribute("compressor").value();
    require(compressor.empty() || compressor == "vtkZLibDataCompressor",
        path + ": unsupported compressor " + compressor);
    const bool compressed = !compressor.empty();

    const pugi::xml_node image = root.child("ImageData");
    require(image, path + ": no <ImageData> element");

    ImageData data;

    const std::string direction = image.attribute("Direction").value();
    if (!direction.empty()) {
        std::istringstream stream(direction);
        double matrix[9]{};
        for (double& entry : matrix)
            stream >> entry;
        const double identity[9]{1, 0, 0, 0, 1, 0, 0, 0, 1};
        for (int index = 0; index < 9; ++index)
            require(std::abs(matrix[index] - identity[index]) < 1e-12,
                path + ": non-identity Direction is not supported");
    }

    {
        std::istringstream stream(image.attribute("WholeExtent").value());
        int lo[3]{}, hi[3]{};
        stream >> lo[0] >> hi[0] >> lo[1] >> hi[1] >> lo[2] >> hi[2];
        require(static_cast<bool>(stream), path + ": malformed WholeExtent");
        for (int axis = 0; axis < 3; ++axis)
            data.dims[axis] = hi[axis] - lo[axis] + 1;
    }
    {
        std::istringstream stream(image.attribute("Origin").value());
        stream >> data.origin[0] >> data.origin[1] >> data.origin[2];
    }
    {
        std::istringstream stream(image.attribute("Spacing").value());
        stream >> data.spacing[0] >> data.spacing[1] >> data.spacing[2];
    }

    const pugi::xml_node piece = image.child("Piece");
    require(piece, path + ": no <Piece> element");
    require(!piece.next_sibling("Piece"), path + ": multi-piece files are not supported");

    for (pugi::xml_node node : piece.child("PointData").children("DataArray")) {
        const std::string format = node.attribute("format").value();
        const std::string name = node.attribute("Name").value();
        require(format == "appended",
            path + ": array " + name + " uses format=\"" + format
                + "\"; only \"appended\" is supported");

        DataArray array;
        array.name = name;
        array.components = node.attribute("NumberOfComponents").as_int(1);

        const std::string type = node.attribute("type").value();
        const std::vector<char> bytes = decode_block(file,
            appended_start,
            node.attribute("offset").as_ullong(),
            compressed,
            wide_header,
            name);

        const std::size_t element_size = size_of_vtk_type(type);
        const std::size_t expected = data.point_count() * array.components;
        require(bytes.size() == expected * element_size,
            path + ": array " + name + " decoded to " + std::to_string(bytes.size())
                + " bytes, expected " + std::to_string(expected * element_size));

        widen_by_type(type, bytes.data(), expected, array.values);
        data.point_arrays.push_back(std::move(array));
    }

    return data;
}

} // namespace ospr
