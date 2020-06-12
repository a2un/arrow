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

#include "parquet/file_reader.h"

#include <algorithm>
#include <limits>
#include <cstdint>
#include <cstring>
#include <memory>
#include <ostream>
#include <string>
#include <utility>

#include "arrow/io/file.h"
#include "arrow/util/logging.h"
#include "arrow/util/ubsan.h"

#include "parquet/column_reader.h"
#include "parquet/column_scanner.h"
#include "parquet/deprecated_io.h"
#include "parquet/exception.h"
#include "parquet/metadata.h"
#include "parquet/platform.h"
#include "parquet/properties.h"
#include "parquet/schema.h"
#include "parquet/types.h"

#include <time.h>

namespace parquet {

// PARQUET-978: Minimize footer reads by reading 64 KB from the end of the file
static constexpr int64_t kDefaultFooterReadSize = 64 * 1024;
static constexpr uint32_t kFooterSize = 8;
static constexpr uint8_t kParquetMagic[4] = {'P', 'A', 'R', '1'};

static constexpr uint32_t kColumnIndexReadSize = 16*1024;
static constexpr uint32_t kOffsetIndexReadSize = 16*1024;

// For PARQUET-816
static constexpr int64_t kMaxDictHeaderSize = 100;

// ----------------------------------------------------------------------
// RowGroupReader public API

RowGroupReader::RowGroupReader(std::unique_ptr<Contents> contents)
    : contents_(std::move(contents)) {}

std::shared_ptr<ColumnReader> RowGroupReader::Column(int i) {
  DCHECK(i < metadata()->num_columns())
      << "The RowGroup only has " << metadata()->num_columns()
      << "columns, requested column: " << i;
  const ColumnDescriptor* descr = metadata()->schema()->Column(i);

  std::unique_ptr<PageReader> page_reader = contents_->GetColumnPageReader(i);
  return ColumnReader::Make(
      descr, std::move(page_reader),
      const_cast<ReaderProperties*>(contents_->properties())->memory_pool());
}

std::unique_ptr<PageReader> RowGroupReader::GetColumnPageReader(int i) {
  DCHECK(i < metadata()->num_columns())
      << "The RowGroup only has " << metadata()->num_columns()
      << "columns, requested column: " << i;
  return contents_->GetColumnPageReader(i);
}


std::unique_ptr<PageReader> RowGroupReader::GetColumnPageReaderWithIndex(int i,void* predicate, int64_t& min_index,
                                            int predicate_col, int64_t& row_index,Type::type type_num, bool with_index, bool binary_search, int64_t& count_pages_scanned,
                                            int64_t& total_num_pages, int64_t& last_first_row, bool with_bloom_filter, bool with_page_bf,
                                            std::vector<int64_t>& unsorted_min_index, std::vector<int64_t>& unsorted_row_index) {
  DCHECK(i < metadata()->num_columns())
      << "The RowGroup only has " << metadata()->num_columns()
      << "columns, requested column: " << i;
  return contents_->GetColumnPageReaderWithIndex(i,predicate, min_index, predicate_col, row_index,type_num, with_index, binary_search, count_pages_scanned,
                                            total_num_pages, last_first_row, with_bloom_filter, with_page_bf,
                                            unsorted_min_index, unsorted_row_index);
}

std::shared_ptr<ColumnReader> RowGroupReader::ColumnWithIndex(int i,void* predicate, int64_t& min_index, int predicate_col, 
                                  int64_t& row_index,Type::type type_num, bool with_index, bool binary_search, int64_t& count_pages_scanned,
                                            int64_t& total_num_pages, int64_t& last_first_row, bool with_bloom_filter, bool with_page_bf,
                                            std::vector<int64_t>& unsorted_min_index, std::vector<int64_t>& unsorted_row_index) {
  DCHECK(i < metadata()->num_columns())
      << "The RowGroup only has " << metadata()->num_columns()
      << "columns, requested column: " << i;
  const ColumnDescriptor* descr = metadata()->schema()->Column(i);

  std::unique_ptr<PageReader> page_reader = contents_->GetColumnPageReaderWithIndex(i,predicate, min_index, predicate_col, row_index,type_num, with_index, binary_search, count_pages_scanned,
                                            total_num_pages, last_first_row, with_bloom_filter, with_page_bf,
                                            unsorted_min_index, unsorted_row_index);
  return ColumnReader::Make(
      descr, std::move(page_reader),
      const_cast<ReaderProperties*>(contents_->properties())->memory_pool());
}

// Returns the rowgroup metadata
const RowGroupMetaData* RowGroupReader::metadata() const { return contents_->metadata(); }

// RowGroupReader::Contents implementation for the Parquet file specification
class SerializedRowGroup : public RowGroupReader::Contents {
 public:
  SerializedRowGroup(const std::shared_ptr<ArrowInputFile>& source,
                     FileMetaData* file_metadata, int row_group_number,
                     const ReaderProperties& props)
      : source_(source), file_metadata_(file_metadata), properties_(props) {
    row_group_metadata_ = file_metadata->RowGroup(row_group_number);
  }

  const RowGroupMetaData* metadata() const override { return row_group_metadata_.get(); }

  const ReaderProperties* properties() const override { return &properties_; }

