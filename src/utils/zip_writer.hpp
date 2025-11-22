/**
 * @author popmonkey+jres@gmail.com
 * @file src/utils/zip_writer.hpp
 * @brief Utility for writing ZIP files
 */

 #pragma once

#include <string>
#include <vector>
#include <fstream>
#include <cstdint>

namespace jres {

    /**
     * A simple, dependency-free writer for uncompressed (STORE) ZIP files.
     */
    class ZipWriter {
    public:
        explicit ZipWriter(const std::string& out_filename);
        ~ZipWriter();

        // Add a file to the zip with the given content string
        void add_file(const std::string& filename, const std::string& content);

        // Finalize the zip file (write central directory)
        void close();

    private:
        struct FileHeader {
            std::string filename;
            uint32_t crc32;
            uint32_t compressed_size;
            uint32_t uncompressed_size;
            uint32_t local_header_offset;
        };

        std::ofstream m_file;
        std::vector<FileHeader> m_files;
        bool m_is_closed;

        void write_local_header(const std::string& filename, uint32_t crc, uint32_t size);
        void write_central_directory();
        static uint32_t calculate_crc32(const std::string& data);
    };

}
