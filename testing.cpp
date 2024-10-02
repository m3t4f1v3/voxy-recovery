#include <iostream>
#include <fstream>
#include <vector>
#include <string>
#include <zlib.h>
#include <cstdint>
#include <nbt_tags.h>
#include <endian.h>

std::string read_mca_file(const std::string &filename)
{
    std::ifstream file(filename, std::ios::binary);
    if (!file.is_open())
    {
        std::cerr << "Error opening file: " << filename << std::endl;
        return {};
    }

    std::string all_decompressed_data; // To hold all decompressed data

    // Read the chunk locations (4096 bytes)
    std::vector<uint8_t> chunk_locations(4096);
    file.read(reinterpret_cast<char *>(chunk_locations.data()), 4096);

    for (int i = 0; i < 4096; i += 4)
    {
        // Extract the offset and sector count for each chunk
        uint32_t offset = (chunk_locations[i] << 16 | chunk_locations[i + 1] << 8 | chunk_locations[i + 2]) * 4096;
        uint8_t sectors = chunk_locations[i + 3];

        if (offset > 0)
        {
            file.seekg(offset, std::ios::beg);

            // Read the length of the chunk data (big-endian)
            uint32_t length;
            file.read(reinterpret_cast<char *>(&length), 4);
            length = __builtin_bswap32(length); // Convert from big-endian to little-endian

            // Read compression type
            uint8_t compression_type;
            file.read(reinterpret_cast<char *>(&compression_type), 1);

            // Read the compressed chunk data
            std::vector<uint8_t> compressed_data(length - 1);
            file.read(reinterpret_cast<char *>(compressed_data.data()), length - 1);

            // Decompress if using zlib (compression_type == 2)
            if (compression_type == 2)
            {
                uLongf decompressed_size = 1024 * 1024; // Set an arbitrary size for decompressed buffer
                std::vector<uint8_t> decompressed_data(decompressed_size);

                int result = uncompress(decompressed_data.data(), &decompressed_size, compressed_data.data(), length - 1);
                if (result == Z_OK)
                {
                    std::cout << "Chunk " << i / 4 << ": Decompressed successfully" << std::endl;
                    // Append the decompressed data to the output string
                    all_decompressed_data.append(reinterpret_cast<char *>(decompressed_data.data()), decompressed_size);
                }
                else
                {
                    std::cerr << "Error decompressing chunk: " << i / 4 << std::endl;
                }
            }
            else
            {
                std::cerr << "Unsupported compression type: " << static_cast<int>(compression_type) << std::endl;
            }
        }
    }

    file.close();
    return all_decompressed_data; // Return the concatenated decompressed data
}

int main()
{
    // Example usage
    std::string decompressedData = read_mca_file("r.0.0.mca");
    // Do something with the decompressed data
    // std::cout << decompressedData << std::endl;

    std::istringstream is(decompressedData);
    nbt::io::stream_reader reader(is);
    auto pair = nbt::io::read_compound(is);
    nbt::tag_compound &chunk = pair.second->as<nbt::tag_compound>();
    // std::cout << chunk << std::endl;
    for (auto &each1 : chunk)
    {
        // also contains pretty much junk, has stuff about xPos and zPos though (chunk coordinates)
        // std::cout << each1.second << std::endl;
        // std::cout << each1.second << std::endl;
        if (each1.first == "sections")
        {
            // sections has 8*3 values
            for (auto &each2 : each1.second.as<nbt::tag_list>())
            {
                // std::cout << each2 << std::endl;
                // each section has a bunch of junk that doesn't really matter to us such as y level, biomes etc.
                // y level is chunk y level, so multiply by 16 to get absolute y level

                for (auto &each3 : each2.as<nbt::tag_compound>())
                {
                    if (each3.first == "block_states")
                    {
                        for (auto &each4 : each3.second.as<nbt::tag_compound>())
                        {
                            if (each4.first == "palette") {
                                // std::cout << each4.first << std::endl;
                                for (auto &each5 : each4.second.as<nbt::tag_list>()){
                                    // std::cout << each5.as<nbt::tag_compound>()["Name"].as<nbt::tag_string>() << std::endl;
                                }
                            } 
                            else if (each4.first == "data") {
                                std::cout << each4.second.as<nbt::tag_long_array>().size() << std::endl;
                                for (auto &each5 : each4.second.as<nbt::tag_long_array>()){
                                    // std::cout << each5 << std::endl;
                                }
                            }
                            else {
                                std::cerr << "??? why is " << each4.first << " in my mf blockstate" << std::endl;
                            }
                        }
                    }
                }
            }
            // std::cout << each1.second.as<nbt::tag_list>() << std::endl;
        }
    }

    return 0;
}
