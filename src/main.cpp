#include <X11/Xlib.h>
#include <fmt/format.h>
#include <pwd.h>
#include <sys/types.h>
#include <unistd.h>

#include <cxxopts.hpp>
#include <filesystem>
#include <fstream>
#include <functional>
#include <iostream>
#include <map>
#include <nlohmann/json.hpp>
#include <regex>
#include <string>
#include <vector>
std::string_view programName;
std::string_view homeDir;
typedef std::pair<std::string, std::vector<std::string>> Entry;
typedef std::vector<Entry> Entries;
typedef std::vector<std::pair<int, Entry>> IndexedEntries;
bool ensureConfigExists() {
    std::filesystem::path configPath(fmt::format("{}/.config/ICDHlpr/config.json", homeDir));
    if (!std::filesystem::exists(configPath)) {
        try {
            std::filesystem::create_directories(fmt::format("{}/.config/ICDHlpr", homeDir));
            std::ofstream configStream(configPath);
            configStream << "{}";
            return true;
        } catch (std::exception &e) {
            fmt::print("Error: {}\n", e.what());
            return false;
        }
    } else
        return true;
}
/**
 * @brief List all ICDs and cache them in a json file
 *        If the json file exists, read from it and warn if the ICDs have
 * changed If the json file does not exist, create it
 * @param args  command line arguments
 * @param DetectedICDs  ICDs detected by the program
 * @return 0 if successful, 1 if failed
 */
int listing(const std::map<std::string, std::vector<std::string>> &DetectedICDs);
int ListIOCDs();
int execute(const cxxopts::ParseResult args);

bool saveconfig(nlohmann::json config) {
    try {
        std::filesystem::path configPath(fmt::format("{}/.config/ICDHlpr/config.json", homeDir));
        std::ofstream configStream(configPath);
        configStream << config.dump(4);
    } catch (std::exception &e) {
        fmt::print("Error: {}\n", e.what());
        return false;
    }
    return true;
}
nlohmann::json loadconfig() {
    std::filesystem::path configPath(fmt::format("{}/.config/ICDHlpr/config.json", homeDir));
    nlohmann::json config;
    std::ifstream configStream(configPath);
    configStream >> config;
    return config;
}
int init() {
    if (!std::filesystem::exists(fmt::format("{}/.config/ICDHlpr", homeDir))) {
        std::filesystem::create_directories(fmt::format("{}/.config/ICDHlpr", homeDir));
        std::ofstream config(fmt::format("{}/.config/ICDHlpr/config.json", homeDir));
    }
    ListIOCDs();
    return 0;
}

bool checkMutexGroups(const cxxopts::ParseResult args, const std::vector<std::vector<std::string>> mutexGroups) {
    for (auto group : mutexGroups) {
        int count = 0;
        for (auto option : group) {
            if (args.count(option)) {
                count++;
            }
        }
        if (count > 1) {
            fmt::print("Error: Options {} cannot be used together\n", fmt::join(group, ", "));
            return false;
        }
    }
    return true;
}
int update(const cxxopts::ParseResult args) {
    if (!ensureConfigExists())
        return 1;
    ListIOCDs(); // update the ICDs and cache them
    auto config = loadconfig();
    IndexedEntries entries = config["ICDs"];
    try {
        size_t idx = args["update"].as<int>();
        if (idx < 0 || idx >= entries.size()) {
            fmt::print("Error: Index out of range\n");
            return 1;
        };
        fmt::print("Updating to {}\n", entries[idx].first);
        config["current"] = entries[idx].first;
    } catch (std::exception &e) {
        fmt::print("Error: Please provide an index\n");
        return 1;
    }
    saveconfig(config);
    return 0;
}

int processOptions(cxxopts::ParseResult args) {
    std::vector<std::vector<std::string>> mutexGroups = {{"update", "override"}, {"list", "executable"}};
    if (!checkMutexGroups(args, mutexGroups))
        return 1;
    //clang-format off
    std::map<std::string, std::function<int(cxxopts::ParseResult)>> optionsMap = {
        {"update", [&args](cxxopts::ParseResult) -> int { return update(args); }},
        {"override", [](cxxopts::ParseResult) { return init(); }},
        {"list", [](cxxopts::ParseResult) { return ListIOCDs(); }},
        {"executable", [&args](cxxopts::ParseResult) -> int { return execute(args); }}};
    //clang-format on
    for (auto option : args.arguments())
        if (optionsMap.count(option.key()))
            return optionsMap[option.key()](args);
    return 1;
}

