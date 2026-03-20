#include <iostream>
#include <string>
#include <vector>
#include <algorithm>
#include <cstdlib>
#include <unistd.h>
#include <curl/curl.h>


static std::string extractJsonString(const std::string& json, const std::string& key) {
    std::string needle = "\"" + key + "\"";
    auto pos = json.find(needle);
    if (pos == std::string::npos) return "";
    pos = json.find(':', pos + needle.size());
    if (pos == std::string::npos) return "";
    while (++pos < json.size() && std::isspace((unsigned char)json[pos])) {}
    if (pos >= json.size()) return "";

    if (json[pos] == '"') {
        ++pos;
        std::string result;
        while (pos < json.size() && json[pos] != '"') {
            if (json[pos] == '\\' && pos + 1 < json.size()) {
                ++pos;
                if (json[pos] == 'n') result += '\n';
                else if (json[pos] == 't') result += '\t';
                else result += json[pos];
            } else {
                result += json[pos];
            }
            ++pos;
        }
        return result;
    }

    if (json.substr(pos, 4) == "null") return "";
    std::string val;
    while (pos < json.size() && json[pos] != ',' && json[pos] != '}' &&
           json[pos] != ']' && !std::isspace((unsigned char)json[pos]))
        val += json[pos++];
    return val;
}

struct Repo {
    std::string name;
    std::string description;
    std::string clone_url;
    std::string ssh_url;
    std::string language;
    bool is_private = false;
};

struct Config {
    std::string token;
    std::string clone_dir;
    bool use_ssh = false;
};


static Config loadConfig() {
    Config cfg;

    const char* home = std::getenv("HOME");
    cfg.clone_dir = home ? std::string(home) + "/Projects" : ".";

    if (const char* t = std::getenv("GITHUB_TOKEN")) cfg.token = t;
    if (const char* d = std::getenv("GH_PICKER_DIR")) cfg.clone_dir = d;
    if (const char* s = std::getenv("GH_PICKER_SSH"); s && std::string(s) == "1")
        cfg.use_ssh = true;

    std::string conf_path = std::string(home ? home : ".") + "/.gh-picker.conf";
    if (FILE* f = fopen(conf_path.c_str(), "r")) {
        char line[512];
        while (fgets(line, sizeof(line), f)) {
            std::string l(line);
            if (!l.empty() && l.back() == '\n') l.pop_back();
            if (l.empty() || l[0] == '#') continue;
            auto eq = l.find('=');
            if (eq == std::string::npos) continue;
            std::string k = l.substr(0, eq), v = l.substr(eq + 1);
            if (k == "token" && cfg.token.empty()) cfg.token = v;
            else if (k == "clone_dir")             cfg.clone_dir = v;
            else if (k == "use_ssh")               cfg.use_ssh = (v == "1" || v == "true");
        }
        fclose(f);
    }
    return cfg;
}


static size_t writeCallback(void* data, size_t size, size_t nmemb, std::string* out) {
    out->append(static_cast<char*>(data), size * nmemb);
    return size * nmemb;
}