  bool isSorted(parquet::format::ColumnIndex col_index,parquet::format::OffsetIndex offset_index,Type::type type_num) const {
      bool sorted = false;
      switch(type_num) {
         case Type::BOOLEAN:{

           break;
         }
         case Type::INT32:{
           int32_t* page_min_prev = (int32_t*)(void*)col_index.min_values[0].c_str();
           for (uint64_t itemindex = 1;itemindex < offset_index.page_locations.size();) {
               int32_t* page_min_curr = (int32_t*)(void*)col_index.min_values[itemindex].c_str();
               if ( *page_min_prev <= *page_min_curr ){
                  itemindex++;
                  page_min_prev = page_min_curr;
               }else{
                  return sorted;
               }
           }
           sorted = true;
           break;
         }
         case Type::INT64:{
           int64_t* page_min_prev = (int64_t*)(void*)col_index.min_values[0].c_str();
           for (uint64_t itemindex = 1;itemindex < offset_index.page_locations.size();) {
               int64_t* page_min_curr = (int64_t*)(void*)col_index.min_values[itemindex].c_str();
               if ( *page_min_prev <= *page_min_curr ){
                  itemindex++;
                  page_min_prev = page_min_curr;
               }else{
                  return sorted;
               }
           }
           sorted = true;
           break;
         }
         case Type::INT96:{
           uint32_t* page_min_prev = (uint32_t*)(void*)col_index.min_values[0].c_str();
           for (uint64_t itemindex = 1;itemindex < offset_index.page_locations.size();) {
               uint32_t* page_min_curr = (uint32_t*)(void*)col_index.min_values[itemindex].c_str();
               if ( *page_min_prev <= *page_min_curr ){
                  itemindex++;
                  page_min_prev = page_min_curr;
               }else{
                  return sorted;
               }
           }
           sorted = true;
           break;
         }
         case Type::FLOAT:{
           float* page_min_prev = (float*)(void*)col_index.min_values[0].c_str();
           for (uint64_t itemindex = 1;itemindex < offset_index.page_locations.size();) {
               float* page_min_curr = (float*)(void*)col_index.min_values[itemindex].c_str();
               if ( *page_min_prev <= *page_min_curr ){
                  itemindex++;
                  page_min_prev = page_min_curr;
               }else{
                  return sorted;
               }
           }
           sorted = true;
           break;
         }
         case Type::DOUBLE:{
           double* page_min_prev = (double*)(void*)col_index.min_values[0].c_str();
           for (uint64_t itemindex = 1;itemindex < offset_index.page_locations.size();) {
               double* page_min_curr = (double*)(void*)col_index.min_values[itemindex].c_str();
               if ( *page_min_prev <= *page_min_curr ){
                  itemindex++;
                  page_min_prev = page_min_curr;
               }else{
                  return sorted;
               }
           }
           sorted = true;
           break;
         }
         case Type::BYTE_ARRAY:{
           char* page_min_prev = (char*)(void*)col_index.min_values[0].c_str();
           for (uint64_t itemindex = 1;itemindex < offset_index.page_locations.size();) {
               char* page_min_curr = (char*)(void*)col_index.min_values[itemindex].c_str();
               if ( strcmp(page_min_prev,page_min_curr) <= 0 ){
                  itemindex++;
                  page_min_prev = page_min_curr;
               }else{
                  return sorted;
               }
           }
           sorted = true;
           break;
         }
         case Type::FIXED_LEN_BYTE_ARRAY:{
           char* page_min_prev = (char*)(void*)col_index.min_values[0].c_str();
           for (uint64_t itemindex = 1;itemindex < offset_index.page_locations.size();) {
               char* page_min_curr = (char*)(void*)col_index.min_values[itemindex].c_str();
               if ( strcmp(page_min_prev,page_min_curr) <= 0 ){
                  itemindex++;
                  page_min_prev = page_min_curr;
               }else{
                  return sorted;
               }
           }
           sorted = true;
           break;
         }
         default:{
           break;
         }
      }
      return sorted;
  }

  void page_bloom_filter_has_value(std::shared_ptr<ArrowInputFile>& source_, ReaderProperties& properties_, void* predicate, format::OffsetIndex& offset_index
                                    , int64_t& min_index, Type::type type_num, int64_t& row_index) const {
      int64_t blf_offset = offset_index.page_bloom_filter_offsets[min_index];
      std::shared_ptr<ArrowInputStream> stream_ = properties_.GetStream(source_, blf_offset,BloomFilter::kMaximumBloomFilterBytes);
      BlockSplitBloomFilter page_blf = BlockSplitBloomFilter::Deserialize(stream_.get());
      row_index = offset_index.page_locations[min_index].first_row_index;
      switch(type_num) {
          case Type::BOOLEAN:{
          break;
          }
          case Type::INT32:{
            int32_t v = *((int32_t*) predicate);
            if (!page_blf.FindHash(page_blf.Hash(v))) row_index = -1;
            break;
          }
          case Type::INT64:{
            int64_t v = *((int64_t*) predicate);
            if (!page_blf.FindHash(page_blf.Hash(v))) row_index = -1;
            break;
          }
          case Type::INT96:{
             uint32_t v = *((uint32_t*) predicate);
             break;
          }
          case Type::FLOAT:{
             float v = *((float*) predicate);
             if (!page_blf.FindHash(page_blf.Hash((float)(int64_t)v))) row_index = -1;
             break;
          }
          case Type::DOUBLE:{
             double v = *((double*) predicate);
             if (!page_blf.FindHash(page_blf.Hash((double)(int64_t)v))) row_index = -1;
             break;
          }
          case Type::BYTE_ARRAY:{
             const char* p = (char*) predicate;
             char dest[FIXED_LENGTH];
             for ( uint32_t i = 0; i < (FIXED_LENGTH-strlen(p));i++) dest[i] = '0';
             for ( uint32_t i = (FIXED_LENGTH-strlen(p)); i < FIXED_LENGTH;i++) dest[i] = p[i-(FIXED_LENGTH-strlen(p))];
             dest[FIXED_LENGTH] = '\0';
             std::string test(dest);
             ByteArray pba(test.size(),reinterpret_cast<const uint8_t*>(test.c_str()));
             if (!page_blf.FindHash(page_blf.Hash(&pba))) row_index = -1;
             break;
          }
          case Type::FIXED_LEN_BYTE_ARRAY:{
             char* v = (char*) predicate;
             uint8_t ptr = *v;
             ByteArray pba((uint32_t)strlen(v),&ptr);
             if (!page_blf.FindHash(page_blf.Hash(&pba))) row_index = -1;
             break;
          }
          default:{
             parquet::ParquetException::NYI("type reader not implemented");
          }
      }
  }


  void page_bloom_filter_has_value(std::shared_ptr<ArrowInputFile>& source_, ReaderProperties& properties_, void* predicate, format::OffsetIndex& offset_index
                                    , std::vector<int64_t>& unsorted_min_index, Type::type type_num, std::vector<int64_t>& unsorted_row_index) const {
      
      for ( int64_t min_index: unsorted_min_index) {
        int64_t blf_offset = offset_index.page_bloom_filter_offsets[min_index];
      std::shared_ptr<ArrowInputStream> stream_ = properties_.GetStream(source_, blf_offset,BloomFilter::kMaximumBloomFilterBytes);
      BlockSplitBloomFilter page_blf = BlockSplitBloomFilter::Deserialize(stream_.get());
      unsorted_row_index.push_back(offset_index.page_locations[min_index].first_row_index);
      switch(type_num) {
          case Type::BOOLEAN:{
          break;
          }
          case Type::INT32:{
            int32_t v = *((int32_t*) predicate);
            if (!page_blf.FindHash(page_blf.Hash(v))) unsorted_row_index.pop_back();
            break;
          }
          case Type::INT64:{
            int64_t v = *((int64_t*) predicate);
            if (!page_blf.FindHash(page_blf.Hash(v))) unsorted_row_index.pop_back();
            break;
          }
          case Type::INT96:{
             uint32_t v = *((uint32_t*) predicate);
             break;
          }
          case Type::FLOAT:{
             float v = *((float*) predicate);
             if (!page_blf.FindHash(page_blf.Hash((float)(int64_t)v))) unsorted_row_index.pop_back();
             break;
          }
          case Type::DOUBLE:{
             double v = *((double*) predicate);
             if (!page_blf.FindHash(page_blf.Hash((double)(int64_t)v))) unsorted_row_index.pop_back();
             break;
          }
          case Type::BYTE_ARRAY:{
             const char* p = (char*) predicate;
             char dest[FIXED_LENGTH];
             for ( uint32_t i = 0; i < (FIXED_LENGTH-strlen(p));i++) dest[i] = '0';
             for ( uint32_t i = (FIXED_LENGTH-strlen(p)); i < FIXED_LENGTH;i++) dest[i] = p[i-(FIXED_LENGTH-strlen(p))];
             dest[FIXED_LENGTH] = '\0';
             std::string test(dest);
             ByteArray pba(test.size(),reinterpret_cast<const uint8_t*>(test.c_str()));
             if (!page_blf.FindHash(page_blf.Hash(&pba))) unsorted_row_index.pop_back();
             break;
          }
          case Type::FIXED_LEN_BYTE_ARRAY:{
             const char* p = (char*) predicate;
             char dest[FIXED_LENGTH];
             for ( uint32_t i = 0; i < (FIXED_LENGTH-strlen(p));i++) dest[i] = '0';
             for ( uint32_t i = (FIXED_LENGTH-strlen(p)); i < FIXED_LENGTH;i++) dest[i] = p[i-(FIXED_LENGTH-strlen(p))];
             dest[FIXED_LENGTH] = '\0';
             std::string test(dest);
             ByteArray pba(test.size(),reinterpret_cast<const uint8_t*>(test.c_str()));
             if (!page_blf.FindHash(page_blf.Hash(&pba))) unsorted_row_index.pop_back();
             break;
          }
          default:{
             parquet::ParquetException::NYI("type reader not implemented");
          }
      }
      } 
  }

