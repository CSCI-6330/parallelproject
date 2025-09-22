/*
compile command:
g++ -std=gnu++17 -O2 gut_txt.cpp \
  -I/usr/local/opt/nlohmann-json/include \
  $(pkg-config --cflags --libs libcurl) \
  -o gut_txt

  change the include path as needed
*/

#include <curl/curl.h>
#include <nlohmann/json.hpp>
#include <fstream>
#include <iostream>
#include <string>
#include <vector>
#include <regex>
#include <filesystem>
namespace fs = std::filesystem;
using json = nlohmann::json;

static size_t write_to_string(void* ptr, size_t size, size_t nmemb, void* userdata){
    auto* s = static_cast<std::string*>(userdata);
    s->append(static_cast<char*>(ptr), size * nmemb);
    return size * nmemb;
}

bool http_get(const std::string& url, std::string& out){
    CURL* curl = curl_easy_init();
    if(!curl) return false;
    curl_easy_setopt(curl, CURLOPT_URL, url.c_str());
    curl_easy_setopt(curl, CURLOPT_FOLLOWLOCATION, 1L);
    curl_easy_setopt(curl, CURLOPT_ACCEPT_ENCODING, "");     // enable gzip/deflate if supported
    curl_easy_setopt(curl, CURLOPT_WRITEFUNCTION, write_to_string);
    curl_easy_setopt(curl, CURLOPT_WRITEDATA, &out);
    curl_easy_setopt(curl, CURLOPT_USERAGENT, "gutendex-cpp/1.0");
    CURLcode res = curl_easy_perform(curl);
    curl_easy_cleanup(curl);
    return res == CURLE_OK;
}

std::string strip_headers(const std::string& raw){
    // Trim Project Gutenberg header/footer and keep the main body
    const std::string begA = "*** START OF THE PROJECT GUTENBERG";
    const std::string begB = "***START OF THE PROJECT GUTENBERG";
    const std::string endA = "*** END OF THE PROJECT GUTENBERG";
    const std::string endB = "***END OF THE PROJECT GUTENBERG";

    size_t b = raw.find(begA);
    if (b == std::string::npos) b = raw.find(begB);
    if (b != std::string::npos) {
        b = raw.find('\n', b);
        if (b == std::string::npos) b = 0;
    } else b = 0;

    size_t e = raw.rfind(endA);
    if (e == std::string::npos) e = raw.rfind(endB);
    if (e == std::string::npos) e = raw.size();

    if (e > b) return raw.substr(b, e - b);
    return raw; // if markers are not found, return the raw content
}

std::string sanitize_filename(int id, const std::string& title){
    std::string t = title;
    // Replace whitespace with underscores, drop unsafe characters, limit length
    for (auto& ch : t) if (ch == ' ' || ch == '\t') ch = '_';
    t = std::regex_replace(t, std::regex(R"([^A-Za-z0-9_\-]+)"), "");
    if (t.empty()) t = "book";
    if (t.size() > 60) t = t.substr(0, 60);
    return std::to_string(id) + "_" + t + ".txt";
}

std::string pick_text_url(const json& formats){
    // Prefer plain text UTF-8; avoid .zip to keep it simple
    static const std::vector<std::string> prefs = {
        "text/plain; charset=utf-8",
        "text/plain; charset=us-ascii",
        "text/plain"
    };
    for (const auto& k : prefs){
        if (formats.contains(k) && formats[k].is_string()){
            std::string u = formats[k].get<std::string>();
            if (u.find(".zip") == std::string::npos) return u;
        }
    }
    // Fallback: pick any text/plain* entry
    for (auto it = formats.begin(); it != formats.end(); ++it){
        if (it.key().rfind("text/plain", 0) == 0 && it.value().is_string()){
            std::string u = it.value().get<std::string>();
            if (u.find(".zip") == std::string::npos) return u;
        }
    }
    return {};
}

int main(int argc, char** argv){

    std::string out_dir = (argc > 2) ? std::string(argv[2]) : std::string("data");
    fs::create_directories(out_dir);    // create if not exists

    size_t target_mb = (argc > 1) ? static_cast<size_t>(std::stoul(argv[1])) : 10;
    const size_t TARGET_BYTES = target_mb * 1024ull * 1024ull; // target in bytes (MB), if use GB change here

    curl_global_init(CURL_GLOBAL_ALL);

    std::string page_url = "https://gutendex.com/books?languages=en&mime_type=text/plain";
    size_t total_bytes = 0;
    size_t file_count = 0;

    while (!page_url.empty()){
        std::string body;
        if (!http_get(page_url, body)){
            std::cerr << "Failed to GET: " << page_url << "\n";
            break;
        }

        json j = json::parse(body, nullptr, false);
        if (j.is_discarded()){
            std::cerr << "JSON parse error\n";
            break;
        }

        if (j.contains("results") && j["results"].is_array()){
            for (const auto& book : j["results"]){
                if (!book.contains("formats")) continue;
                std::string text_url = pick_text_url(book["formats"]);
                if (text_url.empty()) continue;

                int id = book.value("id", 0);
                std::string title = book.value("title", std::string("book"));

                std::string raw;
                if (!http_get(text_url, raw)){
                    std::cerr << "Download failed: " << text_url << "\n";
                    continue;
                }

                std::string clean = strip_headers(raw);
                std::string fname = sanitize_filename(id, title);

                fs::path out_path = fs::path(out_dir) / fname;
                std::ofstream ofs(out_path, std::ios::binary);
                ofs << clean;
                ofs.close();

                total_bytes += clean.size();
                ++file_count;
                std::cout << "Saved " << out_path.string() << " (" << clean.size()
                          << " bytes). Total=" << total_bytes << " bytes\n";

                if (TARGET_BYTES > 0 && total_bytes >= TARGET_BYTES){
                    std::cout << "Reached target ~" << target_mb << " MB. Files: "
                              << file_count << "\n";
                    curl_global_cleanup();
                    return 0;
                }
            }
        }

        if (j.contains("next") && !j["next"].is_null() && j["next"].is_string()){
            page_url = j["next"].get<std::string>();
        } else {
            page_url.clear(); // no more pages
        }
    }

    std::cout << "Done. Files: " << file_count
              << ", Total bytes: " << total_bytes << "\n";
    curl_global_cleanup();
    return 0;
}
