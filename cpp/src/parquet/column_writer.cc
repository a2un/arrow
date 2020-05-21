// Licensed to the Apache Software Foundation (ASF) under one
// or more contributor license agreements.  See the NOTICE file
// distributed with this work for additional information
// regarding copyright ownership.  The ASF licenses this file
// to you under the Apache License, Version 2.0 (the
// "License"); you may not use this file except in compliance
// with the License.  You may obtain a copy of the License at
//
//   http://www.apache.org/licenses/LICENSE-2.0
//
// Unless required by applicable law or agreed to in writing,
// software distributed under the License is distributed on an
// "AS IS" BASIS, WITHOUT WARRANTIES OR CONDITIONS OF ANY
// KIND, either express or implied.  See the License for the
// specific language governing permissions and limitations
// under the License.

#include "parquet/column_writer.h"

#include <algorithm>
#include <cstdint>
#include <cstring>
#include <memory>
#include <utility>
#include <vector>
#include <sstream>

#include "arrow/array.h"
#include "arrow/buffer-builder.h"
#include "arrow/type.h"
#include "arrow/type_traits.h"
#include "arrow/util/bit-stream-utils.h"
#include "arrow/util/checked_cast.h"
#include "arrow/util/compression.h"
#include "arrow/util/logging.h"
#include "arrow/util/rle-encoding.h"

#include "parquet/column_page.h"
#include "parquet/encoding.h"
#include "parquet/metadata.h"
#include "parquet/platform.h"
#include "parquet/properties.h"
#include "parquet/schema.h"
#include "parquet/statistics.h"
#include "parquet/thrift.h"
#include "parquet/types.h"

namespace parquet {

using ::arrow::Status;
using ::arrow::internal::checked_cast;

using BitWriter = ::arrow::BitUtil::BitWriter;
using RleEncoder = ::arrow::util::RleEncoder;

LevelEncoder::LevelEncoder() {}
LevelEncoder::~LevelEncoder() {}

void LevelEncoder::Init(Encoding::type encoding, int16_t max_level,
                        int num_buffered_values, uint8_t* data, int data_size) {
  bit_width_ = BitUtil::Log2(max_level + 1);
  encoding_ = encoding;
  switch (encoding) {
    case Encoding::RLE: {
      rle_encoder_.reset(new RleEncoder(data, data_size, bit_width_));
      break;
    }
    case Encoding::BIT_PACKED: {
      int num_bytes =
          static_cast<int>(BitUtil::BytesForBits(num_buffered_values * bit_width_));
      bit_packed_encoder_.reset(new BitWriter(data, num_bytes));
      break;
    }
    default:
      throw ParquetException("Unknown encoding type for levels.");
  }
}

int LevelEncoder::MaxBufferSize(Encoding::type encoding, int16_t max_level,
                                int num_buffered_values) {
  int bit_width = BitUtil::Log2(max_level + 1);
  int num_bytes = 0;
  switch (encoding) {
    case Encoding::RLE: {
      // TODO: Due to the way we currently check if the buffer is full enough,
      // we need to have MinBufferSize as head room.
      num_bytes = RleEncoder::MaxBufferSize(bit_width, num_buffered_values) +
                  RleEncoder::MinBufferSize(bit_width);
      break;
    }
    case Encoding::BIT_PACKED: {
      num_bytes =
          static_cast<int>(BitUtil::BytesForBits(num_buffered_values * bit_width));
      break;
    }
    default:
      throw ParquetException("Unknown encoding type for levels.");
  }
  return num_bytes;
}

int LevelEncoder::Encode(int batch_size, const int16_t* levels) {
  int num_encoded = 0;
  if (!rle_encoder_ && !bit_packed_encoder_) {
    throw ParquetException("Level encoders are not initialized.");
  }

  if (encoding_ == Encoding::RLE) {
    for (int i = 0; i < batch_size; ++i) {
      if (!rle_encoder_->Put(*(levels + i))) {
        break;
      }
      ++num_encoded;
    }
    rle_encoder_->Flush();
    rle_length_ = rle_encoder_->len();
  } else {
    for (int i = 0; i < batch_size; ++i) {
      if (!bit_packed_encoder_->PutValue(*(levels + i), bit_width_)) {
        break;
      }
      ++num_encoded;
    }
    bit_packed_encoder_->Flush();
  }
  return num_encoded;
}

// ----------------------------------------------------------------------
// PageWriter implementation

// This subclass delimits pages appearing in a serialized stream, each preceded
// by a serialized Thrift format::PageHeader indicating the type of each page
// and the page metadata.
class SerializedPageWriter : public PageWriter {
 public:
  SerializedPageWriter(const std::shared_ptr<ArrowOutputStream>& sink,
                       Compression::type codec, ColumnChunkMetaDataBuilder* metadata,
                       MemoryPool* pool = ::arrow::default_memory_pool())
      : sink_(sink),
        metadata_(metadata),
        pool_(pool),
        num_values_(0),
        dictionary_page_offset_(0),
        data_page_offset_(0),
        total_uncompressed_size_(0),
        total_compressed_size_(0) {
    compressor_ = GetCodecFromArrow(codec);
    thrift_serializer_.reset(new ThriftSerializer);
    current_page_row_set_index = 0;
  }

  int64_t WriteDictionaryPage(const DictionaryPage& page) override {
    int64_t uncompressed_size = page.size();
    std::shared_ptr<Buffer> compressed_data = nullptr;
    if (has_compressor()) {
      auto buffer = std::static_pointer_cast<ResizableBuffer>(
          AllocateBuffer(pool_, uncompressed_size));
      Compress(*(page.buffer().get()), buffer.get());
      compressed_data = std::static_pointer_cast<Buffer>(buffer);
    } else {
      compressed_data = page.buffer();
    }

    format::DictionaryPageHeader dict_page_header;
    dict_page_header.__set_num_values(page.num_values());
    dict_page_header.__set_encoding(ToThrift(page.encoding()));
    dict_page_header.__set_is_sorted(page.is_sorted());

    format::PageHeader page_header;
    page_header.__set_type(format::PageType::DICTIONARY_PAGE);
    page_header.__set_uncompressed_page_size(static_cast<int32_t>(uncompressed_size));
    page_header.__set_compressed_page_size(static_cast<int32_t>(compressed_data->size()));
    page_header.__set_dictionary_page_header(dict_page_header);
    // TODO(PARQUET-594) crc checksum

    int64_t start_pos = -1;
    PARQUET_THROW_NOT_OK(sink_->Tell(&start_pos));
    if (dictionary_page_offset_ == 0) {
      dictionary_page_offset_ = start_pos;
    }
    int64_t header_size = thrift_serializer_->Serialize(&page_header, sink_.get());
    PARQUET_THROW_NOT_OK(sink_->Write(compressed_data->data(), compressed_data->size()));

    total_uncompressed_size_ += uncompressed_size + header_size;
    total_compressed_size_ += compressed_data->size() + header_size;

    int64_t final_pos = -1;
    PARQUET_THROW_NOT_OK(sink_->Tell(&final_pos));
    return final_pos - start_pos;
  }

  void Close(bool has_dictionary, bool fallback) override {
    // index_page_offset = -1 since they are not supported
    metadata_->Finish(num_values_, dictionary_page_offset_, -1, data_page_offset_,
                      total_compressed_size_, total_uncompressed_size_, has_dictionary,
                      fallback);

    // Write metadata at end of column chunk
    metadata_->WriteTo(sink_.get());
  }

  /**
   * Compress a buffer.
   */
  void Compress(const Buffer& src_buffer, ResizableBuffer* dest_buffer) override {
    DCHECK(compressor_ != nullptr);

    // Compress the data
    int64_t max_compressed_size =
        compressor_->MaxCompressedLen(src_buffer.size(), src_buffer.data());

    // Use Arrow::Buffer::shrink_to_fit = false
    // underlying buffer only keeps growing. Resize to a smaller size does not reallocate.
    PARQUET_THROW_NOT_OK(dest_buffer->Resize(max_compressed_size, false));

    int64_t compressed_size;
    PARQUET_THROW_NOT_OK(
        compressor_->Compress(src_buffer.size(), src_buffer.data(), max_compressed_size,
                              dest_buffer->mutable_data(), &compressed_size));
    PARQUET_THROW_NOT_OK(dest_buffer->Resize(compressed_size, false));
  }

  int64_t WriteDataPage(const CompressedDataPage& page) override {
    int64_t uncompressed_size = page.uncompressed_size();
    std::shared_ptr<Buffer> compressed_data = page.buffer();

    format::DataPageHeader data_page_header;
    data_page_header.__set_num_values(page.num_values());
    data_page_header.__set_encoding(ToThrift(page.encoding()));
    data_page_header.__set_definition_level_encoding(
        ToThrift(page.definition_level_encoding()));
    data_page_header.__set_repetition_level_encoding(
        ToThrift(page.repetition_level_encoding()));
    data_page_header.__set_statistics(ToThrift(page.statistics()));

    format::PageHeader page_header;
    page_header.__set_type(format::PageType::DATA_PAGE);
    page_header.__set_uncompressed_page_size(static_cast<int32_t>(uncompressed_size));
    page_header.__set_compressed_page_size(static_cast<int32_t>(compressed_data->size()));
    page_header.__set_data_page_header(data_page_header);
    // TODO(PARQUET-594) crc checksum

    int64_t start_pos = -1;
    PARQUET_THROW_NOT_OK(sink_->Tell(&start_pos));
    if (data_page_offset_ == 0) {
      data_page_offset_ = start_pos;
    }

    int64_t header_size = thrift_serializer_->Serialize(&page_header, sink_.get());
    PARQUET_THROW_NOT_OK(sink_->Write(compressed_data->data(), compressed_data->size()));

    total_uncompressed_size_ += uncompressed_size + header_size;
    total_compressed_size_ += compressed_data->size() + header_size;
    num_values_ += page.num_values();

    int64_t current_pos = -1;
    PARQUET_THROW_NOT_OK(sink_->Tell(&current_pos));
    return current_pos - start_pos;
  }

