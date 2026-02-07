#include <spdlog/common.h>
#include <spdlog/spdlog.h>

#include <CLI/CLI.hpp>
#include <algorithm>
#include <boost/core/noncopyable.hpp>
#include <boost/filesystem/operations.hpp>
#include <boost/filesystem/path.hpp>
#include <boost/system/detail/error_code.hpp>
#include <boost/unordered/unordered_flat_map.hpp>
#include <boost/unordered/unordered_flat_map_fwd.hpp>
#include <boost/wave.hpp>
#include <boost/wave/cpp_context.hpp>
#include <boost/wave/cpp_exceptions.hpp>
#include <boost/wave/cpp_iteration_context.hpp>
#include <boost/wave/cpplexer/cpp_lex_iterator.hpp>
#include <boost/wave/cpplexer/cpp_lex_token.hpp>
#include <boost/wave/cpplexer/cpplexer_exceptions.hpp>
#include <boost/wave/language_support.hpp>
#include <boost/wave/preprocessing_hooks.hpp>
#include <boost/wave/token_ids.hpp>
#include <boost/wave/util/filesystem_compatibility.hpp>
#include <cctype>
#include <cstddef>
#include <cstdint>
#include <cstdlib>
#include <deque>
#include <fstream>
#include <ios>
#include <iostream>
#include <iterator>
#include <ostream>
#include <sstream>
#include <string>
#include <utility>
#include <vector>

struct hook_state : boost::noncopyable {
    std::ostringstream result;
    boost::filesystem::path temp_dir_path;
    boost::filesystem::path dummy_include_path;
    std::uint64_t unique_id = 0;
    bool is_cpp = true;
    bool processing_directive = false;
    bool found_warning = false;
    boost::unordered_flat_map<std::string, std::string> correct_paths;
    bool remove_comments = false;

    using include_list_type = std::deque<std::pair<boost::filesystem::path, std::string>>;
    include_list_type include_paths;

    hook_state() {
        boost::system::error_code ec;
        temp_dir_path = boost::filesystem::temp_directory_path(ec);
        if (ec) {
            spdlog::error("Failed to get system temporary directory: {}", ec.message());
            std::exit(1);
        }
        temp_dir_path /= "cequip";
        if (!boost::filesystem::exists(temp_dir_path)) {
            boost::filesystem::create_directories(temp_dir_path, ec);
            if (ec) {
                spdlog::error("Failed to create temporary directory '{}': {}",
                              temp_dir_path.string(), ec.message());
                std::exit(1);
            }
        }

        dummy_include_path = temp_dir_path / "dummy_include";
        std::ofstream temp_file(dummy_include_path.string(), std::ios::trunc);
        if (!temp_file.is_open()) {
            spdlog::error("Failed to create dummy include file: {}", dummy_include_path.string());
            std::exit(1);
        }
        temp_file.close();
    }
    ~hook_state() {
        if (!temp_dir_path.empty()) {
            boost::system::error_code ec;
            boost::filesystem::remove_all(temp_dir_path, ec);
            if (ec) {
                spdlog::warn("Failed to remove temporary directory '{}': {}",
                             temp_dir_path.string(), ec.message());
            }
        }
    }

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
        state.result << "#include " << (is_system ? '<' : '"') << raw_file_path
                     << (is_system ? '>' : '"');
        state.result << '\n';
        native_name = state.dummy_include_path.string();
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
                state.result << '\n';
            } else if ((id == boost::wave::T_CCOMMENT || id == boost::wave::T_CPPCOMMENT) &&
                       state.remove_comments) {
                auto token_value = token.get_value();
                auto value = std::string(token_value.begin(), token_value.end());
                for (char& ch : value) ch = ::tolower(ch);
                auto check_contains = [&](const std::string& substr) {
                    return value.contains(substr);
                };
                if (check_contains("copyright") || check_contains("license") ||
                    check_contains("(c)") || check_contains("all rights reserved") ||
                    check_contains("©") || check_contains("®")) {
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

int main(int argc, char** argv) {
    CLI::App app;

    bool version_flag = false;
    app.add_flag("-v,--version", version_flag, "Show version information");

    std::string input_file_raw;
    app.add_option("file", input_file_raw, "Input file to process");

    std::string output_file_raw = "stdout";
    app.add_option("-o,--output", output_file_raw, "Output file")->default_val("stdout");

    std::vector<std::string> include_paths_raw;
    app.add_option("-i,--include", include_paths_raw, "Include paths for preprocessing");

    std::vector<std::string> definitions;
    app.add_option("-d,--define", definitions, "Preprocessor definitions");

    bool remove_comments = false;
    app.add_flag("--remove-comments", remove_comments, "Remove comments from output");

    bool quiet_flag = false;
    app.add_flag("-q,--quiet", quiet_flag, "Suppress non-error output");

    std::string lang_str;
    app.add_option("--lang", lang_str, "Language standard (c99, cpp98, cpp11, cpp17, cpp20, cpp23)")
        ->default_val("cpp23");

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

    boost::system::error_code ec;
    auto path = boost::filesystem::canonical(input_file_raw, ec);
    if (ec) {
        spdlog::error("Failed to resolve path '{}': {}", input_file_raw, ec.message());
        return 1;
    }
    const auto file_size = boost::filesystem::file_size(path, ec);
    if (ec) {
        spdlog::error("Failed to get file size for '{}': {}", path.string(), ec.message());
        return 1;
    }
    if (file_size == 0) {
        spdlog::warn("File is empty: {}", path.string());
    }
    std::ifstream file(path.string());
    if (!file.is_open()) {
        spdlog::error("Failed to open file: {}", path.string());
        return 1;
    }
    spdlog::info("Processing file: {}", path.string());
    std::string contents((std::istreambuf_iterator<char>(file)), std::istreambuf_iterator<char>());
    file.close();

    std::vector<boost::filesystem::path> include_paths;
    for (const auto& inc_path_raw : include_paths_raw) {
        const auto inc_path = boost::filesystem::canonical(inc_path_raw, ec);
        if (ec) {
            spdlog::error("Failed to resolve include path '{}': {}", inc_path_raw, ec.message());
            return 1;
        }
        if (!boost::filesystem::is_directory(inc_path, ec)) {
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
        hook_state state;
        const auto path_str = path.string();
        context_type ctx(contents.begin(), contents.end(), path_str.c_str(), custom_hooks(state));
        ctx.set_language(static_cast<boost::wave::language_support>(
            lang | boost::wave::support_option_preserve_comments |
            boost::wave::support_option_single_line |
            boost::wave::support_option_include_guard_detection));
        state.is_cpp = (lang != boost::wave::support_c99);
        state.remove_comments = remove_comments;
        for (const auto& inc_path : include_paths) {
            state.add_include_path(ctx, inc_path.string());
        }
        ctx.add_macro_definition("__CEQUIP__", true);
        ctx.add_macro_definition("true=1", true);
        ctx.add_macro_definition("false=0", true);
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
        if (state.found_warning) {
            spdlog::warn(
                "One or more include files were not found. See comments in output for "
                "details.");
        }
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
        boost::system::error_code ec_out;
        const auto output_path = boost::filesystem::canonical(output_file_raw, ec_out);
        if (ec_out) {
            spdlog::error("Failed to resolve output file path '{}': {}", output_file_raw,
                          ec_out.message());
            return 1;
        }
        spdlog::info("Output written to: {}", output_path.string());
    }

    spdlog::info("Preprocessing completed successfully.");

    return 0;
}
