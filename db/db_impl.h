// Copyright (c) 2011 The LevelDB Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file. See the AUTHORS file for names of contributors.

#ifndef STORAGE_LEVELDB_DB_DB_IMPL_H_
#define STORAGE_LEVELDB_DB_DB_IMPL_H_

#include <atomic>
#include <deque>
#include <set>
#include <string>

#include "db/dbformat.h"
#include "db/log_writer.h"
#include "db/snapshot.h"
#include "leveldb/db.h"
#include "leveldb/env.h"
#include "port/port.h"
#include "port/thread_annotations.h"

namespace leveldb {

class MemTable;
class TableCache;
class Version;
class VersionEdit;
class VersionSet;

/// @brief LevelDB 的核心实现类，继承自 DB 接口类。
///        负责管理数据库的读写操作、MemTable、SST 文件、版本控制等。
class DBImpl : public DB {
 public:
  /// @brief 构造函数，初始化数据库实例。
  /// @param options 数据库选项。
  /// @param dbname 数据库名称。
  DBImpl(const Options& options, const std::string& dbname);

  // 禁止拷贝构造函数和赋值操作符。
  DBImpl(const DBImpl&) = delete;
  DBImpl& operator=(const DBImpl&) = delete;

  /// @brief 析构函数，释放资源。
  ~DBImpl() override;

  // 实现 DB 接口的方法

  /// @brief 插入键值对。
  /// @param options 写入选项。
  /// @param key 要插入的键。
  /// @param value 要插入的值。
  /// @return 操作状态。
  Status Put(const WriteOptions&, const Slice& key, const Slice& value) override;

  /// @brief 删除指定键。
  /// @param options 写入选项。
  /// @param key 要删除的键。
  /// @return 操作状态。
  Status Delete(const WriteOptions&, const Slice& key) override;

  /// @brief 批量写入操作。
  /// @param options 写入选项。
  /// @param updates 批量写入的数据。
  /// @return 操作状态。
  Status Write(const WriteOptions& options, WriteBatch* updates) override;

  /// @brief 读取指定键的值。
  /// @param options 读取选项。
  /// @param key 要读取的键。
  /// @param value 读取到的值。
  /// @return 操作状态。
  Status Get(const ReadOptions& options, const Slice& key, std::string* value) override;

  /// @brief 创建一个迭代器，用于遍历数据库中的键值对。
  /// @param options 读取选项。
  /// @return 迭代器指针。
  Iterator* NewIterator(const ReadOptions&) override;

  /// @brief 获取当前数据库的快照。
  /// @return 快照指针。
  const Snapshot* GetSnapshot() override;

  /// @brief 释放快照。
  /// @param snapshot 要释放的快照。
  void ReleaseSnapshot(const Snapshot* snapshot) override;

  /// @brief 获取数据库的某个属性值。
  /// @param property 属性名称。
  /// @param value 属性值。
  /// @return 是否成功获取属性。
  bool GetProperty(const Slice& property, std::string* value) override;

  /// @brief 估算指定键范围占用的文件系统空间。
  /// @param range 键范围数组。
  /// @param n 键范围的数量。
  /// @param sizes 每个键范围占用的空间大小。
  void GetApproximateSizes(const Range* range, int n, uint64_t* sizes) override;

  /// @brief 压缩指定键范围的数据。
  /// @param begin 压缩的起始键。
  /// @param end 压缩的结束键。
  void CompactRange(const Slice* begin, const Slice* end) override;

  // 以下方法用于测试，不属于公共接口

  /// @brief 压缩指定层级中与 [begin, end] 重叠的文件。
  /// @param level 压缩的层级。
  /// @param begin 压缩的起始键。
  /// @param end 压缩的结束键。
  void TEST_CompactRange(int level, const Slice* begin, const Slice* end);

  /// @brief 强制将当前 MemTable 的内容压缩到磁盘。
  /// @return 操作状态。
  Status TEST_CompactMemTable();

  /// @brief 返回一个内部迭代器，用于遍历数据库的当前状态。
  ///        该迭代器的键是内部键（参见 format.h）。
  /// @return 内部迭代器指针。
  Iterator* TEST_NewInternalIterator();

  /// @brief 返回任何层级 >= 1 的文件在下一层级的最大重叠数据量（字节）。
  /// @return 最大重叠数据量。
  int64_t TEST_MaxNextLevelOverlappingBytes();

  /// @brief 记录在指定内部键处读取的字节数样本。
  ///        大约每读取 config::kReadBytesPeriod 字节记录一次样本。
  /// @param key 内部键。
  void RecordReadSample(Slice key);

 private:
  friend class DB;
  struct CompactionState;
  struct Writer;

  /// @brief 手动压缩的信息。
  struct ManualCompaction {
    /// @brief 压缩的层级。
    int level;

    /// @brief 压缩是否完成。
    bool done;

    /// @brief 压缩的起始键，null 表示键范围的开始。
    const InternalKey* begin;

    /// @brief 压缩的结束键，null 表示键范围的结束。
    const InternalKey* end;

