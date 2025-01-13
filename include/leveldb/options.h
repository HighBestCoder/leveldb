// Copyright (c) 2011 The LevelDB Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file. See the AUTHORS file for names of contributors.

#ifndef STORAGE_LEVELDB_INCLUDE_OPTIONS_H_
#define STORAGE_LEVELDB_INCLUDE_OPTIONS_H_

#include <cstddef>

#include "leveldb/export.h"

namespace leveldb {

class Cache;
class Comparator;
class Env;
class FilterPolicy;
class Logger;
class Snapshot;

/// @brief 用于定义压缩块的压缩方法。
/// 数据库内容存储在一组块中，每个块包含一系列键值对。
/// 每个块在存储到文件之前可能会被压缩。
/// 以下枚举描述了用于压缩块的压缩方法（如果有）。
enum CompressionType {
    /// @brief 无压缩
    /// 注意：不要更改现有条目的值，因为这些值是磁盘上持久化格式的一部分。
    kNoCompression = 0x0,
    /// @brief Snappy压缩
    kSnappyCompression = 0x1,
    /// @brief Zstd压缩
    kZstdCompression = 0x2,
};

// 控制数据库行为的选项（传递给 DB::Open）
struct LEVELDB_EXPORT Options {
    // 创建一个具有默认值的 Options 对象。
    Options();

    // -------------------
    // 影响行为的参数

    /// @brief 用于定义表中键顺序的比较器
    /// 默认：使用字典序字节顺序的比较器。
    /// 要求：客户端必须确保此处提供的比较器与之前对同一数据库
    /// 的打开调用中提供的比较器具有相同的名称和键顺序。
    /// 构造函数中会使用 BytewiseComparator() 作为默认值。
    const Comparator* comparator;

    /// @brief 如果为 true，当数据库缺失时会创建数据库
    bool create_if_missing = false;

    /// @brief 如果为 true，当数据库已存在时会引发错误
    bool error_if_exists = false;

    /// @brief 如果为 true，实现会对正在处理的数据进行严格检查，并在检测到任何错误时提前停止
    bool paranoid_checks = false;

    /// @brief 用于与环境交互的对象，例如读写文件、调度后台工作等
    /// 默认：Env::Default()
    /// 构造函数中会使用 Env::Default() 作为默认值。
    Env* env;

    /// @brief 任何由数据库生成的内部进度/错误信息将被写入此 Logger（如果非空），
    /// 或者写入与数据库内容存储在同一目录中的文件（如果 info_log 为空）
    Logger* info_log = nullptr;

    // -------------------
    // 影响性能的参数

    /// @brief 在内存中积累的数据量（由磁盘上的未排序日志支持），然后转换为排序的磁盘文件
    /// 较大的值会提高性能，尤其是在批量加载期间。
    /// 最多可以同时在内存中保存两个写缓冲区，因此您可能需要调整此参数以控制内存使用。
    /// 此外，较大的写缓冲区将导致下次打开数据库时恢复时间更长。
    size_t write_buffer_size = 4 * 1024 * 1024;

    /// @brief 数据库可以使用的打开文件的数量
    int max_open_files = 1000;

    // 控制块（用户数据存储在一组块中，块是从磁盘读取的单位）。

    /// @brief 用于存储块的缓存，如果为空，leveldb 将自动创建并使用一个 8MB 的内部缓存
    Cache* block_cache = nullptr;

    /// @brief 每个块中打包的用户数据的近似大小，指定的是未压缩的数据大小
    size_t block_size = 4 * 1024;

    /// @brief 键之间的增量编码的重启点数量
    int block_restart_interval = 16;

    /// @brief Leveldb 在切换到新文件之前将向文件写入最多此数量的字节
    size_t max_file_size = 2 * 1024 * 1024;

    /// @brief 用于压缩块的压缩算法
    /// 使用指定的压缩算法压缩块。此参数可以动态更改。
    /// 默认：kSnappyCompression，它提供轻量级但快速的压缩。
    /// 在 Intel(R) Core(TM)2 2.4GHz 上，kSnappyCompression 的典型速度：
    ///    ~200 - 500MB/s 压缩
    ///    ~400 - 800MB/s 解压缩
    /// 请注意，这些速度明显快于大多数持久存储速度，因此通常不值得切换到 kNoCompression。
    /// 即使输入数据不可压缩，kSnappyCompression 实现也会高效地检测到并切换到未压缩模式。
    CompressionType compression = kSnappyCompression;

    /// @brief Zstd 的压缩级别，目前仅支持范围 [-5,22]，默认值为 1
    int zstd_compression_level = 1;

    /// @brief 实验性选项，如果为 true，在打开数据库时追加到现有的 MANIFEST 和日志文件
    /// 这可以显著加快打开速度。
    /// 默认：当前为 false，但以后可能会变为 true。
    bool reuse_logs = false;

    /// @brief 过滤策略，主要用于读操作，减少磁盘读取
    /// 1. 如果非空，使用指定的过滤策略来减少磁盘读取。
    /// 2. 许多应用程序将从传递 NewBloomFilterPolicy() 的结果中受益。
    ///  @note 如果这里非空，那么将是指向DBImpl中的internal_filter_policy_的指针
    const FilterPolicy* filter_policy = nullptr;
};


// 控制读取操作的选项
struct LEVELDB_EXPORT ReadOptions {
    /// @brief 如果为 true，从底层存储读取的所有数据都将根据相应的校验和进行验证
    bool verify_checksums = false;

    /// @brief 此迭代读取的数据是否应缓存在内存中，对于批量扫描，调用者可能希望将此字段设置为 false
    bool fill_cache = true;

    /// @brief 如果 "snapshot" 非空，则读取指定的快照（必须属于正在读取的数据库且未被释放），如果 "snapshot" 为空，则使用此读取操作开始时的隐式快照
    const Snapshot* snapshot = nullptr;
};

// 控制写入操作的选项
struct LEVELDB_EXPORT WriteOptions {
    WriteOptions() = default;

    /// @brief 如果为 true，写入将在操作系统缓冲区缓存中刷新（通过调用 WritableFile::Sync()）后，写入才被视为完成
    bool sync = false;
};

}  // namespace leveldb

#endif  // STORAGE_LEVELDB_INCLUDE_OPTIONS_H_