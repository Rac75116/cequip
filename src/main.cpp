#include "pch.hpp"

//

#include <cstddef>
#include <fstream>
#include <random>
#include <sstream>
#include <stdexcept>
#include <string>
#include <system_error>
#include <vector>

void predefine_macros(auto& ctx, const std::string& compiler, auto lang,
                      std::tuple<int, int, int> version) {
    ctx.add_macro_definition("true=1", true);
    ctx.add_macro_definition("false=0", true);

    const bool is_cpp = !!(lang & boost::wave::support_cpp);

    if (compiler == "gcc") {
        const int gnu_major = std::get<0>(version);
        const int gnu_minor = std::get<1>(version);
        const int gnu_patch = std::get<2>(version);

        ctx.add_macro_definition("__GNUC__=" + std::to_string(gnu_major), true);
        ctx.add_macro_definition("__GNUC_MINOR__=" + std::to_string(gnu_minor), true);
        ctx.add_macro_definition("__GNUC_PATCHLEVEL__=" + std::to_string(gnu_patch), true);

        if (is_cpp) {
            ctx.add_macro_definition("__GNUG__=" + std::to_string(gnu_major), true);
            ctx.add_macro_definition("__GXX_WEAK__=1", true);
            ctx.add_macro_definition("__EXCEPTIONS=1", true);
            ctx.add_macro_definition("__GXX_RTTI=1", true);
            ctx.add_macro_definition("__DEPRECATED=1", true);
        }

        ctx.add_macro_definition("__STRICT_ANSI__=1", true);

        ctx.add_macro_definition(std::string("__VERSION__=\"GCC ") + std::to_string(gnu_major) +
                                     "." + std::to_string(gnu_minor) + "." +
                                     std::to_string(gnu_patch) + "\"",
                                 true);
    } else if (compiler == "clang") {
        const int clang_major = std::get<0>(version);
        const int clang_minor = std::get<1>(version);
        const int clang_patch = std::get<2>(version);

        ctx.add_macro_definition("__clang__=1", true);
        ctx.add_macro_definition("__clang_major__=" + std::to_string(clang_major), true);
        ctx.add_macro_definition("__clang_minor__=" + std::to_string(clang_minor), true);
        ctx.add_macro_definition("__clang_patchlevel__=" + std::to_string(clang_patch), true);
        ctx.add_macro_definition(
            std::string("__clang_version__=\"Clang ") + std::to_string(clang_major) + "." +
                std::to_string(clang_minor) + "." + std::to_string(clang_patch) + "\"",
            true);
        ctx.add_macro_definition("__clang_literal_encoding__=\"UTF-8\"", true);
        ctx.add_macro_definition("__clang_wide_literal_encoding__=\"UTF-32\"", true);
        ctx.add_macro_definition("__GNUC__=" + std::to_string(clang_major), true);
        ctx.add_macro_definition("__GNUC_MINOR__=" + std::to_string(clang_minor), true);
        ctx.add_macro_definition("__GNUC_PATCHLEVEL__=" + std::to_string(clang_patch), true);

        if (is_cpp) {
            ctx.add_macro_definition("__GNUG__=" + std::to_string(clang_major), true);
            ctx.add_macro_definition("__DEPRECATED=1", true);
            ctx.add_macro_definition("__EXCEPTIONS=1", true);
            ctx.add_macro_definition("__GXX_RTTI=1", true);
        }

        ctx.add_macro_definition("__STRICT_ANSI__=1", true);

        ctx.add_macro_definition(std::string("__VERSION__=\"Clang ") + std::to_string(clang_major) +
                                     "." + std::to_string(clang_minor) + "." +
                                     std::to_string(clang_patch) + "\"",
                                 true);
    } else if (compiler == "msvc") {
        const int _MSC_VER_val = std::get<0>(version) * 100 + std::get<1>(version);
        const int _MSC_FULL_VER_val = std::get<0>(version) * 10000000 +
                                      std::get<1>(version) * 100000 + std::get<2>(version) * 10;

        ctx.add_macro_definition("_MSC_VER=" + std::to_string(_MSC_VER_val), true);
        ctx.add_macro_definition("_MSC_FULL_VER=" + std::to_string(_MSC_FULL_VER_val), true);
        ctx.add_macro_definition("_MSC_BUILD=0", true);
        ctx.add_macro_definition("_MSC_EXTENSIONS=1", true);
        ctx.add_macro_definition("__STDC_HOSTED__=1", true);

        if (is_cpp) {
            ctx.add_macro_definition("_CPPUNWIND=1", true);
            ctx.add_macro_definition("_CPPRTTI=1", true);

            if (lang & boost::wave::support_cpp2a) {
                ctx.add_macro_definition("_MSVC_LANG=202302L", true);
            } else if (lang & boost::wave::support_cpp20) {
                ctx.add_macro_definition("_MSVC_LANG=202002L", true);
            } else if (lang & boost::wave::support_cpp17) {
                ctx.add_macro_definition("_MSVC_LANG=201703L", true);
            }
        }

        ctx.add_macro_definition(std::string("__VERSION__=\"MSVC ") +
                                     std::to_string(std::get<0>(version)) + "." +
                                     std::to_string(std::get<1>(version)) + "\"",
                                 true);
    }
};