    /// @brief 用于跟踪压缩进度。
    InternalKey tmp_storage;
  };

  /// @brief 每个层级的压缩统计信息。
  ///        stats_[level] 存储了生成该层级数据的压缩统计信息。
  struct CompactionStats {
    CompactionStats() : micros(0), bytes_read(0), bytes_written(0) {}

    /// @brief 累加另一个 CompactionStats 的值。
    /// @param c 要累加的统计信息。
    void Add(const CompactionStats& c) {
      this->micros += c.micros;
      this->bytes_read += c.bytes_read;
      this->bytes_written += c.bytes_written;
    }

    /// @brief 压缩耗时（微秒）。
    int64_t micros;

    /// @brief 读取的字节数。
    int64_t bytes_read;

    /// @brief 写入的字节数。
    int64_t bytes_written;
  };

  /// @brief 创建一个内部迭代器，用于遍历数据库的当前状态。
  /// @param options 读取选项。
  /// @param latest_snapshot 最新的快照序列号。
  /// @param seed 随机种子。
  /// @return 内部迭代器指针。
  Iterator* NewInternalIterator(const ReadOptions&, SequenceNumber* latest_snapshot, uint32_t* seed);

  /// @brief 创建一个新的数据库。
  /// @return 操作状态。
  Status NewDB();

  /// @brief 从持久化存储中恢复描述符。
  ///        可能会执行大量工作以恢复最近记录的更新。
  /// @param edit 对描述符的更改会添加到此处。
  /// @param save_manifest 是否需要保存清单文件。
  /// @return 操作状态。
  Status Recover(VersionEdit* edit, bool* save_manifest)
      EXCLUSIVE_LOCKS_REQUIRED(mutex_);

  /// @brief 忽略某些错误。
  /// @param s 错误状态。
  void MaybeIgnoreError(Status* s) const;

  /// @brief 删除不需要的文件和过时的内存条目。
  void RemoveObsoleteFiles() EXCLUSIVE_LOCKS_REQUIRED(mutex_);

  /// @brief 将内存中的写缓冲区压缩到磁盘。
  ///        如果成功，切换到新的日志文件/MemTable 并写入新的描述符。
  ///        错误会记录在 bg_error_ 中。
  void CompactMemTable() EXCLUSIVE_LOCKS_REQUIRED(mutex_);

  /// @brief 恢复日志文件。
  /// @param log_number 日志文件编号。
  /// @param last_log 是否是最后一个日志文件。
  /// @param save_manifest 是否需要保存清单文件。
  /// @param edit 对描述符的更改会添加到此处。
  /// @param max_sequence 最大序列号。
  /// @return 操作状态。
  Status RecoverLogFile(uint64_t log_number, bool last_log, bool* save_manifest,
                        VersionEdit* edit, SequenceNumber* max_sequence)
      EXCLUSIVE_LOCKS_REQUIRED(mutex_);

  /// @brief 将 MemTable 写入 Level 0 的 SST 文件。
  /// @param mem 要写入的 MemTable。
  /// @param edit 对描述符的更改会添加到此处。
  /// @param base 当前版本。
  /// @return 操作状态。
  Status WriteLevel0Table(MemTable* mem, VersionEdit* edit, Version* base)
      EXCLUSIVE_LOCKS_REQUIRED(mutex_);

  /// @brief 为写入操作腾出空间。
  /// @param force 即使有空间也压缩？
  /// @return 操作状态。
  Status MakeRoomForWrite(bool force)
      EXCLUSIVE_LOCKS_REQUIRED(mutex_);

  /// @brief 构建一个批量写入组。
  /// @param last_writer 最后一个写入者。
  /// @return 批量写入组。
  WriteBatch* BuildBatchGroup(Writer** last_writer)
      EXCLUSIVE_LOCKS_REQUIRED(mutex_);

  /// @brief 记录后台错误。
  /// @param s 错误状态。
  void RecordBackgroundError(const Status& s);

  /// @brief 可能调度压缩任务。
  void MaybeScheduleCompaction() EXCLUSIVE_LOCKS_REQUIRED(mutex_);

  /// @brief 后台工作线程的入口函数。
  /// @param db DBImpl 实例。
  static void BGWork(void* db);

  /// @brief 后台工作线程的主逻辑。
  void BackgroundCall();

  /// @brief 执行后台压缩任务。
  void BackgroundCompaction() EXCLUSIVE_LOCKS_REQUIRED(mutex_);

  /// @brief 清理压缩状态。
  /// @param compact 压缩状态。
  void CleanupCompaction(CompactionState* compact)
      EXCLUSIVE_LOCKS_REQUIRED(mutex_);

  /// @brief 执行压缩工作。
  /// @param compact 压缩状态。
  /// @return 操作状态。
  Status DoCompactionWork(CompactionState* compact)
      EXCLUSIVE_LOCKS_REQUIRED(mutex_);

