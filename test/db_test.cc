#include "leveldb/db.h"
#include <iostream>

#include <iostream>
#include "leveldb/options.h"
#include "leveldb/comparator.h"
#include "leveldb/env.h"
#include "leveldb/filter_policy.h"

void PrintOptions(const leveldb::Options& options) {
    std::cout << "Options values:\n";

    // Comparator
    std::cout << "  comparator: ";
    if (options.comparator) {
        std::cout << options.comparator->Name();
    } else {
        std::cout << "nullptr";
    }
    std::cout << "\n";

    // create_if_missing
    std::cout << "  create_if_missing: " << (options.create_if_missing ? "true" : "false") << "\n";

    // error_if_exists
    std::cout << "  error_if_exists: " << (options.error_if_exists ? "true" : "false") << "\n";

    // paranoid_checks
    std::cout << "  paranoid_checks: " << (options.paranoid_checks ? "true" : "false") << "\n";

    // env
    std::cout << "  env: ";
    if (options.env) {
        std::cout << (options.env == leveldb::Env::Default()) ? "Env::Default()" : "Env object (no Name method)";
    } else {
        std::cout << "nullptr";
    }
    std::cout << "\n";

    // info_log
    std::cout << "  info_log: ";
    if (options.info_log) {
        // Logger 类没有 Name() 方法，这里假设它是一个自定义类并尝试调用 Name()
        // 如果 Logger 没有 Name() 方法，可以输出类型信息或其他标识
        std::cout << "Logger object (no Name method)";
    } else {
        std::cout << "nullptr";
    }
    std::cout << "\n";

    // write_buffer_size
    std::cout << "  write_buffer_size: " << options.write_buffer_size << " bytes\n";

    // max_open_files
    std::cout << "  max_open_files: " << options.max_open_files << "\n";

    // block_cache
    std::cout << "  block_cache: ";
    if (options.block_cache) {
        // Cache 类没有 Name() 方法，这里假设它是一个自定义类并尝试调用 Name()
        // 如果 Cache 没有 Name() 方法，可以输出类型信息或其他标识
        std::cout << "Cache object (no Name method)";
    } else {
        std::cout << "nullptr";
    }
    std::cout << "\n";

    // block_size
    std::cout << "  block_size: " << options.block_size << " bytes\n";

    // block_restart_interval
    std::cout << "  block_restart_interval: " << options.block_restart_interval << "\n";

    // max_file_size
    std::cout << "  max_file_size: " << options.max_file_size << " bytes\n";

    // compression
    std::cout << "  compression: ";
    switch (options.compression) {
        case leveldb::kNoCompression:
            std::cout << "kNoCompression";
            break;
        case leveldb::kSnappyCompression:
            std::cout << "kSnappyCompression";
            break;
        case leveldb::kZstdCompression:
            std::cout << "kZstdCompression";
            break;
        default:
            std::cout << "Unknown compression type";
            break;
    }
    std::cout << "\n";

    // zstd_compression_level
    std::cout << "  zstd_compression_level: " << options.zstd_compression_level << "\n";

    // reuse_logs
    std::cout << "  reuse_logs: " << (options.reuse_logs ? "true" : "false") << "\n";

    // filter_policy
    std::cout << "  filter_policy: ";
    if (options.filter_policy) {
        std::cout << options.filter_policy->Name();
    } else {
        std::cout << "nullptr";
    }
    std::cout << "\n";
}

int main() {
    leveldb::DB* db;
    leveldb::Options options;
    options.create_if_missing = true;

    // 打开数据库
    leveldb::Status status = leveldb::DB::Open(options, "/tmp/testdb", &db);
    if (!status.ok()) {
        std::cerr << "无法打开数据库: " << status.ToString() << std::endl;
        return 1;
    }


    // 这里打开成功之后，请帮我输出options中各个参数的值
    // 请在这里输出options中各个参数的值

    PrintOptions(options);


    // 写入数据
    status = db->Put(leveldb::WriteOptions(), "key1", "value1");
    if (!status.ok()) {
        std::cerr << "写入失败: " << status.ToString() << std::endl;
    }

    // 读取数据
    std::string value;
    status = db->Get(leveldb::ReadOptions(), "key1", &value);
    if (status.ok()) {
        std::cout << "key1 的值是: " << value << std::endl;
    } else {
        std::cerr << "读取失败: " << status.ToString() << std::endl;
    }

    PrintOptions(options);

    // 关闭数据库
    delete db;
    return 0;
}