struct hook_state : boost::noncopyable {
    std::ostringstream result;
    std::filesystem::path temp_dir_path;
    std::filesystem::path dummy_include_path;
    std::uint64_t unique_id = 0;
    bool processing_directive = false;
    std::vector<std::pair<std::string, std::string>> correct_paths;

    hook_state() {
        std::error_code ec;
        temp_dir_path = std::filesystem::temp_directory_path(ec);
        if (ec) {
            spdlog::error("Failed to get system temporary directory: {}", ec.message());
            std::exit(1);
        }
        temp_dir_path /=
            std::filesystem::path("cequip_tmp_" + std::to_string(std::random_device{}()));
        std::filesystem::create_directories(temp_dir_path, ec);
        if (ec) {
            spdlog::error("Failed to create temporary directory '{}': {}", temp_dir_path.string(),
                          ec.message());
            std::exit(1);
        }

        dummy_include_path = temp_dir_path / "dummy_include";
        std::ofstream temp_file(dummy_include_path, std::ios::trunc);
        if (!temp_file.is_open()) {
            spdlog::error("Failed to create dummy include file: {}", dummy_include_path.string());
            std::exit(1);
        }
        temp_file.close();
    }
    ~hook_state() {
        if (!temp_dir_path.empty()) {
            std::error_code ec;
            std::filesystem::remove_all(temp_dir_path, ec);
            if (ec) {
                spdlog::warn("Failed to remove temporary directory '{}': {}",
                             temp_dir_path.string(), ec.message());
            }
        }
    }

    std::string get_correct_path(const std::string& path) {
        auto it = std::find_if(correct_paths.begin(), correct_paths.end(),
                               [&path](const auto& p) { return p.first == path; });
        if (it != correct_paths.end()) {
            return it->second;
        }
        return path;
    }
};

class custom_hooks : public boost::wave::context_policies::default_preprocessing_hooks {
    using base = boost::wave::context_policies::default_preprocessing_hooks;

    hook_state& state;

    template <typename ContainerT>
    void log_container(ContainerT const& container) {
        for (const auto& tok : container) {
            if (!tok.is_valid()) {
                continue;
            }
            state.result << tok.get_value();
        }
    }

   public:
    custom_hooks(hook_state& hook_state) : state(hook_state) {}

    template <typename ContextT, typename TokenT, typename ContainerT, typename IteratorT>
    bool expanding_function_like_macro(ContextT const&, TokenT const&, std::vector<TokenT> const&,
                                       ContainerT const&, TokenT const&,
                                       std::vector<ContainerT> const&, IteratorT const&,
                                       IteratorT const&) {
        return !state.processing_directive;
    }

    template <typename ContextT, typename TokenT, typename ContainerT>
    bool expanding_object_like_macro(ContextT const&, TokenT const&, ContainerT const&,
                                     TokenT const&) {
        return !state.processing_directive;
    }

