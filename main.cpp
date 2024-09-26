#include <cstring>
#include <iostream>
#include <rocksdb/db.h>
#include <rocksdb/options.h>
#include <stdexcept>
#include <vector>
#include <zstd.h>
#include <bit>

struct WorldSection
{
  int64_t key;
  std::vector<int64_t> data; // Adjust size based on your section's structure
};

class RocksDBHandler
{
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

  template <typename T>
  T readFromBuffer(const char *&buffer);
};

RocksDBHandler::RocksDBHandler(const std::string &db_path)
{
  rocksdb::Options options;
  options.create_if_missing = false;

  std::vector<std::string> column_families = {rocksdb::kDefaultColumnFamilyName,
                                              "world_sections", "id_mappings"};
  std::vector<rocksdb::ColumnFamilyDescriptor> cf_descriptors;
  for (const std::string &cf_name : column_families)
  {
    cf_descriptors.push_back(rocksdb::ColumnFamilyDescriptor(
        cf_name, rocksdb::ColumnFamilyOptions()));
  }

  rocksdb::Status status = rocksdb::DB::Open(rocksdb::DBOptions(), db_path,
                                             cf_descriptors, &handles, &db);
  if (!status.ok())
  {
    throw std::runtime_error("Error opening RocksDB database: " +
                             status.ToString());
  }
}

RocksDBHandler::~RocksDBHandler()
{
  for (auto handle : handles)
  {
    db->DestroyColumnFamilyHandle(handle);
  }
  delete db;
}

#include <fstream>

void writeBufferToFile(const char *buffer, size_t size,
                       const std::string &filename)
{
  std::ofstream outFile(filename, std::ios::binary);
  if (!outFile)
  {
    throw std::runtime_error("Failed to open file: " + filename);
  }

  outFile.write(buffer, size);
  if (!outFile.good())
  {
    throw std::runtime_error("Failed to write to file: " + filename);
  }

  outFile.close();
}

void writeVectorToFile(const std::vector<int64_t> &data,
                       const std::string &filename)
{
  std::ofstream outFile(filename, std::ios::binary);
  if (!outFile)
  {
    throw std::runtime_error("Failed to open file: " + filename);
  }

  outFile.write(reinterpret_cast<const char *>(data.data()),
                data.size() * sizeof(int64_t));
  if (!outFile.good())
  {
    throw std::runtime_error("Failed to write to file: " + filename);
  }

  outFile.close();
}

std::string RocksDBHandler::decompressZSTD(const std::string &compressed_data)
{
  size_t decompressed_size =
      ZSTD_getFrameContentSize(compressed_data.data(), compressed_data.size());

  if (decompressed_size == ZSTD_CONTENTSIZE_ERROR)
  {
    throw std::runtime_error("Error: Not a valid ZSTD compressed stream!");
  }
  if (decompressed_size == ZSTD_CONTENTSIZE_UNKNOWN)
  {
    throw std::runtime_error(
        "Error: Original size unknown. Cannot decompress.");
  }

  std::string decompressed_data(decompressed_size, 0);
  size_t result =
      ZSTD_decompress(&decompressed_data[0], decompressed_size,
                      compressed_data.data(), compressed_data.size());
  if (ZSTD_isError(result))
  {
    throw std::runtime_error("Error during decompression: " +
                             std::string(ZSTD_getErrorName(result)));
  }

  return decompressed_data;
}

template <typename T>
T RocksDBHandler::readFromBuffer(const char *&buffer)
{
  T value;
  std::memcpy(&value, buffer, sizeof(T));
  buffer += sizeof(T);
  return value;
}

