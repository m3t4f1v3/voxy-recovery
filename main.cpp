#include <rocksdb/db.h>
#include <rocksdb/options.h>
#include <iostream>
#include <zstd.h>
#include <arpa/inet.h>
#include <zlib.h>

std::string decompressZSTD(const std::string &compressed_data)
{
    // Get the decompressed size from the compressed data (ZSTD provides this info in the compressed stream)
    size_t decompressed_size = ZSTD_getFrameContentSize(compressed_data.data(), compressed_data.size());

    if (decompressed_size == ZSTD_CONTENTSIZE_ERROR)
    {
        throw std::runtime_error("Error: Not a valid ZSTD compressed stream!");
    }
    if (decompressed_size == ZSTD_CONTENTSIZE_UNKNOWN)
    {
        throw std::runtime_error("Error: Original size unknown. Cannot decompress.");
    }

    // Allocate memory for decompressed data
    std::string decompressed_data(decompressed_size, 0);

    // Decompress the data
    size_t result = ZSTD_decompress(&decompressed_data[0], decompressed_size, compressed_data.data(), compressed_data.size());
    if (ZSTD_isError(result))
    {
        throw std::runtime_error("Error during decompression: " + std::string(ZSTD_getErrorName(result)));
    }

    return decompressed_data;
}

// Function to get the level from the ID
int getLevel(long id)
{
    return static_cast<int>((id >> 60) & 0xF);
}

int getX(long id)
{
    return static_cast<int>((id << 36) >> 40);
}

// Function to extract Y from the id
int getY(long id)
{
    return static_cast<int>((id << 4) >> 56);
}

// Function to extract Z from the id
int getZ(long id)
{
    return static_cast<int>((id << 12) >> 40);
}

// int32_t bytesToInt(const uint8_t* bytes) {
//     return (static_cast<int32_t>(bytes[0]) << 24) |
//            (static_cast<int32_t>(bytes[1]) << 16) |
//            (static_cast<int32_t>(bytes[2]) << 8)  |
//            (static_cast<int32_t>(bytes[3]));
// }

// uint64_t getWorldSectionId(int lvl, int x, int y, int z) {
//     return (static_cast<uint64_t>(lvl) << 60) |
//            (static_cast<uint64_t>(y & 0xFF) << 52) |
//            (static_cast<uint64_t>(z & ((1 << 24) - 1)) << 28) |
//            (static_cast<uint64_t>(x & ((1 << 24) - 1)) << 4); // 4 bits spare
// }



int main()
{

    // Path to the RocksDB folder containing the .sst, OPTIONS, MANIFEST, etc.
    std::string db_path = ".voxy/saves/89.168.27.174/488bb03782ab4d61796535db006f641b/storage/";
    // std::string db_path = "/home/edward_wong/.local/share/PrismLauncher/instances/voxy testing/minecraft/saves/test/voxy/0a5a3e2a9aa1c1a11e532ce67e5b6811/storage/";

    // Configure RocksDB options
    rocksdb::Options options;
    options.create_if_missing = false; // Database already exists

    // Column family names (including default column family)
    std::vector<std::string> column_families = {rocksdb::kDefaultColumnFamilyName, "world_sections", "id_mappings"};

    // Column family descriptors and handles
    std::vector<rocksdb::ColumnFamilyDescriptor> cf_descriptors;
    for (const std::string &cf_name : column_families)
    {
        cf_descriptors.push_back(rocksdb::ColumnFamilyDescriptor(cf_name, rocksdb::ColumnFamilyOptions()));
    }

    // Database instance and column family handles
    rocksdb::DB *db;
    std::vector<rocksdb::ColumnFamilyHandle *> handles;

    // Open the database with column families
    rocksdb::Status status = rocksdb::DB::Open(rocksdb::DBOptions(), db_path, cf_descriptors, &handles, &db);
    if (!status.ok())
    {
        std::cerr << "Error opening RocksDB database: " << status.ToString() << std::endl;
        return 1;
    }

    // Example: Use an iterator to read key-value pairs from a column family (e.g., world_sections)
    // 2 = id_mappings
    // Reading from the `world_sections` column family (integers)
    std::cout << "Reading from world_sections (integers):" << std::endl;
    rocksdb::Iterator *it_world_sections = db->NewIterator(rocksdb::ReadOptions(), handles[1]);
    for (it_world_sections->SeekToFirst(); it_world_sections->Valid(); it_world_sections->Next())
    {
        rocksdb::Slice key = it_world_sections->key();
        // rocksdb::Slice value = it_world_sections->value();

        std::string compressed_value = it_world_sections->value().ToString();
        std::string decompressed_value;
        try
        {
            decompressed_value = decompressZSTD(compressed_value); // Decompress the value
        }
        catch (const std::runtime_error &e)
        {
            std::cerr << "Decompression error: " << e.what() << std::endl;
            continue;
        }
        // long id = bytesToInt(key.ToString(true));
        // uint8_t raw[8];
        long id;
        // std::cout << sizeof(long) << std::endl;
        // std::cout << key.size() << std::endl;
        // std::cout << key.size() << std::endl;
        // std::string id = key.ToString(true);
        std::memcpy(&id, key.data(), key.size());
        id = __builtin_bswap64(id); //todo: write one that does it automatically based on host 
        // std::cout << id << std::endl;
        // long id = raw;

        // id = htonl(id); // java is big endian, so convert to ours
        // std::cout << bytesToInt(id) << std::endl;
        // std::cout << key.ToString(true) << std::endl;
        // std::cout << value.ToString() << std::endl;
        // std::cout << key.ToString(true) << " " << id << std::endl;
        // std::cout << "Key: " << key.ToString(true) << std::endl;
        // for some reason the lvl can get up to 4 => 5 dimensions (???)
        std::cout << "World " << getLevel(id) << ": x=" << getX(id) << ", y=" << getY(id) << ", z=" << getZ(id) << std::endl;
        // std::cout << decompressed_value << std::endl;
    }

    // Reading from the `id_mappings` column family (ZSTD compressed)
    std::cout << "\nReading from id_mappings:" << std::endl;
    rocksdb::Iterator *it_id_mappings = db->NewIterator(rocksdb::ReadOptions(), handles[2]);
    for (it_id_mappings->SeekToFirst(); it_id_mappings->Valid(); it_id_mappings->Next())
    {
        
        rocksdb::Slice key = it_id_mappings->key();
        long long id;
        // std::cout << key.size() << std::endl;
        // std::string id = key.ToString(true);
        std::memcpy(&id, key.data(), key.size());
        id = __builtin_bswap64(id);
        // std::cout << id << std::endl;
        // id = htonl(id);
        // id = __builtin_bswap32(id); // java is big endian
        // long id = std::stoul(it_id_mappings->key().ToString(true), nullptr, 16);
        // std::cout << key.ToString(true) << " " << id << std::endl;
        // std::cout << "Key: " << id << ", Decompressed Value: " << decompressed_value << std::endl;
    }

    // Clean up
    delete it_world_sections;
    delete it_id_mappings;
    for (auto handle : handles)
    {
        db->DestroyColumnFamilyHandle(handle);
    }
    delete db;

    return 0;
}