  int64_t WriteDataPagesWithIndex(const CompressedDataPage& page, format::PageLocation& ploc) override {
    int64_t uncompressed_size = page.uncompressed_size();
    std::shared_ptr<Buffer> compressed_data = page.buffer();

    format::DataPageHeader data_page_header;
    data_page_header.__set_num_values(page.num_values());
    data_page_header.__set_encoding(ToThrift(page.encoding()));
    data_page_header.__set_definition_level_encoding(
        ToThrift(page.definition_level_encoding()));
    data_page_header.__set_repetition_level_encoding(
        ToThrift(page.repetition_level_encoding()));
    data_page_header.__set_statistics(ToThrift(page.statistics()));

    format::PageHeader page_header;
    page_header.__set_type(format::PageType::DATA_PAGE);
    page_header.__set_uncompressed_page_size(static_cast<int32_t>(uncompressed_size));
    page_header.__set_compressed_page_size(static_cast<int32_t>(compressed_data->size()));
    page_header.__set_data_page_header(data_page_header);
    // TODO(PARQUET-594) crc checksum

    int64_t start_pos = -1;
    PARQUET_THROW_NOT_OK(sink_->Tell(&start_pos));
    if (data_page_offset_ == 0) {
      data_page_offset_ = start_pos;
    }

    int64_t header_size = thrift_serializer_->Serialize(&page_header, sink_.get());
    PARQUET_THROW_NOT_OK(sink_->Write(compressed_data->data(), compressed_data->size()));

    total_uncompressed_size_ += uncompressed_size + header_size;
    total_compressed_size_ += compressed_data->size() + header_size;
    num_values_ += page.num_values();

    int64_t current_pos = -1;
    PARQUET_THROW_NOT_OK(sink_->Tell(&current_pos));
    
    ploc.offset = start_pos;
    ploc.first_row_index = current_page_row_set_index;
    ploc.compressed_page_size = page_header.compressed_page_size + (current_pos - start_pos);
    current_page_row_set_index += page_header.data_page_header.num_values;

    return current_pos - start_pos;
}


  /* sample Adding ColumnIndex from chunk to offset.
  * Status HdfsParquetTableWriter::WritePageIndex() {
  if (!state_->query_options().parquet_write_page_index) return Status::OK();

  // Currently Impala only write Parquet files with a single row group. The current
  // page index logic depends on this behavior as it only keeps one row group's
  // statistics in memory.
  DCHECK_EQ(file_metadata_.row_groups.size(), 1);

  parquet::RowGroup* row_group = &(file_metadata_.row_groups[0]);
  // Write out the column indexes.
  for (int i = 0; i < columns_.size(); ++i) {
    auto& column = *columns_[i]; // column-writer
    if (!column.valid_column_index_) continue;
    column.column_index_.__set_boundary_order(
        column.row_group_stats_base_->GetBoundaryOrder());
    // We always set null_counts.
    column.column_index_.__isset.null_counts = true;
    uint8_t* buffer = nullptr;
    uint32_t len = 0;
    RETURN_IF_ERROR(thrift_serializer_->SerializeToBuffer(
        &column.column_index_, &len, &buffer));
    RETURN_IF_ERROR(Write(buffer, len));
    // Update the column_index_offset and column_index_length of the ColumnChunk
    row_group->columns[i].__set_column_index_offset(file_pos_);
    row_group->columns[i].__set_column_index_length(len);
    file_pos_ += len;
  }
  // Write out the offset indexes.
  for (int i = 0; i < columns_.size(); ++i) {
    auto& column = *columns_[i];  // column-writer
    uint8_t* buffer = nullptr;
    uint32_t len = 0;
    RETURN_IF_ERROR(thrift_serializer_->SerializeToBuffer(
        &column.offset_index_, &len, &buffer));
    RETURN_IF_ERROR(Write(buffer, len));
    // Update the offset_index_offset and offset_index_length of the ColumnChunk
    row_group->columns[i].__set_offset_index_offset(file_pos_);
    row_group->columns[i].__set_offset_index_length(len);
    file_pos_ += len;
  }
  return Status::OK();
}
  * 
  */

  void WriteIndex(int64_t& file_pos_, int64_t& ci_offset, int64_t& oi_offset, format::ColumnIndex& ci, format::OffsetIndex& oi) {
     // index_page_offset = -1 since they are not supported

    uint32_t ci_len, oi_len;
    uint8_t* buffer;
    
    thrift_serializer_->SerializeToBuffer(&ci,&ci_len,&buffer);
    sink_->Write(buffer,ci_len);
    thrift_serializer_->SerializeToBuffer(&oi,&oi_len,&buffer);
    sink_->Write(buffer,oi_len);

    if (oi_offset == 0 && ci_offset == 0) {
       oi_offset = ci_len;
    }

    metadata_->WriteIndex(file_pos_, ci_offset, oi_offset, ci_len, oi_len);

    ci_offset += ci_len;
    oi_offset += oi_len;
    // Write metadata at end of column chunk
    metadata_->WriteTo(sink_.get());
  }

  bool has_compressor() override { return (compressor_ != nullptr); }

  int64_t num_values() { return num_values_; }

  int64_t dictionary_page_offset() { return dictionary_page_offset_; }

  int64_t data_page_offset() { return data_page_offset_; }

  int64_t total_compressed_size() { return total_compressed_size_; }

  int64_t total_uncompressed_size() { return total_uncompressed_size_; }

 private:
  std::shared_ptr<ArrowOutputStream> sink_;
  ColumnChunkMetaDataBuilder* metadata_;
  MemoryPool* pool_;
  int64_t num_values_;
  int64_t dictionary_page_offset_;
  int64_t data_page_offset_;
  int64_t total_uncompressed_size_;
  int64_t total_compressed_size_;

  std::unique_ptr<ThriftSerializer> thrift_serializer_;

  // Compression codec to use.
  std::unique_ptr<::arrow::util::Codec> compressor_;

  BlockSplitBloomFilter blf;

};

// This implementation of the PageWriter writes to the final sink on Close .
class BufferedPageWriter : public PageWriter {
 public:
  BufferedPageWriter(const std::shared_ptr<ArrowOutputStream>& sink,
                     Compression::type codec, ColumnChunkMetaDataBuilder* metadata,
                     MemoryPool* pool = ::arrow::default_memory_pool())
      : final_sink_(sink), metadata_(metadata) {
    in_memory_sink_ = CreateOutputStream(pool);
    pager_ = std::unique_ptr<SerializedPageWriter>(
        new SerializedPageWriter(in_memory_sink_, codec, metadata, pool));
  }

  int64_t WriteDictionaryPage(const DictionaryPage& page) override {
    return pager_->WriteDictionaryPage(page);
  }

  void Close(bool has_dictionary, bool fallback) override {
    // index_page_offset = -1 since they are not supported
    int64_t final_position = -1;
    PARQUET_THROW_NOT_OK(final_sink_->Tell(&final_position));
    metadata_->Finish(
        pager_->num_values(), pager_->dictionary_page_offset() + final_position, -1,
        pager_->data_page_offset() + final_position, pager_->total_compressed_size(),
        pager_->total_uncompressed_size(), has_dictionary, fallback);

    // Write metadata at end of column chunk
    metadata_->WriteTo(in_memory_sink_.get());

    // flush everything to the serialized sink
    std::shared_ptr<Buffer> buffer;
    PARQUET_THROW_NOT_OK(in_memory_sink_->Finish(&buffer));
    PARQUET_THROW_NOT_OK(final_sink_->Write(buffer->data(), buffer->size()));
  }

  int64_t WriteDataPage(const CompressedDataPage& page) override {
    return pager_->WriteDataPage(page);
  }

  int64_t WriteDataPagesWithIndex(const parquet::CompressedDataPage &page, format::PageLocation& ploc) override {
      return pager_->WriteDataPagesWithIndex(page, ploc);
  }
  
  void WriteIndex(int64_t& file_pos_, int64_t& ci_offset, int64_t& oi_offset, format::ColumnIndex& ci, format::OffsetIndex& oi) {
      pager_->WriteIndex(file_pos_, ci_offset, oi_offset, ci, oi);
  }

  void Compress(const Buffer& src_buffer, ResizableBuffer* dest_buffer) override {
    pager_->Compress(src_buffer, dest_buffer);
  }

  bool has_compressor() override { return pager_->has_compressor(); }

 private:
  std::shared_ptr<ArrowOutputStream> final_sink_;
  ColumnChunkMetaDataBuilder* metadata_;
  std::shared_ptr<::arrow::io::BufferOutputStream> in_memory_sink_;
  std::unique_ptr<SerializedPageWriter> pager_;
};

std::unique_ptr<PageWriter> PageWriter::Open(
    const std::shared_ptr<ArrowOutputStream>& sink, Compression::type codec,
    ColumnChunkMetaDataBuilder* metadata, MemoryPool* pool, bool buffered_row_group) {
  if (buffered_row_group) {
    return std::unique_ptr<PageWriter>(
        new BufferedPageWriter(sink, codec, metadata, pool));
  } else {
    return std::unique_ptr<PageWriter>(
        new SerializedPageWriter(sink, codec, metadata, pool));
  }
}

// ----------------------------------------------------------------------
// ColumnWriter

std::shared_ptr<WriterProperties> default_writer_properties() {
  static std::shared_ptr<WriterProperties> default_writer_properties =
      WriterProperties::Builder().build();
  return default_writer_properties;
}

class ColumnWriterImpl {
 public:
  ColumnWriterImpl(ColumnChunkMetaDataBuilder* metadata,
                   std::unique_ptr<PageWriter> pager, const bool use_dictionary,
                   Encoding::type encoding, const WriterProperties* properties)
      : metadata_(metadata),
        descr_(metadata->descr()),
        pager_(std::move(pager)),
        has_dictionary_(use_dictionary),
        encoding_(encoding),
        properties_(properties),
        allocator_(properties->memory_pool()),
        num_buffered_values_(0),
        num_buffered_encoded_values_(0),
        rows_written_(0),
        total_bytes_written_(0),
        total_compressed_bytes_(0),
        closed_(false),
        fallback_(false),
        definition_levels_sink_(allocator_),
        repetition_levels_sink_(allocator_) {
    definition_levels_rle_ =
        std::static_pointer_cast<ResizableBuffer>(AllocateBuffer(allocator_, 0));
    repetition_levels_rle_ =
        std::static_pointer_cast<ResizableBuffer>(AllocateBuffer(allocator_, 0));
    uncompressed_data_ =
        std::static_pointer_cast<ResizableBuffer>(AllocateBuffer(allocator_, 0));
    if (pager_->has_compressor()) {
      compressed_data_ =
          std::static_pointer_cast<ResizableBuffer>(AllocateBuffer(allocator_, 0));
    }
  }