    template <typename ContextT>
    bool locate_include_file(ContextT& ctx, std::string& file_path, bool is_system,
                             char const* current_name, std::string& dir_path,
                             std::string& native_name) {
        const auto raw_file_path = file_path;
        if (!ctx.find_include_file(file_path, dir_path, false, current_name)) {
            state.result << "#include " << (is_system ? '<' : '"') << file_path
                         << (is_system ? '>' : '"') << "\n";
            native_name = state.dummy_include_path.string();
            return true;
        }
        native_name = file_path;
        state.correct_paths.emplace_back(raw_file_path, native_name);
        return true;
    }

    template <typename ContextT>
    bool found_include_directive(ContextT const&, std::string const&, bool) {
        state.processing_directive = true;
        return false;
    }

    template <typename ContextT, typename TokenT>
    void detected_pragma_once(ContextT const&, TokenT const&, std::string const&) {
        state.processing_directive = false;
    }

    template <typename ContextT, typename ContainerT>
    bool interpret_pragma(ContextT const&, ContainerT&, typename ContextT::token_type const& option,
                          ContainerT const& values, typename ContextT::token_type const&) {
        state.result << "#pragma " << option.get_value() << ' ';
        log_container(values);
        state.result << '\n';
        state.processing_directive = false;
        return true;
    }

    template <typename ContextT, typename TokenT, typename ParametersT, typename DefinitionT>
    void defined_macro(ContextT const&, TokenT const& macro_name, bool is_functionlike,
                       ParametersT const& parameters, DefinitionT const& definition,
                       bool is_predefined) {
        if (!is_predefined) {
            state.result << "#define " << macro_name.get_value();
            if (is_functionlike) {
                state.result << '(';
                for (std::size_t i = 0; i < parameters.size(); ++i) {
                    state.result << parameters[i].get_value();
                    if (i + 1 < parameters.size()) {
                        state.result << ", ";
                    }
                }
                state.result << ')';
            }
            state.result << ' ';
            for (const auto& tok : definition) {
                state.result << tok.get_value();
            }
            state.result << '\n';
        }
        state.processing_directive = false;
    }

    template <typename ContextT, typename TokenT>
    void undefined_macro(ContextT const&, TokenT const& macro_name) {
        state.result << "#undef " << macro_name.get_value() << '\n';
        state.processing_directive = false;
    }

    template <typename ContextT, typename TokenT>
    bool found_directive(ContextT const&, TokenT const&) {
        state.processing_directive = true;
        return false;
    }

    template <typename ContextT, typename ContainerT>
    bool found_unknown_directive(ContextT const&, ContainerT const& line, ContainerT&) {
        log_container(line);
        state.result << '\n';
        state.processing_directive = false;
        return false;
    }

    template <typename ContextT, typename TokenT, typename ContainerT>
    bool evaluated_conditional_expression(ContextT const&, TokenT const&, ContainerT const&, bool) {
        state.processing_directive = false;
        return false;
    }

    template <typename ContextT, typename TokenT>
    TokenT const& generated_token(ContextT const&, TokenT const& token) {
        if (token.is_valid()) {
            const auto value = token.get_value();
            if (value == "\r\n" || value == "\r") {
                state.result << '\n';
            } else {
                state.result << token.get_value();
            }
        }
        return token;
    }

    template <typename ContextT, typename ContainerT>
    bool found_warning_directive(ContextT const&, ContainerT const& message) {
        state.result << "#warning ";
        log_container(message);
        state.result << '\n';
        state.processing_directive = false;
        return true;
    }

    template <typename ContextT, typename ContainerT>
    bool found_error_directive(ContextT const&, ContainerT const& message) {
        state.result << "#error ";
        log_container(message);
        state.result << '\n';
        state.processing_directive = false;
        return true;
    }

    template <typename ContextT, typename ContainerT>
    void found_line_directive(ContextT const&, ContainerT const& arguments, unsigned int,
                              std::string const&) {
        state.result << "#line ";
        log_container(arguments);
        state.result << '\n';
        state.processing_directive = false;
    }
};

