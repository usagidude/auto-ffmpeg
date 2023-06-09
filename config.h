#pragma once
typedef std::pair<std::string, std::vector<std::regex>> probe_match_section;

enum class outmode
{
    local,
    source,
    absolute
};

class config_t
{
private:
    const std::map<std::string, std::string> config;
    static inline auto load_config_map(const std::string& file)
    {
        std::map<std::string, std::string> out_map;
        std::regex config_rx("^ *([^ >]+) *> *(.+)$");

        std::ifstream config_file(os::this_process::directory().append(file));
        for (std::string line; std::getline(config_file, line);) {
            std::smatch m;
            std::regex_match(line, m, config_rx);
            out_map.emplace(m[1], m[2]);
        }
        config_file.close();

        return out_map;
    }
    static inline auto get_outmode(const std::string& str)
    {
        if (str == "local")
            return outmode::local;
        if (str == "source")
            return outmode::source;
        else
            return outmode::absolute;
    }
    static inline auto get_inexts(const std::string& inexts_str)
    {
        std::set<std::string> inexts;
        for (const auto& ext : std::views::split(inexts_str, '|'))
            inexts.emplace(ext.begin(), ext.end());
        return inexts;
    }
    static inline auto get_probe_sections(const std::string& probe_sections)
    {
        std::vector<probe_match_section> probe_matches;
        if (probe_sections != ".") {
            for (const auto& section : std::views::split(probe_sections, '~')) {
                bool first = true;
                std::string stream;
                std::vector<std::regex> rx_strs;
                for (const auto& sub_section : std::views::split(section, ';')) {
                    if (first) {
                        stream.assign(sub_section.begin(), sub_section.end());
                        first = false;
                        continue;
                    }
                    rx_strs.emplace_back(sub_section.begin(), sub_section.end(),
                        std::regex_constants::icase);
                }
                probe_matches.emplace_back(stream, rx_strs);
            }
        }
        return probe_matches;
    }
public:
    const std::string cmd;
    const int count;
    const bool recursive;
    const std::string outdir;
    const outmode omode;
    const std::regex infilter;
    const bool filter_by_name;
    const std::vector<probe_match_section> probe_matches;
    const std::set<std::string> inexts;
    const std::string outext;
    const bool resume;
    const bool hide_window;
    const std::string argv;
    const int argc;
    inline config_t(const std::string& file, const char* argv, int argc) :
        config(load_config_map(file)),
        cmd(config.at("cmd")),
        count(std::stoi(config.at("count"))),
        recursive(config.at("recursive") == "true"),
        outdir(config.at("outdir")),
        omode(get_outmode(config.at("outmode"))),
        infilter(config.at("infilter"), std::regex_constants::icase),
        filter_by_name(config.at("infilter") != "."),
        probe_matches(get_probe_sections(config.at("probe_match"))),
        inexts(get_inexts(config.at("inext"))),
        outext(config.at("outext")),
        resume(config.at("resume") == "true"),
        hide_window(config.at("hide_window") == "true"),
        argv(argv),
        argc(argc) { }
    config_t() = delete;
};