int main(int argc, char **argv) {
    struct passwd *pw = getpwuid(getuid());
    homeDir = pw->pw_dir;
    programName = argv[0];
    cxxopts::Options options(std::string(programName), "ICD Helper for Vulkan applications");
    // clang-format off
    options.add_options()("h,help", "Print help")
    ("u,update", "Update using ICD driver", cxxopts::value<int>())
    ("o,override", "Override existing ICD driver")
    ("l,list", "List all ICD drivers")
    ("executable", "Executable file", cxxopts::value<std::string>())
    ("p,positional", "Positional arguments",cxxopts::value<std::vector<std::string>>());
    // clang-format on
    // default mapping is to executable and positional arguments
    options.parse_positional({"executable", "positional"});
    auto result = options.parse(argc, argv);
    if (result.count("help") || result.arguments().size() == 0) {
        fmt::print("IDK how I get here\n");
        fmt::print("{}\n", options.help());
        return 0;
    }
    return processOptions(result);
}

bool checkDisplay(std::string display) {
    fmt::print("Checking display {}\n", display);
    Display *d = XOpenDisplay(display.data());
    if (d) {
        XCloseDisplay(d);
        return true;
    } else
        return false;
}

int execute(const cxxopts::ParseResult args) {
    if (!ensureConfigExists())
        return 1;
    auto config = loadconfig();

    std::string executable = args["executable"].as<std::string>();
    // check if executable exists
    if (!std::filesystem::exists(executable)) {
        fmt::print("Error: Executable {} does not exist\n", executable);
        // try to find it in PATH
        std::string pathEnv = std::getenv("PATH");
        fmt::print("PATH: {}\n", pathEnv);
        std::vector<std::string> paths;
        std::string delimiter = ":";
        size_t pos = 0;
        std::string token;
        while ((pos = pathEnv.find(delimiter)) != std::string::npos) {
            token = pathEnv.substr(0, pos);
            paths.push_back(token);
            pathEnv.erase(0, pos + delimiter.length());
        }
        for (auto &path : paths) {
            fmt::print("Checking {} for {}\n", path, executable);
            std::filesystem::path executablePath(fmt::format("{}/{}", path, executable));
            if (std::filesystem::exists(executablePath)) {
                fmt::print("Found executable {} in {}\n", executable, path);
                executable = executablePath.string();
                break;
            }
        }
        if (!std::filesystem::exists(executable))
            return 1;
    }
    std::vector<std::string> positional;
    try {
        positional = args["positional"].as<std::vector<std::string>>();
    } catch (std::exception &e) {
        fmt::print("Running without positional arguments\n");
    }
    std::vector<char *> envs;
    // std::string env = fmt::format("AMD_VULKAN_ICD=RADV
    // DISABLE_LAYER_AMD_SWITCHABLE_GRAPHICS_1=1");
    envs.push_back(strdup("AMD_VULKAN_ICD=RADV"));
    envs.push_back(strdup("DISABLE_LAYER_AMD_SWITCHABLE_GRAPHICS_1=1"));
    // find working display
    std::string display = std::getenv("DISPLAY");
    std::string displayvalue = display.substr(display.find_first_of(":") + 1);
    if (!checkDisplay(display)) {
        fmt::print("Error: DISPLAY environment variable is not set\n");
        // find a working display
        std::vector<std::string> displays = {":0", ":1", ":2", ":3", ":4", ":5", ":6", ":7", ":8", ":9"};
        bool found = false;
        for (auto &display : displays) {
            if (checkDisplay(display)) {
                found = true;
                envs.push_back(strdup(fmt::format("DISPLAY={}", display).c_str()));
                break;
            }
        }
        if (!found) {
            fmt::print("Error: No working display found\n");
            fmt::print("Please set DISPLAY environment variable\n");
            fmt::print("or review your X11 configuration\n");
            return 1;
        }
    } else {
        // first test if the display is working
        envs.push_back(strdup(fmt::format("DISPLAY={}", display).c_str()));
    }
    IndexedEntries entries = config["ICDs"];
    try {
        int ICDName = config["current"];
        std::vector<std::string> ICDs = entries[ICDName].second.second;
        envs.push_back(strdup(fmt::format("VK_ICD_FILENAMES={}", fmt::join(ICDs, ":")).c_str()));
    } catch (std::exception &e) {
        fmt::print("Error: Please select an ICD driver\n");
        fmt::print("Use {} -l to list all ICD drivers\n", programName);
        fmt::print("Use {} -u <index> to select an ICD driver\n", programName);
        return 1;
    }
    envs.push_back(nullptr);
    char **envp = new char *[envs.size()];
    for (size_t i = 0; i < envs.size(); ++i) {
        envp[i] = envs[i];
    }
    std::vector<char *> argv;
    argv.push_back(strdup(executable.c_str()));
    for (auto &arg : positional) {
        argv.push_back(strdup(arg.c_str()));
    }
    argv.push_back(nullptr);
    // print how the program will be executed
    std::string command;
    for (auto &arg : argv) {
        if (arg == nullptr)
            break;
        command += arg;
        command += " ";
    }
    fmt::print("Environment variables:\n");
    for (auto &env : envs) {
        if (env == nullptr) {
            continue;
        }
        fmt::print("{}\n", env);
    }
    // create new process
    int result = execve(executable.c_str(), argv.data(), envp);
    for (auto &arg : argv)
        delete[] arg;
    for (size_t i = 0; i < envs.size(); ++i)
        free(envp[i]);
    delete[] envp;
    return result;
}

