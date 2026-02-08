#include <spdlog/spdlog.h>

#include <CLI/CLI.hpp>
#include <boost/filesystem.hpp>
#include <boost/filesystem/operations.hpp>
#include <boost/unordered/unordered_flat_map.hpp>
#include <boost/unordered/unordered_flat_set.hpp>
#include <boost/wave.hpp>
#include <boost/wave/cpplexer/cpp_lex_iterator.hpp>
#include <cstdint>
#include <deque>
#include <fstream>
#include <iostream>
#include <iterator>
#include <sstream>
#include <string>
#include <utility>
#include <vector>

enum class eol_type { as_is, native, lf, crlf };

struct run_config {
    bool version_flag = false;
    bool quiet_flag = false;
    bool remove_comments = false;
    std::string input_file_raw;
    std::string output_file_raw = "stdout";
    std::vector<std::string> include_paths_raw;
    std::vector<std::string> definitions;
    std::string lang_str = "cpp23";
    std::string eol_str = "as-is";
    eol_type eol = eol_type::as_is;
    boost::wave::language_support lang = boost::wave::language_support::support_cpp2a;
};

struct hook_state : boost::noncopyable {
    std::ostringstream result;
    std::uint64_t unique_id = 0;
    bool is_cpp = true;
    bool processing_directive = false;
    boost::unordered_flat_map<std::string, std::string> correct_paths;
    bool remove_comments = false;
    eol_type eol = eol_type::as_is;
    boost::unordered_flat_set<std::string> included_system_headers;

    using include_list_type = std::deque<std::pair<boost::filesystem::path, std::string>>;
    include_list_type include_paths;

    std::string get_correct_path(const std::string& path) {
        auto it = correct_paths.find(path);
        if (it != correct_paths.end()) {
            return it->second;
        }
        return path;
    }

    void add_include_path(auto& ctx, const std::string& path) {
        auto new_path = boost::wave::util::complete_path(path, ctx.get_current_directory());
        if (!boost::filesystem::is_directory(new_path)) {
            spdlog::error("Include path is not a directory: {}", new_path.string());
            std::exit(1);
        }
        include_paths.emplace_back(new_path, path);
    }

    bool find_include_file(std::string& s, std::string& dir, char const* current_file) const {
        auto it = include_paths.begin();
        auto include_paths_end = include_paths.end();
        if (0 != current_file) {
            boost::filesystem::path file_path(current_file);
            for (; it != include_paths_end; ++it) {
                boost::filesystem::path currpath((*it).first.string());
                if (std::equal(currpath.begin(), currpath.end(), file_path.begin())) {
                    ++it;
                    break;
                }
            }
        }
        for (; it != include_paths_end; ++it) {
            boost::filesystem::path currpath(s);
            if (!currpath.has_root_directory()) {
                currpath = (*it).first.string();
                currpath /= s;
            }
            if (boost::filesystem::is_regular_file(currpath)) {
                boost::filesystem::path dirpath(s);
                if (!dirpath.has_root_directory()) {
                    dirpath = (*it).second;
                    dirpath /= s;
                }
                dir = dirpath.string();
                s = boost::wave::util::normalize(currpath).string();
                return true;
            }
        }
        return false;
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
                             char const* current_file, std::string& dir_path,
                             std::string& native_name) {
        const auto raw_file_path = file_path;
        if ((0 == current_file && ctx.find_include_file(file_path, dir_path, false, current_file) &&
             boost::filesystem::is_regular_file(file_path)) ||
            state.find_include_file(file_path, dir_path, current_file)) {
            native_name = file_path;
            state.correct_paths.emplace(raw_file_path, native_name);
            return true;
        }
        if (is_system) {
            bool inserted = state.included_system_headers.insert(raw_file_path).second;
            if (inserted) {
                state.result << "#include <" << raw_file_path << ">\n";
            }
        } else {
            state.result << "#include \"" << raw_file_path << "\"\n";
        }
#if defined(_WIN32)
        native_name = "NUL";
#else
        native_name = "/dev/null";
#endif
        return true;
    }

    template <typename ContextT>
    bool found_include_directive(ContextT const&, std::string const&, bool) {
        state.processing_directive = false;
        return false;
    }

