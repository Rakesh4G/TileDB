/**
 * @file   helpers.cc
 *
 * @section LICENSE
 *
 * The MIT License
 *
 * @copyright Copyright (c) 2017-2019 TileDB, Inc.
 *
 * Permission is hereby granted, free of charge, to any person obtaining a copy
 * of this software and associated documentation files (the "Software"), to deal
 * in the Software without restriction, including without limitation the rights
 * to use, copy, modify, merge, publish, distribute, sublicense, and/or sell
 * copies of the Software, and to permit persons to whom the Software is
 * furnished to do so, subject to the following conditions:
 *
 * The above copyright notice and this permission notice shall be included in
 * all copies or substantial portions of the Software.
 *
 * THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR
 * IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY,
 * FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT. IN NO EVENT SHALL THE
 * AUTHORS OR COPYRIGHT HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER
 * LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING FROM,
 * OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS IN
 * THE SOFTWARE.
 *
 * @section DESCRIPTION
 *
 * This file defines some test suite helper functions.
 */

#include "helpers.h"
#include "catch.hpp"

template <class T>
void check_partitions(
    tiledb::sm::SubarrayPartitioner& partitioner,
    const std::vector<SubarrayRanges<T>>& partitions,
    bool last_unsplittable) {
  bool unsplittable = false;

  // Special case for empty partitions
  if (partitions.empty()) {
    CHECK(partitioner.next(&unsplittable).ok());
    if (last_unsplittable) {
      CHECK(unsplittable);
    } else {
      CHECK(!unsplittable);
      CHECK(partitioner.done());
    }
    return;
  }

  // Non-empty partitions
  for (const auto& p : partitions) {
    CHECK(!partitioner.done());
    CHECK(!unsplittable);
    CHECK(partitioner.next(&unsplittable).ok());
    auto partition = partitioner.current();
    check_subarray<T>(partition, p);
  }

  // Check last unsplittable
  if (last_unsplittable) {
    CHECK(unsplittable);
  } else {
    CHECK(!unsplittable);
    //    CHECK(partitioner.next(&unsplittable).ok());
    //    CHECK(!unsplittable);
    CHECK(partitioner.done());
  }
}

template <class T>
void check_subarray(
    tiledb::sm::Subarray& subarray, const SubarrayRanges<T>& ranges) {
  // Check empty subarray
  auto subarray_range_num = subarray.range_num();
  if (ranges.empty()) {
    CHECK(subarray_range_num == 0);
    return;
  }
  uint64_t range_num = 1;
  for (const auto& dim_ranges : ranges)
    range_num *= dim_ranges.size() / 2;
  CHECK(subarray_range_num == range_num);

  // Check dim num
  auto dim_num = subarray.dim_num();
  CHECK(dim_num == ranges.size());

  // Check ranges
  uint64_t dim_range_num = 0;
  const T* range;
  for (unsigned i = 0; i < dim_num; ++i) {
    CHECK(subarray.get_range_num(i, &dim_range_num).ok());
    CHECK(dim_range_num == ranges[i].size() / 2);
    for (uint64_t j = 0; j < dim_range_num; ++j) {
      subarray.get_range(i, j, (const void**)&range);
      CHECK(range[0] == ranges[i][2 * j]);
      CHECK(range[1] == ranges[i][2 * j + 1]);
    }
  }
}

void close_array(tiledb_ctx_t* ctx, tiledb_array_t* array) {
  int rc = tiledb_array_close(ctx, array);
  CHECK(rc == TILEDB_OK);
}