bool RocksDBHandler::deserialize(WorldSection &section,
                                 const std::string &decompressedData,
                                 bool ignoreMismatchPosition)
{
  const char *buffer = decompressedData.data();
  // writeBufferToFile(decompressedData.data(), decompressedData.size(),
  // "epic.dat");

  // std::cout << decompressedData.size() << std::endl;
  int64_t hash = 0;

  // Read key
  int64_t key = readFromBuffer<int64_t>(buffer);
  // key = __builtin_bswap64(key);
  // Check if the section's key matches the key in the data
  if (!ignoreMismatchPosition && section.key != key)
  {
    std::cerr << "Decompressed section key mismatch. Got: " << key
              << ", Expected: " << section.key << std::endl;
    return false;
  }

  // Read LUT length
  int32_t lutLen = readFromBuffer<int32_t>(buffer);
  std::vector<int64_t> lut(lutLen);
  // Hash calculation for key
  hash = key ^ (lutLen * 1293481298141LL);

  // Read LUT and compute hash
  for (int32_t i = 0; i < lutLen; ++i)
  {
    lut[i] = readFromBuffer<int64_t>(buffer);
    // std::cout << lut[i] << std::endl;
    hash *= 1230987149811L;
    hash += 12831;
    hash ^= lut[i];
  }

  // Read section data using LUT
  // section.data.resize(1 << 15); // WHYYYYYYYYYYYYYYYYYYYYYYYYYYYYYYYY
  // todo: WHY does 2**15 work, this.data = new long[32 * 32 * 32]; from
  // WorldSection.java
  // std::cout << section.data.size() << std::endl;

  for (int32_t i = 0; i < section.data.size(); ++i)
  {
    int16_t lutId = readFromBuffer<int16_t>(buffer);
    section.data[i] = static_cast<int64_t>(lutId);

    // Continue hashing
    hash *= 1230987149811L;
    hash += 12831;
    hash ^= (lutId * 1827631L) ^ lut[lutId];
  }
  // std::cout << key << " " << lutLen << std::endl;

  // Read expected hash
  int64_t expectedHash = readFromBuffer<int64_t>(buffer);
  // Check if hashes match
  if (expectedHash != hash)
  {
    std::cerr << "Hash mismatch. Got: " << hash
              << ", Expected: " << expectedHash << std::endl;
    return false;
  }

  // Check if there's remaining data in the buffer
  if (buffer != decompressedData.data() + decompressedData.size())
  {
    std::cerr << "Decompressed section had excess data." << std::endl;
    return false;
  }

  return true;
}

int32_t getLevel(int64_t id)
{
  return static_cast<int32_t>((id >> 60) & 0xF);
}

int32_t getX(int64_t id)
{
  return static_cast<int32_t>((id << 36) >> 40);
}

// Function to extract Y from the id
int32_t getY(int64_t id)
{
  return static_cast<int32_t>((id << 4) >> 56);
}

// Function to extract Z from the id
int32_t getZ(int64_t id)
{
  return static_cast<int32_t>((id << 12) >> 40);
}

bool isAir(int64_t id)
{
  return ((id >> 27) & ((1 << 20) - 1)) == 0;
}

int32_t getBlockId(int64_t id)
{
  return static_cast<int32_t>((id >> 27) & ((1 << 20) - 1));
}

int32_t getBiomeId(int64_t id)
{
  return static_cast<int32_t>((id >> 47) & 0x1FF);
}

int32_t getLightId(int64_t id)
{
  return static_cast<int32_t>((id >> 56) & 0xFF);
}
int64_t withLight(int64_t id, int32_t light)
{
  return (id & ~(0xFFLL << 56)) | (static_cast<int64_t>(light) << 56);
}

