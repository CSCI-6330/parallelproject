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
#include <thread>
#include <chrono>

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
        curl_easy_setopt(curl, CURLOPT_CONNECTTIMEOUT, 15L);  // time out 15s
        curl_easy_setopt(curl, CURLOPT_TIMEOUT, 300L);
        CURLcode res = curl_easy_perform(curl);
        curl_easy_cleanup(curl);
        return res == CURLE_OK;
}

bool http_get_with_retry(const std::string& url, std::string& out,
                                                 int max_retries = 3, int sleep_seconds = 2) {
        for (int attempt = 1; attempt <= max_retries; ++attempt) {
                out.clear();
                if (http_get(url, out)) return true;
                std::cerr << "http_get failed for " << url
                                    << ", attempt " << attempt << "/" << max_retries << "\n";
                if (attempt < max_retries) {
                        std::this_thread::sleep_for(std::chrono::seconds(sleep_seconds));
                }
        }
        return false;
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



int main(int argc, char** argv) {

    if (argc < 4) {
        std::cerr << "Usage: " << argv[0]
                            << " <size> <MB|GB> <out_dir>\n"
                            << "  e.g. " << argv[0] << " 1 GB data_1G\n"
                            << "       " << argv[0] << " 5 GB data_5G\n"
                            << "       " << argv[0] << " 10 GB data_10G\n";
        return 1;
    }

    // 1 Parse arguments: size + unit + output directory
    size_t size_val = std::stoull(argv[1]);
    std::string unit = argv[2];
    std::string out_dir = argv[3];

    size_t factor;
    if (unit == "GB" || unit == "gb") {
            factor = 1024ull * 1024ull * 1024ull;
    } else { // Default to MB
            factor = 1024ull * 1024ull;
    }
    const size_t TARGET_BYTES = size_val * factor;

    fs::create_directories(out_dir);

    curl_global_init(CURL_GLOBAL_ALL);

    // 2 Construct URLs using page=1,2,3,... instead of relying on JSON's "next"
    const std::string base_url =
            "https://gutendex.com/books?languages=en&mime_type=text/plain&page=";

    size_t total_bytes = 0;
    size_t file_count  = 0;
    int page = 1;

    while (true) {
            std::string page_url = base_url + std::to_string(page);
            std::string body;

            std::cout << "Fetching metadata page " << page
                                << ": " << page_url << "\n";

            // 3 Fetch the current page JSON (with retries)
            if (!http_get_with_retry(page_url, body, 3, 2)) {
                    std::cerr << "Failed to GET page: " << page_url << "\n";
                    break;  // Exit if retries fail (likely a network issue)
            }

            // 4 Parse JSON, skip this page if parsing fails, and continue to the next page
            json j = json::parse(body, nullptr, false);
            if (j.is_discarded()) {
                    std::cerr << "JSON parse error on page " << page
                                        << ", skipping this page.\n";
                    ++page;
                    continue;  // Skip this page
            }

            if (!j.contains("results") || !j["results"].is_array()
                    || j["results"].empty()) {
                    std::cout << "No more results at page " << page << ".\n";
                    break;  // No more books, exit
            }

            // 5 Iterate through the books on this page
            for (const auto& book : j["results"]) {
                    if (!book.contains("formats")) continue;

                    std::string text_url = pick_text_url(book["formats"]);
                    if (text_url.empty()) continue;

                    int id = book.value("id", 0);
                    std::string title = book.value("title", std::string("book"));

                    std::string fname = sanitize_filename(id, title);
                    fs::path out_path = fs::path(out_dir) / fname;

                    // 5.1 Resume: If the file already exists and is non-empty, skip downloading but count its size
                    if (fs::exists(out_path) && fs::file_size(out_path) > 0) {
                            auto sz = fs::file_size(out_path);
                            total_bytes += sz;
                            ++file_count;
                            std::cout << "Skip existing " << out_path.string()
                                                << " (" << sz << " bytes). Total=" << total_bytes << "\n";

                            if (TARGET_BYTES > 0 && total_bytes >= TARGET_BYTES) {
                                    std::cout << "Reached target ~" << size_val << " " << unit
                                                        << " via existing files. Files: " << file_count << "\n";
                                    curl_global_cleanup();
                                    return 0;
                            }
                            continue;
                    }

                    // 5.2 Download the content (with retries)
                    std::string raw;
                    if (!http_get_with_retry(text_url, raw, 3, 2)) {
                            std::cerr << "Download failed after retries: " << text_url << "\n";
                            continue;
                    }

                    std::string clean = strip_headers(raw);

                    std::ofstream ofs(out_path, std::ios::binary);
                    ofs << clean;
                    ofs.close();

                    total_bytes += clean.size();
                    ++file_count;
                    std::cout << "Saved " << out_path.string() << " ("
                                        << clean.size() << " bytes). Total=" << total_bytes << "\n";

                    if (TARGET_BYTES > 0 && total_bytes >= TARGET_BYTES) {
                            std::cout << "Reached target ~" << size_val << " " << unit
                                                << ". Files: " << file_count << "\n";
                            curl_global_cleanup();
                            return 0;
                    }

                    // Throttle slightly to avoid too frequent requests
                    std::this_thread::sleep_for(std::chrono::milliseconds(100));
            }

            // 6 Finished processing this page, move to the next page
            ++page;
    }

    std::cout << "Done. Files: " << file_count
                        << ", Total bytes: " << total_bytes << "\n";
    curl_global_cleanup();
    return 0;
}
