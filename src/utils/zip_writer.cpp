#include "utils/zip_writer.hpp"
#include <array>

namespace jres {

    ZipWriter::ZipWriter(const std::string& out_filename) : m_is_closed(false) {
        m_file.open(out_filename, std::ios::binary);
    }

    ZipWriter::~ZipWriter() {
        if (!m_is_closed) {
            close();
        }
    }

    // Basic CRC32 implementation
    uint32_t ZipWriter::calculate_crc32(const std::string& data) {
        uint32_t crc = 0xFFFFFFFF;
        for (unsigned char c : data) {
            crc ^= c;
            for (int i = 0; i < 8; i++) {
                crc = (crc >> 1) ^ (0xEDB88320 & (-(crc & 1)));
            }
        }
        return ~crc;
    }

    template<typename T>
    void write_val(std::ofstream& out, T value) {
        out.write(reinterpret_cast<const char*>(&value), sizeof(T));
    }

    void ZipWriter::add_file(const std::string& filename, const std::string& content) {
        if (m_is_closed) return;

        uint32_t offset = static_cast<uint32_t>(m_file.tellp());
        uint32_t crc = calculate_crc32(content);
        uint32_t size = static_cast<uint32_t>(content.size());

        FileHeader header;
        header.filename = filename;
        header.crc32 = crc;
        header.compressed_size = size; // STORE method (no compression)
        header.uncompressed_size = size;
        header.local_header_offset = offset;
        m_files.push_back(header);

        write_local_header(filename, crc, size);
        m_file.write(content.data(), size);
    }

    void ZipWriter::write_local_header(const std::string& filename, uint32_t crc, uint32_t size) {
        write_val<uint32_t>(m_file, 0x04034b50); // Signature
        write_val<uint16_t>(m_file, 10);         // Version needed
        write_val<uint16_t>(m_file, 0);          // Flags
        write_val<uint16_t>(m_file, 0);          // Method (0 = STORE)
        write_val<uint16_t>(m_file, 0);          // Time (dummy)
        write_val<uint16_t>(m_file, 0);          // Date (dummy)
        write_val<uint32_t>(m_file, crc);
        write_val<uint32_t>(m_file, size);
        write_val<uint32_t>(m_file, size);
        write_val<uint16_t>(m_file, static_cast<uint16_t>(filename.size()));
        write_val<uint16_t>(m_file, 0);          // Extra len
        m_file.write(filename.data(), filename.size());
    }

    void ZipWriter::write_central_directory() {
        uint32_t start_cd = static_cast<uint32_t>(m_file.tellp());

        for (const auto& f : m_files) {
            write_val<uint32_t>(m_file, 0x02014b50); // CD Signature
            write_val<uint16_t>(m_file, 10);         // Version made by
            write_val<uint16_t>(m_file, 10);         // Version needed
            write_val<uint16_t>(m_file, 0);          // Flags
            write_val<uint16_t>(m_file, 0);          // Method (0 = STORE)
            write_val<uint16_t>(m_file, 0);          // Time
            write_val<uint16_t>(m_file, 0);          // Date
            write_val<uint32_t>(m_file, f.crc32);
            write_val<uint32_t>(m_file, f.compressed_size);
            write_val<uint32_t>(m_file, f.uncompressed_size);
            write_val<uint16_t>(m_file, static_cast<uint16_t>(f.filename.size()));
            write_val<uint16_t>(m_file, 0);          // Extra len
            write_val<uint16_t>(m_file, 0);          // Comment len
            write_val<uint16_t>(m_file, 0);          // Disk start
            write_val<uint16_t>(m_file, 0);          // Internal attr
            write_val<uint32_t>(m_file, 0);          // External attr
            write_val<uint32_t>(m_file, f.local_header_offset);
            m_file.write(f.filename.data(), f.filename.size());
        }

        uint32_t end_cd = static_cast<uint32_t>(m_file.tellp());
        uint32_t size_cd = end_cd - start_cd;

        // End of Central Directory Record
        write_val<uint32_t>(m_file, 0x06054b50); // Signature
        write_val<uint16_t>(m_file, 0);          // Disk number
        write_val<uint16_t>(m_file, 0);          // Disk w/ CD
        write_val<uint16_t>(m_file, static_cast<uint16_t>(m_files.size())); // Entries on disk
        write_val<uint16_t>(m_file, static_cast<uint16_t>(m_files.size())); // Total entries
        write_val<uint32_t>(m_file, size_cd);
        write_val<uint32_t>(m_file, start_cd);
        write_val<uint16_t>(m_file, 0);          // Comment len
    }

    void ZipWriter::close() {
        if (m_is_closed) return;
        write_central_directory();
        m_file.close();
        m_is_closed = true;
    }

}
