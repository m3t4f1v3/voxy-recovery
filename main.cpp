#include <cstring>
#include <iostream>
#include <rocksdb/db.h>
#include <rocksdb/options.h>
#include <stdexcept>
#include <vector>
#include <zstd.h>
#include <bit>

struct WorldSection {
  uint64_t key;
  std::vector<uint64_t> data; // Adjust size based on your section's structure
};

class RocksDBHandler {
public:
  RocksDBHandler(const std::string &db_path);
  ~RocksDBHandler();

  void processWorldSections();
  void readIdMappings();

private:
  rocksdb::DB *db;
  std::vector<rocksdb::ColumnFamilyHandle *> handles;

  std::string decompressZSTD(const std::string &compressed_data);
  bool deserialize(WorldSection &section, const std::string &decompressedData,
                   bool ignoreMismatchPosition);

  template <typename T> T readFromBuffer(const char *&buffer);
};

RocksDBHandler::RocksDBHandler(const std::string &db_path) {
  rocksdb::Options options;
  options.create_if_missing = false;

  std::vector<std::string> column_families = {rocksdb::kDefaultColumnFamilyName,
                                              "world_sections", "id_mappings"};
  std::vector<rocksdb::ColumnFamilyDescriptor> cf_descriptors;
  for (const std::string &cf_name : column_families) {
    cf_descriptors.push_back(rocksdb::ColumnFamilyDescriptor(
        cf_name, rocksdb::ColumnFamilyOptions()));
  }

  rocksdb::Status status = rocksdb::DB::Open(rocksdb::DBOptions(), db_path,
                                             cf_descriptors, &handles, &db);
  if (!status.ok()) {
    throw std::runtime_error("Error opening RocksDB database: " +
                             status.ToString());
  }
}

RocksDBHandler::~RocksDBHandler() {
  for (auto handle : handles) {
    db->DestroyColumnFamilyHandle(handle);
  }
  delete db;
}

#include <fstream>

