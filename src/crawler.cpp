#include "crawler.hpp"

#include <cpr/cpr.h>
#include <nlohmann/json.hpp>

#include <algorithm>
#include <atomic>
#include <chrono>
#include <condition_variable>
#include <cctype>
#include <deque>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <mutex>
#include <optional>
#include <queue>
#include <regex>
#include <sstream>
#include <string>
#include <thread>
#include <unordered_map>
#include <unordered_set>
#include <vector>

using nlohmann::json;
namespace fs = std::filesystem;

static const std::string kUserAgent = "BookScraper/1.0 (+https://freecomputerbooks.com crawler for personal archiving)";

// -------------------- ctor --------------------
Crawler::Crawler(std::string baseUrl,
                 std::string outputDir,
                 int maxPages,
                 int maxConcurrency,
                 int delayMs,
                 std::vector<std::string> targetExtensions)
    : baseUrl_(std::move(baseUrl)),
      outDir_(std::move(outputDir)),
      maxPages_(maxPages),
      maxConcurrency_(maxConcurrency),
      delayMs_(delayMs),
      targetExtensions_(std::move(targetExtensions)) {
    auto parts = parse_url(baseUrl_);
    if (!parts) throw std::runtime_error("Invalid base URL");
    baseScheme_ = parts->scheme;
    baseHost_ = parts->host;
}

// -------------------- small utils --------------------
std::string Crawler::to_lower(const std::string& s) {
    std::string r = s;
    for (auto& ch : r) ch = static_cast<char>(::tolower(static_cast<unsigned char>(ch)));
    return r;
}

bool Crawler::starts_with(const std::string& s, const std::string& pre) {
    return s.rfind(pre, 0) == 0;
}

bool Crawler::ends_with(const std::string& s, const std::string& suf) {
    if (s.size() < suf.size()) return false;
    return std::equal(s.end() - suf.size(), s.end(), suf.begin());
}

std::optional<Crawler::UrlParts> Crawler::parse_url(const std::string& url) {
    static const std::regex re(R"(^([a-zA-Z][a-zA-Z0-9+.-]*):\/\/([^\/]+)(\/.*)?$)");
    std::smatch m;
    if (std::regex_match(url, m, re)) {
        UrlParts p;
        p.scheme = m[1].str();
        p.host = m[2].str();
        p.path = m.size() >= 4 ? m[3].str() : "/";
        if (p.path.empty()) p.path = "/";
        return p;
    }
    return std::nullopt;
}

bool Crawler::is_absolute_url(const std::string& url) {
    return url.find("://") != std::string::npos;
}

std::string Crawler::dirname_path(const std::string& path) {
    auto pos = path.rfind('/');
    if (pos == std::string::npos) return "/";
    if (pos == 0) return "/";
    return path.substr(0, pos);
}

std::string Crawler::join_url(const UrlParts& base, const std::string& link) {
    if (link.empty()) return base.scheme + "://" + base.host + base.path;
    // protocol-relative
    if (link.size() > 1 && link[0] == '/' && link[1] == '/') return base.scheme + ":" + link;
    if (link[0] == '/') return base.scheme + "://" + base.host + link;
    // simple relative join
    std::string dir = dirname_path(base.path);
    if (!ends_with(dir, "/")) dir += "/";
    return base.scheme + "://" + base.host + dir + link;
}

std::string Crawler::normalize_url(const std::string& url) {
    // drop fragment and normalize trailing slash
    auto hash = url.find('#');
    std::string u = hash == std::string::npos ? url : url.substr(0, hash);
    if (u.size() > 1 && ends_with(u, "/")) u.pop_back();
    return u;
}

bool Crawler::same_host(const std::string& url) const {
    auto p = parse_url(url);
    return p && to_lower(p->host) == to_lower(baseHost_);
}

bool Crawler::is_pdf_url(const std::string& url) const {
    std::string lower = to_lower(url);
    return ends_with_any(lower, targetExtensions_);
}

bool Crawler::ends_with_any(const std::string& s, const std::vector<std::string>& suffices) const {
    for (const auto& suf : suffices) {
        if (ends_with(s, to_lower(suf))) return true;
    }
    return false;
}

void Crawler::polite_delay() const {
    std::this_thread::sleep_for(std::chrono::milliseconds(delayMs_));
}