  virtual ~ColumnWriterImpl() = default;

  int64_t Close();

  int64_t CloseWithIndex();

  void WriteIndex(int64_t& file_pos_, int64_t& ci_offset, int64_t& oi_offset);

  void WriteBloomFilterOffset(int64_t& file_pos);

 protected:
  virtual std::shared_ptr<Buffer> GetValuesBuffer() = 0;

  // Serializes Dictionary Page if enabled
  virtual void WriteDictionaryPage() = 0;

  // Plain-encoded statistics of the current page
  virtual EncodedStatistics GetPageStatistics() = 0;

  // Plain-encoded statistics of the whole chunk
  virtual EncodedStatistics GetChunkStatistics() = 0;

  // Merges page statistics into chunk statistics, then resets the values
  virtual void ResetPageStatistics() = 0;

  // Adds Data Pages to an in memory buffer in dictionary encoding mode
  // Serializes the Data Pages in other encoding modes
  void AddDataPage();

  
  void AddDataPageWithIndex();

  // Serializes Data Pages
  void WriteDataPage(const CompressedDataPage& page) {
    total_bytes_written_ += pager_->WriteDataPage(page);
  }

  void WriteDataPageWithIndex(const CompressedDataPage& page, format::PageLocation& ploc) {
    total_bytes_written_ += pager_->WriteDataPagesWithIndex(page, ploc);
  }

  // Write multiple definition levels
  void WriteDefinitionLevels(int64_t num_levels, const int16_t* levels) {
    DCHECK(!closed_);
    PARQUET_THROW_NOT_OK(
        definition_levels_sink_.Append(levels, sizeof(int16_t) * num_levels));
  }

  // Write multiple repetition levels
  void WriteRepetitionLevels(int64_t num_levels, const int16_t* levels) {
    DCHECK(!closed_);
    PARQUET_THROW_NOT_OK(
        repetition_levels_sink_.Append(levels, sizeof(int16_t) * num_levels));
  }

  // RLE encode the src_buffer into dest_buffer and return the encoded size
  int64_t RleEncodeLevels(const void* src_buffer, ResizableBuffer* dest_buffer,
                          int16_t max_level);

  // Serialize the buffered Data Pages
  void FlushBufferedDataPages();

  void FlushBufferedDataPagesWithIndex();

  ColumnChunkMetaDataBuilder* metadata_;
  const ColumnDescriptor* descr_;

  std::unique_ptr<PageWriter> pager_;

  std::unique_ptr<ThriftSerializer> thrift_serializer_;

  bool has_dictionary_;
  Encoding::type encoding_;
  const WriterProperties* properties_;

  LevelEncoder level_encoder_;

  MemoryPool* allocator_;

  // The total number of values stored in the data page. This is the maximum of
  // the number of encoded definition levels or encoded values. For
  // non-repeated, required columns, this is equal to the number of encoded
  // values. For repeated or optional values, there may be fewer data values
  // than levels, and this tells you how many encoded levels there are in that
  // case.
  int64_t num_buffered_values_;

  // The total number of stored values. For repeated or optional values, this
  // number may be lower than num_buffered_values_.
  int64_t num_buffered_encoded_values_;

  // Total number of rows written with this ColumnWriter
  int rows_written_;

  // Records the total number of bytes written by the serializer
  int64_t total_bytes_written_;

  // Records the current number of compressed bytes in a column
  int64_t total_compressed_bytes_;

  // Flag to check if the Writer has been closed
  bool closed_;

  // Flag to infer if dictionary encoding has fallen back to PLAIN
  bool fallback_;

  ::arrow::BufferBuilder definition_levels_sink_;
  ::arrow::BufferBuilder repetition_levels_sink_;

  std::shared_ptr<ResizableBuffer> definition_levels_rle_;
  std::shared_ptr<ResizableBuffer> repetition_levels_rle_;

  std::shared_ptr<ResizableBuffer> uncompressed_data_;
  std::shared_ptr<ResizableBuffer> compressed_data_;

  std::vector<CompressedDataPage> data_pages_;

  /// In parquet::ColumnIndex we store the min and max values for each page.
  /// However, we don't want to store very long strings, so we truncate them.
  /// The value of it must not be too small, since we don't want to truncate
  /// non-string values.
  static const int PAGE_INDEX_MAX_STRING_LENGTH = 64;

  ::arrow::Status AddMemoryConsumptionForPageIndex(int64_t new_memory_allocation) {
      page_index_memory_consumption_ += new_memory_allocation;
      return ::arrow::Status::OK();
  }

  ::arrow::Status ReserveOffsetIndex(int64_t capacity) {
    PARQUET_THROW_NOT_OK(AddMemoryConsumptionForPageIndex(capacity * sizeof(parquet::format::PageLocation)));
    offset_index_.page_locations.reserve(capacity);
    return ::arrow::Status::OK();
  }

  void AddLocationToOffsetIndex(const parquet::format::PageLocation location) {
    offset_index_.page_locations.push_back(location);
  }

  void AddBloomFilterOffsetToOffsetIndex(const int64_t page_blf_offset) {
    offset_index_.page_bloom_filter_offsets.push_back(page_blf_offset);
  }
   
  ::arrow::Status TruncateDown ( std::string min, int32_t max_length, std::string* result ) {
    *result = min.substr(0, std::min(static_cast<int32_t>(min.length()), max_length));
    return Status::OK();
  }

  ::arrow::Status TruncateUp ( std::string max, int32_t max_length, std::string* result) {
     if (max.length() <= (uint32_t) max_length) {
       *result = max;
     }

      *result = max.substr(0, max_length);
      int i = max_length - 1;
      while (i > 0 && static_cast<int32_t>((*result)[i]) == -1) {
        (*result)[i] += 1;
        --i;
      }
      // We convert it to unsigned because signed overflow results in undefined behavior.
      unsigned char uch = static_cast<unsigned char>((*result)[i]);
      uch += 1;
      (*result)[i] = uch;
      if (i == 0 && (*result)[i] == 0) {
        return Status(::arrow::StatusCode::CapacityError,"TruncateUp() couldn't increase string.");
      }
      result->resize(i + 1);
      return Status::OK();
  }
  
  ::arrow::Status AddPageStatsToColumnIndex(const parquet::EncodedStatistics page_stats) {
    // If pages_stats contains min_value and max_value, then append them to min_values_
    // and max_values_ and also mark the page as not null. In case min and max values are
    // not set, push empty strings to maintain the consistency of the index and mark the
    // page as null. Always push the null_count.
    std::string min_val;
    std::string max_val;
    
    if (page_stats.is_set()) {
      
      Status s_min = TruncateDown(page_stats.min(), PAGE_INDEX_MAX_STRING_LENGTH, &min_val);
      
      Status s_max = TruncateUp(page_stats.max(), PAGE_INDEX_MAX_STRING_LENGTH, &max_val);

      if (!s_min.ok()) {
        return s_min;
      }
      if (!s_max.ok()) {
        return s_max;
      }
      
      column_index_.null_pages.push_back(false);
    } else {
      DCHECK(!page_stats.is_set());
      column_index_.null_pages.push_back(true);
    }
    PARQUET_THROW_NOT_OK(
        AddMemoryConsumptionForPageIndex(min_val.capacity() + max_val.capacity()));
    column_index_.min_values.emplace_back(std::move(min_val));
    column_index_.max_values.emplace_back(std::move(max_val));
    column_index_.null_counts.push_back(page_stats.null_count);
    return Status::OK();
  }

 private:
  void InitSinks() {
    definition_levels_sink_.Rewind(0);
    repetition_levels_sink_.Rewind(0);
  }

   // OffsetIndex stores the locations of the pages.
  parquet::format::OffsetIndex offset_index_;

  // ColumnIndex stores the statistics of the pages.
  parquet::format::ColumnIndex column_index_;

  // Memory consumption of the min/max values in the page index.
  int64_t page_index_memory_consumption_ = 0;

