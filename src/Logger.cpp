#include "Logger.hpp"

#include <spdlog/sinks/rotating_file_sink.h>

void set_arcane_logger() {
    const long max_size = 1048576 * 5; // 5MB
    const int max_files = 1;
    auto new_logger = spdlog::rotating_logger_mt("A2D_logger", "logs/arcane2d_logs.txt", max_size, max_files);
    spdlog::set_default_logger(new_logger);
    spdlog::debug("new logger log message");
}