  void GetPageIndex(std::shared_ptr<ArrowInputFile>& source_, ReaderProperties& properties_, void* predicate, 
                    int64_t& min_index,int64_t& row_index, parquet::format::ColumnIndex col_index, 
                    parquet::format::OffsetIndex offset_index,Type::type type_num, bool sorted, 
                    bool with_binarysearch, int64_t& count_pages_scanned,
                    parquet::BlockSplitBloomFilter& blf, bool with_bloom_filter, bool with_page_bf) const {
      
      switch(type_num) {
         case Type::BOOLEAN:{
           // doesn't make sense for bool
           break;
         }
         case Type::INT32:{
              int32_t v = *((int32_t*) predicate);
              
              if (with_bloom_filter && !blf.FindHash(blf.Hash(v))) {
                 row_index = -1; return;
              }

              if(sorted && with_binarysearch){
                  if(col_index.min_values.size() >= 2){
                    uint64_t last_index = col_index.min_values.size()-1;
                    uint64_t begin_index = 0;
                    uint64_t itemindex = (begin_index + last_index)/2;

                    while(begin_index <= last_index) {
                      itemindex = (begin_index + last_index)/2;
                      int32_t* page_min_curr = (int32_t*)col_index.min_values[itemindex].c_str(); 
                      
                      if ( v < *page_min_curr ){
                         last_index -= 1;
                         count_pages_scanned++;
                         continue;
                      } 
                      if ( itemindex < last_index ){
                        int32_t* page_min_next = (int32_t*)col_index.min_values[itemindex+1].c_str();
                        if ( v > *page_min_next ){
                          begin_index += 1;
                          count_pages_scanned++;
                        }
                        if ( v < *page_min_next && v > *page_min_curr ){
                            begin_index = last_index + 1;
                            count_pages_scanned++;
                        }
                      }else {
                         begin_index = last_index + 1;
                         count_pages_scanned++;
                      }
                    }
                    min_index = itemindex;
                  }
                  else
                  {
                    min_index = 0;
                  }
              }
              else{
                for (uint64_t itemindex = 0;itemindex < offset_index.page_locations.size();itemindex++) {
                 int32_t* page_min = (int32_t*)(void *)col_index.min_values[itemindex].c_str();
                 int32_t* page_max = (int32_t*)(void *)col_index.max_values[itemindex].c_str();
                 int32_t max_diff = *page_max - *page_min;

                  if ( *page_min <= v && v <= *page_max ) {
                    min_index = itemindex;
                  }
                  count_pages_scanned = itemindex;
                }
                min_index = (count_pages_scanned == ((int)offset_index.page_locations.size()-1) && min_index == -1)? count_pages_scanned:min_index;
              }
           break;
         }
         case Type::INT64:
         {
             int64_t v = *((int64_t*) predicate);
             if (with_bloom_filter && !blf.FindHash(blf.Hash(v))) {
                 row_index = -1; return;
             }
             
             if(sorted && with_binarysearch){
                  if(col_index.min_values.size() >= 2){
                    uint64_t last_index = col_index.min_values.size()-1;
                    uint64_t begin_index = 0;
                    uint64_t itemindex = (begin_index + last_index)/2;

                    while(begin_index <= last_index) {
                      itemindex = (begin_index + last_index)/2;
                      int64_t* page_min_curr = (int64_t*)col_index.min_values[itemindex].c_str(); 
                      
                      if ( v < *page_min_curr ){
                         last_index -= 1;
                         count_pages_scanned++;
                         continue;
                      } 
                      if(itemindex < last_index){
                        int64_t* page_min_next = (int64_t*)col_index.min_values[itemindex+1].c_str();
                        if ( v > *page_min_next ){
                          begin_index += 1;
                          count_pages_scanned++;
                        }
                        if ( v < *page_min_next && v > *page_min_curr ){
                            begin_index = last_index + 1;
                            count_pages_scanned++;
                        }
                      }else{
                        begin_index = last_index + 1;
                        count_pages_scanned++;
                      }
                   }
                    min_index = itemindex;
                  }
                  else
                  {
                    min_index = 0;
                  }
              }
              else{
                 for (uint64_t itemindex = 0;itemindex < offset_index.page_locations.size();itemindex++) {
                  int64_t* page_min = (int64_t*)(void *)col_index.min_values[itemindex].c_str();
                  int64_t* page_max = (int64_t*)(void *)col_index.max_values[itemindex].c_str();
                  int64_t max_diff = *page_max - *page_min;
           
                  if ( *page_min <= v && v <= *page_max ) {
                    min_index = itemindex;
                  }
                  count_pages_scanned = itemindex;
                }
                min_index = (count_pages_scanned == ((int)offset_index.page_locations.size()-1) && min_index == -1)? count_pages_scanned:min_index;
              }
            break;
         }
         case Type::INT96:
         {
             uint32_t v = *((uint32_t*) predicate);
              if(sorted && with_binarysearch){
                  if(col_index.min_values.size() >= 2){
                    uint64_t last_index = col_index.min_values.size()-1;
                    uint64_t begin_index = 0;
                    uint64_t itemindex = (begin_index + last_index)/2;

                    while(begin_index <= last_index) {
                      itemindex = (begin_index + last_index)/2;
                      uint32_t* page_min_curr = (uint32_t*)col_index.min_values[itemindex].c_str(); 
                      
                      if ( v < *page_min_curr ){
                         last_index -= 1;
                         count_pages_scanned++;
                         continue;
                      } 
                      if ( itemindex < last_index ){
                        uint32_t* page_min_next = (uint32_t*)col_index.min_values[itemindex+1].c_str();
                        if ( v > *page_min_next ){
                          begin_index += 1;
                          count_pages_scanned++;
                        }
                        if ( v < *page_min_next && v > *page_min_curr ){
                            begin_index = last_index + 1;
                            count_pages_scanned++;
                        }
                      }else{
                        begin_index = last_index + 1;
                        count_pages_scanned++;
                      }
                    }
                    min_index = itemindex;
                  }
                  else
                  {
                    min_index = 0;
                  }
              }
              else {
                for (uint64_t itemindex = 0;itemindex < offset_index.page_locations.size();itemindex++) {
                  uint32_t* page_min = (uint32_t*)(void *)col_index.min_values[itemindex].c_str();
                  uint32_t* page_max = (uint32_t*)(void *)col_index.max_values[itemindex].c_str();
                  uint32_t max_diff = (*page_max - *page_min);
            
                  if ( *page_min <= v && max_diff >= (uint32_t) abs(v - *page_min) ) {
                   min_index = itemindex;
                   count_pages_scanned = itemindex;
                  }
                }
              }
           break;
         }
         case Type::FLOAT:
         {
             float v = *((float*) predicate);
             if (with_bloom_filter && !blf.FindHash(blf.Hash((float)(int64_t)v))) {
                 row_index = -1; return;
             }
             
             if(sorted && with_binarysearch){
                  if(col_index.min_values.size() >= 2){
                    uint64_t last_index = col_index.min_values.size()-1;
                    uint64_t begin_index = 0;
                    uint64_t itemindex = (begin_index + last_index)/2;

                    while(begin_index <= last_index) {
                      itemindex = (begin_index + last_index)/2;
                      float* page_min_curr = (float*)col_index.min_values[itemindex].c_str(); 
                      
                      if ( v < *page_min_curr ){
                         last_index -= 1;
                         count_pages_scanned++;
                         continue;
                      } 
                      if ( itemindex < last_index ){
                        float* page_min_next = (float*)col_index.min_values[itemindex+1].c_str();
                        if ( v > *page_min_next ){
                          begin_index += 1;
                          count_pages_scanned++;
                        }
                        if ( v < *page_min_next && v > *page_min_curr ){
                            begin_index = last_index + 1;
                            count_pages_scanned++;
                        }
                      }else{
                        begin_index = last_index + 1;
                        count_pages_scanned++;
                      }
                    }
                    min_index = itemindex;
                  }
                  else
                  {
                    min_index = 0;
                  }
              }
              else{
                for (uint64_t itemindex = 0;itemindex < offset_index.page_locations.size();itemindex++) {
                  float* page_min = (float*)(void *)col_index.min_values[itemindex].c_str();
                  float* page_max = (float*)(void *)col_index.max_values[itemindex].c_str();
             
                  auto epsilon = std::numeric_limits<float>::epsilon();
                  float error_factor = 9*pow(10,15);
                  float max_diff = *page_max - *page_min;

                  if ( *page_min < v && v < *page_max ) {
                    min_index = itemindex;
                  }
                  count_pages_scanned = itemindex;
                }
                min_index = (count_pages_scanned == ((int)offset_index.page_locations.size()-1) && min_index == -1)? count_pages_scanned:min_index;
              }
           break;
         }
         case Type::DOUBLE:
         {
             double v = *((double*) predicate);
             if (with_bloom_filter && !blf.FindHash(blf.Hash((double)(int64_t)v))) {
                 row_index = -1; return;
             }
             
             if(sorted && with_binarysearch){
                  if(col_index.min_values.size() >= 2){
                    uint64_t last_index = col_index.min_values.size()-1;
                    uint64_t begin_index = 0;
                    uint64_t itemindex = (begin_index + last_index)/2;

                    while(begin_index <= last_index) {
                      itemindex = (begin_index + last_index)/2;
                      double* page_min_curr = (double*)col_index.min_values[itemindex].c_str(); 
                      
                      if ( v < *page_min_curr ){
                         last_index -= 1;
                         count_pages_scanned++;
                         continue;
                      } 
                      if ( itemindex < last_index ){
                        double* page_min_next = (double*)col_index.min_values[itemindex+1].c_str();
                        if ( v > *page_min_next ){
                          begin_index += 1;
                          count_pages_scanned++;
                        }
                        if ( v < *page_min_next && v > *page_min_curr ){
                            begin_index = last_index + 1;
                            count_pages_scanned++;
                        }
                      }else{
                        begin_index = last_index + 1;
                        count_pages_scanned++;
                      }
                    }
                    min_index = itemindex;
                  }
                  else
                  {
                    min_index = 0;
                  }
              }
              else{
                for (uint64_t itemindex = 0;itemindex < offset_index.page_locations.size();itemindex++) {
                   double* page_min = (double*)(void *)col_index.min_values[itemindex].c_str();
                   double* page_max = (double*)(void *)col_index.max_values[itemindex].c_str();
                   double max_diff = *page_max - *page_min;

                   auto epsilon = std::numeric_limits<double>::epsilon();
                   double error_factor = 9*pow(10,15);

                   if ( *page_min < v && v < *page_max ) {
                      min_index = itemindex;
                   }
                   count_pages_scanned = itemindex;
                }
                min_index = (count_pages_scanned == ((int)offset_index.page_locations.size()-1) && min_index == -1)? count_pages_scanned:min_index;
              }
           break;
         }
         case Type::BYTE_ARRAY:
         {
             char* v = (char*) predicate;
             char* p = (char*) predicate;
             // remove leading zeroes in the predicate, if present.
             int checkzero = 0;
             while ( p [checkzero] == '0') checkzero++; 
             p = (p + checkzero);
             char dest[FIXED_LENGTH];
             for ( uint32_t i = 0; i < (FIXED_LENGTH-strlen(p));i++) dest[i] = '0';
             for ( uint32_t i = (FIXED_LENGTH-strlen(p)); i < FIXED_LENGTH;i++) dest[i] = p[i-(FIXED_LENGTH-strlen(p))];
             dest[FIXED_LENGTH] = '\0';
             std::string test(dest);
             ByteArray pba(test.size(),reinterpret_cast<const uint8_t*>(test.c_str()));
             if (with_bloom_filter && !blf.FindHash(blf.Hash(&pba))) {
                 row_index = -1; return;
             }

             std::string str(v);
             if(sorted && with_binarysearch){
                  if(col_index.min_values.size() >= 2){
                    uint64_t last_index = col_index.min_values.size()-1;
                    uint64_t begin_index = 0;
                    uint64_t itemindex = (begin_index + last_index)/2;

                    while(begin_index <= last_index) {
                      itemindex = (begin_index + last_index)/2;
                      std::string page_min_curr_orig = (std::string)col_index.min_values[itemindex].c_str(); 
                      std::string page_min_curr(page_min_curr_orig.substr(page_min_curr_orig.length() - str.length(),str.length()));
                      if ( test.compare(page_min_curr_orig) < 0 ){
                         last_index -= 1;
                         count_pages_scanned++;
                         continue;
                      } 
                      if ( itemindex < last_index ){
                        std::string page_min_next_orig = (std::string)col_index.min_values[itemindex+1].c_str();
                        std::string page_min_next(page_min_next_orig.substr(page_min_curr_orig.length() - str.length(),str.length()));
                        if ( test.compare(page_min_next_orig) > 0 ){
                          begin_index += 1;
                          count_pages_scanned++;
                        }
                        if ( test.compare(page_min_next_orig) < 0 && test.compare(page_min_curr_orig) > 0 ){
                            begin_index = last_index + 1;
                            count_pages_scanned++;
                        }
                      }else{
                         begin_index = last_index + 1;
                         count_pages_scanned++;
                      }
                    }
                    min_index = itemindex;
                  }
                  else
                  {
                    min_index = 0;
                  }
              }
              else {
                 for (uint64_t itemindex = 0;itemindex < offset_index.page_locations.size();itemindex++) {
                    std::string page_min_orig = (std::string)col_index.min_values[itemindex].c_str();
                    std::string page_max_orig = (std::string)col_index.max_values[itemindex].c_str();
                    std::string page_min(page_min_orig.substr(page_min_orig.length()-str.length(),str.length()));
                    std::string page_max(page_max_orig.substr(page_max_orig.length()-str.length(),str.length()));

                   if ( test.compare(page_min_orig) > 0 && test.compare(page_max_orig) < 0 ) {
                      min_index = itemindex;
                   }
                   count_pages_scanned = itemindex;
                }
                min_index = (count_pages_scanned == ((int)offset_index.page_locations.size()-1) && min_index == -1)? count_pages_scanned:min_index;
              }
           break;
         }
         case Type::FIXED_LEN_BYTE_ARRAY:
         {
             char* v = (char*) predicate;

             uint8_t ptr = *v;
             ByteArray pba((uint32_t)strlen(v),&ptr);
             if (with_bloom_filter && !blf.FindHash(blf.Hash(&pba))) {
                 row_index = -1; return;
             }

             std::string str(v);
             if(sorted && with_binarysearch){
                  if(col_index.min_values.size() >= 2){
                    uint64_t last_index = col_index.min_values.size()-1;
                    uint64_t begin_index = 0;
                    uint64_t itemindex = (begin_index + last_index)/2;

                    while(begin_index <= last_index) {
                      itemindex = (begin_index + last_index)/2;
                      std::string page_min_curr = (std::string)col_index.min_values[itemindex].c_str(); 
                      
                      if ( str.compare(page_min_curr) < 0 ){
                         last_index -= 1;
                         count_pages_scanned++;
                         continue;
                      } 
                      if ( itemindex < last_index ){
                        std::string page_min_next = (std::string)col_index.min_values[itemindex+1].c_str();
                        if ( str.compare(page_min_next) > 0 ){
                          begin_index += 1;
                          count_pages_scanned++;
                        }
                        if ( str.compare(page_min_next) < 0 && str.compare(page_min_curr) > 0 ){
                            begin_index = last_index + 1;
                            count_pages_scanned++;
                        }
                      }else{
                         begin_index = last_index + 1;
                         count_pages_scanned++;
                      }
                    }
                    min_index = itemindex;
                  }
                  else
                  {
                    min_index = 0;
                  }
              }
              else {
                 for (uint64_t itemindex = 0;itemindex < offset_index.page_locations.size();itemindex++) {
                    std::string page_min = col_index.min_values[itemindex];
                    std::string page_max = col_index.max_values[itemindex];

                   if ( str.compare(page_min)>0 && str.compare(page_max)<0 ) {
                      min_index = itemindex;
                      count_pages_scanned = itemindex;
                   }
                }
              }
           break;
         }
         default:
         {
           parquet::ParquetException::NYI("type reader not implemented");
         }
      }
      
      if (with_page_bf)
         page_bloom_filter_has_value(source_,properties_,predicate, offset_index,min_index,type_num, row_index);
      else 
         row_index = offset_index.page_locations[min_index].first_row_index;
  }