static std::vector<Repo> fetchRepos(const std::string& token, int page = 1) {
    std::vector<Repo> repos;

    CURL* curl = curl_easy_init();
    if (!curl) return repos;

    std::string url = "https://api.github.com/user/repos?per_page=100&page="
                    + std::to_string(page) + "&sort=updated&affiliation=owner";
    std::string response;

    curl_slist* headers = nullptr;
    headers = curl_slist_append(headers, ("Authorization: Bearer " + token).c_str());
    headers = curl_slist_append(headers, "Accept: application/vnd.github+json");
    headers = curl_slist_append(headers, "X-GitHub-Api-Version: 2022-11-28");
    headers = curl_slist_append(headers, "User-Agent: github-picker/1.0");

    curl_easy_setopt(curl, CURLOPT_URL, url.c_str());
    curl_easy_setopt(curl, CURLOPT_HTTPHEADER, headers);
    curl_easy_setopt(curl, CURLOPT_WRITEFUNCTION, writeCallback);
    curl_easy_setopt(curl, CURLOPT_WRITEDATA, &response);
    curl_easy_setopt(curl, CURLOPT_FOLLOWLOCATION, 1L);

    CURLcode res = curl_easy_perform(curl);
    long http_code = 0;
    curl_easy_getinfo(curl, CURLINFO_RESPONSE_CODE, &http_code);
    curl_slist_free_all(headers);
    curl_easy_cleanup(curl);

    if (res != CURLE_OK || http_code != 200) {
        std::cerr << "Error: HTTP " << http_code << " from GitHub.\n";
        auto msg = extractJsonString(response, "message");
        if (!msg.empty()) std::cerr << "GitHub says: " << msg << "\n";
        return repos;
    }

    size_t pos = 0;
    while ((pos = response.find('{', pos)) != std::string::npos) {
        int depth = 0;
        bool in_str = false;
        size_t start = pos;
        for (size_t i = pos; i < response.size(); ++i) {
            char c = response[i];
            if (c == '\\' && in_str) { ++i; continue; }
            if (c == '"')  { in_str = !in_str; continue; }
            if (in_str)    continue;
            if (c == '{')  ++depth;
            else if (c == '}' && --depth == 0) {
                std::string obj = response.substr(start, i - start + 1);
                Repo r;
                r.name        = extractJsonString(obj, "name");
                r.clone_url   = extractJsonString(obj, "clone_url");
                r.ssh_url     = extractJsonString(obj, "ssh_url");
                r.description = extractJsonString(obj, "description");
                r.language    = extractJsonString(obj, "language");
                r.is_private  = (extractJsonString(obj, "private") == "true");
                if (!r.name.empty() && !r.clone_url.empty())
                    repos.push_back(r);
                pos = i + 1;
                break;
            }
        }
        if (depth != 0) break;
    }
    return repos;
}


static void printRepos(const std::vector<Repo>& repos) {
    for (int i = 0; i < (int)repos.size(); ++i) {
        const auto& r = repos[i];
        std::cout << "  " << (i + 1) << ".  " << r.name;
        if (r.is_private) std::cout << " [private]";
        if (!r.language.empty()) std::cout << "  ·  " << r.language;
        if (!r.description.empty()) {
            std::string desc = r.description;
            if (desc.size() > 60) desc = desc.substr(0, 59) + "…";
            std::cout << "\n       " << desc;
        }
        std::cout << "\n";
    }
}

static int pickRepo(const std::vector<Repo>& repos) {
    std::string filter;
    std::cout << "\nFilter by name (or press Enter to skip): ";
    std::getline(std::cin, filter);

    std::vector<int> matches;
    std::string fl = filter;
    std::transform(fl.begin(), fl.end(), fl.begin(), ::tolower);
    for (int i = 0; i < (int)repos.size(); ++i) {
        std::string name = repos[i].name;
        std::transform(name.begin(), name.end(), name.begin(), ::tolower);
        if (fl.empty() || name.find(fl) != std::string::npos)
            matches.push_back(i);
    }

    if (matches.empty()) {
        std::cout << "No repos match \"" << filter << "\".\n";
        return -1;
    }

    std::cout << "\n";
    for (int j = 0; j < (int)matches.size(); ++j) {
        const auto& r = repos[matches[j]];
        std::cout << "  " << (j + 1) << ".  " << r.name;
        if (r.is_private) std::cout << " [private]";
        if (!r.language.empty()) std::cout << "  ·  " << r.language;
        if (!r.description.empty()) {
            std::string desc = r.description;
            if (desc.size() > 60) desc = desc.substr(0, 59) + "…";
            std::cout << "\n       " << desc;
        }
        std::cout << "\n";
    }

    std::cout << "\nPick a number (1–" << matches.size() << "): ";
    std::string line;
    if (!std::getline(std::cin, line) || line.empty()) return -1;

    int n = 0;
    try { n = std::stoi(line); } catch (...) { return -1; }
    if (n < 1 || n > (int)matches.size()) return -1;

    return matches[n - 1];
}

