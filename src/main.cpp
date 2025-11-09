#include <spdlog/spdlog.h>

#include <CLI/CLI.hpp>
#include <filesystem>
#include <fstream>
#include <system_error>

#include "expand.hpp"

int main(int argc, char** argv) {
    CLI::App app;

    bool version_flag = false;
    app.add_flag("-v,--version", version_flag, "Show version information");

    std::string input_file_raw;
    app.add_option("file", input_file_raw, "Input file to process");

    std::string output_file_raw = "stdout";
    app.add_option("-o,--output", output_file_raw, "Output file (default: stdout)");

    std::vector<std::string> include_paths_raw;
    app.add_option("-I,--include", include_paths_raw, "Include paths for preprocessing");

    std::vector<std::string> definitions;
    app.add_option("-D,--define", definitions, "Preprocessor definitions");

    bool quiet_flag = false;
    app.add_flag("-q,--quiet", quiet_flag, "Suppress non-error output");

    std::string lang_str = "cpp23";
    app.add_option("--lang", lang_str,
                   "Language standard (c99, cpp98, cpp11, cpp17, cpp20, cpp23)");

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

    std::vector<std::filesystem::path> include_paths;
    for (const auto& inc_path_raw : include_paths_raw) {
        const auto inc_path = std::filesystem::canonical(inc_path_raw, ec);
        if (ec) {
            spdlog::error("Failed to resolve include path '{}': {}", inc_path_raw, ec.message());
            return 1;
        }
        if (!std::filesystem::is_directory(inc_path, ec)) {
            spdlog::error("Include path is not a directory: {}", inc_path.string());
            return 1;
        }
        include_paths.push_back(inc_path);
    }

    LanguageSupport lang;
    if (lang_str == "c99") {
        lang = LanguageSupport::c99;
    } else if (lang_str == "cpp98") {
        lang = LanguageSupport::cpp98;
    } else if (lang_str == "cpp11") {
        lang = LanguageSupport::cpp11;
    } else if (lang_str == "cpp17") {
        lang = LanguageSupport::cpp17;
    } else if (lang_str == "cpp20") {
        lang = LanguageSupport::cpp20;
    } else if (lang_str == "cpp23") {
        lang = LanguageSupport::cpp23;
    } else {
        spdlog::error("Unknown language standard: {}", lang_str);
        return 1;
    }

    std::string result;
    try {
        result = Expand(contents, include_paths, definitions, path, lang);
    } catch (const std::runtime_error& e) {
        spdlog::error("Error during preprocessing: {}", e.what());
        return 1;
    }

    if (output_file_raw == "stdout") {
        std::cout << result;
    } else if (output_file_raw == "stderr") {
        std::cerr << result;
    } else {
        std::ofstream output_file(output_file_raw);
        if (!output_file.is_open()) {
            spdlog::error("Failed to open output file: {}", output_file_raw);
            return 1;
        }
        output_file << result;
        output_file.close();
    }

    return 0;
}