  void GetPageIndex(std::shared_ptr<ArrowInputFile>& source_, ReaderProperties& properties_, void* predicate, 
                    std::vector<int64_t>& unsorted_min_index, std::vector<int64_t>& unsorted_row_index,
                    parquet::format::ColumnIndex col_index, parquet::format::OffsetIndex offset_index,
                    Type::type type_num, bool sorted, bool with_binarysearch, int64_t& count_pages_scanned,
                    parquet::BlockSplitBloomFilter& blf, bool with_bloom_filter, bool with_page_bf) const {
      
      switch(type_num) {
         case Type::BOOLEAN:{
           // doesn't make sense for bool
           break;
         }
         case Type::INT32:{
              int32_t v = *((int32_t*) predicate);
              
              if (with_bloom_filter && !blf.FindHash(blf.Hash(v))) {
                 return;
              }
              
              for (uint64_t itemindex = 0;itemindex < offset_index.page_locations.size();itemindex++) {
                int32_t* page_min = (int32_t*)(void *)col_index.min_values[itemindex].c_str();
                int32_t* page_max = (int32_t*)(void *)col_index.max_values[itemindex].c_str();
                int32_t max_diff = *page_max - *page_min;

                if ( *page_min <= v && max_diff >= abs(v - *page_min) ) {
                  unsorted_min_index.push_back(itemindex);
                  count_pages_scanned = itemindex;
                }
              }
           break;
         }
         case Type::INT64:
         {
             int64_t v = *((int64_t*) predicate);
             if (with_bloom_filter && !blf.FindHash(blf.Hash(v))) {
                 return;
             }
             
             
             for (uint64_t itemindex = 0;itemindex < offset_index.page_locations.size();itemindex++) {
                int64_t* page_min = (int64_t*)(void *)col_index.min_values[itemindex].c_str();
                int64_t* page_max = (int64_t*)(void *)col_index.max_values[itemindex].c_str();
                int64_t max_diff = *page_max - *page_min;
           
                if ( *page_min <= v && max_diff >= abs(v - *page_min) ) {
                  unsorted_min_index.push_back(itemindex);
                  count_pages_scanned = itemindex;
                }
              }

            break;
         }
         case Type::INT96:
         {
           break;
         }
         case Type::FLOAT:
         {
             float v = *((float*) predicate);
             if (with_bloom_filter && !blf.FindHash(blf.Hash((float)(int64_t)v))) {
                 return;
             }
             
             
             for (uint64_t itemindex = 0;itemindex < offset_index.page_locations.size();itemindex++) {
                float* page_min = (float*)(void *)col_index.min_values[itemindex].c_str();
                float* page_max = (float*)(void *)col_index.max_values[itemindex].c_str();
             
                auto epsilon = std::numeric_limits<float>::epsilon();
                float error_factor = 9*pow(10,15);
                float max_diff = *page_max - *page_min;

                if ( fabs(max_diff - (fabs(v-*page_min)+fabs(*page_max-v))) <= error_factor*epsilon ) {

                  unsorted_min_index.push_back(itemindex);
                  count_pages_scanned = itemindex;

                }
             }

           break;
         }
         case Type::DOUBLE:
         {
             double v = *((double*) predicate);
             if (with_bloom_filter && !blf.FindHash(blf.Hash((double)(int64_t)v))) {
                 return;
             }
             
             
              for (uint64_t itemindex = 0;itemindex < offset_index.page_locations.size();itemindex++) {
                double* page_min = (double*)(void *)col_index.min_values[itemindex].c_str();
                double* page_max = (double*)(void *)col_index.max_values[itemindex].c_str();
                double max_diff = *page_max - *page_min;

                auto epsilon = std::numeric_limits<double>::epsilon();
                double error_factor = 9*pow(10,15);

                if ( fabs(max_diff - (fabs(v-*page_min)+fabs(*page_max-v))) <= error_factor*epsilon  ) {

                  unsorted_min_index.push_back(itemindex);
                  count_pages_scanned = itemindex;
                }
              }

           break;
         }
         case Type::BYTE_ARRAY:
         {
             char* v = (char*) predicate;

             const char* p = (char*) predicate;
             char dest[FIXED_LENGTH];
             for ( uint32_t i = 0; i < (FIXED_LENGTH-strlen(p));i++) dest[i] = '0';
             for ( uint32_t i = (FIXED_LENGTH-strlen(p)); i < FIXED_LENGTH;i++) dest[i] = p[i-(FIXED_LENGTH-strlen(p))];
             dest[FIXED_LENGTH] = '\0';
             std::string test(dest);
             ByteArray pba(test.size(),reinterpret_cast<const uint8_t*>(test.c_str()));
             if (with_bloom_filter && !blf.FindHash(blf.Hash(&pba))) {
                return;
             }

             std::string str(v);
             
             for (uint64_t itemindex = 0;itemindex < offset_index.page_locations.size();itemindex++) {
                std::string page_min_orig = col_index.min_values[itemindex];
                std::string page_max_orig = col_index.max_values[itemindex];
                std::string page_min(page_min_orig.substr(page_min_orig.length()-str.length(),str.length()));
                std::string page_max(page_max_orig.substr(page_max_orig.length()-str.length(),str.length()));

                if ( test.compare(page_min_orig)>0 && test.compare(page_max_orig)<0 ) {
                  unsorted_min_index.push_back(itemindex);
                  count_pages_scanned = itemindex;
                }
             }

           break;
         }
         case Type::FIXED_LEN_BYTE_ARRAY:
         {
             
           break;
         }
         default:
         {
           parquet::ParquetException::NYI("type reader not implemented");
         }
      }
      
      if (with_page_bf)
         page_bloom_filter_has_value(source_,properties_,predicate, offset_index,unsorted_min_index,type_num, unsorted_row_index);
      else {
          for (int64_t min_index : unsorted_min_index)
            unsorted_row_index.push_back(offset_index.page_locations[min_index].first_row_index);
      }
         
  }