void create_array(
    tiledb_ctx_t* ctx,
    const std::string& array_name,
    tiledb_array_type_t array_type,
    const std::vector<std::string>& dim_names,
    const std::vector<tiledb_datatype_t>& dim_types,
    const std::vector<void*>& dim_domains,
    const std::vector<void*>& tile_extents,
    const std::vector<std::string>& attr_names,
    const std::vector<tiledb_datatype_t>& attr_types,
    const std::vector<uint32_t>& cell_val_num,
    const std::vector<std::pair<tiledb_filter_type_t, int>>& compressors,
    tiledb_layout_t tile_order,
    tiledb_layout_t cell_order,
    uint64_t capacity) {
  // For easy reference
  auto dim_num = dim_names.size();
  auto attr_num = attr_names.size();

  // Sanity checks
  assert(dim_types.size() == dim_num);
  assert(dim_domains.size() == dim_num);
  assert(tile_extents.size() == dim_num);
  assert(attr_types.size() == attr_num);
  assert(cell_val_num.size() == attr_num);
  assert(compressors.size() == attr_num);

  // Create array schema
  tiledb_array_schema_t* array_schema;
  int rc = tiledb_array_schema_alloc(ctx, array_type, &array_schema);
  REQUIRE(rc == TILEDB_OK);
  rc = tiledb_array_schema_set_cell_order(ctx, array_schema, cell_order);
  REQUIRE(rc == TILEDB_OK);
  rc = tiledb_array_schema_set_tile_order(ctx, array_schema, tile_order);
  REQUIRE(rc == TILEDB_OK);
  rc = tiledb_array_schema_set_capacity(ctx, array_schema, capacity);
  REQUIRE(rc == TILEDB_OK);

  // Create dimensions and domain
  tiledb_domain_t* domain;
  rc = tiledb_domain_alloc(ctx, &domain);
  REQUIRE(rc == TILEDB_OK);
  for (size_t i = 0; i < dim_num; ++i) {
    tiledb_dimension_t* d;
    rc = tiledb_dimension_alloc(
        ctx,
        dim_names[i].c_str(),
        dim_types[i],
        dim_domains[i],
        tile_extents[i],
        &d);
    REQUIRE(rc == TILEDB_OK);
    rc = tiledb_domain_add_dimension(ctx, domain, d);
    REQUIRE(rc == TILEDB_OK);
    tiledb_dimension_free(&d);
  }

  // Set domain to schema
  rc = tiledb_array_schema_set_domain(ctx, array_schema, domain);
  REQUIRE(rc == TILEDB_OK);
  tiledb_domain_free(&domain);

  // Create attributes
  for (size_t i = 0; i < attr_num; ++i) {
    tiledb_attribute_t* a;
    rc = tiledb_attribute_alloc(ctx, attr_names[i].c_str(), attr_types[i], &a);
    REQUIRE(rc == TILEDB_OK);
    rc = set_attribute_compression_filter(
        ctx, a, compressors[i].first, compressors[i].second);
    REQUIRE(rc == TILEDB_OK);
    rc = tiledb_attribute_set_cell_val_num(ctx, a, cell_val_num[i]);
    REQUIRE(rc == TILEDB_OK);
    rc = tiledb_array_schema_add_attribute(ctx, array_schema, a);
    REQUIRE(rc == TILEDB_OK);
    tiledb_attribute_free(&a);
  }

  // Check array schema
  rc = tiledb_array_schema_check(ctx, array_schema);
  REQUIRE(rc == TILEDB_OK);

  // Create array
  rc = tiledb_array_create(ctx, array_name.c_str(), array_schema);
  REQUIRE(rc == TILEDB_OK);

  // Clean up
  tiledb_array_schema_free(&array_schema);
}

void create_s3_bucket(
    const std::string& bucket_name,
    bool s3_supported,
    tiledb_ctx_t* ctx,
    tiledb_vfs_t* vfs) {
  if (s3_supported) {
    // Create bucket if it does not exist
    int is_bucket = 0;
    int rc = tiledb_vfs_is_bucket(ctx, vfs, bucket_name.c_str(), &is_bucket);
    REQUIRE(rc == TILEDB_OK);
    if (!is_bucket) {
      rc = tiledb_vfs_create_bucket(ctx, vfs, bucket_name.c_str());
      REQUIRE(rc == TILEDB_OK);
    }
  }
}