  parquet::format::PageLocation ploc;

};

// return the size of the encoded buffer
int64_t ColumnWriterImpl::RleEncodeLevels(const void* src_buffer,
                                          ResizableBuffer* dest_buffer,
                                          int16_t max_level) {
  // TODO: This only works with due to some RLE specifics
  int64_t rle_size = LevelEncoder::MaxBufferSize(Encoding::RLE, max_level,
                                                 static_cast<int>(num_buffered_values_)) +
                     sizeof(int32_t);

  // Use Arrow::Buffer::shrink_to_fit = false
  // underlying buffer only keeps growing. Resize to a smaller size does not reallocate.
  PARQUET_THROW_NOT_OK(dest_buffer->Resize(rle_size, false));

  level_encoder_.Init(Encoding::RLE, max_level, static_cast<int>(num_buffered_values_),
                      dest_buffer->mutable_data() + sizeof(int32_t),
                      static_cast<int>(dest_buffer->size() - sizeof(int32_t)));
  int encoded = level_encoder_.Encode(static_cast<int>(num_buffered_values_),
                                      reinterpret_cast<const int16_t*>(src_buffer));
  DCHECK_EQ(encoded, num_buffered_values_);
  reinterpret_cast<int32_t*>(dest_buffer->mutable_data())[0] = level_encoder_.len();
  int64_t encoded_size = level_encoder_.len() + sizeof(int32_t);
  return encoded_size;
}

void ColumnWriterImpl::AddDataPage() {
  int64_t definition_levels_rle_size = 0;
  int64_t repetition_levels_rle_size = 0;

  std::shared_ptr<Buffer> values = GetValuesBuffer();

  if (descr_->max_definition_level() > 0) {
    definition_levels_rle_size =
        RleEncodeLevels(definition_levels_sink_.data(), definition_levels_rle_.get(),
                        descr_->max_definition_level());
  }

  if (descr_->max_repetition_level() > 0) {
    repetition_levels_rle_size =
        RleEncodeLevels(repetition_levels_sink_.data(), repetition_levels_rle_.get(),
                        descr_->max_repetition_level());
  }

  int64_t uncompressed_size =
      definition_levels_rle_size + repetition_levels_rle_size + values->size();

  // Use Arrow::Buffer::shrink_to_fit = false
  // underlying buffer only keeps growing. Resize to a smaller size does not reallocate.
  PARQUET_THROW_NOT_OK(uncompressed_data_->Resize(uncompressed_size, false));

  // Concatenate data into a single buffer
  uint8_t* uncompressed_ptr = uncompressed_data_->mutable_data();
  memcpy(uncompressed_ptr, repetition_levels_rle_->data(), repetition_levels_rle_size);
  uncompressed_ptr += repetition_levels_rle_size;
  memcpy(uncompressed_ptr, definition_levels_rle_->data(), definition_levels_rle_size);
  uncompressed_ptr += definition_levels_rle_size;
  memcpy(uncompressed_ptr, values->data(), values->size());

  EncodedStatistics page_stats = GetPageStatistics();
  page_stats.ApplyStatSizeLimits(properties_->max_statistics_size(descr_->path()));
  page_stats.set_is_signed(SortOrder::SIGNED == descr_->sort_order());
  ResetPageStatistics();

  std::shared_ptr<Buffer> compressed_data;
  if (pager_->has_compressor()) {
    pager_->Compress(*(uncompressed_data_.get()), compressed_data_.get());
    compressed_data = compressed_data_;
  } else {
    compressed_data = uncompressed_data_;
  }

  // Write the page to OutputStream eagerly if there is no dictionary or
  // if dictionary encoding has fallen back to PLAIN
  if (has_dictionary_ && !fallback_) {  // Save pages until end of dictionary encoding
    std::shared_ptr<Buffer> compressed_data_copy;
    PARQUET_THROW_NOT_OK(compressed_data->Copy(0, compressed_data->size(), allocator_,
                                               &compressed_data_copy));
    CompressedDataPage page(compressed_data_copy,
                            static_cast<int32_t>(num_buffered_values_), encoding_,
                            Encoding::RLE, Encoding::RLE, uncompressed_size, page_stats);
    total_compressed_bytes_ += page.size() + sizeof(format::PageHeader);
    data_pages_.push_back(std::move(page));
  } else {  // Eagerly write pages
    CompressedDataPage page(compressed_data, static_cast<int32_t>(num_buffered_values_),
                            encoding_, Encoding::RLE, Encoding::RLE, uncompressed_size,
                            page_stats);
    WriteDataPage(page);
  }

  // Re-initialize the sinks for next Page.
  InitSinks();
  num_buffered_values_ = 0;
  num_buffered_encoded_values_ = 0;
}

void ColumnWriterImpl::AddDataPageWithIndex() {
  int64_t definition_levels_rle_size = 0;
  int64_t repetition_levels_rle_size = 0;

  std::shared_ptr<Buffer> values = GetValuesBuffer();

  if (descr_->max_definition_level() > 0) {
    definition_levels_rle_size =
        RleEncodeLevels(definition_levels_sink_.data(), definition_levels_rle_.get(),
                        descr_->max_definition_level());
  }

  if (descr_->max_repetition_level() > 0) {
    repetition_levels_rle_size =
        RleEncodeLevels(repetition_levels_sink_.data(), repetition_levels_rle_.get(),
                        descr_->max_repetition_level());
  }

  int64_t uncompressed_size =
      definition_levels_rle_size + repetition_levels_rle_size + values->size();

  // Use Arrow::Buffer::shrink_to_fit = false
  // underlying buffer only keeps growing. Resize to a smaller size does not reallocate.
  PARQUET_THROW_NOT_OK(uncompressed_data_->Resize(uncompressed_size, false));

  // Concatenate data into a single buffer
  uint8_t* uncompressed_ptr = uncompressed_data_->mutable_data();
  memcpy(uncompressed_ptr, repetition_levels_rle_->data(), repetition_levels_rle_size);
  uncompressed_ptr += repetition_levels_rle_size;
  memcpy(uncompressed_ptr, definition_levels_rle_->data(), definition_levels_rle_size);
  uncompressed_ptr += definition_levels_rle_size;
  memcpy(uncompressed_ptr, values->data(), values->size());

  EncodedStatistics page_stats = GetPageStatistics();
  page_stats.ApplyStatSizeLimits(properties_->max_statistics_size(descr_->path()));
  page_stats.set_is_signed(SortOrder::SIGNED == descr_->sort_order());
  AddPageStatsToColumnIndex(page_stats);
  ResetPageStatistics();

  std::shared_ptr<Buffer> compressed_data;
  if (pager_->has_compressor()) {
    pager_->Compress(*(uncompressed_data_.get()), compressed_data_.get());
    compressed_data = compressed_data_;
  } else {
    compressed_data = uncompressed_data_;
  }

  // Write the page to OutputStream eagerly if there is no dictionary or
  // if dictionary encoding has fallen back to PLAIN
  if (has_dictionary_ && !fallback_) {  // Save pages until end of dictionary encoding
    std::shared_ptr<Buffer> compressed_data_copy;
    PARQUET_THROW_NOT_OK(compressed_data->Copy(0, compressed_data->size(), allocator_,
                                               &compressed_data_copy));
    CompressedDataPage page(compressed_data_copy,
                            static_cast<int32_t>(num_buffered_values_), encoding_,
                            Encoding::RLE, Encoding::RLE, uncompressed_size, page_stats);
    total_compressed_bytes_ += page.size() + sizeof(format::PageHeader);
    data_pages_.push_back(std::move(page));
  } else {  // Eagerly write pages
    CompressedDataPage page(compressed_data, static_cast<int32_t>(num_buffered_values_),
                            encoding_, Encoding::RLE, Encoding::RLE, uncompressed_size,
                            page_stats);
    WriteDataPageWithIndex(page,ploc);
    AddLocationToOffsetIndex(ploc);
  }

  // Re-initialize the sinks for next Page.
  InitSinks();
  num_buffered_values_ = 0;
  num_buffered_encoded_values_ = 0;
}

int64_t ColumnWriterImpl::Close() {
  if (!closed_) {
    closed_ = true;
    if (has_dictionary_ && !fallback_) {
      WriteDictionaryPage();
    }

    FlushBufferedDataPages();

    EncodedStatistics chunk_statistics = GetChunkStatistics();
    chunk_statistics.ApplyStatSizeLimits(
        properties_->max_statistics_size(descr_->path()));
    chunk_statistics.set_is_signed(SortOrder::SIGNED == descr_->sort_order());

    // Write stats only if the column has at least one row written
    if (rows_written_ > 0 && chunk_statistics.is_set()) {
      metadata_->SetStatistics(chunk_statistics);
    }
    pager_->Close(has_dictionary_, fallback_);
  }

  return total_bytes_written_;
}

int64_t ColumnWriterImpl::CloseWithIndex() {
  if (!closed_) {
    closed_ = true;
    if (has_dictionary_ && !fallback_) {
      WriteDictionaryPage();
    }
    
    FlushBufferedDataPagesWithIndex();

    EncodedStatistics chunk_statistics = GetChunkStatistics();
    chunk_statistics.ApplyStatSizeLimits(
        properties_->max_statistics_size(descr_->path()));
    chunk_statistics.set_is_signed(SortOrder::SIGNED == descr_->sort_order());

    if (rows_written_ > 0 && chunk_statistics.is_set()) {
      metadata_->SetStatistics(chunk_statistics);
    }
    
    pager_->Close(has_dictionary_, fallback_);
  }

  return total_bytes_written_;
}

void ColumnWriterImpl::WriteIndex(int64_t& file_pos_, int64_t& ci_offset, int64_t& oi_offset) {
    pager_->WriteIndex(file_pos_, ci_offset, oi_offset, column_index_, offset_index_);
}

void ColumnWriterImpl::WriteBloomFilterOffset(int64_t& file_pos_) {
   metadata_->WriteBloomFilterOffset(file_pos_);
}

void ColumnWriterImpl::FlushBufferedDataPages() {
  // Write all outstanding data to a new page
  if (num_buffered_values_ > 0) {
    AddDataPage();
  }
  for (size_t i = 0; i < data_pages_.size(); i++) {
    WriteDataPage(data_pages_[i]);
  }
  data_pages_.clear();
  total_compressed_bytes_ = 0;
}

void ColumnWriterImpl::FlushBufferedDataPagesWithIndex() {

  if (num_buffered_values_ > 0) {
    AddDataPageWithIndex();
  }
  
  PARQUET_THROW_NOT_OK(ReserveOffsetIndex(data_pages_.size()));

  for (size_t i = 0; i < data_pages_.size(); i++) {
    // AddPageStatsToColumnIndex(data_pages_[i].statistics());
    WriteDataPageWithIndex(data_pages_[i],ploc);
    AddLocationToOffsetIndex(ploc);
  }

  data_pages_.clear();
  total_compressed_bytes_ = 0;
}

// ----------------------------------------------------------------------
// TypedColumnWriter

template <typename DType>
class TypedColumnWriterImpl : public ColumnWriterImpl, public TypedColumnWriter<DType> {
 public:
  using T = typename DType::c_type;

