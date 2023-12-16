// test cxxopts
// and fmt
#include <iostream>
#include <string>
#include <vector>
#include <cxxopts.hpp>
#include <fmt/format.h>
#include <filesystem>
#include <nlohmann/json.hpp>
#include <fstream>
#include <map>
#include <functional>
#include <unistd.h>
#include <sys/types.h>
#include <pwd.h>
#include <regex>
std::string_view programName;
std::string_view homeDir;
/**
 * @brief List all ICDs and cache them in a json file
 *        If the json file exists, read from it and warn if the ICDs have changed
 *        If the json file does not exist, create it
 * @param args  command line arguments
 * @param DetectedICDs  ICDs detected by the program
 * @return 0 if successful, 1 if failed
 */
int listing(const std::map<std::string, std::vector<std::string>> &DetectedICDs);

int init(const cxxopts::ParseResult args)
{
    if (!std::filesystem::exists(fmt::format("{}/.config/ICDHlpr", homeDir)))
    {
        std::filesystem::create_directories(fmt::format("{}/.config/ICDHlpr", homeDir));
        std::ofstream config(fmt::format("{}/.config/ICDHlpr/config.json", homeDir));
    }
    return 0;
}

bool checkMutexGroups(const cxxopts::ParseResult args, const std::vector<std::vector<std::string>> mutexGroups)
{
    for (auto group : mutexGroups)
    {
        int count = 0;
        for (auto option : group)
        {
            if (args.count(option))
            {
                count++;
            }
        }
        if (count > 1)
        {
            fmt::print("Error: Options {} cannot be used together\n", fmt::join(group, ", "));
            return false;
        }
    }
    return true;
}

int ListIOCDs(const cxxopts::ParseResult args)
{
    // list all ICDs in /etc/vulkan/icd.d
    std::filesystem::path systemICDPath("/usr/share/vulkan/icd.d");
    if (!std::filesystem::exists(systemICDPath)) // sanity check
    {
        fmt::print("Error: Directory {} does not exist\n", systemICDPath.string());
        fmt::print("Please make sure the Vulkan drivers are installed\n");
        return 1;
    }
    std::map<std::string, std::vector<std::string>> ICDs;
    ICDs["system"] = {};
    for (auto &p : std::filesystem::directory_iterator(systemICDPath))
    {
        if (p.path().extension() != ".json")
            continue;
        ICDs["system"].push_back(p.path().string());
    }
    std::filesystem::path userICDPath(fmt::format("{}/.local/share/vulkan/icd.d", homeDir));
    if (std::filesystem::exists(userICDPath))
    {
        ICDs["user"] = {};
        for (auto &p : std::filesystem::directory_iterator(userICDPath))
        {
            // check if file is json
            if (p.path().extension() != ".json")
                continue;
            ICDs["user"].push_back(p.path().string());
        }
    }
    else
    {
        fmt::print("Warning: Directory {} does not exist\n", userICDPath.string());
        fmt::print("User ICDs will not be listed\n");
    }
    return listing(ICDs);
}

int prcessOptions(cxxopts::ParseResult args)
{
    std::vector<std::vector<std::string>> mutexGroups = {
        {"update", "override"},
        {"list", "executable"}};
    if (!checkMutexGroups(args, mutexGroups))
        return 1;
    std::map<std::string, std::function<int(const cxxopts::ParseResult)>> optionsMap = {
        {"update", init},
        {"override", init},
        {"list", ListIOCDs},
        {"executable", init}};
    for (auto option : args.arguments())
        if (optionsMap.count(option.key()))
            return optionsMap[option.key()](args);
}

int main(int argc, char **argv)
{
    struct passwd *pw = getpwuid(getuid());
    homeDir = pw->pw_dir;
    programName = argv[0];
    cxxopts::Options options(std::string(programName), "ICD Helper for Vulkan applications");
    options.add_options()("h,help", "Print help")("u,update", "Update using ICD driver")("o,override", "Override existing ICD driver")("l,list", "List all ICD drivers")("executable", "Executable file", cxxopts::value<std::string>())("p,positional", "Positional arguments", cxxopts::value<std::vector<std::string>>());
    // default mapping is to executable and positional arguments
    options.parse_positional({"executable", "positional"});
    auto result = options.parse(argc, argv);
    if (result.count("help") || result.arguments().size() == 0)
    {
        fmt::print("IDK how I get here\n");
        fmt::print("{}\n", options.help());
        return 0;
    }
    return prcessOptions(result);
}
std::vector<std::pair<std::string, std::vector<std::string>>> combineICDs(const std::vector<std::string> &ICDs)
{
    using path = std::filesystem::path;
    // Map to store unique ICD names with their architectures and paths
    std::map<std::string, std::pair<std::vector<std::string>, std::vector<std::string>>> mapCombined;

    auto removeExtension = [](const std::string &ICDName)
    {
        return std::regex_replace(ICDName, std::regex(".json"), "");
    };

    for (const auto &ICD : ICDs)
    {
        path ICDPath(ICD);
        std::string ICDName = ICDPath.filename().string();
        int index = ICDName.find_first_of("0123456789");
        std::string ICDNameWithoutArch = ICDName.substr(0, index);
        std::string arch = removeExtension(ICDName.substr(index));

        // Insert or update the map entry
        mapCombined[ICDNameWithoutArch].first.push_back(arch);
        mapCombined[ICDNameWithoutArch].second.push_back(ICD);
    }

    // Remove duplicate architectures and create the final combined vector
    std::vector<std::pair<std::string, std::vector<std::string>>> combined;
    for (auto &entry : mapCombined)
    {
        // Remove duplicates in architectures
        auto &archs = entry.second.first;
        std::sort(archs.begin(), archs.end());
        archs.erase(std::unique(archs.begin(), archs.end()), archs.end());

        // Format the string with architectures
        std::string formattedName = fmt::format("{}({})", entry.first, fmt::join(archs, ","));

        combined.push_back({formattedName, entry.second.second});
    }
    return combined;
}

int listing(const std::map<std::string, std::vector<std::string>> &DetectedICDs)
{

    std::filesystem::path configPath(fmt::format("{}/.config/ICDHlpr/config.json", homeDir));
    //  this lambda function should combine 32 and 64 bit ICDs into one and provide a string which will tell the arch of the ICD

    using json = nlohmann::json;
    json config;
    if (std::filesystem::exists(configPath)) // sanity check
    {
        std::ifstream configStream(configPath);
        configStream >> config;
    }
    else
    {
        std::filesystem::create_directories(fmt::format("{}/.config/ICDHlpr", homeDir));
        std::ofstream configStream(configPath);
        configStream << "{}";
    }
    std::vector<std::string> combined = DetectedICDs.at("system");
    if (DetectedICDs.find("user") != DetectedICDs.end())
        combined.insert(combined.end(), DetectedICDs.at("user").begin(), DetectedICDs.at("user").end());
    std::sort(combined.begin(), combined.end());
    // check if ICDs have changed
    auto Entries = combineICDs(combined);
    if (config["ICDs"] != Entries)
    {
        fmt::print("Warning: ICDs have changed\n");
    }
    for (int i = 0; i < Entries.size(); i++)
    {
        fmt::print("{}: {}\n", i, Entries[i].first);
    }
    config["ICDs"] = Entries;
    std::ofstream configStream(configPath);
    configStream << config.dump(4);
    return 0;
}