void create_ctx_and_vfs(
    bool s3_supported, tiledb_ctx_t** ctx, tiledb_vfs_t** vfs) {
  // Create TileDB context
  tiledb_config_t* config = nullptr;
  tiledb_error_t* error = nullptr;
  REQUIRE(tiledb_config_alloc(&config, &error) == TILEDB_OK);
  REQUIRE(error == nullptr);
  if (s3_supported) {
#ifndef TILEDB_TESTS_AWS_S3_CONFIG
    REQUIRE(
        tiledb_config_set(
            config, "vfs.s3.endpoint_override", "localhost:9999", &error) ==
        TILEDB_OK);
    REQUIRE(
        tiledb_config_set(config, "vfs.s3.scheme", "http", &error) ==
        TILEDB_OK);
    REQUIRE(
        tiledb_config_set(
            config, "vfs.s3.use_virtual_addressing", "false", &error) ==
        TILEDB_OK);
    REQUIRE(error == nullptr);
#endif
  }
  REQUIRE(tiledb_ctx_alloc(config, ctx) == TILEDB_OK);
  REQUIRE(error == nullptr);

  // Create VFS
  *vfs = nullptr;
  REQUIRE(tiledb_vfs_alloc(*ctx, config, vfs) == TILEDB_OK);
  tiledb_config_free(&config);
}

void create_dir(const std::string& path, tiledb_ctx_t* ctx, tiledb_vfs_t* vfs) {
  remove_dir(path, ctx, vfs);
  REQUIRE(tiledb_vfs_create_dir(ctx, vfs, path.c_str()) == TILEDB_OK);
}

template <class T>
void create_subarray(
    tiledb::sm::Array* array,
    const SubarrayRanges<T>& ranges,
    tiledb::sm::Layout layout,
    tiledb::sm::Subarray* subarray) {
  tiledb::sm::Subarray ret(array, layout);

  auto dim_num = (unsigned)ranges.size();
  for (unsigned i = 0; i < dim_num; ++i) {
    auto dim_range_num = ranges[i].size() / 2;
    for (size_t j = 0; j < dim_range_num; ++j) {
      ret.add_range(i, &ranges[i][2 * j]);
    }
  }

  *subarray = ret;
}

void get_supported_fs(bool* s3_supported, bool* hdfs_supported) {
  tiledb_ctx_t* ctx = nullptr;
  REQUIRE(tiledb_ctx_alloc(nullptr, &ctx) == TILEDB_OK);

  int is_supported = 0;
  int rc = tiledb_ctx_is_supported_fs(ctx, TILEDB_S3, &is_supported);
  REQUIRE(rc == TILEDB_OK);
  *s3_supported = (bool)is_supported;
  rc = tiledb_ctx_is_supported_fs(ctx, TILEDB_HDFS, &is_supported);
  REQUIRE(rc == TILEDB_OK);
  *hdfs_supported = (bool)is_supported;

  tiledb_ctx_free(&ctx);
}

void open_array(
    tiledb_ctx_t* ctx, tiledb_array_t* array, tiledb_query_type_t query_type) {
  int rc = tiledb_array_open(ctx, array, query_type);
  CHECK(rc == TILEDB_OK);
}

std::string random_bucket_name(const std::string& prefix) {
  std::stringstream ss;
  ss << prefix << "-" << std::this_thread::get_id() << "-"
     << TILEDB_TIMESTAMP_NOW_MS;
  return ss.str();
}

void remove_dir(const std::string& path, tiledb_ctx_t* ctx, tiledb_vfs_t* vfs) {
  int is_dir = 0;
  REQUIRE(tiledb_vfs_is_dir(ctx, vfs, path.c_str(), &is_dir) == TILEDB_OK);
  if (is_dir)
    REQUIRE(tiledb_vfs_remove_dir(ctx, vfs, path.c_str()) == TILEDB_OK);
}

void remove_s3_bucket(
    const std::string& bucket_name,
    bool s3_supported,
    tiledb_ctx_t* ctx,
    tiledb_vfs_t* vfs) {
  if (s3_supported) {
    int is_bucket = 0;
    int rc = tiledb_vfs_is_bucket(ctx, vfs, bucket_name.c_str(), &is_bucket);
    CHECK(rc == TILEDB_OK);
    if (is_bucket) {
      rc = tiledb_vfs_remove_bucket(ctx, vfs, bucket_name.c_str());
      CHECK(rc == TILEDB_OK);
    }
  }
}