    template <typename ContextT>
    void detected_include_guard(ContextT const&, std::string const&, std::string const&) {
        state.processing_directive = false;
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
        return false;
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
            const auto id = boost::wave::token_id(token);
            if (id == boost::wave::T_NEWLINE) {
                if (state.eol == eol_type::as_is) {
                    state.result << token.get_value();
                } else if (state.eol == eol_type::native) {
#if defined(_WIN32)
                    state.result << "\r\n";
#else
                    state.result << "\n";
#endif
                } else if (state.eol == eol_type::lf) {
                    state.result << "\n";
                } else if (state.eol == eol_type::crlf) {
                    state.result << "\r\n";
                }
            } else if ((id == boost::wave::T_CCOMMENT || id == boost::wave::T_CPPCOMMENT) &&
                       state.remove_comments) {
                auto token_value = token.get_value();
                auto value = std::string(token_value.begin(), token_value.end());
                for (char& ch : value) ch = ::tolower(ch);
                if (value.contains("copyright") || value.contains("license") ||
                    value.contains("(c)") || value.contains("all rights reserved") ||
                    value.contains("©") || value.contains("®")) {
                    state.result << token.get_value();
                }
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

run_config parse_cli(int argc, char** argv) {
    run_config config;
    CLI::App app;

    app.add_flag("-v,--version", config.version_flag, "Show version information");
    app.add_option("file", config.input_file_raw, "Input file to process");
    app.add_option("-o,--output", config.output_file_raw, "Output file")->default_val("stdout");
    app.add_option("-i,--include", config.include_paths_raw, "Include paths for preprocessing");
    app.add_option("-d,--define", config.definitions, "Preprocessor definitions");
    app.add_flag("--remove-comments", config.remove_comments, "Remove comments from output");
    app.add_option("--end-of-line", config.eol_str, "End-of-line sequence")
        ->check(CLI::IsMember({"as-is", "native", "lf", "crlf"}))
        ->default_val("as-is");
    app.add_flag("-q,--quiet", config.quiet_flag, "Suppress non-error output");
    app.add_option("--lang", config.lang_str, "Language standard")
        ->check(CLI::IsMember({"c99", "cpp98", "cpp11", "cpp17", "cpp20", "cpp23"}))
        ->default_val("cpp23");

    try {
        app.parse(argc, argv);
    } catch (const CLI::ParseError& e) {
        std::exit(app.exit(e));
    }

    return config;
}

void configure_logging(const run_config& config) {
    spdlog::default_logger()->set_pattern("[%^%l%$] %v");
    if (config.quiet_flag) {
        spdlog::set_level(spdlog::level::err);
    }
}

bool resolve_input_path(const std::string& input_file_raw, boost::filesystem::path& path) {
    if (input_file_raw.empty()) {
        spdlog::error("No input files provided. Use --help for usage information.");
        return false;
    }

    boost::system::error_code ec;
    path = boost::filesystem::canonical(input_file_raw, ec);
    if (ec) {
        spdlog::error("Failed to resolve path '{}': {}", input_file_raw, ec.message());
        return false;
    }
    const auto file_size = boost::filesystem::file_size(path, ec);
    if (ec) {
        spdlog::error("Failed to get file size for '{}': {}", path.string(), ec.message());
        return false;
    }
    if (file_size == 0) {
        spdlog::warn("File is empty: {}", path.string());
    }
    return true;
}

bool load_file_contents(const boost::filesystem::path& path, std::string& contents) {
    std::ifstream file(path.string());
    if (!file.is_open()) {
        spdlog::error("Failed to open file: {}", path.string());
        return false;
    }
    contents.assign((std::istreambuf_iterator<char>(file)), std::istreambuf_iterator<char>());
    return true;
}

bool resolve_include_paths(const std::vector<std::string>& include_paths_raw,
                           std::vector<boost::filesystem::path>& include_paths) {
    boost::system::error_code ec;
    for (const auto& inc_path_raw : include_paths_raw) {
        const auto inc_path = boost::filesystem::canonical(inc_path_raw, ec);
        if (ec) {
            spdlog::error("Failed to resolve include path '{}': {}", inc_path_raw, ec.message());
            return false;
        }
        if (!boost::filesystem::is_directory(inc_path, ec)) {
            spdlog::error("Include path is not a directory: {}", inc_path.string());
            return false;
        }
        include_paths.push_back(inc_path);
    }
    return true;
}

boost::wave::language_support parse_language(const std::string& lang_str) {
    if (lang_str == "c99") {
        return boost::wave::language_support::support_c99;
    }
    if (lang_str == "cpp98") {
        return boost::wave::language_support::support_cpp;
    }
    if (lang_str == "cpp11") {
        return boost::wave::language_support::support_cpp11;
    }
    if (lang_str == "cpp17") {
        return boost::wave::language_support::support_cpp17;
    }
    if (lang_str == "cpp20") {
        return boost::wave::language_support::support_cpp20;
    }
    if (lang_str == "cpp23") {
        return boost::wave::language_support::support_cpp2a;
    }
    std::unreachable();
}

eol_type parse_eol(const std::string& eol_value) {
    if (eol_value == "as-is") {
        return eol_type::as_is;
    }
    if (eol_value == "native") {
        return eol_type::native;
    }
    if (eol_value == "lf") {
        return eol_type::lf;
    }
    if (eol_value == "crlf") {
        return eol_type::crlf;
    }
    std::unreachable();
}

std::string preprocess(const run_config& config, const boost::filesystem::path& path,
                       const std::vector<boost::filesystem::path>& include_paths,
                       const std::string& contents) {
    using lex_iterator_type =
        boost::wave::cpplexer::lex_iterator<boost::wave::cpplexer::lex_token<>>;
    using context_type =
        boost::wave::context<std::string::const_iterator, lex_iterator_type,
                             boost::wave::iteration_context_policies::load_file_to_string,
                             custom_hooks>;

    hook_state state;
    const auto path_str = path.string();
    context_type ctx(contents.begin(), contents.end(), path_str.c_str(), custom_hooks(state));
    ctx.set_language(static_cast<boost::wave::language_support>(
        config.lang | boost::wave::support_option_preserve_comments |
        boost::wave::support_option_single_line |
        boost::wave::support_option_include_guard_detection));
    state.is_cpp = (config.lang != boost::wave::support_c99);
    state.remove_comments = config.remove_comments;
    state.eol = config.eol;
    for (const auto& inc_path : include_paths) {
        state.add_include_path(ctx, inc_path.string());
    }
    ctx.add_macro_definition("__CEQUIP__", true);
    ctx.add_macro_definition("true=1", true);
    ctx.add_macro_definition("false=0", true);
    for (const auto& def : config.definitions) {
        ctx.add_macro_definition(def, true);
    }

    try {
        for (auto it = ctx.begin(); it != ctx.end(); ++it) {
        }
    } catch (const boost::wave::preprocess_exception& e) {
        spdlog::error("Preprocessing error: {} at {}:{}:{}", e.description(),
                      state.get_correct_path(e.file_name()), e.line_no(), e.column_no());
        std::exit(1);
    } catch (boost::wave::cpplexer::lexing_exception& e) {
        spdlog::error("Lexing error: {} at {}:{}:{}", e.description(),
                      state.get_correct_path(e.file_name()), e.line_no(), e.column_no());
        std::exit(1);
    }
    return state.result.str();
}

bool write_output(const std::string& output_file_raw, const std::string& result) {
    if (output_file_raw == "stdout") {
        std::cout << result;
        return true;
    }
    if (output_file_raw == "stderr") {
        std::cerr << result;
        return true;
    }

    std::ofstream output_file(output_file_raw);
    if (!output_file.is_open()) {
        spdlog::error("Failed to open output file: {}", output_file_raw);
        return false;
    }
    output_file << result;
    output_file.close();

    boost::system::error_code ec_out;
    const auto output_path = boost::filesystem::canonical(output_file_raw, ec_out);
    if (ec_out) {
        spdlog::error("Failed to resolve output file path '{}': {}", output_file_raw,
                      ec_out.message());
        return false;
    }
    spdlog::info("Output written to: {}", output_path.string());
    return true;
}

int main(int argc, char** argv) {
    run_config config = parse_cli(argc, argv);
    configure_logging(config);

    if (config.version_flag) {
        std::cout << PROJECT_VERSION << std::endl;
        return 0;
    }

    config.lang = parse_language(config.lang_str);
    config.eol = parse_eol(config.eol_str);

    boost::filesystem::path path;
    if (!resolve_input_path(config.input_file_raw, path)) {
        return 1;
    }

    spdlog::info("Processing file: {}", path.string());

    std::string contents;
    if (!load_file_contents(path, contents)) {
        return 1;
    }

    std::vector<boost::filesystem::path> include_paths;
    if (!resolve_include_paths(config.include_paths_raw, include_paths)) {
        return 1;
    }

    std::string result = preprocess(config, path, include_paths, contents);

    if (!write_output(config.output_file_raw, result)) {
        return 1;
    }

    spdlog::info("Preprocessing completed successfully.");
    return 0;
}
