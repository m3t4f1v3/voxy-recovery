#include <rocksdb/db.h>
#include <rocksdb/options.h>
#include <iostream>
#include <zstd.h>
#include <vector>
#include <cstring>
#include <stdexcept>

struct WorldSection {
    long key;
    std::vector<long> data;  // Adjust size based on your section's structure
};

class RocksDBHandler {
public:
    RocksDBHandler(const std::string& db_path);
    ~RocksDBHandler();

    void processWorldSections();

private:
    rocksdb::DB* db;
    std::vector<rocksdb::ColumnFamilyHandle*> handles;

    std::string decompressZSTD(const std::string& compressed_data);
    bool deserialize(WorldSection& section, const std::string& decompressedData, bool ignoreMismatchPosition);
    
    template<typename T>
    T readFromBuffer(const char*& buffer);
};

RocksDBHandler::RocksDBHandler(const std::string& db_path) {
    rocksdb::Options options;
    options.create_if_missing = false;

    std::vector<std::string> column_families = {rocksdb::kDefaultColumnFamilyName, "world_sections", "id_mappings"};
    std::vector<rocksdb::ColumnFamilyDescriptor> cf_descriptors;
    for (const std::string& cf_name : column_families) {
        cf_descriptors.push_back(rocksdb::ColumnFamilyDescriptor(cf_name, rocksdb::ColumnFamilyOptions()));
    }

    rocksdb::Status status = rocksdb::DB::Open(rocksdb::DBOptions(), db_path, cf_descriptors, &handles, &db);
    if (!status.ok()) {
        throw std::runtime_error("Error opening RocksDB database: " + status.ToString());
    }
}

RocksDBHandler::~RocksDBHandler() {
    for (auto handle : handles) {
        db->DestroyColumnFamilyHandle(handle);
    }
    delete db;
}

#include <fstream>

void writeBufferToFile(const char* buffer, size_t size, const std::string& filename) {
    std::ofstream outFile(filename, std::ios::binary);
    if (!outFile) {
        throw std::runtime_error("Failed to open file: " + filename);
    }

    outFile.write(buffer, size);
    if (!outFile.good()) {
        throw std::runtime_error("Failed to write to file: " + filename);
    }

    outFile.close();
}

void writeVectorToFile(const std::vector<long>& data, const std::string& filename) {
    std::ofstream outFile(filename, std::ios::binary);
    if (!outFile) {
        throw std::runtime_error("Failed to open file: " + filename);
    }

    outFile.write(reinterpret_cast<const char*>(data.data()), data.size() * sizeof(long));
    if (!outFile.good()) {
        throw std::runtime_error("Failed to write to file: " + filename);
    }

    outFile.close();
}

std::string RocksDBHandler::decompressZSTD(const std::string& compressed_data) {
    size_t decompressed_size = ZSTD_getFrameContentSize(compressed_data.data(), compressed_data.size());

    if (decompressed_size == ZSTD_CONTENTSIZE_ERROR) {
        throw std::runtime_error("Error: Not a valid ZSTD compressed stream!");
    }
    if (decompressed_size == ZSTD_CONTENTSIZE_UNKNOWN) {
        throw std::runtime_error("Error: Original size unknown. Cannot decompress.");
    }

    std::string decompressed_data(decompressed_size, 0);
    size_t result = ZSTD_decompress(&decompressed_data[0], decompressed_size, compressed_data.data(), compressed_data.size());
    if (ZSTD_isError(result)) {
        throw std::runtime_error("Error during decompression: " + std::string(ZSTD_getErrorName(result)));
    }

    return decompressed_data;
}

template<typename T>
T RocksDBHandler::readFromBuffer(const char*& buffer) {
    T value;
    std::memcpy(&value, buffer, sizeof(T));
    buffer += sizeof(T);
    return value;
}

bool RocksDBHandler::deserialize(WorldSection& section, const std::string& decompressedData, bool ignoreMismatchPosition) {
    const char* buffer = decompressedData.data();
    // writeBufferToFile(decompressedData.data(), decompressedData.size(), "epic.dat");

    // std::cout << decompressedData.size() << std::endl;
    long hash = 0;

    // Read key
    long key = readFromBuffer<long>(buffer);
    
    // Check if the section's key matches the key in the data
    if (!ignoreMismatchPosition && section.key != key) {
        std::cerr << "Decompressed section key mismatch. Got: " << key << ", Expected: " << section.key << std::endl;
        return false;
    }

    // Read LUT length
    int lutLen = readFromBuffer<int>(buffer);
    std::vector<long> lut(lutLen);
    // Hash calculation for key
    hash = key ^ (lutLen * 1293481298141L);

    // Read LUT and compute hash
    for (int i = 0; i < lutLen; ++i) {
        lut[i] = readFromBuffer<long>(buffer);
        // std::cout << lut[i] << std::endl;
        hash *= 1230987149811L;
        hash += 12831;
        hash ^= lut[i];
    }


    // Read section data using LUT
    section.data.resize(1<<15);  // WHYYYYYYYYYYYYYYYYYYYYYYYYYYYYYYYY
    // todo: WHY does 2**15 work
    for (size_t i = 0; i < 1<<15; ++i) {
        short lutId = readFromBuffer<short>(buffer);
        section.data[i] = lut[lutId];

        // Continue hashing
        hash *= 1230987149811L;
        hash += 12831;
        hash ^= (lutId * 1827631L) ^ lut[lutId];
    }
    // std::cout << key << " " << lutLen << std::endl;

    // Read expected hash
    long expectedHash = readFromBuffer<long>(buffer);
    // Check if hashes match
    if (expectedHash != hash) {
        std::cerr << "Hash mismatch. Got: " << hash << ", Expected: " << expectedHash << std::endl;
        return false;
    }

    // Check if there's remaining data in the buffer
    if (buffer != decompressedData.data() + decompressedData.size()) {
        std::cerr << "Decompressed section had excess data." << std::endl;
        return false;
    }

    return true;
}

void RocksDBHandler::processWorldSections() {
    rocksdb::Iterator* it_world_sections = db->NewIterator(rocksdb::ReadOptions(), handles[1]);
    for (it_world_sections->SeekToFirst(); it_world_sections->Valid(); it_world_sections->Next()) {
        rocksdb::Slice key = it_world_sections->key();
        std::string compressed_value = it_world_sections->value().ToString();
        std::string decompressed_value;

        try {
            decompressed_value = decompressZSTD(compressed_value);
        }
        catch (const std::runtime_error& e) {
            std::cerr << "Decompression error: " << e.what() << std::endl;
            continue;
        }

        long id;
        std::memcpy(&id, key.data(), key.size());
        // 64 bits = 8 bytes * 8
        id = __builtin_bswap64(id);  // Adjust byte order

        WorldSection section;
        section.key = id;

        if (deserialize(section, decompressed_value, false)) {
            std::cout << "World Section Deserialized Successfully:" << std::endl;
            std::cout << "Key: " << id << std::endl;
            // Access section.data here
            writeVectorToFile(section.data, "test.dat");
            exit(-1);
        }
    }

    delete it_world_sections;
}

int main() {
    try {
        RocksDBHandler dbHandler("../.voxy/saves/89.168.27.174/488bb03782ab4d61796535db006f641b/storage/");
        dbHandler.processWorldSections();
    }
    catch (const std::exception& e) {
        std::cerr << "Error: " << e.what() << std::endl;
    }

    return 0;
}