  void GetPageWithRowIndex(int64_t& page_index, parquet::format::OffsetIndex offset_index, int64_t& row_index) const {
      
      for (uint64_t page_index = 0;page_index < offset_index.page_locations.size() && 
              offset_index.page_locations[page_index].first_row_index!=row_index;page_index++) {
          
      }
  }



/// ---- Page filtering ----
/// A Parquet file can contain a so called "page index". It has two parts, a column index
/// and an offset index. The column index contains statistics like minimum and maximum
/// values for each page. The offset index contains information about page locations in
/// the Parquet file and top-level row ranges. HdfsParquetScanner evaluates the min/max
/// conjuncts against the column index and determines the surviving pages with the help of
/// the offset index. Then it will configure the column readers to only scan the pages
/// and row ranges that have a chance to store rows that pass the conjuncts.


  bool HasPageIndex(ColumnChunkMetaData* col) {

    int64_t column_index_offset = col->column_index_offset();
    int64_t offset_index_offset = col->offset_index_offset();
    int64_t column_index_length = col->column_index_length();
    int64_t offset_index_length = col->offset_index_length();

    int64_t ci_start = std::numeric_limits<int64_t>::max();
    int64_t oi_start = std::numeric_limits<int64_t>::max();
    int64_t ci_end = -1;
    int64_t oi_end = -1;

    if (column_index_offset && column_index_length){
       ci_start = std::min(ci_start, column_index_offset);
       ci_end = std::max(ci_end, column_index_offset + column_index_length);
    }
    if (offset_index_offset && offset_index_length) {
       oi_start = std::min(oi_start, offset_index_offset);
       oi_end = std::max(oi_end, offset_index_offset + offset_index_length);
    }

    return oi_end != -1 && ci_end != -1; 

  }