void RocksDBHandler::processWorldSections()
{
  rocksdb::Iterator *it_world_sections =
      db->NewIterator(rocksdb::ReadOptions(), handles[1]);
  for (it_world_sections->SeekToFirst(); it_world_sections->Valid();
       it_world_sections->Next())
  {
    rocksdb::Slice key = it_world_sections->key();
    // std::cout << key.ToString(true) << std::endl;
    // exit(-1);
    std::string compressed_value = it_world_sections->value().ToString();
    std::string decompressed_value;

    try
    {
      decompressed_value = decompressZSTD(compressed_value);
    }
    catch (const std::runtime_error &e)
    {
      std::cerr << "Decompression error: " << e.what() << std::endl;
      continue;
    }

    int64_t id;
    // std::cout << key.size() << " " << sizeof(int64_t) << std::endl;
    std::memcpy(&id, key.data(), key.size());
    int64_t bswap_id = __builtin_bswap64(id);
    // std::cout << "Key: " << id << std::endl;
    // 64 bits = 8 bytes * 8
    // id = __builtin_bswap64(id); // Adjust byte order

    WorldSection section;
    section.data.resize(1 << 15);
    section.key = bswap_id;

    if (deserialize(section, decompressed_value, false))
    {
      std::cout << "World Section Deserialized Successfully:" << std::endl;
      std::cout << "Key: " << bswap_id << std::endl;
      // id = __builtin_bswap64(id);
      // id = 0x90000010000170;
      std::cout << "World " << getLevel(bswap_id) << ": x=" << getX(bswap_id) << ", y=" << getY(bswap_id) << ", z=" << getZ(bswap_id) << std::endl;
      for (int64_t blockIdMaybe : section.data)
      {
        // std::cout << blockIdMaybe << std::endl;
        if (!isAir(blockIdMaybe))
        { // why use isAir if id is always 0 if its air :skull:
          std::cout << blockIdMaybe << " is " << getBlockId(blockIdMaybe) << std::endl;
        }
      }
      // Access section.data here
      //   writeVectorToFile(section.data, "test.dat");
      //   exit(-1);
    }
  }

  delete it_world_sections;
}

int32_t bytesToInt(const std::string &i)
{
  return (static_cast<int32_t>(static_cast<uint8_t>(i[0])) << 24) |
         (static_cast<int32_t>(static_cast<uint8_t>(i[1])) << 16) |
         (static_cast<int32_t>(static_cast<uint8_t>(i[2])) << 8) |
         static_cast<int32_t>(static_cast<uint8_t>(i[3]));
}

void RocksDBHandler::readIdMappings()
{
  std::cout << "\nReading from id_mappings:" << std::endl;
  rocksdb::Iterator *it_id_mappings =
      db->NewIterator(rocksdb::ReadOptions(), handles[2]);
  for (it_id_mappings->SeekToFirst(); it_id_mappings->Valid();
       it_id_mappings->Next())
  {
    rocksdb::Slice key = it_id_mappings->key();
    rocksdb::Slice value = it_id_mappings->value();
    // value is an nbt thingo
    // writeBufferToFile(value.data(), value.size(), "skibidi.dat");
    // exit(-1);

    int32_t id;
    // int32_t modelId;
    // nbt::io::stream_reader;
    // std::cout << value.size() << " " << sizeof(int32_t) << std::endl; // = 8
    id = bytesToInt(key.ToString());
    // std::memcpy(&id, key.data(), key.size());
    // int32_t bswap_id = __builtin_bswap32(id);

    // std::memcpy(&modelId, value.data(), value.size());
    // int32_t bswap_modelId = __builtin_bswap32(modelId);

    int32_t entryType = id >> 30;
    int32_t blockId = id & ((1 << 30) - 1);
    if (entryType == 1) // if its a block, 2 when biome, don't really care about biomes atm since we have the seed
    {
      std::cout << "Key: " << blockId << " Value: " << value.ToString(true) << std::endl;
    }
    // std::cout << "Key: " << id << " Value: " << value.ToString(true) << std::endl;
  }
  delete it_id_mappings;
}

int main()
{
  // int64_t id = 0x0010000070000170;
  // id = __builtin_bswap64(id);
  // std::cout << "World " << getLevel(id) << ": x=" << getX(id) << ", y=" << getY(id) << ", z=" << getZ(id) << std::endl;
  // SHOULD be 0, 23, 9, 1
  // exit(-1);
  try
  {
    RocksDBHandler dbHandler(
        // "/home/edward_wong/.local/share/PrismLauncher/instances/voxy testing/minecraft/saves/debug/voxy/4f2f217931c68a362682a81467b1c221/storage/"
        "../.voxy/saves/89.168.27.174/0ede3c4bcf2e01e7d3e7fc317625ccae/storage/");
    // dbHandler.processWorldSections();
    dbHandler.readIdMappings();
  }
  catch (const std::exception &e)
  {
    std::cerr << "Error: " << e.what() << std::endl;
  }

  return 0;
}