Entries combineICDs(const std::vector<std::string> &ICDs) {
    using path = std::filesystem::path;
    // Map to store unique ICD names with their architectures and paths
    std::map<std::string, std::pair<std::vector<std::string>, std::vector<std::string>>> mapCombined;

    auto removeExtension = [](const std::string &ICDName) {
        return std::regex_replace(ICDName, std::regex(".json"), "");
    };

    for (const auto &ICD : ICDs) {
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
    Entries combined;
    for (auto &entry : mapCombined) {
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

int ListIOCDs() {
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
    for (auto &p : std::filesystem::directory_iterator(systemICDPath)) {
        if (p.path().extension() != ".json")
            continue;
        ICDs["system"].push_back(p.path().string());
    }
    std::filesystem::path userICDPath(fmt::format("{}/.local/share/vulkan/icd.d", homeDir));
    if (std::filesystem::exists(userICDPath)) {
        ICDs["user"] = {};
        for (auto &p : std::filesystem::directory_iterator(userICDPath)) {
            // check if file is json
            if (p.path().extension() != ".json")
                continue;
            ICDs["user"].push_back(p.path().string());
        }
    } else {
        fmt::print("Warning: Directory {} does not exist\n", userICDPath.string());
        fmt::print("User ICDs will not be listed\n");
    }
    return listing(ICDs);
}

int listing(const std::map<std::string, std::vector<std::string>> &DetectedICDs) {
    auto config = loadconfig();
    std::vector<std::string> combined = DetectedICDs.at("system");
    if (DetectedICDs.find("user") != DetectedICDs.end())
        combined.insert(combined.end(), DetectedICDs.at("user").begin(), DetectedICDs.at("user").end());
    std::sort(combined.begin(), combined.end());
    Entries entries = combineICDs(combined);
    int returnCode = 0;
    IndexedEntries EntriesWithIndex;
    for (size_t i = 0; i < entries.size(); i++) {
        fmt::print("{}: {}\n", i, entries[i].first);
        EntriesWithIndex.push_back({i, entries[i]});
    }
    if (config["ICDs"] != EntriesWithIndex) {
        fmt::print("Warning: ICDs have changed\n");
        returnCode = 1;
    }
    config["ICDs"] = EntriesWithIndex;
    saveconfig(config);
    return returnCode;
}