int set_attribute_compression_filter(
    tiledb_ctx_t* ctx,
    tiledb_attribute_t* attr,
    tiledb_filter_type_t compressor,
    int32_t level) {
  if (compressor == TILEDB_FILTER_NONE)
    return TILEDB_OK;

  tiledb_filter_t* filter;
  int rc = tiledb_filter_alloc(ctx, compressor, &filter);
  REQUIRE(rc == TILEDB_OK);
  rc = tiledb_filter_set_option(ctx, filter, TILEDB_COMPRESSION_LEVEL, &level);
  REQUIRE(rc == TILEDB_OK);
  tiledb_filter_list_t* list;
  rc = tiledb_filter_list_alloc(ctx, &list);
  REQUIRE(rc == TILEDB_OK);
  rc = tiledb_filter_list_add_filter(ctx, list, filter);
  REQUIRE(rc == TILEDB_OK);
  rc = tiledb_attribute_set_filter_list(ctx, attr, list);
  REQUIRE(rc == TILEDB_OK);

  tiledb_filter_free(&filter);
  tiledb_filter_list_free(&list);

  return TILEDB_OK;
}

void write_array(
    tiledb_ctx_t* ctx,
    const std::string& array_name,
    tiledb_layout_t layout,
    const AttrBuffers& attr_buffers) {
  // Open array
  tiledb_array_t* array;
  int rc = tiledb_array_alloc(ctx, array_name.c_str(), &array);
  CHECK(rc == TILEDB_OK);
  rc = tiledb_array_open(ctx, array, TILEDB_WRITE);
  CHECK(rc == TILEDB_OK);

  // Create query
  tiledb_query_t* query;
  rc = tiledb_query_alloc(ctx, array, TILEDB_WRITE, &query);
  CHECK(rc == TILEDB_OK);
  rc = tiledb_query_set_layout(ctx, query, layout);
  CHECK(rc == TILEDB_OK);

  // Set buffers
  for (const auto& b : attr_buffers) {
    if (b.second.var_ == nullptr) {  // Fixed-sized
      rc = tiledb_query_set_buffer(
          ctx,
          query,
          b.first.c_str(),
          b.second.fixed_,
          (uint64_t*)&(b.second.fixed_size_));
      CHECK(rc == TILEDB_OK);
    } else {  // Var-sized
      rc = tiledb_query_set_buffer_var(
          ctx,
          query,
          b.first.c_str(),
          (uint64_t*)b.second.fixed_,
          (uint64_t*)&(b.second.fixed_size_),
          b.second.var_,
          (uint64_t*)&(b.second.var_size_));
      CHECK(rc == TILEDB_OK);
    }
  }

  // Submit query
  rc = tiledb_query_submit(ctx, query);
  CHECK(rc == TILEDB_OK);

  // Finalize query
  rc = tiledb_query_finalize(ctx, query);
  CHECK(rc == TILEDB_OK);

  // Close array
  rc = tiledb_array_close(ctx, array);
  CHECK(rc == TILEDB_OK);

  // Clean up
  tiledb_array_free(&array);
  tiledb_query_free(&query);
}

template <class T>
void read_array(
    tiledb_ctx_t* ctx,
    tiledb_array_t* array,
    const SubarrayRanges<T>& ranges,
    tiledb_layout_t layout,
    const AttrBuffers& attr_buffers) {
  // Create query
  tiledb_query_t* query;
  int rc = tiledb_query_alloc(ctx, array, TILEDB_READ, &query);
  CHECK(rc == TILEDB_OK);
  rc = tiledb_query_set_layout(ctx, query, layout);
  CHECK(rc == TILEDB_OK);

  auto dim_num = (unsigned)ranges.size();
  for (unsigned i = 0; i < dim_num; ++i) {
    auto dim_range_num = ranges[i].size() / 2;
    for (size_t j = 0; j < dim_range_num; ++j) {
      rc = tiledb_query_add_range(
          ctx, query, i, &ranges[i][2 * j], &ranges[i][2 * j + 1], nullptr);
      CHECK(rc == TILEDB_OK);
    }
  }

  // Set buffers
  for (const auto& b : attr_buffers) {
    if (b.second.var_ == nullptr) {  // Fixed-sized
      rc = tiledb_query_set_buffer(
          ctx,
          query,
          b.first.c_str(),
          b.second.fixed_,
          (uint64_t*)&(b.second.fixed_size_));
      CHECK(rc == TILEDB_OK);
    } else {  // Var-sized
      rc = tiledb_query_set_buffer_var(
          ctx,
          query,
          b.first.c_str(),
          (uint64_t*)b.second.fixed_,
          (uint64_t*)&(b.second.fixed_size_),
          b.second.var_,
          (uint64_t*)&(b.second.var_size_));
      CHECK(rc == TILEDB_OK);
    }
  }

  // Submit query
  rc = tiledb_query_submit(ctx, query);
  CHECK(rc == TILEDB_OK);

  // Check status
  tiledb_query_status_t status;
  rc = tiledb_query_get_status(ctx, query, &status);
  CHECK(rc == TILEDB_OK);
  CHECK(status == TILEDB_COMPLETED);

  // Clean up
  tiledb_query_free(&query);
}

