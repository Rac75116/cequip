#include <spdlog/spdlog.h>

#include <CLI/CLI.hpp>
#include <boost/wave.hpp>
#include <filesystem>
#include <fstream>
#include <system_error>

int main(int argc, char** argv) {
    CLI::App app;

    bool version_flag = false;
    app.add_flag("-v,--version", version_flag, "Show version information");

    std::string input_file_raw;
    app.add_option("file", input_file_raw, "Input file to process");

    std::string output_file_raw;
    app.add_option("-o,--output", output_file_raw, "Output file (default: stdout)");

    std::vector<std::string> include_paths_raw;
    app.add_option("-I,--include", include_paths_raw, "Include paths for preprocessing");

    std::vector<std::string> definitions;
    app.add_option("-D,--define", definitions, "Preprocessor definitions");

    bool quiet_flag = false;
    app.add_flag("-q,--quiet", quiet_flag, "Suppress non-error output");

    CLI11_PARSE(app, argc, argv);

    spdlog::default_logger()->set_pattern("[%^%l%$] %v");
    if (quiet_flag) {
        spdlog::set_level(spdlog::level::err);
    }

    if (version_flag) {
        std::cout << PROJECT_VERSION << std::endl;
        return 0;
    }

    if (input_file_raw.empty()) {
        spdlog::error("No input files provided. Use --help for usage information.");
        return 1;
    }

    if (output_file_raw.empty()) {
        output_file_raw = "stdout";
    }

    std::error_code ec;
    auto path = std::filesystem::canonical(input_file_raw, ec);
    if (ec) {
        spdlog::error("Failed to resolve path '{}': {}", input_file_raw, ec.message());
        return 1;
    }
    const auto file_size = std::filesystem::file_size(path, ec);
    if (ec) {
        spdlog::error("Failed to get file size for '{}': {}", path.string(), ec.message());
        return 1;
    }
    if (file_size == 0) {
        spdlog::warn("File is empty: {}", path.string());
    }
    if (file_size > 1024 * 1024 * 1024) {
        spdlog::error("File is too large (>1GB): {}", path.string());
        return 1;
    }
    std::ifstream file(path);
    if (!file.is_open()) {
        spdlog::error("Failed to open file: {}", path.string());
        return 1;
    }
    spdlog::info("Processing file: {}", path.string());
    std::string contents((std::istreambuf_iterator<char>(file)), std::istreambuf_iterator<char>());
    file.close();

    return 0;
}