static void openInEditor(const std::string& path) {
    std::vector<std::string> candidates = {
        "code", "cursor", "zed", "idea", "nvim", "vim",
    };

    if (const char* ide = std::getenv("GH_PICKER_IDE")) {
        std::system((std::string(ide) + " \"" + path + "\" &").c_str());
        return;
    }
    if (const char* ed = std::getenv("EDITOR")) candidates.insert(candidates.begin(), ed);

    for (const auto& cmd : candidates) {
        if (std::system(("which " + cmd + " >/dev/null 2>&1").c_str()) == 0) {
            std::cout << "Opening with " << cmd << "...\n";
            std::system((cmd + " \"" + path + "\" &").c_str());
            return;
        }
    }

#ifdef __APPLE__
    std::system(("open \"" + path + "\"").c_str());
#else
    std::system(("xdg-open \"" + path + "\" 2>/dev/null &").c_str());
#endif
}

static bool cloneRepo(const Repo& repo, bool use_ssh, const std::string& base_dir) {
    std::string url  = use_ssh ? repo.ssh_url : repo.clone_url;
    std::string dest = base_dir + "/" + repo.name;
    std::cout << "Cloning into " << dest << "...\n";
    return std::system(("git clone " + url + " \"" + dest + "\"").c_str()) == 0;
}


int main(int argc, char* argv[]) {
    Config cfg = loadConfig();

    for (int i = 1; i < argc; ++i) {
        std::string a(argv[i]);
        if ((a == "--token" || a == "-t") && i + 1 < argc) cfg.token = argv[++i];
        else if ((a == "--dir" || a == "-d") && i + 1 < argc) cfg.clone_dir = argv[++i];
        else if (a == "--ssh" || a == "-s") cfg.use_ssh = true;
        else if (a == "--help" || a == "-h") {
            std::cout <<
                "github-picker — browse and clone your GitHub repos\n\n"
                "Usage: github-picker [options]\n\n"
                "Options:\n"
                "  -t, --token <token>   GitHub personal access token\n"
                "  -d, --dir   <path>    Where to clone repos (default: ~/Projects)\n"
                "  -s, --ssh             Use SSH instead of HTTPS\n"
                "  -h, --help            Show this help\n\n"
                "Environment variables:\n"
                "  GITHUB_TOKEN          Personal access token\n"
                "  GH_PICKER_DIR         Clone target directory\n"
                "  GH_PICKER_SSH=1       Use SSH URLs\n"
                "  GH_PICKER_IDE         Editor command (e.g. code, cursor, zed)\n\n"
                "Config file: ~/.gh-picker.conf\n"
                "  token=ghp_...\n"
                "  clone_dir=/home/you/Projects\n"
                "  use_ssh=0\n";
            return 0;
        }
    }

    if (cfg.token.empty()) {
        std::cerr << "No GitHub token found.\n"
                  << "Set GITHUB_TOKEN, use --token, or add token= to ~/.gh-picker.conf\n"
                  << "Generate one at: https://github.com/settings/tokens\n"
                  << "Required scope: repo (or public_repo for public only)\n";
        return 1;
    }

    std::cout << "Fetching your repositories...\n";

    std::vector<Repo> repos;
    for (int p = 1; p <= 5; ++p) {
        auto page = fetchRepos(cfg.token, p);
        if (page.empty()) break;
        repos.insert(repos.end(), page.begin(), page.end());
        if ((int)page.size() < 100) break;
    }

    if (repos.empty()) {
        std::cerr << "No repositories found (or token error).\n";
        return 1;
    }

    std::cout << "Found " << repos.size() << " repos.\n";

    int chosen = pickRepo(repos);
    if (chosen < 0 || chosen >= (int)repos.size()) {
        std::cout << "Cancelled.\n";
        return 0;
    }

    const Repo& repo = repos[chosen];
    std::system(("mkdir -p \"" + cfg.clone_dir + "\"").c_str());

    std::string dest = cfg.clone_dir + "/" + repo.name;
    if (access(dest.c_str(), F_OK) == 0) {
        std::cout << dest << " already exists, skipping clone.\n";
    } else {
        if (!cloneRepo(repo, cfg.use_ssh, cfg.clone_dir)) {
            std::cerr << "Clone failed.\n";
            return 1;
        }
    }

    openInEditor(dest);
    return 0;
}