// -------------------- robots --------------------
void Crawler::fetch_robots() {
    const std::string robots_url = baseScheme_ + "://" + baseHost_ + "/robots.txt";
    long status = 0;
    std::string body = fetch_text(robots_url, &status);
    if (status != 200 || body.empty()) return;

    std::istringstream iss(body);
    std::string line;
    bool in_all = false;
    auto trim = [](std::string& s){
        size_t a = s.find_first_not_of(" \t\r\n");
        size_t b = s.find_last_not_of(" \t\r\n");
        if (a == std::string::npos) { s.clear(); return; }
        s = s.substr(a, b - a + 1);
    };
    while (std::getline(iss, line)) {
        auto hash = line.find('#');
        if (hash != std::string::npos) line = line.substr(0, hash);
        trim(line);
        if (line.empty()) continue;
        std::string lower = to_lower(line);
        if (starts_with(lower, "user-agent:")) {
            auto v = line.substr(11);
            trim(v);
            in_all = (to_lower(v) == "*");
        } else if (in_all && (starts_with(lower, "allow:") || starts_with(lower, "disallow:"))) {
            bool allow = starts_with(lower, "allow:");
            auto v = line.substr(allow ? 6 : 9);
            trim(v);
            if (allow) robotsAllow_.push_back(v);
            else robotsDisallow_.push_back(v);
        }
    }
}

bool Crawler::robots_allowed(const std::string& path) const {
    size_t disLen = 0;
    for (const auto& d : robotsDisallow_) {
        if (starts_with(path, d) && d.size() > disLen) disLen = d.size();
    }
    size_t allowLen = 0;
    for (const auto& a : robotsAllow_) {
        if (starts_with(path, a) && a.size() > allowLen) allowLen = a.size();
    }
    return allowLen >= disLen;
}

// -------------------- path/category helpers --------------------
std::string Crawler::get_category_from_url(const std::string& url) const {
    auto p = parse_url(url);
    if (!p) return "uncategorized";
    std::string path = p->path;
    if (path.size() > 1 && path[0] == '/') path = path.substr(1);
    auto slash = path.find('/');
    std::string seg = (slash == std::string::npos) ? path : path.substr(0, slash);
    if (seg.empty()) return "root";
    return sanitize_filename(seg);
}

std::string Crawler::sanitize_filename(const std::string& name) {
    std::string s = name;
    for (char& c : s) {
        if (c == '/' || c == '\\' || c == ':' || c == '*' || c == '?' || c == '"' || c == '<' || c == '>' || c == '|') c = '_';
    }
    return s;
}

