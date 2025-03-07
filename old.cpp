#include <bit>
#include <cstring>
#include <filesystem>
#include <fstream>
#include <iomanip>
#include <iostream>
#include <nbt_tags.h>
#include <rocksdb/db.h>
#include <rocksdb/options.h>
#include <sstream>
#include <stdexcept>
#include <vector>
#include <zlib.h>
#include <zstd.h>
#include <io/ozlibstream.h>
#include <format>
#include <thread>

struct WorldSection
{
  int64_t key;
  std::vector<int64_t> data;
};

std::vector<nbt::tag_compound> mappings(1 << 20);
std::mutex io_mutex;

class RocksDBHandler
{
public:
  RocksDBHandler(const std::string &db_path);
  ~RocksDBHandler();

  void processWorldSection(const rocksdb::Slice key, const rocksdb::Slice compressed_value);
  void processWorldSections();
  void readIdMappings();

private:
  rocksdb::DB *db;
  std::vector<rocksdb::ColumnFamilyHandle *> handles;

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
  std::cout << "Opening " << handles.size() << " column families" << std::endl;
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

std::string decompressZSTD(const std::string &compressed_data)
{
  std::size_t decompressed_size =
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
  std::size_t result =
      ZSTD_decompress(&decompressed_data[0], decompressed_size,
                      compressed_data.data(), compressed_data.size());
  if (ZSTD_isError(result))
  {
    throw std::runtime_error("Error during decompression: " +
                             std::string(ZSTD_getErrorName(result)));
  }

  return decompressed_data;
}

std::string decompressGzip(const rocksdb::Slice &compressed_data)
{
  const int CHUNK_SIZE = 4096;
  z_stream zs;                // zlib stream
  memset(&zs, 0, sizeof(zs)); // Zero out the zlib stream

  // Initialize the decompression with gzip header handling
  if (inflateInit2(&zs, 16 + MAX_WBITS) != Z_OK)
  {
    throw std::runtime_error("inflateInit2 failed while decompressing.");
  }

  zs.next_in =
      reinterpret_cast<Bytef *>(const_cast<char *>(compressed_data.data()));
  zs.avail_in = compressed_data.size();

  std::string decompressed_data; // String to hold the decompressed data
  char outBuffer[CHUNK_SIZE];    // Buffer for decompression output

  int ret;

  // Decompression loop
  do
  {
    zs.next_out = reinterpret_cast<Bytef *>(outBuffer);
    zs.avail_out = CHUNK_SIZE;

    ret = inflate(&zs, Z_NO_FLUSH);

    // Check for errors during decompression
    if (ret == Z_STREAM_ERROR || ret == Z_DATA_ERROR || ret == Z_MEM_ERROR)
    {
      inflateEnd(&zs); // Clean up
      throw std::runtime_error("Error during zlib decompression.");
    }

    // Append the output from the buffer to the decompressed string
    std::size_t have = CHUNK_SIZE - zs.avail_out; // Amount of data produced
    decompressed_data.append(outBuffer, have);    // Append to the string

  } while (ret != Z_STREAM_END);

  // Clean up
  inflateEnd(&zs);

  return decompressed_data; // Return the decompressed data as std::string
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
    std::lock_guard<std::mutex> lock(io_mutex);
    std::cerr << "Decompressed section key mismatch. Got: " << key
              << ", Expected: " << section.key << std::endl;
    return false;
  }

  // Read LUT length
  int32_t lutLen = readFromBuffer<int32_t>(buffer);
  // std::cout << "lutlen: " << lutLen << std::endl;
  std::vector<int64_t> lut(lutLen);
  // Hash calculation for key
  hash = key ^ (lutLen * 1293481298141L);

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
    // std::cout << lutId << std::endl;
    section.data[i] = lut[lutId];