  void DeserializeColumnIndex(const ColumnChunkMetaData& col_chunk, parquet::format::ColumnIndex* column_index, std::shared_ptr<ArrowInputFile>& source_, ReaderProperties& properties_) {
    int64_t ci_start = std::numeric_limits<int64_t>::max(); 
    int64_t ci_end = std::numeric_limits<int64_t>::max();
    string_view page_buffer;
    ci_start = std::min(ci_start,col_chunk.column_index_offset());
    ci_end = std::max(ci_end,col_chunk.column_index_offset() + col_chunk.column_index_length());
    int8_t buffer_offset = col_chunk.column_index_offset() - ci_start;
    uint32_t length = col_chunk.column_index_length();

    std::shared_ptr<ArrowInputStream> stream_ = properties_.GetStream(source_, ci_start, length);
    PARQUET_THROW_NOT_OK(stream_->Peek(kColumnIndexReadSize,&page_buffer));
    if (page_buffer.size() == 0) {
       return;
    }

    DeserializeThriftMsg(reinterpret_cast<const uint8_t*>(page_buffer.data()), &length, column_index);
  }

  void DeserializeOffsetIndex(const ColumnChunkMetaData& col_chunk, parquet::format::OffsetIndex* offset_index, std::shared_ptr<ArrowInputFile>& source_, ReaderProperties& properties_) {
    int64_t oi_start = std::numeric_limits<int64_t>::max(); 
    int64_t oi_end = std::numeric_limits<int64_t>::max();
    string_view page_buffer;
    oi_start = std::min(oi_start,col_chunk.offset_index_offset());
    oi_end = std::min(oi_end, col_chunk.offset_index_offset() + col_chunk.offset_index_length());
    int8_t buffer_offset = col_chunk.offset_index_offset() - oi_start;
    uint32_t length = col_chunk.offset_index_length();

    std::shared_ptr<ArrowInputStream> stream_ = properties_.GetStream(source_, oi_start, length);
    PARQUET_THROW_NOT_OK(stream_->Peek(kOffsetIndexReadSize, &page_buffer));
    if (page_buffer.size() == 0) {
       return;
    }

    DeserializeThriftMsg(reinterpret_cast<const uint8_t*>(page_buffer.data()), &length, offset_index);
  }

  void DeserializeBloomFilter(const ColumnChunkMetaData& col_chunk, parquet::BlockSplitBloomFilter& blf, std::shared_ptr<ArrowInputFile>& source_, ReaderProperties& properties_) {
      int64_t blf_offset = col_chunk.bloom_filter_offset();
      std::shared_ptr<ArrowInputStream> stream_ = properties_.GetStream(source_, blf_offset,BloomFilter::kMaximumBloomFilterBytes);
      blf = BlockSplitBloomFilter::Deserialize(stream_.get());
  }

