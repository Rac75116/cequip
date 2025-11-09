#include <spdlog/spdlog.h>

#include <CLI/CLI.hpp>
#include <boost/wave.hpp>
#include <boost/wave/cpplexer/cpp_lex_iterator.hpp>
#include <boost/wave/language_support.hpp>
#include <filesystem>
#include <fstream>
#include <random>
#include <sstream>
#include <stdexcept>
#include <string>
#include <system_error>
#include <vector>

struct temporary_directory {
    std::filesystem::path path;
    temporary_directory() {
        std::error_code ec;
        path = std::filesystem::temp_directory_path(ec);
        if (ec) {
            throw std::runtime_error("Failed to get system temporary directory: " + ec.message());
        }
        path /= std::filesystem::path("cequip_tmp_" + std::to_string(std::random_device{}()));
        std::filesystem::create_directories(path, ec);
        if (ec) {
            throw std::runtime_error("Failed to create temporary directory '" + path.string() +
                                     "': " + ec.message());
        }
    }
    ~temporary_directory() {
        std::error_code ec;
        std::filesystem::remove_all(path, ec);
        if (ec) {
            spdlog::warn("Failed to remove temporary directory '{}': {}", path.string(),
                         ec.message());
        }
    }
};

struct hook_state : boost::noncopyable {
    std::ostringstream result;
    std::filesystem::path tmp_dir;
    std::uint64_t unique_id = 0;
    bool processing_directive = false;
    bool generated_dummy_include_file = false;

    hook_state(const std::filesystem::path& tmp_dir) : tmp_dir(tmp_dir) {}
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
    bool expanding_object_like_macro(ContextT const&, TokenT const& macro, ContainerT const&,
                                     TokenT const&) {
        return !state.processing_directive || macro.get_value() == "__DATE__" ||
               macro.get_value() == "__TIME__";
    }

    template <typename ContextT>
    bool locate_include_file(ContextT& ctx, std::string& file_path, bool is_system,
                             char const* current_name, std::string& dir_path,
                             std::string& native_name) {
        if (!ctx.find_include_file(file_path, dir_path, false, current_name)) {
            state.result << "#include " << (is_system ? '<' : '"') << file_path
                         << (is_system ? '>' : '"') << "\n";
            auto dummy_include_path = state.tmp_dir / "dummy_include";
            if (!state.generated_dummy_include_file) {
                std::error_code ec;
                std::ofstream temp_file(dummy_include_path, std::ios::trunc);
                if (!temp_file.is_open()) {
                    BOOST_WAVE_THROW_CTX(ctx, boost::wave::preprocess_exception, bad_include_file,
                                         file_path.c_str(), ctx.get_main_pos());
                    return false;
                }
                temp_file.close();
                state.generated_dummy_include_file = true;
            }
            native_name = dummy_include_path.string();
            return true;
        }

        try {
            const auto src_path = std::filesystem::path(dir_path) / file_path;
            std::ifstream in(src_path, std::ios::binary);
            if (!in.is_open()) {
                BOOST_WAVE_THROW_CTX(ctx, boost::wave::preprocess_exception, bad_include_file,
                                     file_path.c_str(), ctx.get_main_pos());
                return false;
            }
            std::string buf((std::istreambuf_iterator<char>(in)), std::istreambuf_iterator<char>());
            in.close();

            if (!buf.empty() && buf.back() != '\n') {
                buf.push_back('\n');
            }

            auto out_path = state.tmp_dir / ("include_" + std::to_string(state.unique_id++));
            std::ofstream out(out_path, std::ios::binary | std::ios::trunc);
            if (!out.is_open()) {
                BOOST_WAVE_THROW_CTX(ctx, boost::wave::preprocess_exception, bad_include_file,
                                     file_path.c_str(), ctx.get_main_pos());
                return false;
            }
            out.write(buf.data(), static_cast<std::streamsize>(buf.size()));
            out.close();
            native_name = out_path.string();
            return true;
        } catch (...) {
            BOOST_WAVE_THROW_CTX(ctx, boost::wave::preprocess_exception, bad_include_file,
                                 file_path.c_str(), ctx.get_main_pos());
            return false;
        }
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

    std::string result;
    {
        typedef boost::wave::cpplexer::lex_iterator<boost::wave::cpplexer::lex_token<>>
            lex_iterator_type;
        typedef boost::wave::context<std::string::iterator, lex_iterator_type,
                                     boost::wave::iteration_context_policies::load_file_to_string,
                                     custom_hooks>
            context_type;
        auto temp_directory_path = temporary_directory();
        hook_state state(temp_directory_path.path);
        context_type ctx(contents.begin(), contents.end(), path.c_str(), custom_hooks(state));
        ctx.set_language(static_cast<boost::wave::language_support>(
            lang | boost::wave::support_option_preserve_comments));
        for (const auto& inc_path : include_paths) {
            ctx.add_include_path(inc_path.c_str());
        }
        ctx.add_macro_definition("true=1", true);
        ctx.add_macro_definition("false=0", true);
        for (const auto& def : definitions) {
            ctx.add_macro_definition(def, true);
        }

        try {
            for (auto it = ctx.begin(); it != ctx.end(); ++it) {
            }
        } catch (const boost::wave::preprocess_exception& e) {
            spdlog::error("Preprocessing error: {}", e.description());
            return 1;
        } catch (boost::wave::cpplexer::lexing_exception& e) {
            spdlog::error("Lexing error: {}", e.description());
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
    }

    spdlog::info("Preprocessing completed successfully.");

    return 0;
}