int main(int argc, char** argv) {
    CLI::App app;

    bool version_flag = false;
    app.add_flag("-v,--version", version_flag, "Show version information");

    std::string input_file_raw;
    app.add_option("file", input_file_raw, "Input file to process");

    std::string output_file_raw = "stdout";
    app.add_option("-o,--output", output_file_raw, "Output file (default: stdout)");

    std::vector<std::string> include_paths_raw;
    app.add_option("-i,--include", include_paths_raw, "Include paths for preprocessing");

    std::vector<std::string> definitions;
    app.add_option("-d,--define", definitions, "Preprocessor definitions");

    std::string compiler = "gcc";
    app.add_option("--compiler", compiler, "Compiler to emulate (gcc, msvc, clang)");

    std::string compiler_version = "";
    app.add_option("--compiler-version", compiler_version, "Compiler version string to emulate");

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

    boost::wave::language_support lang;
    if (lang_str == "c99") {
        lang = boost::wave::language_support::support_c99;
    } else if (lang_str == "cpp98") {
        lang = boost::wave::language_support::support_cpp;
    } else if (lang_str == "cpp11") {
        lang = boost::wave::language_support::support_cpp11;
    } else if (lang_str == "cpp17") {
        lang = boost::wave::language_support::support_cpp17;
    } else if (lang_str == "cpp20") {
        lang = boost::wave::language_support::support_cpp20;
    } else if (lang_str == "cpp23") {
        lang = boost::wave::language_support::support_cpp2a;
    } else {
        spdlog::error("Unknown language standard: {}", lang_str);
        return 1;
    }

    if (compiler != "gcc" && compiler != "msvc" && compiler != "clang") {
        spdlog::error("Unknown compiler: {}. Supported compilers are: gcc, clang, msvc", compiler);
        return 1;
    }

    std::tuple<int, int, int> compiler_version_tuple;
    if (compiler_version.empty()) {
        compiler_version_tuple = {99, 0, 0};
    } else {
        char dot1 = 0;
        char dot2 = 0;
        std::istringstream version_stream(compiler_version);
        version_stream >> std::get<0>(compiler_version_tuple) >> dot1 >>
            std::get<1>(compiler_version_tuple) >> dot2 >> std::get<2>(compiler_version_tuple);
        if (version_stream.fail() || dot1 != '.' || dot2 != '.') {
            spdlog::error("Invalid compiler version format: {}. Expected format: major.minor.patch",
                          compiler_version);
            return 1;
        }
    }

    std::string result;
    {
        typedef boost::wave::cpplexer::lex_iterator<boost::wave::cpplexer::lex_token<>>
            lex_iterator_type;
        typedef boost::wave::context<std::string::iterator, lex_iterator_type,
                                     boost::wave::iteration_context_policies::load_file_to_string,
                                     custom_hooks>
            context_type;
        hook_state state;
        context_type ctx(contents.begin(), contents.end(), path.c_str(), custom_hooks(state));
        ctx.set_language(static_cast<boost::wave::language_support>(
            lang | boost::wave::support_option_preserve_comments |
            boost::wave::support_option_single_line |
            boost::wave::support_option_emit_contnewlines));
        for (const auto& inc_path : include_paths) {
            ctx.add_include_path(inc_path.c_str());
        }
        predefine_macros(ctx, compiler, lang, compiler_version_tuple);
        for (const auto& def : definitions) {
            ctx.add_macro_definition(def, true);
        }

        try {
            for (auto it = ctx.begin(); it != ctx.end(); ++it) {
            }
        } catch (const boost::wave::preprocess_exception& e) {
            spdlog::error("Preprocessing error: {} at {}:{}:{}", e.description(),
                          state.get_correct_path(e.file_name()), e.line_no(), e.column_no());
            return 1;
        } catch (boost::wave::cpplexer::lexing_exception& e) {
            spdlog::error("Lexing error: {} at {}:{}:{}", e.description(),
                          state.get_correct_path(e.file_name()), e.line_no(), e.column_no());
            return 1;
        }
        result = state.result.str();
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
        std::error_code ec;
        const auto output_path = std::filesystem::canonical(output_file_raw, ec);
        if (ec) {
            spdlog::error("Failed to resolve output file path '{}': {}", output_file_raw,
                          ec.message());
            return 1;
        }
        spdlog::info("Output written to: {}", output_path.string());
    }

    spdlog::info("Preprocessing completed successfully.");

    return 0;
}