  std::unique_ptr<PageReader> GetColumnPageReaderWithIndex(int column_index, void* predicate, int64_t& min_index, 
                              int predicate_col, int64_t& row_index,Type::type type_num, bool with_index, bool with_binarysearch, int64_t& count_pages_scanned,
                              int64_t& total_num_pages, int64_t& last_first_row, bool with_bloom_filter, bool with_page_bf,
                              std::vector<int64_t>& unsorted_min_index, std::vector<int64_t>& unsorted_row_index) {
    // Read column chunk from the file
    auto col = row_group_metadata_->ColumnChunk(column_index);

    auto sorting_columns = row_group_metadata_->sorting_columns();

    int64_t col_start = col->data_page_offset();
    if (col->has_dictionary_page() && col->dictionary_page_offset() > 0 &&
        col_start > col->dictionary_page_offset()) {
      col_start = col->dictionary_page_offset();
    }

    int64_t col_length = col->total_compressed_size();
    
    bool has_page_index = HasPageIndex((reinterpret_cast<ColumnChunkMetaData*>(col.get())));

    if ( has_page_index ) {
        parquet::format::ColumnIndex col_index;
        parquet::format::OffsetIndex offset_index;
        BlockSplitBloomFilter blf;
        DeserializeColumnIndex(*reinterpret_cast<ColumnChunkMetaData*>(col.get()),&col_index, source_, properties_);
        DeserializeOffsetIndex(*reinterpret_cast<ColumnChunkMetaData*>(col.get()),&offset_index, source_, properties_);
        DeserializeBloomFilter(*reinterpret_cast<ColumnChunkMetaData*>(col.get()),blf,source_,properties_);
        total_num_pages = offset_index.page_locations.size();
        last_first_row = offset_index.page_locations[offset_index.page_locations.size()-1].first_row_index;
        if ( predicate_col == column_index ) {
            bool sorted = isSorted(col_index,offset_index,type_num);
            if ( sorted )
              GetPageIndex(source_, properties_, predicate, min_index,row_index, col_index,offset_index,type_num,sorted, with_binarysearch, count_pages_scanned, blf, with_bloom_filter, with_page_bf);    
            else
            {
              GetPageIndex(source_, properties_, predicate, unsorted_min_index,unsorted_row_index, col_index,offset_index,type_num,sorted, with_binarysearch, count_pages_scanned, blf, with_bloom_filter, with_page_bf);    
            }
            
        }
        else 
           GetPageWithRowIndex(min_index, offset_index, row_index);
    }
    
    // PARQUET-816 workaround for old files created by older parquet-mr
    const ApplicationVersion& version = file_metadata_->writer_version();
    if (version.VersionLt(ApplicationVersion::PARQUET_816_FIXED_VERSION())) {
      // The Parquet MR writer had a bug in 1.2.8 and below where it didn't include the
      // dictionary page header size in total_compressed_size and total_uncompressed_size
      // (see IMPALA-694). We add padding to compensate.
      int64_t size = -1;
      PARQUET_THROW_NOT_OK(source_->GetSize(&size));
      int64_t bytes_remaining = size - (col_start + col_length);
      int64_t padding = std::min<int64_t>(kMaxDictHeaderSize, bytes_remaining);
      col_length += padding;
    }

    std::shared_ptr<ArrowInputStream> stream =
        properties_.GetStream(source_, col_start , col_length);

    return PageReader::Open(stream, col->num_values(), col->compression(),
                            properties_.memory_pool());
  }



  std::unique_ptr<PageReader> GetColumnPageReader(int i) override {
    // Read column chunk from the file
    auto col = row_group_metadata_->ColumnChunk(i);

    int64_t col_start = col->data_page_offset();
    if (col->has_dictionary_page() && col->dictionary_page_offset() > 0 &&
        col_start > col->dictionary_page_offset()) {
      col_start = col->dictionary_page_offset();
    }

    int64_t col_length = col->total_compressed_size();

    bool has_page_index = HasPageIndex((reinterpret_cast<ColumnChunkMetaData*>(col.get())));
    if ( has_page_index ) {
        parquet::format::ColumnIndex col_index;
        parquet::format::OffsetIndex offset_index;
        DeserializeColumnIndex(*reinterpret_cast<ColumnChunkMetaData*>(col.get()),&col_index, source_, properties_);
        DeserializeOffsetIndex(*reinterpret_cast<ColumnChunkMetaData*>(col.get()),&offset_index, source_, properties_);
    }

    // PARQUET-816 workaround for old files created by older parquet-mr
    const ApplicationVersion& version = file_metadata_->writer_version();
    if (version.VersionLt(ApplicationVersion::PARQUET_816_FIXED_VERSION())) {
      // The Parquet MR writer had a bug in 1.2.8 and below where it didn't include the
      // dictionary page header size in total_compressed_size and total_uncompressed_size
      // (see IMPALA-694). We add padding to compensate.
      int64_t size = -1;
      PARQUET_THROW_NOT_OK(source_->GetSize(&size));
      int64_t bytes_remaining = size - (col_start + col_length);
      int64_t padding = std::min<int64_t>(kMaxDictHeaderSize, bytes_remaining);
      col_length += padding;
    }

    std::shared_ptr<ArrowInputStream> stream =
        properties_.GetStream(source_, col_start, col_length);
    return PageReader::Open(stream, col->num_values(), col->compression(),
                            properties_.memory_pool());
  }

 private:
  std::shared_ptr<ArrowInputFile> source_;
  FileMetaData* file_metadata_;
  std::unique_ptr<RowGroupMetaData> row_group_metadata_;
  ReaderProperties properties_;
};

// ----------------------------------------------------------------------
// SerializedFile: An implementation of ParquetFileReader::Contents that deals
// with the Parquet file structure, Thrift deserialization, and other internal
// matters

// This class takes ownership of the provided data source
class SerializedFile : public ParquetFileReader::Contents {
 public:
  SerializedFile(const std::shared_ptr<ArrowInputFile>& source,
                 const ReaderProperties& props = default_reader_properties())
      : source_(source), properties_(props) {}

  void Close() override {}

  std::shared_ptr<RowGroupReader> GetRowGroup(int i) override {
    std::unique_ptr<SerializedRowGroup> contents(
        new SerializedRowGroup(source_, file_metadata_.get(), i, properties_));
    return std::make_shared<RowGroupReader>(std::move(contents));
  }

  std::shared_ptr<FileMetaData> metadata() const override { return file_metadata_; }

  void set_metadata(const std::shared_ptr<FileMetaData>& metadata) {
    file_metadata_ = metadata;
  }