// -------------------- network & parsing --------------------
std::vector<std::string> Crawler::extract_links(const std::string& html, const std::string& base_url) const {
    std::vector<std::string> links;
    auto base = parse_url(base_url).value();
    // Only <a ... href="...">, case-insensitive
    static const std::regex a_href_re(R"xxx(<\s*a\b[^>]*?href\s*=\s*(?:"([^"]+)"|'([^']+)'))xxx", std::regex::icase);
    for (std::sregex_iterator it(html.begin(), html.end(), a_href_re), end; it != end; ++it) {
        std::string link = (*it)[1].matched ? (*it)[1].str() : (*it)[2].str();
        if (link.empty()) continue;
        if (!is_absolute_url(link)) link = join_url(base, link);
        link = normalize_url(link);
        if (!same_host(link)) continue;
        links.push_back(link);
    }
    return links;
}

std::string Crawler::fetch_text(const std::string& url, long* status) {
    cpr::Response r = cpr::Get(cpr::Url{url},
                               cpr::Header{{"User-Agent", kUserAgent}},
                               cpr::Timeout{30000},
                               cpr::Redirect{true});
    if (status) *status = r.status_code;
    if (r.error) return {};
    return r.text;
}

std::optional<std::pair<long,long long>> Crawler::download_to_file(
    const std::string& url,
    const std::string& filepath,
    const std::unordered_map<std::string,std::string>& headers) {

    cpr::Header hdr{{"User-Agent", kUserAgent}};
    for (auto& kv : headers) hdr[kv.first] = kv.second;

    cpr::Response r = cpr::Get(cpr::Url{url}, hdr, cpr::Timeout{120000}, cpr::Redirect{true});
    if (r.error) return std::nullopt;

    long status = r.status_code;
    long long content_len = -1;
    auto it = r.header.find("content-length");
    if (it == r.header.end()) it = r.header.find("Content-Length");
    if (it != r.header.end()) {
        try { content_len = std::stoll(it->second); } catch (...) {}
    }
    if (status == 200 && !r.text.empty()) {
        std::ofstream ofs(filepath, std::ios::binary);
        ofs.write(r.text.data(), static_cast<std::streamsize>(r.text.size()));
        ofs.close();
    }
    return std::make_optional(std::make_pair(status, content_len));
}

void Crawler::ensure_dir(const std::string& path) const {
    fs::create_directories(path);
}

void Crawler::write_manifest(const std::string& filepath) const {
    json j = json::array();
    {
        std::lock_guard<std::mutex> lk(manifest_mtx_);
        for (const auto& m : manifest_) {
            j.push_back({
                {"pdf_url", m.pdf_url},
                {"saved_path", m.saved_path},
                {"referer", m.referer},
                {"category", m.category},
                {"status", m.status},
                {"content_length", m.content_length}
            });
        }
    }
    std::ofstream ofs(filepath);
    ofs << j.dump(2);
}

// -------------------- workers --------------------
void Crawler::crawl_worker() {
    for (;;) {
        std::string url;
        {
            std::unique_lock<std::mutex> lk(queue_mtx_);
            queue_cv_.wait(lk, [&]{ return !url_queue_.empty() || pending_pages_ == 0; });
            if (url_queue_.empty() && pending_pages_ == 0) break;
            url = std::move(url_queue_.front());
            url_queue_.pop_front();
        }

        url = normalize_url(url);
        auto parts = parse_url(url);
        if (!parts) continue;

        // visited check
        {
            std::lock_guard<std::mutex> lk(visited_mtx_);
            if (visitedPages_.count(url)) continue;
            visitedPages_.insert(url);
        }

    if (!robots_allowed(parts->path)) continue;

        polite_delay();

        long status = 0;
        std::cout << "Visiting: " << url << std::endl;
        std::string html = fetch_text(url, &status);
        std::cout << "  Status: " << status << ", bytes: " << html.size() << std::endl;
        if (status != 200 || html.empty()) {
            // done with this URL
            {
                std::lock_guard<std::mutex> lk(queue_mtx_);
                --pending_pages_;
                if (pending_pages_ == 0 && url_queue_.empty()) queue_cv_.notify_all();
            }
            continue;
        }

        int crawled_now = ++pages_crawled_;

        // Extract links
        auto links = extract_links(html, url);

        // Extract PDFs on page (allow external hosts)
        // Build regex dynamically for target extensions: (\.pdf|\.epub|...)
        static thread_local std::regex pdf_re; // lazily init below per thread
        if (!pdf_re.mark_count()) {
            std::string exts;
            for (size_t i = 0; i < targetExtensions_.size(); ++i) {
                std::string e = targetExtensions_[i];
                if (!e.empty() && e[0] != '.') e = "." + e;
                // escape dot
                std::string esc;
                for (char c : e) { if (c == '.') esc += "\\."; else esc += c; }
                if (i) exts += "|";
                exts += esc;
            }
            if (exts.empty()) exts = "\\.pdf";
            std::string pat = R"((?:href|src)\s*=\s*(?:\"([^\"]+(EXTS))\"|'([^']+(EXTS))'))";
            // replace EXTS with (\.pdf|\.epub...)
            const std::string group = std::string("(") + exts + ")";
            size_t pos;
            while ((pos = pat.find("EXTS")) != std::string::npos) pat.replace(pos, 4, group);
            pdf_re = std::regex(pat, std::regex::icase);
        }
        
        
        
        std::unordered_set<std::string> page_pdfs;
        for (std::sregex_iterator it(html.begin(), html.end(), pdf_re), end; it != end; ++it) {
            std::string link = (*it)[1].matched ? (*it)[1].str() : (*it)[2].str();
            if (link.empty()) continue;
            if (!is_absolute_url(link)) link = join_url(parts.value(), link);
            link = normalize_url(link);
            if (!is_pdf_url(link)) continue;
            page_pdfs.insert(link);
        }
        std::cout << "  PDFs found on page: " << page_pdfs.size() << std::endl;

        // Enqueue downloads
        std::string category = get_category_from_url(url);
        for (const auto& pdf : page_pdfs) {
            bool should_enqueue = false;
            {
                std::lock_guard<std::mutex> lk(downloaded_mtx_);
                if (!downloadedPdfs_.count(pdf)) {
                    downloadedPdfs_.insert(pdf);
                    should_enqueue = true;
                }
            }
            if (should_enqueue) {
                {
                    std::lock_guard<std::mutex> lk(download_mtx_);
                    download_queue_.push_back(DownloadTask{pdf, url, category});
                    ++pending_downloads_;
                }
                download_cv_.notify_one();
            }
        }

        // Enqueue more same-host links if under maxPages
    if (maxPages_ == 0 || crawled_now < maxPages_) {
            int added = 0;
            for (const auto& l : links) {
                bool add = false;
                {
                    std::lock_guard<std::mutex> lk(enqueued_mtx_);
                    if (!enqueuedPages_.count(l)) {
                        enqueuedPages_.insert(l);
                        add = true;
                    }
                }
                if (add) {
                    std::lock_guard<std::mutex> lk(queue_mtx_);
                    url_queue_.push_back(l);
                    ++pending_pages_;
                    ++added;
                }
            }
            if (added) queue_cv_.notify_all();
        }

        // If we reached limit and no more queued pages, close crawling
    if (maxPages_ > 0 && pages_crawled_ >= maxPages_) {
            std::lock_guard<std::mutex> lk(queue_mtx_);
            if (pending_pages_ == 0 && url_queue_.empty()) {
                pending_pages_ = 0; // ensure signal
                queue_cv_.notify_all();
            }
        }

        // finished processing current URL
        {
            std::lock_guard<std::mutex> lk(queue_mtx_);
            --pending_pages_;
            if (pending_pages_ == 0 && url_queue_.empty()) queue_cv_.notify_all();
        }
    }
    // When a crawler exits, wake downloaders in case they are waiting on finalization
    download_cv_.notify_all();
}

void Crawler::download_worker() {
    for (;;) {
        DownloadTask task;
        {
            std::unique_lock<std::mutex> lk(download_mtx_);
            download_cv_.wait(lk, [&]{ return !download_queue_.empty() || (pending_downloads_ == 0 && pending_pages_ == 0); });
            if (download_queue_.empty() && pending_downloads_ == 0 && pending_pages_ == 0) break;
            task = std::move(download_queue_.front());
            download_queue_.pop_front();
            --pending_downloads_;
        }

        // Ensure category dir
        fs::path catDir = fs::path(outDir_) / task.category;
        ensure_dir(catDir.string());
        // Build filename
        std::string filename;
        auto slash = task.url.find_last_of('/');
        filename = (slash == std::string::npos) ? task.url : task.url.substr(slash + 1);
        filename = sanitize_filename(filename);
        fs::path savePath = catDir / filename;

        polite_delay();
        auto res = download_to_file(task.url, savePath.string(), {{"Referer", task.referer}});

        ManifestItem item;
        item.pdf_url = task.url;
        item.saved_path = savePath.string();
        item.referer = task.referer;
        item.category = task.category;
        if (res) { item.status = res->first; item.content_length = res->second; }
        {
            std::lock_guard<std::mutex> lk(manifest_mtx_);
            manifest_.push_back(std::move(item));
        }
        if (res) {
            std::cout << "  Downloaded: " << task.url << " -> " << savePath.string() << " (status " << res->first << ", length " << (res->second) << ")" << std::endl;
        } else {
            std::cout << "  Failed: " << task.url << std::endl;
        }
    }
}

// -------------------- orchestration --------------------
void Crawler::run() {
    ensure_dir(outDir_);
    fetch_robots();

    std::string start = normalize_url(baseUrl_);
    {
        std::lock_guard<std::mutex> lk(queue_mtx_);
        url_queue_.push_back(start);
        pending_pages_ = 1;
    }
    queue_cv_.notify_all();

    int crawl_threads = std::max(1, maxConcurrency_);
    int download_threads = std::max(1, maxConcurrency_);

    std::vector<std::thread> crawlers;
    std::vector<std::thread> downloaders;
    crawlers.reserve(crawl_threads);
    downloaders.reserve(download_threads);

    for (int i = 0; i < crawl_threads; ++i) crawlers.emplace_back(&Crawler::crawl_worker, this);
    for (int i = 0; i < download_threads; ++i) downloaders.emplace_back(&Crawler::download_worker, this);

    for (auto& t : crawlers) t.join();
    // Crawling done; wake downloaders to allow exit once queue drains
    download_cv_.notify_all();
    for (auto& t : downloaders) t.join();

    auto manifest_path = (fs::path(outDir_) / "manifest.json").string();
    write_manifest(manifest_path);
    std::cout << "Manifest written: " << manifest_path << ", items: " << manifest_.size() << std::endl;
}