// Explicit template instantiations

template void check_subarray<int8_t>(
    tiledb::sm::Subarray& subarray, const SubarrayRanges<int8_t>& ranges);
template void check_subarray<uint8_t>(
    tiledb::sm::Subarray& subarray, const SubarrayRanges<uint8_t>& ranges);
template void check_subarray<int16_t>(
    tiledb::sm::Subarray& subarray, const SubarrayRanges<int16_t>& ranges);
template void check_subarray<uint16_t>(
    tiledb::sm::Subarray& subarray, const SubarrayRanges<uint16_t>& ranges);
template void check_subarray<int32_t>(
    tiledb::sm::Subarray& subarray, const SubarrayRanges<int32_t>& ranges);
template void check_subarray<uint32_t>(
    tiledb::sm::Subarray& subarray, const SubarrayRanges<uint32_t>& ranges);
template void check_subarray<int64_t>(
    tiledb::sm::Subarray& subarray, const SubarrayRanges<int64_t>& ranges);
template void check_subarray<uint64_t>(
    tiledb::sm::Subarray& subarray, const SubarrayRanges<uint64_t>& ranges);
template void check_subarray<float>(
    tiledb::sm::Subarray& subarray, const SubarrayRanges<float>& ranges);
template void check_subarray<double>(
    tiledb::sm::Subarray& subarray, const SubarrayRanges<double>& ranges);

template void create_subarray<int8_t>(
    tiledb::sm::Array* array,
    const SubarrayRanges<int8_t>& ranges,
    tiledb::sm::Layout layout,
    tiledb::sm::Subarray* subarray);
template void create_subarray<uint8_t>(
    tiledb::sm::Array* array,
    const SubarrayRanges<uint8_t>& ranges,
    tiledb::sm::Layout layout,
    tiledb::sm::Subarray* subarray);
template void create_subarray<int16_t>(
    tiledb::sm::Array* array,
    const SubarrayRanges<int16_t>& ranges,
    tiledb::sm::Layout layout,
    tiledb::sm::Subarray* subarray);
template void create_subarray<uint16_t>(
    tiledb::sm::Array* array,
    const SubarrayRanges<uint16_t>& ranges,
    tiledb::sm::Layout layout,
    tiledb::sm::Subarray* subarray);
template void create_subarray<int32_t>(
    tiledb::sm::Array* array,
    const SubarrayRanges<int32_t>& ranges,
    tiledb::sm::Layout layout,
    tiledb::sm::Subarray* subarray);
template void create_subarray<uint32_t>(
    tiledb::sm::Array* array,
    const SubarrayRanges<uint32_t>& ranges,
    tiledb::sm::Layout layout,
    tiledb::sm::Subarray* subarray);
template void create_subarray<int64_t>(
    tiledb::sm::Array* array,
    const SubarrayRanges<int64_t>& ranges,
    tiledb::sm::Layout layout,
    tiledb::sm::Subarray* subarray);
template void create_subarray<uint64_t>(
    tiledb::sm::Array* array,
    const SubarrayRanges<uint64_t>& ranges,
    tiledb::sm::Layout layout,
    tiledb::sm::Subarray* subarray);
template void create_subarray<float>(
    tiledb::sm::Array* array,
    const SubarrayRanges<float>& ranges,
    tiledb::sm::Layout layout,
    tiledb::sm::Subarray* subarray);
template void create_subarray<double>(
    tiledb::sm::Array* array,
    const SubarrayRanges<double>& ranges,
    tiledb::sm::Layout layout,
    tiledb::sm::Subarray* subarray);

template void check_partitions<int8_t>(
    tiledb::sm::SubarrayPartitioner& partitioner,
    const std::vector<SubarrayRanges<int8_t>>& partitions,
    bool last_unsplittable);