  void ParseMetaData() {
    int64_t file_size = -1;
    PARQUET_THROW_NOT_OK(source_->GetSize(&file_size));

    if (file_size == 0) {
      throw ParquetException("Invalid Parquet file size is 0 bytes");
    } else if (file_size < kFooterSize) {
      std::stringstream ss;
      ss << "Invalid Parquet file size is " << file_size
         << " bytes, smaller than standard file footer (" << kFooterSize << " bytes)";
      throw ParquetException(ss.str());
    }

    std::shared_ptr<Buffer> footer_buffer;
    int64_t footer_read_size = std::min(file_size, kDefaultFooterReadSize);
    PARQUET_THROW_NOT_OK(
        source_->ReadAt(file_size - footer_read_size, footer_read_size, &footer_buffer));

    // Check if all bytes are read. Check if last 4 bytes read have the magic bits
    if (footer_buffer->size() != footer_read_size ||
        memcmp(footer_buffer->data() + footer_read_size - 4, kParquetMagic, 4) != 0) {
      throw ParquetException("Invalid parquet file. Corrupt footer.");
    }

    uint32_t metadata_len = arrow::util::SafeLoadAs<uint32_t>(
        reinterpret_cast<const uint8_t*>(footer_buffer->data()) + footer_read_size -
        kFooterSize);
    int64_t metadata_start = file_size - kFooterSize - metadata_len;
    if (kFooterSize + metadata_len > file_size) {
      throw ParquetException(
          "Invalid parquet file. File is less than "
          "file metadata size.");
    }

    std::shared_ptr<Buffer> metadata_buffer;
    // Check if the footer_buffer contains the entire metadata
    if (footer_read_size >= (metadata_len + kFooterSize)) {
      metadata_buffer = SliceBuffer(
          footer_buffer, footer_read_size - metadata_len - kFooterSize, metadata_len);
    } else {
      PARQUET_THROW_NOT_OK(
          source_->ReadAt(metadata_start, metadata_len, &metadata_buffer));
      if (metadata_buffer->size() != metadata_len) {
        throw ParquetException("Invalid parquet file. Could not read metadata bytes.");
      }
    }
    file_metadata_ = FileMetaData::Make(metadata_buffer->data(), &metadata_len);
  }

 private:
  std::shared_ptr<ArrowInputFile> source_;
  std::shared_ptr<FileMetaData> file_metadata_;
  ReaderProperties properties_;
};

// ----------------------------------------------------------------------
// ParquetFileReader public API

ParquetFileReader::ParquetFileReader() {}

ParquetFileReader::~ParquetFileReader() {
  try {
    Close();
  } catch (...) {
  }
}

// Open the file. If no metadata is passed, it is parsed from the footer of
// the file
std::unique_ptr<ParquetFileReader::Contents> ParquetFileReader::Contents::Open(
    const std::shared_ptr<ArrowInputFile>& source, const ReaderProperties& props,
    const std::shared_ptr<FileMetaData>& metadata) {
  std::unique_ptr<ParquetFileReader::Contents> result(new SerializedFile(source, props));

  // Access private methods here, but otherwise unavailable
  SerializedFile* file = static_cast<SerializedFile*>(result.get());

  if (metadata == nullptr) {
    // Validates magic bytes, parses metadata, and initializes the SchemaDescriptor
    file->ParseMetaData();
  } else {
    file->set_metadata(metadata);
  }

  return result;
}

std::unique_ptr<ParquetFileReader> ParquetFileReader::Open(
    const std::shared_ptr<::arrow::io::RandomAccessFile>& source,
    const ReaderProperties& props, const std::shared_ptr<FileMetaData>& metadata) {
  auto contents = SerializedFile::Open(source, props, metadata);
  std::unique_ptr<ParquetFileReader> result(new ParquetFileReader());
  result->Open(std::move(contents));
  return result;
}

std::unique_ptr<ParquetFileReader> ParquetFileReader::Open(
    std::unique_ptr<RandomAccessSource> source, const ReaderProperties& props,
    const std::shared_ptr<FileMetaData>& metadata) {
  auto wrapper = std::make_shared<ParquetInputWrapper>(std::move(source));
  return Open(wrapper, props, metadata);
}

std::unique_ptr<ParquetFileReader> ParquetFileReader::OpenFile(
    const std::string& path, bool memory_map, const ReaderProperties& props,
    const std::shared_ptr<FileMetaData>& metadata) {
  std::shared_ptr<::arrow::io::RandomAccessFile> source;
  if (memory_map) {
    std::shared_ptr<::arrow::io::MemoryMappedFile> handle;
    PARQUET_THROW_NOT_OK(
        ::arrow::io::MemoryMappedFile::Open(path, ::arrow::io::FileMode::READ, &handle));
    source = handle;
  } else {
    std::shared_ptr<::arrow::io::ReadableFile> handle;
    PARQUET_THROW_NOT_OK(
        ::arrow::io::ReadableFile::Open(path, props.memory_pool(), &handle));
    source = handle;
  }

  return Open(source, props, metadata);
}

void ParquetFileReader::Open(std::unique_ptr<ParquetFileReader::Contents> contents) {
  contents_ = std::move(contents);
}

void ParquetFileReader::Close() {
  if (contents_) {
    contents_->Close();
  }
}

std::shared_ptr<FileMetaData> ParquetFileReader::metadata() const {
  return contents_->metadata();
}

std::shared_ptr<RowGroupReader> ParquetFileReader::RowGroup(int i) {
  DCHECK(i < metadata()->num_row_groups())
      << "The file only has " << metadata()->num_row_groups()
      << "row groups, requested reader for: " << i;
  return contents_->GetRowGroup(i);
}

// ----------------------------------------------------------------------
// File metadata helpers

std::shared_ptr<FileMetaData> ReadMetaData(
    const std::shared_ptr<::arrow::io::RandomAccessFile>& source) {
  return ParquetFileReader::Open(source)->metadata();
}

// ----------------------------------------------------------------------
// File scanner for performance testing

int64_t ScanFileContents(std::vector<int> columns, const int32_t column_batch_size,
                         ParquetFileReader* reader) {
  std::vector<int16_t> rep_levels(column_batch_size);
  std::vector<int16_t> def_levels(column_batch_size);

  int num_columns = static_cast<int>(columns.size());

  // columns are not specified explicitly. Add all columns
  if (columns.size() == 0) {
    num_columns = reader->metadata()->num_columns();
    columns.resize(num_columns);
    for (int i = 0; i < num_columns; i++) {
      columns[i] = i;
    }
  }

  std::vector<int64_t> total_rows(num_columns, 0);

  for (int r = 0; r < reader->metadata()->num_row_groups(); ++r) {
    auto group_reader = reader->RowGroup(r);
    int col = 0;
    for (auto i : columns) {
      std::shared_ptr<ColumnReader> col_reader = group_reader->Column(i);
      size_t value_byte_size = GetTypeByteSize(col_reader->descr()->physical_type());
      std::vector<uint8_t> values(column_batch_size * value_byte_size);

      int64_t values_read = 0;
      while (col_reader->HasNext()) {
        int64_t levels_read =
            ScanAllValues(column_batch_size, def_levels.data(), rep_levels.data(),
                          values.data(), &values_read, col_reader.get());
        if (col_reader->descr()->max_repetition_level() > 0) {
          for (int64_t i = 0; i < levels_read; i++) {
            if (rep_levels[i] == 0) {
              total_rows[col]++;
            }
          }
        } else {
          total_rows[col] += levels_read;
        }
      }
      col++;
    }
  }

  for (int i = 1; i < num_columns; ++i) {
    if (total_rows[0] != total_rows[i]) {
      throw ParquetException("Parquet error: Total rows among columns do not match");
    }
  }

  return total_rows[0];
}

}  // namespace parquet