  TypedColumnWriterImpl(ColumnChunkMetaDataBuilder* metadata,
                        std::unique_ptr<PageWriter> pager, const bool use_dictionary,
                        Encoding::type encoding, const WriterProperties* properties)
      : ColumnWriterImpl(metadata, std::move(pager), use_dictionary, encoding,
                         properties) {
    current_encoder_ = MakeEncoder(DType::type_num, encoding, use_dictionary, descr_,
                                   properties->memory_pool());

    if (properties->statistics_enabled(descr_->path()) &&
        (SortOrder::UNKNOWN != descr_->sort_order())) {
      page_statistics_ = TypedStats::Make(descr_, allocator_);
      chunk_statistics_ = TypedStats::Make(descr_, allocator_);
    }
  }

  int64_t Close() override { return ColumnWriterImpl::Close(); }

  int64_t CloseWithIndex() override { 
    return ColumnWriterImpl::CloseWithIndex(); 
  }

  void WriteIndex(int64_t file_pos_, int64_t ci_offset, int64_t oi_offset) override { 
    return ColumnWriterImpl::WriteIndex(file_pos_,  ci_offset, oi_offset); 
  }

  void WriteBloomFilterOffset(int64_t& file_pos) override {
     ColumnWriterImpl::WriteBloomFilterOffset(file_pos);
  } 

  void AppendColumnBloomFilter(int64_t num_values, T*values, BlockSplitBloomFilter& blf);

  void WriteBatch(int64_t num_values, const int16_t* def_levels,
                  const int16_t* rep_levels, const T* values, bool with_index) override;

  void WriteBatchSpaced(int64_t num_values, const int16_t* def_levels,
                        const int16_t* rep_levels, const uint8_t* valid_bits,
                        int64_t valid_bits_offset, const T* values) override;

  Status WriteArrow(const int16_t* def_levels, const int16_t* rep_levels,
                    int64_t num_levels, const ::arrow::Array& array,
                    ArrowWriteContext* context) override;

  int64_t EstimatedBufferedValueBytes() const override {
    return current_encoder_->EstimatedDataEncodedSize();
  }

 protected:
  std::shared_ptr<Buffer> GetValuesBuffer() override {
    return current_encoder_->FlushValues();
  }

  void WriteDictionaryPage() override {
    // We have to dynamic cast here because of TypedEncoder<Type> as
    // some compilers don't want to cast through virtual inheritance
    auto dict_encoder = dynamic_cast<DictEncoder<DType>*>(current_encoder_.get());
    DCHECK(dict_encoder);
    std::shared_ptr<ResizableBuffer> buffer =
        AllocateBuffer(properties_->memory_pool(), dict_encoder->dict_encoded_size());
    dict_encoder->WriteDict(buffer->mutable_data());

    DictionaryPage page(buffer, dict_encoder->num_entries(),
                        properties_->dictionary_page_encoding());
    total_bytes_written_ += pager_->WriteDictionaryPage(page);
  }

  // Checks if the Dictionary Page size limit is reached
  // If the limit is reached, the Dictionary and Data Pages are serialized
  // The encoding is switched to PLAIN
  void CheckDictionarySizeLimit(bool with_index);

  EncodedStatistics GetPageStatistics() override {
    EncodedStatistics result;
    if (page_statistics_) result = page_statistics_->Encode();
    return result;
  }

  EncodedStatistics GetChunkStatistics() override {
    EncodedStatistics result;
    if (chunk_statistics_) result = chunk_statistics_->Encode();
    return result;
  }

  void ResetPageStatistics() override {
    if (chunk_statistics_ != nullptr) {
      chunk_statistics_->Merge(*page_statistics_);
      page_statistics_->Reset();
    }
  }

  Type::type type() const override { return descr_->physical_type(); }

  const ColumnDescriptor* descr() const override { return descr_; }

  int64_t rows_written() const override { return rows_written_; }

  int64_t total_compressed_bytes() const override { return total_compressed_bytes_; }

  int64_t total_bytes_written() const override { return total_bytes_written_; }

  const WriterProperties* properties() override { return properties_; }

 private:
  using ValueEncoderType = typename EncodingTraits<DType>::Encoder;
  using TypedStats = TypedStatistics<DType>;
  std::unique_ptr<Encoder> current_encoder_;
  std::shared_ptr<TypedStats> page_statistics_;
  std::shared_ptr<TypedStats> chunk_statistics_;

  inline int64_t WriteMiniBatch(int64_t num_values, const int16_t* def_levels,
                                const int16_t* rep_levels, const T* values, bool with_index);

  inline int64_t WriteMiniBatchSpaced(int64_t num_values, const int16_t* def_levels,
                                      const int16_t* rep_levels,
                                      const uint8_t* valid_bits,
                                      int64_t valid_bits_offset, const T* values,
                                      int64_t* num_spaced_written);

  // Write values to a temporary buffer before they are encoded into pages
  void WriteValues(int64_t num_values, const T* values) {
    dynamic_cast<ValueEncoderType*>(current_encoder_.get())
        ->Put(values, static_cast<int>(num_values));
  }

