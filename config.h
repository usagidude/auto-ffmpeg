#pragma once
typedef std::pair<std::string, std::vector<std::regex>> probe_match_section;

enum class outmode_t
{
    local,
    source,
    absolute
};

class config_t
{
public:
    std::string cmd;
    int count;
    bool recursive;
    std::string outdir;
    outmode_t outmode;
    std::regex infilter;
    bool filter_by_name;
    std::vector<probe_match_section> probe_matches;
    std::set<std::string> inexts;
    std::string outext;
    bool resume;
    bool hide_window;
    std::string argv;
    int argc;
    inline config_t(const std::map<std::string, std::string>& config, const char* argv, int argc) :
        cmd(config.at("cmd")),
        count(std::stoi(config.at("count"))),
        recursive(config.at("recursive") == "true"),
        outdir(config.at("outdir")),
        outmode(outmode_t::local),
        infilter(config.at("infilter"), std::regex_constants::icase),
        filter_by_name(config.at("infilter") != "."),
        outext(config.at("outext")),
        resume(config.at("resume") == "true"),
        hide_window(config.at("hide_window") == "true"),
        argv(argv),
        argc(argc)
    {
        const auto& _outmode = config.at("outmode");
        if (_outmode == "local")
            outmode = outmode_t::local;
        if (_outmode == "source")
            outmode = outmode_t::source;
        if (_outmode == "absolute")
            outmode = outmode_t::absolute;

        const auto& probe_sections = config.at("probe_match");
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

        for (const auto& ext : std::views::split(config.at("inext"), '|'))
            inexts.emplace(ext.begin(), ext.end());
    }
};