void writeBufferToFile(const char *buffer, size_t size,
                       const std::string &filename) {
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

void writeVectorToFile(const std::vector<uint64_t> &data,
                       const std::string &filename) {
  std::ofstream outFile(filename, std::ios::binary);
  if (!outFile) {
    throw std::runtime_error("Failed to open file: " + filename);
  }

  outFile.write(reinterpret_cast<const char *>(data.data()),
                data.size() * sizeof(uint64_t));
  if (!outFile.good()) {
    throw std::runtime_error("Failed to write to file: " + filename);
  }

  outFile.close();
}

std::string RocksDBHandler::decompressZSTD(const std::string &compressed_data) {
  size_t decompressed_size =
      ZSTD_getFrameContentSize(compressed_data.data(), compressed_data.size());

  if (decompressed_size == ZSTD_CONTENTSIZE_ERROR) {
    throw std::runtime_error("Error: Not a valid ZSTD compressed stream!");
  }
  if (decompressed_size == ZSTD_CONTENTSIZE_UNKNOWN) {
    throw std::runtime_error(
        "Error: Original size unknown. Cannot decompress.");
  }

  std::string decompressed_data(decompressed_size, 0);
  size_t result =
      ZSTD_decompress(&decompressed_data[0], decompressed_size,
                      compressed_data.data(), compressed_data.size());
  if (ZSTD_isError(result)) {
    throw std::runtime_error("Error during decompression: " +
                             std::string(ZSTD_getErrorName(result)));
  }

  return decompressed_data;
}

template <typename T> T RocksDBHandler::readFromBuffer(const char *&buffer) {
  T value;
  std::memcpy(&value, buffer, sizeof(T));
  buffer += sizeof(T);
  return value;
}

bool RocksDBHandler::deserialize(WorldSection &section,
                                 const std::string &decompressedData,
                                 bool ignoreMismatchPosition) {
  const char *buffer = decompressedData.data();
  // writeBufferToFile(decompressedData.data(), decompressedData.size(),
  // "epic.dat");

  // std::cout << decompressedData.size() << std::endl;
  uint64_t hash = 0;

  // Read key
  uint64_t key = readFromBuffer<uint64_t>(buffer);
  // key = __builtin_bswap64(key);
  // Check if the section's key matches the key in the data
  if (!ignoreMismatchPosition && section.key != key) {
    std::cerr << "Decompressed section key mismatch. Got: " << key
              << ", Expected: " << section.key << std::endl;
    return false;
  }

  // Read LUT length
  uint32_t lutLen = readFromBuffer<uint32_t>(buffer);
  std::vector<uint64_t> lut(lutLen);
  // Hash calculation for key
  hash = key ^ (lutLen * 1293481298141LL);

  // Read LUT and compute hash
  for (uint32_t i = 0; i < lutLen; ++i) {
    lut[i] = readFromBuffer<uint64_t>(buffer);
    // std::cout << lut[i] << std::endl;
    hash *= 1230987149811L;
    hash += 12831;
    hash ^= lut[i];
  }

  // Read section data using LUT
  section.data.resize(1 << 15); // WHYYYYYYYYYYYYYYYYYYYYYYYYYYYYYYYY
  // todo: WHY does 2**15 work, this.data = new long[32 * 32 * 32]; from
  // WorldSection.java
  for (uint32_t i = 0; i < 1 << 15; ++i) {
    uint16_t lutId = readFromBuffer<short>(buffer);
    section.data[i] = lut[lutId];

    // Continue hashing
    hash *= 1230987149811L;
    hash += 12831;
    hash ^= (lutId * 1827631L) ^ lut[lutId];
  }
  // std::cout << key << " " << lutLen << std::endl;

  // Read expected hash
  uint64_t expectedHash = readFromBuffer<uint64_t>(buffer);
  // Check if hashes match
  if (expectedHash != hash) {
    std::cerr << "Hash mismatch. Got: " << hash
              << ", Expected: " << expectedHash << std::endl;
    return false;
  }

  // Check if there's remaining data in the buffer
  if (buffer != decompressedData.data() + decompressedData.size()) {
    std::cerr << "Decompressed section had excess data." << std::endl;
    return false;
  }

  return true;
}



uint32_t getLevel(u_int64_t id)
{
    return static_cast<uint32_t>((id >> 60) & 0xF);
}

uint32_t getX(u_int64_t id)
{
    return static_cast<uint32_t>((id << 36) >> 40);
}

// Function to extract Y from the id
uint32_t getY(u_int64_t id)
{
    return static_cast<uint32_t>((id << 4) >> 56);
}

// Function to extract Z from the id
uint32_t getZ(u_int64_t id)
{
    return static_cast<uint32_t>((id << 12) >> 40);
}


void RocksDBHandler::processWorldSections() {
  rocksdb::Iterator *it_world_sections =
      db->NewIterator(rocksdb::ReadOptions(), handles[1]);
  for (it_world_sections->SeekToFirst(); it_world_sections->Valid();
       it_world_sections->Next()) {
    rocksdb::Slice key = it_world_sections->key();
    std::string compressed_value = it_world_sections->value().ToString();
    std::string decompressed_value;

    try {
      decompressed_value = decompressZSTD(compressed_value);
    } catch (const std::runtime_error &e) {
      std::cerr << "Decompression error: " << e.what() << std::endl;
      continue;
    }

    uint64_t id;
    // std::cout << key.size() << " " << sizeof(uint64_t) << std::endl;
    std::memcpy(&id, key.data(), key.size());
    uint64_t bswap_id = __builtin_bswap64(id);
    // std::cout << "Key: " << id << std::endl;
    // 64 bits = 8 bytes * 8
    // id = __builtin_bswap64(id); // Adjust byte order

    WorldSection section;
    section.key = bswap_id;

    if (deserialize(section, decompressed_value, false)) {
        std::cout << "World Section Deserialized Successfully:" << std::endl;
        std::cout << "Key: " << bswap_id << std::endl;
        // id = __builtin_bswap64(id);
        // id = 0x90000010000170;
        std::cout << "World " << getLevel(id) << ": x=" << getX(id) << ", y=" << getY(id) << ", z=" << getZ(id) << std::endl;

      // Access section.data here
    //   writeVectorToFile(section.data, "test.dat");
    //   exit(-1);
    }
  }

  delete it_world_sections;
}

void RocksDBHandler::readIdMappings() {
  std::cout << "\nReading from id_mappings:" << std::endl;
  rocksdb::Iterator *it_id_mappings =
      db->NewIterator(rocksdb::ReadOptions(), handles[2]);
  for (it_id_mappings->SeekToFirst(); it_id_mappings->Valid();
       it_id_mappings->Next()) {
    rocksdb::Slice key = it_id_mappings->key();
    u_int32_t id;
    // std::cout << key.size() << sizeof(u_int32_t) << std::endl; // = 8
    std::memcpy(&id, key.data(), key.size());
    // id = __builtin_bswap32(id);
    std::cout << "Key: " << id << std::endl;
  }
  delete it_id_mappings;
}

int main() {
    // uint64_t id = 368UL;
    // id = __builtin_bswap64(id);
    // std::cout << "World " << getLevel(id) << ": x=" << getX(id) << ", y=" << getY(id) << ", z=" << getZ(id) << std::endl;
    // SHOULD be 0, 23, 9, 1
  try {
    RocksDBHandler dbHandler("../.voxy/saves/89.168.27.174/"
                             "488bb03782ab4d61796535db006f641b/storage/");
    dbHandler.processWorldSections();
    // dbHandler.readIdMappings();
  } catch (const std::exception &e) {
    std::cerr << "Error: " << e.what() << std::endl;
  }

  return 0;
}