    // Continue hashing
    hash *= 1230987149811L;
    hash += 12831;
    hash ^= (lutId * 1827631L) ^ section.data[i];
    // std::cout << section.data[i] << std::endl;
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

int32_t getLevel(int64_t id) { return static_cast<int32_t>((id >> 60) & 0xF); }

int32_t getX(int64_t id) { return static_cast<int32_t>((id << 36) >> 40); }

// Function to extract Y from the id
int32_t getY(int64_t id) { return static_cast<int32_t>((id << 4) >> 56); }

// Function to extract Z from the id
int32_t getZ(int64_t id) { return static_cast<int32_t>((id << 12) >> 40); }

bool isAir(int64_t id) { return ((id >> 27) & ((1 << 20) - 1)) == 0; }

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

int32_t getIndex(int32_t x, int32_t y, int32_t z)
{
  const int32_t M = (1 << 5) - 1; // Mask for the lowest 5 bits
  if (x < 0 || x > M || y < 0 || y > M || z < 0 || z > M)
  {
    throw std::invalid_argument("Out of bounds: " + std::to_string(x) + ", " +
                                std::to_string(y) + ", " + std::to_string(z));
  }
  return ((y & M) << 10) | ((z & M) << 5) | (x & M);
}

std::tuple<int, int, int> getCoordinates(int index)
{
  int M = (1 << 5) - 1; // Same M value as in getIndex

  // Extract x, y, z from the index using bitwise operations
  int x = index & M;         // Get the last 5 bits
  int y = (index >> 10) & M; // Get bits 10 to 14
  int z = (index >> 5) & M;  // Get bits 5 to 9

  // Check bounds for x, y, z
  if (x < 0 || x > M || y < 0 || y > M || z < 0 || z > M)
  {
    throw std::out_of_range("Out of bounds: " + std::to_string(x) + ", " +
                            std::to_string(y) + ", " + std::to_string(z));
  }

  return std::make_tuple(x, y, z);
}

void writeVectorToFile(const std::vector<long> &vec,
                       const std::string &filename)
{
  std::ofstream outFile(filename, std::ios::binary); // Open file in binary mode

  if (!outFile.is_open())
  {
    std::cerr << "Error opening file: " << filename << std::endl;
    return;
  }

  // Write the size of the vector first (optional, helps in reading later)
  std::size_t vectorSize = vec.size();
  outFile.write(reinterpret_cast<const char *>(&vectorSize),
                sizeof(vectorSize));

  // Write the raw binary data of the vector to the file
  outFile.write(reinterpret_cast<const char *>(vec.data()),
                vec.size() * sizeof(long));

  outFile.close(); // Close the file
}

// dont really want to edit the libnbtplusplus library

template <typename T>
nbt::tag_list vectorToNbtList(const std::vector<T> &vec)
{
  nbt::tag_list nbtList;

  for (const auto &item : vec)
  {
    nbtList.emplace_back<T>(
        item); // Using emplace_back to add each element to the tag_list
  }

  return nbtList;
}

nbt::tag_long_array vectorOfLongsToNbtArray(const std::vector<int64_t> &vec)
{
  nbt::tag_long_array nbtArray;

  for (const auto &item : vec)
  {
    nbtArray.push_back(item); // Using emplace_back to add each element to the tag_list
  }
  // std::cout << nbtArray << std::endl;
  return nbtArray;
}

void RocksDBHandler::processWorldSection(const rocksdb::Slice key, const rocksdb::Slice compressed_value)
{
  // int i = 0;

  // i++;
  // rocksdb::Slice key = it_world_sections->key();
  // std::cout << key.ToString(true) << std::endl;
  // exit(-1);
  // std::string compressed_data = it_world_sections->value().ToString();
  std::string decompressed_value;

  try
  {
    decompressed_value = decompressZSTD(compressed_value.ToString());
  }
  catch (const std::runtime_error &e)
  {
    std::lock_guard<std::mutex> lock(io_mutex);
    std::cerr << "Decompression error: " << e.what() << std::endl;
    return;
    // continue;
  }

  int64_t id;
  // std::cout << key.size() << " " << sizeof(int64_t) << std::endl;
  std::memcpy(&id, key.data(), key.size());
  int64_t bswap_id = __builtin_bswap64(id);
  // std::cout << "Key: " << id << std::endl;
  // 64 bits = 8 bytes * 8
  // id = __builtin_bswap64(id); // Adjust byte order
  // only interested in zeroeth lod
  if (getLevel(bswap_id) == 0)
  {
    // std::cout << "World " << getLevel(bswap_id) << ": x=" << getX(bswap_id) << ", y=" << getY(bswap_id) << ", z=" << getZ(bswap_id) << std::endl;
    WorldSection section;
    section.data.resize(1 << 15);
    section.key = bswap_id;

    nbt::tag_compound new_chunk_data;

    new_chunk_data.put("DataVersion", 3953);
    new_chunk_data.put(
        "Heightmaps",
        nbt::tag_compound{
            std::pair<std::string, nbt::tag_long_array>(
                "MOTION_BLOCKING",
                {65, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0,
                 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0}),
            std::pair<std::string, nbt::tag_long_array>(
                "MOTION_BLOCKING_NO_LEAVES",
                {65, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0,
                 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0}),
            std::pair<std::string, nbt::tag_long_array>(
                "OCEAN_FLOOR",
                {65, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0,
                 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0}),
            std::pair<std::string, nbt::tag_long_array>(
                "WORLD_SURFACE",
                {65, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0,
                 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0})});
    new_chunk_data.put("InhabitedTime", 1081l);
    new_chunk_data.put("LastUpdate", 4036l);
    new_chunk_data.put(
        "PostProcessing",
        nbt::tag_list({nbt::tag_list(), nbt::tag_list(), nbt::tag_list(),
                       nbt::tag_list(), nbt::tag_list(), nbt::tag_list(),
                       nbt::tag_list(), nbt::tag_list(), nbt::tag_list(),
                       nbt::tag_list(), nbt::tag_list(), nbt::tag_list(),
                       nbt::tag_list(), nbt::tag_list(), nbt::tag_list(),
                       nbt::tag_list(), nbt::tag_list(), nbt::tag_list(),
                       nbt::tag_list(), nbt::tag_list(), nbt::tag_list(),
                       nbt::tag_list(), nbt::tag_list(), nbt::tag_list()}));
    new_chunk_data.put("Status", "minecraft:full");
    new_chunk_data.put("block_entities", nbt::tag_list());
    new_chunk_data.put("block_ticks", nbt::tag_list());
    new_chunk_data.put("fluid_ticks", nbt::tag_list());
    new_chunk_data.put("isLightOn", nbt::tag_byte(true));
    new_chunk_data.put(
        "structures",
        nbt::tag_compound{std::pair<std::string, nbt::tag_compound>(
                              "References", nbt::tag_compound()),
                          std::pair<std::string, nbt::tag_compound>(
                              "starts", nbt::tag_compound())});
    new_chunk_data.put("xPos", getX(bswap_id));
    new_chunk_data.put("zPos", getZ(bswap_id));
    // hardcoded to 1.18 level
    new_chunk_data.put("yPos", -4);
    new_chunk_data.put("sections", nbt::tag_list());
    if (deserialize(section, decompressed_value, false))
    {
      // std::cout << section.data.size() << std::endl;

      // ignore BlockLight and SkyLight, we can recalc those once we load in
      for (int i = 0; i < 8; i++)
      {
        // std::cout << getY(bswap_id);
        new_chunk_data.at("sections").as<nbt::tag_list>().push_back({nbt::tag_compound{std::pair<std::string, nbt::tag_byte>("Y", nbt::tag_byte(getY(bswap_id) + i)), std::pair<std::string, nbt::tag_compound>("biomes", nbt::tag_compound{std::pair<std::string, nbt::tag_list>("palette", {"minecraft:plains"})}), std::pair<std::string, nbt::tag_compound>("block_states", nbt::tag_compound{std::pair<std::string, nbt::tag_list>("palette", vectorToNbtList(mappings)), std::pair<std::string, nbt::tag_long_array>("data", vectorOfLongsToNbtArray(std::vector<int64_t>(i * 4096, (i + 1) * 4096)))})}});
      }
      // std::cout << new_chunk_data << std::endl;

      // save chunk data with compression
      std::ostringstream oss;
      oss << "saves/" << getX(bswap_id) << "." << getZ(bswap_id) << ".mca";
      std::ofstream output_file(oss.str(), std::ios_base::binary);
      zlib::ozlibstream zlib_out(output_file);

      if (!output_file.is_open())
      {
        std::cerr << "Failed to open output file.\n";
        // return 1;
      }

      nbt::io::stream_writer writer(zlib_out);

      new_chunk_data.write_payload(writer);
      zlib_out.close();
      output_file.close();
    }
  }

  // exit(-1);

  // std::cout << "World Section Deserialized Successfully:" << std::endl;
  // std::cout << "Key: " << bswap_id << std::endl;
  // id = __builtin_bswap64(id);
  // id = 0x90000010000170;
  // std::cout << "World " << getLevel(bswap_id) << ": x=" << getX(bswap_id)
  // << ", y=" << getY(bswap_id) << ", z=" << getZ(bswap_id) << std::endl;

  // if (getLevel(bswap_id) == 0) // we are only interested in the zeroth LOD,
  //                              // idk why its stored in getLevel
  // {
  //   for (int x = 0; x < 32; x++)
  //   {
  //     for (int y = 0; y < 32; y++)
  //     {
  //       for (int z = 0; z < 32; z++)
  //       {
  //         // x, y, z, selfBlockId, key
  //         // should be 0, 0, 16, 2, 4503599358935040
  //         // std::cout << getIndex(0, 1, 16) << std::endl;
  //         // std::cout << section.data[getIndex(x, y, z)] << std::endl;
  //         // std::cout << mappings[section.data[getIndex(0, 1, 16)]] <<
  //         // std::endl;

  //         // filter out air

  //         int64_t bid = getBlockId(section.data[getIndex(x, y, z)]);

  //         if (bid != 0)
  //         {
  //           // std::cout << "World " << getLevel(bswap_id) << ": x=" <<
  //           // getX(bswap_id) * 32 + x << ", y=" << getY(bswap_id) * 32 + y
  //           // << ", z=" << getZ(bswap_id) * 32 + z << std::endl; std::cout
  //           // << "x: " << x << " y: " << y << " z: " << z << std::endl;
  //           // std::cout << section.data[getIndex(x, y, z)] << std::endl;
  //           // std::cout << bid << std::endl;
  //           // std::cout << section.data[getIndex(x, y, z)] << std::endl;
  //           // std::cout << mappings[bid] << std::endl;
  //           nbt::tag_compound &block =
  //               mappings[bid].as<nbt::tag_compound>();
  //           // std::cout << block.at("Name") << std::endl;
  //           // std::cout << block << std::endl;

  //           // mappings

  //           // std::cout << "{";

  //           // std::cout << '"' << "x" << '"' << ":" << getX(bswap_id) * 32
  //           // + x << ","; std::cout << '"' << "y" << '"' << ":" <<
  //           // getY(bswap_id) * 32 + y << ","; std::cout << '"' << "z" <<
  //           // '"' << ":" << getZ(bswap_id) * 32 + z << ",";

  //           // for (const auto &kv : block)
  //           // {
  //           //   if (kv.first == "Properties")
  //           //   {
  //           //     std::cout << '"' << "Properties" << '"' << ":{";
  //           //     const auto &properties =
  //           //     block.at("Properties").as<nbt::tag_compound>(); auto it =
  //           //     properties.begin(); while (it != properties.end())
  //           //     {
  //           //       std::cout << '"' << it->first << '"' << ":" <<
  //           //       it->second;
  //           //       ++it;
  //           //       if (it != properties.end())
  //           //       {
  //           //         std::cout << ",";
  //           //       }
  //           //       // std::cout << std::endl;
  //           //     }
  //           //     std::cout << "}"; // No comma here, we will handle it
  //           //     later
  //           //   }
  //           //   else
  //           //   {
  //           //     std::cout << '"' << kv.first << '"' << ":" << kv.second;
  //           //   }

  //           //   // Add a comma if there are more elements or properties
  //           //   if (kv.first != "Properties" &&
  //           //   block.has_key("Properties"))
  //           //   {
  //           //     std::cout << ",";
  //           //   }
  //           // }

  //           // // Remove trailing comma after the last element
  //           // std::cout << "}" << std::endl;
  // }
  // }
  // }

  // delete it_world_sections;
}

void RocksDBHandler::processWorldSections()
{
  rocksdb::Iterator *it_world_sections = db->NewIterator(rocksdb::ReadOptions(), handles[1]);
  std::vector<std::thread> workers;

  // Iterate over world sections
  for (it_world_sections->SeekToFirst(); it_world_sections->Valid(); it_world_sections->Next())
  {
    rocksdb::Slice key = it_world_sections->key();
    rocksdb::Slice compressed_value = it_world_sections->value();
    // std::string compressed_value = it_world_sections->value().ToString();

    // Create a thread for each world section
    workers.emplace_back(&RocksDBHandler::processWorldSection, this, key, compressed_value);
  }

  // Wait for all threads to finish
  for (auto &worker : workers)
  {
    if (worker.joinable())
    {
      worker.join();
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
  std::cout << "Reading from id_mappings:" << std::endl;
  rocksdb::Iterator *it_id_mappings =
      db->NewIterator(rocksdb::ReadOptions(), handles[2]);
  // int i = 0;
  std::size_t total_blocks = 0;

  // set initial air block
  mappings[0] = nbt::tag_compound({std::pair<std::string, nbt::tag_string>("Name", nbt::tag_string("minecraft:air"))});

  for (it_id_mappings->SeekToFirst(); it_id_mappings->Valid();
       it_id_mappings->Next())
  {
    // i++;
    rocksdb::Slice key = it_id_mappings->key();
    rocksdb::Slice value = it_id_mappings->value();
    // value is an nbt thingo
    // writeBufferToFile(value.data(), value.size(), "skibidi.dat");
    // exit(-1);

    // int32_t id;
    // std::cout << key.size() << std::endl;
    // memcpy(&id, key.data(), key.size());
    // id = __builtin_bswap32(id);
    int32_t id = bytesToInt(key.ToString());
    // id = __builtin_bswap32(id);
    // std::cout << id << std::endl;
    // doesnt need to be byteswapped because cortex uses some weird ass
    // conversion id = bytesToInt(key.ToString());

    int32_t entryType = id >> 30;
    int32_t blockId = id & ((1 << 30) - 1);
    // std::cout << entryType << std::endl;
    if (entryType == 1) // if its a block, 2 when biome, don't really care about
                        // biomes atm since we have the seed, this is FUCKED up
                        // !, it is sometimes -2 for some god awful reason
    {
      // value.ToString
      // std::cout << decompressGzip(value) << std::endl;
      std::istringstream is(decompressGzip(value));
      nbt::io::stream_reader reader(is);
      auto pair = nbt::io::read_compound(is);
      if (pair.second->at("id") != nbt::tag_int(blockId))
      {
        std::cerr << "Mapping ID doesn't match NBT" << std::endl;
      }
      else
      {
        // std::cout << id << std::endl;
        // std::cout << __builtin_bswap32(id) << std::endl;
        // std::cout << blockId << std::endl;
        // std::cout << pair.second->at("block_state") << std::endl;
        mappings[blockId] =
            pair.second->at("block_state").as<nbt::tag_compound>();
        total_blocks++;
        // std::cout << "Key: " << blockId << std::endl;
      }
    }
    // std::cout << "Key: " << id << " Value: " << value.ToString(true) <<
    // std::endl;
  }
  // std::cout << i << std::endl;
  delete it_id_mappings;
}

int main()
{
  // int64_t index = getIndex(10, 10, 10);
  // std::cout << index << std::endl;
  // std::tuple<int, int, int> coords = getCoordinates(index);
  // std::cout << std::get<0>(coords) << ", " << std::get<1>(coords) << ", " <<
  // std::get<2>(coords) << std::endl; exit(-1); int64_t id =
  // 0x0010000070000170; id = __builtin_bswap64(id);

  // std::cout << "World " << getLevel(id) << ": x=" << getX(id) << ", y=" <<
  // getY(id) << ", z=" << getZ(id) << std::endl; SHOULD be 0, 23, 9, 1
  // exit(-1);
  try
  {
    // std::string basePath = "../.voxy/saves/89.168.27.174/";
    std::string basePath = "/home/edward_wong/.local/share/PrismLauncher/"
                           "instances/voxy testing/minecraft/saves/empty/voxy/";

    // Iterate through all directories in the base path
    for (const auto &entry : std::filesystem::directory_iterator(basePath))
    {
      if (std::filesystem::is_directory(entry))
      {
        std::string storagePath = entry.path().string() + "/storage/";
        // std::string storagePath = "../.voxy/saves/89.168.27.174/9f24721cf6af1d30bacc19de8c77a9b6/storage/";

        // Create a RocksDBHandler for each folder
        RocksDBHandler dbHandler(storagePath);

        // Read ID mappings and process world sections
        dbHandler.readIdMappings();
        dbHandler.processWorldSections();
      }
    }
    // RocksDBHandler dbHandler(
    //     // "/home/edward_wong/.local/share/PrismLauncher/instances/voxy
    //     testing/minecraft/saves/empty/voxy/5e08f4cd49c6e5fae140ae6a9bc4228f/storage/");
    // "../.voxy/saves/89.168.27.174/8c3bc415b64e63ed06e5296642cddd71/storage/");

    // dbHandler.readIdMappings();
    // std::cout << mappings.size() << std::endl;
    // exit(-1);
    // dbHandler.processWorldSections();
    // dbHandler.readIdMappings();
  }
  catch (const std::exception &e)
  {
    std::cerr << "Error: " << e.what() << std::endl;
  }

  return 0;
}
