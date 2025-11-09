#pragma once
#include <spdlog/spdlog.h>

#include <boost/core/noncopyable.hpp>
#include <boost/wave.hpp>
#include <boost/wave/cpplexer/cpp_lex_iterator.hpp>
#include <filesystem>
#include <random>
#include <stdexcept>
#include <system_error>

enum class LanguageSupport {
    c99 = boost::wave::support_c99,
    cpp98 = boost::wave::support_cpp,
    cpp11 = boost::wave::support_cpp11,
    cpp17 = boost::wave::support_cpp17,
    cpp20 = boost::wave::support_cpp20,
    cpp23 = boost::wave::support_cpp2a
};

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
    std::filesystem::path tmp_dir;
    std::uint64_t unique_id = 0;

    hook_state(const std::filesystem::path& tmp_dir) : tmp_dir(tmp_dir) {}
};

class custom_hooks : public boost::wave::context_policies::default_preprocessing_hooks {
    using base = boost::wave::context_policies::default_preprocessing_hooks;

    hook_state& state;

   public:
    custom_hooks(hook_state& hook_state) : state(hook_state) {}

    template <typename ContextT>
    bool locate_include_file(ContextT& ctx, std::string& file_path, bool is_system,
                             char const* current_name, std::string& dir_path,
                             std::string& native_name) {
        if (!ctx.find_include_file(file_path, dir_path, is_system, current_name)) {
            std::error_code ec;
            auto temp_path =
                state.tmp_dir / ("missing_include_" + std::to_string(state.unique_id++) + ".tmp");
            std::ofstream temp_file(temp_path);
            if (!temp_file.is_open()) {
                BOOST_WAVE_THROW_CTX(ctx, boost::wave::preprocess_exception, bad_include_file,
                                     file_path.c_str(), ctx.get_main_pos());
                return false;
            }
            temp_file << "// Temporary placeholder for missing include: "
                      << (is_system ? "<" : "\"") << file_path << (is_system ? ">" : "\"") << "\n";
            temp_file.close();
            native_name = temp_path.string();
            return true;
        }
        if (!std::filesystem::exists(file_path)) {
            BOOST_WAVE_THROW_CTX(ctx, boost::wave::preprocess_exception, bad_include_file,
                                 file_path.c_str(), ctx.get_main_pos());
            return false;
        }
        native_name = file_path;
        return true;
    }
};

inline std::string Expand(std::string& content,
                          const std::vector<std::filesystem::path>& include_paths,
                          const std::vector<std::string>& definitions,
                          const std::filesystem::path& file_path, LanguageSupport lang) {
    typedef boost::wave::cpplexer::lex_iterator<boost::wave::cpplexer::lex_token<>>
        lex_iterator_type;
    typedef boost::wave::context<std::string::iterator, lex_iterator_type,
                                 boost::wave::iteration_context_policies::load_file_to_string,
                                 custom_hooks>
        context_type;
    auto temp_directory_path = temporary_directory();
    hook_state temp_state(temp_directory_path.path);
    context_type ctx(content.begin(), content.end(), file_path.string().c_str(),
                     custom_hooks(temp_state));
    ctx.set_language(static_cast<boost::wave::language_support>(lang));
    for (const auto& inc_path : include_paths) {
        ctx.add_include_path(inc_path.string().c_str());
    }
    for (const auto& def : definitions) {
        ctx.add_macro_definition(def, true);
    }

    std::string result;
    try {
        context_type::iterator_type first = ctx.begin();
        context_type::iterator_type last = ctx.end();
        while (first != last) {
            const auto& tok = (*first).get_value();
            result.append(tok.begin(), tok.end());
            ++first;
        }
    } catch (const boost::wave::preprocess_exception& e) {
        throw std::runtime_error(e.description());
    }

    return result;
}
