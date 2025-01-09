// Copyright (c) 2011 The LevelDB Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file. See the AUTHORS file for names of contributors.

#ifndef STORAGE_LEVELDB_INCLUDE_DB_H_
#define STORAGE_LEVELDB_INCLUDE_DB_H_

#include <cstdint>
#include <cstdio>

#include "leveldb/export.h"
#include "leveldb/iterator.h"
#include "leveldb/options.h"

namespace leveldb {

// 当前leveldb的版本
static const int kMajorVersion = 1;
static const int kMinorVersion = 23;

//
struct Options;
struct ReadOptions;
struct WriteOptions;


class WriteBatch;

// 数据库特定状态的抽象句柄。
// Snapshot 是一个不可变对象，因此可以在没有任何外部同步的情况下安全地从多个线程访问。
class LEVELDB_EXPORT Snapshot {
 protected:
  virtual ~Snapshot();
};

// 键的范围
struct LEVELDB_EXPORT Range {
  Range() = default;
  Range(const Slice& s, const Slice& l) : start(s), limit(l) {}

  Slice start;  // 包含在范围内
  Slice limit;  // 不包含在范围内
};

// DB 是一个从键到值的持久化有序映射。
// DB 可以在没有任何外部同步的情况下安全地从多个线程并发访问。
class LEVELDB_EXPORT DB {
 public:
  // 使用指定的 "name" 打开数据库。
  // 在 *dbptr 中存储指向堆分配的数据库的指针，并在成功时返回 OK。
  // 在错误时在 *dbptr 中存储 nullptr 并返回非 OK 状态。
  // 调用者应在不再需要时删除 *dbptr。
  static Status Open(const Options& options, const std::string& name,
                     DB** dbptr);

  DB() = default;

  DB(const DB&) = delete;
  DB& operator=(const DB&) = delete;

  virtual ~DB();

  // 将数据库中的 "key" 条目设置为 "value"。成功时返回 OK，错误时返回非 OK 状态。
  // 注意：考虑设置 options.sync = true。
  virtual Status Put(const WriteOptions& options, const Slice& key,
                     const Slice& value) = 0;

  // 删除数据库中的 "key" 条目（如果有）。成功时返回 OK，错误时返回非 OK 状态。
  // 如果 "key" 在数据库中不存在，则不是错误。
  // 注意：考虑设置 options.sync = true。
  virtual Status Delete(const WriteOptions& options, const Slice& key) = 0;

  // 将指定的更新应用到数据库。
  // 成功时返回 OK，失败时返回非 OK。
  // 注意：考虑设置 options.sync = true。
  virtual Status Write(const WriteOptions& options, WriteBatch* updates) = 0;

  // 如果数据库包含 "key" 的条目，则将相应的值存储在 *value 中并返回 OK。
  //
  // 如果没有 "key" 的条目，则保持 *value 不变并返回 Status::IsNotFound() 为 true 的状态。
  //
  // 在错误时可能返回其他状态。
  virtual Status Get(const ReadOptions& options, const Slice& key,
                     std::string* value) = 0;

  // 返回一个堆分配的内容迭代器。
  // NewIterator() 的结果最初是无效的（调用者必须在使用它之前调用迭代器的 Seek 方法之一）。
  //
  // 调用者应在不再需要时删除迭代器。
  // 返回的迭代器应在此 db 被删除之前删除。
  virtual Iterator* NewIterator(const ReadOptions& options) = 0;

  // 返回当前数据库状态的句柄。使用此句柄创建的迭代器将观察到当前数据库状态的稳定快照。
  // 调用者必须在快照不再需要时调用 ReleaseSnapshot(result)。
  virtual const Snapshot* GetSnapshot() = 0;

  // 释放先前获取的快照。调用者在此调用后不得使用 "snapshot"。
  virtual void ReleaseSnapshot(const Snapshot* snapshot) = 0;

  // DB 实现可以通过此方法导出有关其状态的属性。如果 "property" 是此 DB 实现理解的有效属性，
  // 则将 "*value" 填充为其当前值并返回 true。否则返回 false。
  //
  //
  // 有效的属性名称包括：
  //
  //  "leveldb.num-files-at-level<N>" - 返回级别 <N> 的文件数，
  //     其中 <N> 是级别号的 ASCII 表示（例如 "0"）。
  //  "leveldb.stats" - 返回描述数据库内部操作统计信息的多行字符串。
  //  "leveldb.sstables" - 返回描述构成数据库内容的所有 sstable 的多行字符串。
  //  "leveldb.approximate-memory-usage" - 返回数据库使用的近似内存字节数。
  virtual bool GetProperty(const Slice& property, std::string* value) = 0;

  // 对于每个 i 在 [0,n-1] 中，将 "[range[i].start .. range[i].limit)" 范围内的键
  // 使用的近似文件系统空间存储在 "sizes[i]" 中。
  //
  // 注意，返回的大小测量文件系统空间使用情况，因此如果用户数据压缩了十倍，
  // 返回的大小将是相应用户数据大小的十分之一。
  //
  // 结果可能不包括最近写入数据的大小。
  virtual void GetApproximateSizes(const Range* range, int n,
                                   uint64_t* sizes) = 0;

  // 压缩键范围 [*begin,*end] 的底层存储。
  // 特别是，删除和覆盖的版本将被丢弃，并重新排列数据以减少访问数据所需的操作成本。
  // 通常只有了解底层实现的用户才应调用此操作。
  //
  // begin==nullptr 被视为数据库所有键之前的键。
  // end==nullptr 被视为数据库所有键之后的键。
  // 因此，以下调用将压缩整个数据库：
  //    db->CompactRange(nullptr, nullptr);
  virtual void CompactRange(const Slice* begin, const Slice* end) = 0;
};

// 销毁指定数据库的内容。
// 使用此方法时要非常小心。
//
// 注意：为了向后兼容，如果 DestroyDB 无法列出数据库文件，仍将返回 Status::OK() 以掩盖此失败。
LEVELDB_EXPORT Status DestroyDB(const std::string& name,
                                const Options& options);

// 如果无法打开 DB，您可以尝试调用此方法以尽可能恢复数据库的内容。
// 可能会丢失一些数据，因此在包含重要信息的数据库上调用此函数时要小心。
LEVELDB_EXPORT Status RepairDB(const std::string& dbname,
                               const Options& options);

}  // namespace leveldb

#endif  // STORAGE_LEVELDB_INCLUDE_DB_H_