template void check_partitions<uint8_t>(
    tiledb::sm::SubarrayPartitioner& partitioner,
    const std::vector<SubarrayRanges<uint8_t>>& partitions,
    bool last_unsplittable);
template void check_partitions<int16_t>(
    tiledb::sm::SubarrayPartitioner& partitioner,
    const std::vector<SubarrayRanges<int16_t>>& partitions,
    bool last_unsplittable);
template void check_partitions<uint16_t>(
    tiledb::sm::SubarrayPartitioner& partitioner,
    const std::vector<SubarrayRanges<uint16_t>>& partitions,
    bool last_unsplittable);
template void check_partitions<int32_t>(
    tiledb::sm::SubarrayPartitioner& partitioner,
    const std::vector<SubarrayRanges<int32_t>>& partitions,
    bool last_unsplittable);
template void check_partitions<uint32_t>(
    tiledb::sm::SubarrayPartitioner& partitioner,
    const std::vector<SubarrayRanges<uint32_t>>& partitions,
    bool last_unsplittable);
template void check_partitions<int64_t>(
    tiledb::sm::SubarrayPartitioner& partitioner,
    const std::vector<SubarrayRanges<int64_t>>& partitions,
    bool last_unsplittable);
template void check_partitions<uint64_t>(
    tiledb::sm::SubarrayPartitioner& partitioner,
    const std::vector<SubarrayRanges<uint64_t>>& partitions,
    bool last_unsplittable);
template void check_partitions<float>(
    tiledb::sm::SubarrayPartitioner& partitioner,
    const std::vector<SubarrayRanges<float>>& partitions,
    bool last_unsplittable);
template void check_partitions<double>(
    tiledb::sm::SubarrayPartitioner& partitioner,
    const std::vector<SubarrayRanges<double>>& partitions,
    bool last_unsplittable);

template void read_array<int8_t>(
    tiledb_ctx_t* ctx,
    tiledb_array_t* array,
    const SubarrayRanges<int8_t>& ranges,
    tiledb_layout_t layout,
    const AttrBuffers& attr_buffers);
template void read_array<uint8_t>(
    tiledb_ctx_t* ctx,
    tiledb_array_t* array,
    const SubarrayRanges<uint8_t>& ranges,
    tiledb_layout_t layout,
    const AttrBuffers& attr_buffers);
template void read_array<int16_t>(
    tiledb_ctx_t* ctx,
    tiledb_array_t* array,
    const SubarrayRanges<int16_t>& ranges,
    tiledb_layout_t layout,
    const AttrBuffers& attr_buffers);
template void read_array<uint16_t>(
    tiledb_ctx_t* ctx,
    tiledb_array_t* array,
    const SubarrayRanges<uint16_t>& ranges,
    tiledb_layout_t layout,
    const AttrBuffers& attr_buffers);
template void read_array<int32_t>(
    tiledb_ctx_t* ctx,
    tiledb_array_t* array,
    const SubarrayRanges<int32_t>& ranges,
    tiledb_layout_t layout,
    const AttrBuffers& attr_buffers);
template void read_array<uint32_t>(
    tiledb_ctx_t* ctx,
    tiledb_array_t* array,
    const SubarrayRanges<uint32_t>& ranges,
    tiledb_layout_t layout,
    const AttrBuffers& attr_buffers);
template void read_array<int64_t>(
    tiledb_ctx_t* ctx,
    tiledb_array_t* array,
    const SubarrayRanges<int64_t>& ranges,
    tiledb_layout_t layout,
    const AttrBuffers& attr_buffers);
template void read_array<uint64_t>(
    tiledb_ctx_t* ctx,
    tiledb_array_t* array,
    const SubarrayRanges<uint64_t>& ranges,
    tiledb_layout_t layout,
    const AttrBuffers& attr_buffers);
template void read_array<float>(
    tiledb_ctx_t* ctx,
    tiledb_array_t* array,
    const SubarrayRanges<float>& ranges,
    tiledb_layout_t layout,
    const AttrBuffers& attr_buffers);
template void read_array<double>(
    tiledb_ctx_t* ctx,
    tiledb_array_t* array,
    const SubarrayRanges<double>& ranges,
    tiledb_layout_t layout,
    const AttrBuffers& attr_buffers);