  /// @brief 打开压缩输出文件。
  /// @param compact 压缩状态。
  /// @return 操作状态。
  Status OpenCompactionOutputFile(CompactionState* compact);

  /// @brief 完成压缩输出文件。
  /// @param compact 压缩状态。
  /// @param input 输入迭代器。
  /// @return 操作状态。
  Status FinishCompactionOutputFile(CompactionState* compact, Iterator* input);

  /// @brief 安装压缩结果。
  /// @param compact 压缩状态。
  /// @return 操作状态。
  Status InstallCompactionResults(CompactionState* compact)
      EXCLUSIVE_LOCKS_REQUIRED(mutex_);

  /// @brief 返回用户比较器。
  /// @return 用户比较器。
  const Comparator* user_comparator() const {
    return internal_comparator_.user_comparator();
  }

  // 以下成员在构造后是常量
  /// @brief 环境接口，用于文件系统和线程操作。
  Env* const env_;

  /// @brief 内部键比较器。
  const InternalKeyComparator internal_comparator_;

  /// @brief 内部过滤策略。
  const InternalFilterPolicy internal_filter_policy_;

  /// @brief 数据库选项。
  const Options options_;

  /// @brief 是否拥有 info_log_ 的所有权。
  const bool owns_info_log_;

  /// @brief 是否拥有 cache_ 的所有权。
  const bool owns_cache_;

  /// @brief 数据库名称。
  const std::string dbname_;

  // table_cache_ 提供自己的同步机制
  /// @brief 表缓存，用于管理打开的 SST 文件。
  TableCache* const table_cache_;

  // 保护持久化数据库状态的锁。如果成功获取，则非空。
  /// @brief 用于保护持久化数据库状态的锁，成功获取时非空。
  FileLock* db_lock_;

  // 以下状态由 mutex_ 保护

  /// @brief 互斥锁，用于保护共享状态。
  port::Mutex mutex_;

  /// @brief 数据库是否正在关闭。
  std::atomic<bool> shutting_down_;

  /// @brief 后台工作完成信号，由 mutex_ 保护。
  port::CondVar background_work_finished_signal_ GUARDED_BY(mutex_); 

  /// @brief 当前的 MemTable。
  MemTable* mem_;

  /// @brief 正在被压缩的 MemTable，由 mutex_ 保护。
  MemTable* imm_ GUARDED_BY(mutex_);

  /// @brief 用于后台线程检测非空的 imm_。
  std::atomic<bool> has_imm_;

  /// @brief 当前的日志文件。
  WritableFile* logfile_;

  /// @brief 日志文件编号，由 mutex_ 保护。
  uint64_t logfile_number_ GUARDED_BY(mutex_);

  /// @brief 日志写入器。
  log::Writer* log_;
 
  /// @brief 用于采样，由 mutex_ 保护。
  uint32_t seed_ GUARDED_BY(mutex_);

  // 写入者队列。
  /// @brief 写入者队列，由 mutex_ 保护。
  std::deque<Writer*> writers_ GUARDED_BY(mutex_);

  /// @brief 临时的 WriteBatch，由 mutex_ 保护。
  WriteBatch* tmp_batch_ GUARDED_BY(mutex_);

  // 快照列表。
  /// @brief 快照列表，由 mutex_ 保护。
  SnapshotList snapshots_ GUARDED_BY(mutex_);

  // 由于正在进行压缩而需要保护的文件集合。
  /// @brief 由于正在进行压缩而需要保护的文件集合，由 mutex_ 保护。
  std::set<uint64_t> pending_outputs_ GUARDED_BY(mutex_);

  // 是否已调度或正在运行后台压缩任务？
  /// @brief 是否已调度或正在运行后台压缩任务，由 mutex_ 保护。
  bool background_compaction_scheduled_ GUARDED_BY(mutex_);

  // 手动压缩任务。
  /// @brief 手动压缩任务，由 mutex_ 保护。
  ManualCompaction* manual_compaction_ GUARDED_BY(mutex_);

  // 版本集合。
  /// @brief 版本集合，由 mutex_ 保护。
  VersionSet* const versions_ GUARDED_BY(mutex_);

  /// @brief 在 paranoid 模式下是否遇到了后台错误？由 mutex_ 保护。
  Status bg_error_ GUARDED_BY(mutex_);

  // 每个层级的压缩统计信息。
  /// @brief 每个层级的压缩统计信息，由 mutex_ 保护。
  CompactionStats stats_[config::kNumLevels] GUARDED_BY(mutex_);
};

/// @brief 清理数据库选项。
///        如果 result.info_log 不等于 src.info_log，调用者需要删除它。
/// @param db 数据库名称。
/// @param icmp 内部键比较器。
/// @param ipolicy 内部过滤策略。
/// @param src 原始选项。
/// @return 清理后的选项。
Options SanitizeOptions(const std::string& db,
                        const InternalKeyComparator* icmp,
                        const InternalFilterPolicy* ipolicy,
                        const Options& src);

}  // namespace leveldb

#endif  // STORAGE_LEVELDB_DB_DB_IMPL_H_