  void WriteValuesSpaced(int64_t num_values, const uint8_t* valid_bits,
                         int64_t valid_bits_offset, const T* values) {
    dynamic_cast<ValueEncoderType*>(current_encoder_.get())
        ->PutSpaced(values, static_cast<int>(num_values), valid_bits, valid_bits_offset);
  }
};

// Only one Dictionary Page is written.
// Fallback to PLAIN if dictionary page limit is reached.
template <typename DType>
void TypedColumnWriterImpl<DType>::CheckDictionarySizeLimit(bool with_index) {
  // We have to dynamic cast here because TypedEncoder<Type> as some compilers
  // don't want to cast through virtual inheritance
  auto dict_encoder = dynamic_cast<DictEncoder<DType>*>(current_encoder_.get());
  if (dict_encoder->dict_encoded_size() >= properties_->dictionary_pagesize_limit()) {
    WriteDictionaryPage();
    // Serialize the buffered Dictionary Indicies
    if (!with_index)
       FlushBufferedDataPages();
    else
       FlushBufferedDataPagesWithIndex();
    fallback_ = true;
    // Only PLAIN encoding is supported for fallback in V1
    current_encoder_ = MakeEncoder(DType::type_num, Encoding::PLAIN, false, descr_,
                                   properties_->memory_pool());
    encoding_ = Encoding::PLAIN;
  }
}

// ----------------------------------------------------------------------
// Instantiate templated classes

template <typename DType>
int64_t TypedColumnWriterImpl<DType>::WriteMiniBatch(int64_t num_values,
                                                     const int16_t* def_levels,
                                                     const int16_t* rep_levels,
                                                     const T* values,
                                                     bool with_index) {
  int64_t values_to_write = 0;
  // If the field is required and non-repeated, there are no definition levels
  if (descr_->max_definition_level() > 0) {
    for (int64_t i = 0; i < num_values; ++i) {
      if (def_levels[i] == descr_->max_definition_level()) {
        ++values_to_write;
      }
    }

    WriteDefinitionLevels(num_values, def_levels);
  } else {
    // Required field, write all values
    values_to_write = num_values;
  }

  // Not present for non-repeated fields
  if (descr_->max_repetition_level() > 0) {
    // A row could include more than one value
    // Count the occasions where we start a new row
    for (int64_t i = 0; i < num_values; ++i) {
      if (rep_levels[i] == 0) {
        rows_written_++;
      }
    }

    WriteRepetitionLevels(num_values, rep_levels);
  } else {
    // Each value is exactly one row
    rows_written_ += static_cast<int>(num_values);
  }

  // PARQUET-780
  if (values_to_write > 0) {
    DCHECK(nullptr != values) << "Values ptr cannot be NULL";
  }

  WriteValues(values_to_write, values);

  if (page_statistics_ != nullptr) {
    page_statistics_->Update(values, values_to_write, num_values - values_to_write);
  }

  num_buffered_values_ += num_values;
  num_buffered_encoded_values_ += values_to_write;

  if (current_encoder_->EstimatedDataEncodedSize() >= properties_->data_pagesize()) {
    if (!with_index)
       AddDataPage();
    else
       AddDataPageWithIndex();
  }
  if (has_dictionary_ && !fallback_) {
    CheckDictionarySizeLimit(with_index);
  }

  return values_to_write;
}

template <typename DType>
int64_t TypedColumnWriterImpl<DType>::WriteMiniBatchSpaced(
    int64_t num_levels, const int16_t* def_levels, const int16_t* rep_levels,
    const uint8_t* valid_bits, int64_t valid_bits_offset, const T* values,
    int64_t* num_spaced_written) {
  int64_t values_to_write = 0;
  int64_t spaced_values_to_write = 0;
  // If the field is required and non-repeated, there are no definition levels
  if (descr_->max_definition_level() > 0) {
    // Minimal definition level for which spaced values are written
    int16_t min_spaced_def_level = descr_->max_definition_level();
    if (descr_->schema_node()->is_optional()) {
      min_spaced_def_level--;
    }
    for (int64_t i = 0; i < num_levels; ++i) {
      if (def_levels[i] == descr_->max_definition_level()) {
        ++values_to_write;
      }
      if (def_levels[i] >= min_spaced_def_level) {
        ++spaced_values_to_write;
      }
    }

    WriteDefinitionLevels(num_levels, def_levels);
  } else {
    // Required field, write all values
    values_to_write = num_levels;
    spaced_values_to_write = num_levels;
  }

  // Not present for non-repeated fields
  if (descr_->max_repetition_level() > 0) {
    // A row could include more than one value
    // Count the occasions where we start a new row
    for (int64_t i = 0; i < num_levels; ++i) {
      if (rep_levels[i] == 0) {
        rows_written_++;
      }
    }

    WriteRepetitionLevels(num_levels, rep_levels);
  } else {
    // Each value is exactly one row
    rows_written_ += static_cast<int>(num_levels);
  }

  if (descr_->schema_node()->is_optional()) {
    WriteValuesSpaced(spaced_values_to_write, valid_bits, valid_bits_offset, values);
  } else {
    WriteValues(values_to_write, values);
  }
  *num_spaced_written = spaced_values_to_write;

  if (page_statistics_ != nullptr) {
    page_statistics_->UpdateSpaced(values, valid_bits, valid_bits_offset, values_to_write,
                                   spaced_values_to_write - values_to_write);
  }

  num_buffered_values_ += num_levels;
  num_buffered_encoded_values_ += values_to_write;

  if (current_encoder_->EstimatedDataEncodedSize() >= properties_->data_pagesize()) {
    AddDataPage();
  }
  if (has_dictionary_ && !fallback_) {
    CheckDictionarySizeLimit(false);
  }

  return values_to_write;
}

template <typename DType>
void TypedColumnWriterImpl<DType>::WriteBatch(int64_t num_values,
                                              const int16_t* def_levels,
                                              const int16_t* rep_levels,
                                              const T* values,
                                              bool with_index) {
  // We check for DataPage limits only after we have inserted the values. If a user
  // writes a large number of values, the DataPage size can be much above the limit.
  // The purpose of this chunking is to bound this. Even if a user writes large number
  // of values, the chunking will ensure the AddDataPage() is called at a reasonable
  // pagesize limit
  int64_t write_batch_size = properties_->write_batch_size();
  int num_batches = static_cast<int>(num_values / write_batch_size);
  int64_t num_remaining = num_values % write_batch_size;
  int64_t value_offset = 0;
  for (int round = 0; round < num_batches; round++) {
    int64_t offset = round * write_batch_size;
    int64_t num_values = WriteMiniBatch(write_batch_size, &def_levels[offset],
                                        &rep_levels[offset], &values[value_offset], with_index);
    value_offset += num_values;
  }
  // Write the remaining values
  int64_t offset = num_batches * write_batch_size;
  WriteMiniBatch(num_remaining, &def_levels[offset], &rep_levels[offset],
                 &values[value_offset], with_index);
}

template <typename DType>
void TypedColumnWriterImpl<DType>::AppendColumnBloomFilter(int64_t num_values, T*values, BlockSplitBloomFilter& blf) {
}

template <typename DType>
void TypedColumnWriterImpl<DType>::WriteBatchSpaced(
    int64_t num_values, const int16_t* def_levels, const int16_t* rep_levels,
    const uint8_t* valid_bits, int64_t valid_bits_offset, const T* values) {
  // We check for DataPage limits only after we have inserted the values. If a user
  // writes a large number of values, the DataPage size can be much above the limit.
  // The purpose of this chunking is to bound this. Even if a user writes large number
  // of values, the chunking will ensure the AddDataPage() is called at a reasonable
  // pagesize limit
  int64_t write_batch_size = properties_->write_batch_size();
  int num_batches = static_cast<int>(num_values / write_batch_size);
  int64_t num_remaining = num_values % write_batch_size;
  int64_t num_spaced_written = 0;
  int64_t values_offset = 0;
  for (int round = 0; round < num_batches; round++) {
    int64_t offset = round * write_batch_size;
    WriteMiniBatchSpaced(write_batch_size, &def_levels[offset], &rep_levels[offset],
                         valid_bits, valid_bits_offset + values_offset,
                         values + values_offset, &num_spaced_written);
    values_offset += num_spaced_written;
  }
  // Write the remaining values
  int64_t offset = num_batches * write_batch_size;
  WriteMiniBatchSpaced(num_remaining, &def_levels[offset], &rep_levels[offset],
                       valid_bits, valid_bits_offset + values_offset,
                       values + values_offset, &num_spaced_written);
}

// ----------------------------------------------------------------------
// Direct Arrow write path

template <typename ParquetType, typename ArrowType, typename Enable = void>
struct SerializeFunctor {
  using ArrowCType = typename ArrowType::c_type;
  using ArrayType = typename ::arrow::TypeTraits<ArrowType>::ArrayType;
  using ParquetCType = typename ParquetType::c_type;
  Status Serialize(const ArrayType& array, ArrowWriteContext*, ParquetCType* out) {
    const ArrowCType* input = array.raw_values();
    if (array.null_count() > 0) {
      for (int i = 0; i < array.length(); i++) {
        out[i] = static_cast<ParquetCType>(input[i]);
      }
    } else {
      std::copy(input, input + array.length(), out);
    }
    return Status::OK();
  }
};

template <typename ParquetType, typename ArrowType>
inline Status SerializeData(const ::arrow::Array& array, ArrowWriteContext* ctx,
                            typename ParquetType::c_type* out) {
  using ArrayType = typename ::arrow::TypeTraits<ArrowType>::ArrayType;
  SerializeFunctor<ParquetType, ArrowType> functor;
  return functor.Serialize(checked_cast<const ArrayType&>(array), ctx, out);
}

template <typename ParquetType, typename ArrowType>
Status WriteArrowSerialize(const ::arrow::Array& array, int64_t num_levels,
                           const int16_t* def_levels, const int16_t* rep_levels,
                           ArrowWriteContext* ctx,
                           TypedColumnWriter<ParquetType>* writer) {
  using ParquetCType = typename ParquetType::c_type;

  ParquetCType* buffer;
  PARQUET_THROW_NOT_OK(ctx->GetScratchData<ParquetCType>(array.length(), &buffer));

  bool no_nulls =
      writer->descr()->schema_node()->is_required() || (array.null_count() == 0);

  Status s = SerializeData<ParquetType, ArrowType>(array, ctx, buffer);
  RETURN_NOT_OK(s);
  if (no_nulls) {
    PARQUET_CATCH_NOT_OK(writer->WriteBatch(num_levels, def_levels, rep_levels, buffer));
  } else {
    PARQUET_CATCH_NOT_OK(writer->WriteBatchSpaced(num_levels, def_levels, rep_levels,
                                                  array.null_bitmap_data(),
                                                  array.offset(), buffer));
  }
  return Status::OK();
}

template <typename ParquetType>
Status WriteArrowZeroCopy(const ::arrow::Array& array, int64_t num_levels,
                          const int16_t* def_levels, const int16_t* rep_levels,
                          ArrowWriteContext* ctx,
                          TypedColumnWriter<ParquetType>* writer) {
  using T = typename ParquetType::c_type;
  const auto& data = static_cast<const ::arrow::PrimitiveArray&>(array);
  const T* values = nullptr;
  // The values buffer may be null if the array is empty (ARROW-2744)
  if (data.values() != nullptr) {
    values = reinterpret_cast<const T*>(data.values()->data()) + data.offset();
  } else {
    DCHECK_EQ(data.length(), 0);
  }
  if (writer->descr()->schema_node()->is_required() || (data.null_count() == 0)) {
    PARQUET_CATCH_NOT_OK(writer->WriteBatch(num_levels, def_levels, rep_levels, values));
  } else {
    PARQUET_CATCH_NOT_OK(writer->WriteBatchSpaced(num_levels, def_levels, rep_levels,
                                                  data.null_bitmap_data(), data.offset(),
                                                  values));
  }
  return Status::OK();
}

#define WRITE_SERIALIZE_CASE(ArrowEnum, ArrowType, ParquetType)  \
  case ::arrow::Type::ArrowEnum:                                 \
    return WriteArrowSerialize<ParquetType, ::arrow::ArrowType>( \
        array, num_levels, def_levels, rep_levels, ctx, this);

#define WRITE_ZERO_COPY_CASE(ArrowEnum, ArrowType, ParquetType)                       \
  case ::arrow::Type::ArrowEnum:                                                      \
    return WriteArrowZeroCopy<ParquetType>(array, num_levels, def_levels, rep_levels, \
                                           ctx, this);

#define ARROW_UNSUPPORTED()                                          \
  std::stringstream ss;                                              \
  ss << "Arrow type " << array.type()->ToString()                    \
     << " cannot be written to Parquet type " << descr_->ToString(); \
  return Status::Invalid(ss.str());

// ----------------------------------------------------------------------
// Write Arrow to BooleanType

template <>
Status TypedColumnWriterImpl<BooleanType>::WriteArrow(const int16_t* def_levels,
                                                      const int16_t* rep_levels,
                                                      int64_t num_levels,
                                                      const ::arrow::Array& array,
                                                      ArrowWriteContext* ctx) {
  if (array.type_id() != ::arrow::Type::BOOL) {
    ARROW_UNSUPPORTED();
  }
  bool* buffer = nullptr;
  RETURN_NOT_OK(ctx->GetScratchData<bool>(array.length(), &buffer));

  const auto& data = static_cast<const ::arrow::BooleanArray&>(array);
  const uint8_t* values = nullptr;
  // The values buffer may be null if the array is empty (ARROW-2744)
  if (data.values() != nullptr) {
    values = reinterpret_cast<const uint8_t*>(data.values()->data());
  } else {
    DCHECK_EQ(data.length(), 0);
  }

  int buffer_idx = 0;
  int64_t offset = array.offset();
  for (int i = 0; i < data.length(); i++) {
    if (data.IsValid(i)) {
      buffer[buffer_idx++] = BitUtil::GetBit(values, offset + i);
    }
  }
  PARQUET_CATCH_NOT_OK(WriteBatch(num_levels, def_levels, rep_levels, buffer,false));
  return Status::OK();
}

// ----------------------------------------------------------------------
// Write Arrow types to INT32

template <>
struct SerializeFunctor<Int32Type, ::arrow::Date64Type> {
  Status Serialize(const ::arrow::Date64Array& array, ArrowWriteContext*, int32_t* out) {
    const int64_t* input = array.raw_values();
    for (int i = 0; i < array.length(); i++) {
      *out++ = static_cast<int32_t>(*input++ / 86400000);
    }
    return Status::OK();
  }
};

template <>
struct SerializeFunctor<Int32Type, ::arrow::Time32Type> {
  Status Serialize(const ::arrow::Time32Array& array, ArrowWriteContext*, int32_t* out) {
    const int32_t* input = array.raw_values();
    const auto& type = static_cast<const ::arrow::Time32Type&>(*array.type());
    if (type.unit() == ::arrow::TimeUnit::SECOND) {
      for (int i = 0; i < array.length(); i++) {
        out[i] = input[i] * 1000;
      }
    } else {
      std::copy(input, input + array.length(), out);
    }
    return Status::OK();
  }
};

template <>
Status TypedColumnWriterImpl<Int32Type>::WriteArrow(const int16_t* def_levels,
                                                    const int16_t* rep_levels,
                                                    int64_t num_levels,
                                                    const ::arrow::Array& array,
                                                    ArrowWriteContext* ctx) {
  switch (array.type()->id()) {
    case ::arrow::Type::NA: {
      PARQUET_CATCH_NOT_OK(WriteBatch(num_levels, def_levels, rep_levels, nullptr,false));
    } break;
      WRITE_SERIALIZE_CASE(INT8, Int8Type, Int32Type)
      WRITE_SERIALIZE_CASE(UINT8, UInt8Type, Int32Type)
      WRITE_SERIALIZE_CASE(INT16, Int16Type, Int32Type)
      WRITE_SERIALIZE_CASE(UINT16, UInt16Type, Int32Type)
      WRITE_SERIALIZE_CASE(UINT32, UInt32Type, Int32Type)
      WRITE_ZERO_COPY_CASE(INT32, Int32Type, Int32Type)
      WRITE_ZERO_COPY_CASE(DATE32, Date32Type, Int32Type)
      WRITE_SERIALIZE_CASE(DATE64, Date64Type, Int32Type)
      WRITE_SERIALIZE_CASE(TIME32, Time32Type, Int32Type)
    default:
      ARROW_UNSUPPORTED()
  }
  return Status::OK();
}

// ----------------------------------------------------------------------
// Write Arrow to Int64 and Int96

#define INT96_CONVERT_LOOP(ConversionFunction) \
  for (int64_t i = 0; i < array.length(); i++) ConversionFunction(input[i], &out[i]);

template <>
struct SerializeFunctor<Int96Type, ::arrow::TimestampType> {
  Status Serialize(const ::arrow::TimestampArray& array, ArrowWriteContext*, Int96* out) {
    const int64_t* input = array.raw_values();
    const auto& type = static_cast<const ::arrow::TimestampType&>(*array.type());
    switch (type.unit()) {
      case ::arrow::TimeUnit::NANO:
        INT96_CONVERT_LOOP(internal::NanosecondsToImpalaTimestamp);
        break;
      case ::arrow::TimeUnit::MICRO:
        INT96_CONVERT_LOOP(internal::MicrosecondsToImpalaTimestamp);
        break;
      case ::arrow::TimeUnit::MILLI:
        INT96_CONVERT_LOOP(internal::MillisecondsToImpalaTimestamp);
        break;
      case ::arrow::TimeUnit::SECOND:
        INT96_CONVERT_LOOP(internal::SecondsToImpalaTimestamp);
        break;
    }
    return Status::OK();
  }
};

#define COERCE_DIVIDE -1
#define COERCE_INVALID 0
#define COERCE_MULTIPLY +1

static std::pair<int, int64_t> kTimestampCoercionFactors[4][4] = {
    // from seconds ...
    {{COERCE_INVALID, 0},                      // ... to seconds
     {COERCE_MULTIPLY, 1000},                  // ... to millis
     {COERCE_MULTIPLY, 1000000},               // ... to micros
     {COERCE_MULTIPLY, INT64_C(1000000000)}},  // ... to nanos
    // from millis ...
    {{COERCE_INVALID, 0},
     {COERCE_MULTIPLY, 1},
     {COERCE_MULTIPLY, 1000},
     {COERCE_MULTIPLY, 1000000}},
    // from micros ...
    {{COERCE_INVALID, 0},
     {COERCE_DIVIDE, 1000},
     {COERCE_MULTIPLY, 1},
     {COERCE_MULTIPLY, 1000}},
    // from nanos ...
    {{COERCE_INVALID, 0},
     {COERCE_DIVIDE, 1000000},
     {COERCE_DIVIDE, 1000},
     {COERCE_MULTIPLY, 1}}};

template <>
struct SerializeFunctor<Int64Type, ::arrow::TimestampType> {
  Status Serialize(const ::arrow::TimestampArray& array, ArrowWriteContext* ctx,
                   int64_t* out) {
    const auto& source_type = static_cast<const ::arrow::TimestampType&>(*array.type());
    auto source_unit = source_type.unit();
    const int64_t* values = array.raw_values();

    ::arrow::TimeUnit::type target_unit = ctx->properties->coerce_timestamps_unit();
    auto target_type = ::arrow::timestamp(target_unit);
    bool truncation_allowed = ctx->properties->truncated_timestamps_allowed();

    auto DivideBy = [&](const int64_t factor) {
      for (int64_t i = 0; i < array.length(); i++) {
        if (!truncation_allowed && array.IsValid(i) && (values[i] % factor != 0)) {
          return Status::Invalid("Casting from ", source_type.ToString(), " to ",
                                 target_type->ToString(),
                                 " would lose data: ", values[i]);
        }
        out[i] = values[i] / factor;
      }
      return Status::OK();
    };

    auto MultiplyBy = [&](const int64_t factor) {
      for (int64_t i = 0; i < array.length(); i++) {
        out[i] = values[i] * factor;
      }
      return Status::OK();
    };

    const auto& coercion = kTimestampCoercionFactors[static_cast<int>(source_unit)]
                                                    [static_cast<int>(target_unit)];

    // .first -> coercion operation; .second -> scale factor
    DCHECK_NE(coercion.first, COERCE_INVALID);
    return coercion.first == COERCE_DIVIDE ? DivideBy(coercion.second)
                                           : MultiplyBy(coercion.second);
  }
};

#undef COERCE_DIVIDE
#undef COERCE_INVALID
#undef COERCE_MULTIPLY

Status WriteTimestamps(const ::arrow::Array& values, int64_t num_levels,
                       const int16_t* def_levels, const int16_t* rep_levels,
                       ArrowWriteContext* ctx, TypedColumnWriter<Int64Type>* writer) {
  const auto& source_type = static_cast<const ::arrow::TimestampType&>(*values.type());

  auto WriteCoerce = [&](const ArrowWriterProperties* properties) {
    ArrowWriteContext temp_ctx = *ctx;
    temp_ctx.properties = properties;
    return WriteArrowSerialize<Int64Type, ::arrow::TimestampType>(
        values, num_levels, def_levels, rep_levels, &temp_ctx, writer);
  };

  if (ctx->properties->coerce_timestamps_enabled()) {
    // User explicitly requested coercion to specific unit
    if (source_type.unit() == ctx->properties->coerce_timestamps_unit()) {
      // No data conversion necessary
      return WriteArrowZeroCopy<Int64Type>(values, num_levels, def_levels, rep_levels,
                                           ctx, writer);
    } else {
      return WriteCoerce(ctx->properties);
    }
  } else if (writer->properties()->version() == ParquetVersion::PARQUET_1_0 &&
             source_type.unit() == ::arrow::TimeUnit::NANO) {
    // Absent superseding user instructions, when writing Parquet version 1.0 files,
    // timestamps in nanoseconds are coerced to microseconds
    std::shared_ptr<ArrowWriterProperties> properties =
        (ArrowWriterProperties::Builder())
            .coerce_timestamps(::arrow::TimeUnit::MICRO)
            ->disallow_truncated_timestamps()
            ->build();
    return WriteCoerce(properties.get());
  } else if (source_type.unit() == ::arrow::TimeUnit::SECOND) {
    // Absent superseding user instructions, timestamps in seconds are coerced to
    // milliseconds
    std::shared_ptr<ArrowWriterProperties> properties =
        (ArrowWriterProperties::Builder())
            .coerce_timestamps(::arrow::TimeUnit::MILLI)
            ->build();
    return WriteCoerce(properties.get());
  } else {
    // No data conversion necessary
    return WriteArrowZeroCopy<Int64Type>(values, num_levels, def_levels, rep_levels, ctx,
                                         writer);
  }
}

template <>
Status TypedColumnWriterImpl<Int64Type>::WriteArrow(const int16_t* def_levels,
                                                    const int16_t* rep_levels,
                                                    int64_t num_levels,
                                                    const ::arrow::Array& array,
                                                    ArrowWriteContext* ctx) {
  switch (array.type()->id()) {
    case ::arrow::Type::TIMESTAMP:
      return WriteTimestamps(array, num_levels, def_levels, rep_levels, ctx, this);
      WRITE_ZERO_COPY_CASE(INT64, Int64Type, Int64Type)
      WRITE_SERIALIZE_CASE(UINT32, UInt32Type, Int64Type)
      WRITE_SERIALIZE_CASE(UINT64, UInt64Type, Int64Type)
      WRITE_ZERO_COPY_CASE(TIME64, Time64Type, Int64Type)
    default:
      ARROW_UNSUPPORTED();
  }
}

template <>
Status TypedColumnWriterImpl<Int96Type>::WriteArrow(const int16_t* def_levels,
                                                    const int16_t* rep_levels,
                                                    int64_t num_levels,
                                                    const ::arrow::Array& array,
                                                    ArrowWriteContext* ctx) {
  if (array.type_id() != ::arrow::Type::TIMESTAMP) {
    ARROW_UNSUPPORTED();
  }
  return WriteArrowSerialize<Int96Type, ::arrow::TimestampType>(
      array, num_levels, def_levels, rep_levels, ctx, this);
}

// ----------------------------------------------------------------------
// Floating point types

template <>
Status TypedColumnWriterImpl<FloatType>::WriteArrow(const int16_t* def_levels,
                                                    const int16_t* rep_levels,
                                                    int64_t num_levels,
                                                    const ::arrow::Array& array,
                                                    ArrowWriteContext* ctx) {
  if (array.type_id() != ::arrow::Type::FLOAT) {
    ARROW_UNSUPPORTED();
  }
  return WriteArrowZeroCopy<FloatType>(array, num_levels, def_levels, rep_levels, ctx,
                                       this);
}

template <>
Status TypedColumnWriterImpl<DoubleType>::WriteArrow(const int16_t* def_levels,
                                                     const int16_t* rep_levels,
                                                     int64_t num_levels,
                                                     const ::arrow::Array& array,
                                                     ArrowWriteContext* ctx) {
  if (array.type_id() != ::arrow::Type::DOUBLE) {
    ARROW_UNSUPPORTED();
  }
  return WriteArrowZeroCopy<DoubleType>(array, num_levels, def_levels, rep_levels, ctx,
                                        this);
}

// ----------------------------------------------------------------------
// Write Arrow to BYTE_ARRAY

template <typename ParquetType, typename ArrowType>
struct SerializeFunctor<ParquetType, ArrowType, ::arrow::enable_if_binary<ArrowType>> {
  Status Serialize(const ::arrow::BinaryArray& array, ArrowWriteContext*,
                   ByteArray* out) {
    // In the case of an array consisting of only empty strings or all null,
    // array.data() points already to a nullptr, thus array.data()->data() will
    // segfault.
    const uint8_t* values = nullptr;
    if (array.value_data()) {
      values = reinterpret_cast<const uint8_t*>(array.value_data()->data());
      DCHECK(values != nullptr);
    }

    // Slice offset is accounted for in raw_value_offsets
    const int32_t* value_offset = array.raw_value_offsets();
    if (array.null_count() == 0) {
      // no nulls, just dump the data
      for (int64_t i = 0; i < array.length(); i++) {
        out[i] =
            ByteArray(value_offset[i + 1] - value_offset[i], values + value_offset[i]);
      }
    } else {
      for (int64_t i = 0; i < array.length(); i++) {
        if (array.IsValid(i)) {
          out[i] =
              ByteArray(value_offset[i + 1] - value_offset[i], values + value_offset[i]);
        }
      }
    }
    return Status::OK();
  }
};

template <>
Status TypedColumnWriterImpl<ByteArrayType>::WriteArrow(const int16_t* def_levels,
                                                        const int16_t* rep_levels,
                                                        int64_t num_levels,
                                                        const ::arrow::Array& array,
                                                        ArrowWriteContext* ctx) {
  switch (array.type()->id()) {
    WRITE_SERIALIZE_CASE(BINARY, BinaryType, ByteArrayType)
    WRITE_SERIALIZE_CASE(STRING, BinaryType, ByteArrayType)
    default:
      ARROW_UNSUPPORTED();
  }
}

// ----------------------------------------------------------------------
// Write Arrow to FIXED_LEN_BYTE_ARRAY

template <typename ParquetType, typename ArrowType>
struct SerializeFunctor<ParquetType, ArrowType,
                        ::arrow::enable_if_fixed_size_binary<ArrowType>> {
  Status Serialize(const ::arrow::FixedSizeBinaryArray& array, ArrowWriteContext*,
                   FLBA* out) {
    if (array.null_count() == 0) {
      // no nulls, just dump the data
      // todo(advancedxy): use a writeBatch to avoid this step
      for (int64_t i = 0; i < array.length(); i++) {
        out[i] = FixedLenByteArray(array.GetValue(i));
      }
    } else {
      for (int64_t i = 0; i < array.length(); i++) {
        if (array.IsValid(i)) {
          out[i] = FixedLenByteArray(array.GetValue(i));
        }
      }
    }
    return Status::OK();
  }
};

template <>
Status WriteArrowSerialize<FLBAType, ::arrow::Decimal128Type>(
    const ::arrow::Array& array, int64_t num_levels, const int16_t* def_levels,
    const int16_t* rep_levels, ArrowWriteContext* ctx,
    TypedColumnWriter<FLBAType>* writer) {
  const auto& data = static_cast<const ::arrow::Decimal128Array&>(array);
  const int64_t length = data.length();

  FLBA* buffer;
  RETURN_NOT_OK(ctx->GetScratchData<FLBA>(num_levels, &buffer));

  const auto& decimal_type = static_cast<const ::arrow::Decimal128Type&>(*data.type());
  const int32_t offset =
      decimal_type.byte_width() - internal::DecimalSize(decimal_type.precision());

  const bool does_not_have_nulls =
      writer->descr()->schema_node()->is_required() || data.null_count() == 0;

  const auto valid_value_count = static_cast<size_t>(length - data.null_count()) * 2;
  std::vector<uint64_t> big_endian_values(valid_value_count);

  // TODO(phillipc): Look into whether our compilers will perform loop unswitching so we
  // don't have to keep writing two loops to handle the case where we know there are no
  // nulls
  if (does_not_have_nulls) {
    // no nulls, just dump the data
    // todo(advancedxy): use a writeBatch to avoid this step
    for (int64_t i = 0, j = 0; i < length; ++i, j += 2) {
      auto unsigned_64_bit = reinterpret_cast<const uint64_t*>(data.GetValue(i));
      big_endian_values[j] = ::arrow::BitUtil::ToBigEndian(unsigned_64_bit[1]);
      big_endian_values[j + 1] = ::arrow::BitUtil::ToBigEndian(unsigned_64_bit[0]);
      buffer[i] = FixedLenByteArray(
          reinterpret_cast<const uint8_t*>(&big_endian_values[j]) + offset);
    }
  } else {
    for (int64_t i = 0, buffer_idx = 0, j = 0; i < length; ++i) {
      if (data.IsValid(i)) {
        auto unsigned_64_bit = reinterpret_cast<const uint64_t*>(data.GetValue(i));
        big_endian_values[j] = ::arrow::BitUtil::ToBigEndian(unsigned_64_bit[1]);
        big_endian_values[j + 1] = ::arrow::BitUtil::ToBigEndian(unsigned_64_bit[0]);
        buffer[buffer_idx++] = FixedLenByteArray(
            reinterpret_cast<const uint8_t*>(&big_endian_values[j]) + offset);
        j += 2;
      }
    }
  }
  PARQUET_CATCH_NOT_OK(writer->WriteBatch(num_levels, def_levels, rep_levels, buffer));
  return Status::OK();
}

template <>
Status TypedColumnWriterImpl<FLBAType>::WriteArrow(const int16_t* def_levels,
                                                   const int16_t* rep_levels,
                                                   int64_t num_levels,
                                                   const ::arrow::Array& array,
                                                   ArrowWriteContext* ctx) {
  switch (array.type()->id()) {
    WRITE_SERIALIZE_CASE(FIXED_SIZE_BINARY, FixedSizeBinaryType, FLBAType)
    WRITE_SERIALIZE_CASE(DECIMAL, Decimal128Type, FLBAType)
    default:
      break;
  }
  return Status::OK();
}

// ----------------------------------------------------------------------
// Dynamic column writer constructor

std::shared_ptr<ColumnWriter> ColumnWriter::Make(ColumnChunkMetaDataBuilder* metadata,
                                                 std::unique_ptr<PageWriter> pager,
                                                 const WriterProperties* properties) {
  const ColumnDescriptor* descr = metadata->descr();
  const bool use_dictionary = properties->dictionary_enabled(descr->path()) &&
                              descr->physical_type() != Type::BOOLEAN;
  Encoding::type encoding = properties->encoding(descr->path());
  if (use_dictionary) {
    encoding = properties->dictionary_index_encoding();
  }
  switch (descr->physical_type()) {
    case Type::BOOLEAN:
      return std::make_shared<TypedColumnWriterImpl<BooleanType>>(
          metadata, std::move(pager), use_dictionary, encoding, properties);
    case Type::INT32:
      return std::make_shared<TypedColumnWriterImpl<Int32Type>>(
          metadata, std::move(pager), use_dictionary, encoding, properties);
    case Type::INT64:
      return std::make_shared<TypedColumnWriterImpl<Int64Type>>(
          metadata, std::move(pager), use_dictionary, encoding, properties);
    case Type::INT96:
      return std::make_shared<TypedColumnWriterImpl<Int96Type>>(
          metadata, std::move(pager), use_dictionary, encoding, properties);
    case Type::FLOAT:
      return std::make_shared<TypedColumnWriterImpl<FloatType>>(
          metadata, std::move(pager), use_dictionary, encoding, properties);
    case Type::DOUBLE:
      return std::make_shared<TypedColumnWriterImpl<DoubleType>>(
          metadata, std::move(pager), use_dictionary, encoding, properties);
    case Type::BYTE_ARRAY:
      return std::make_shared<TypedColumnWriterImpl<ByteArrayType>>(
          metadata, std::move(pager), use_dictionary, encoding, properties);
    case Type::FIXED_LEN_BYTE_ARRAY:
      return std::make_shared<TypedColumnWriterImpl<FLBAType>>(
          metadata, std::move(pager), use_dictionary, encoding, properties);
    default:
      ParquetException::NYI("type reader not implemented");
  }
  // Unreachable code, but supress compiler warning
  return std::shared_ptr<ColumnWriter>(nullptr);
}

}  // namespace parquet
