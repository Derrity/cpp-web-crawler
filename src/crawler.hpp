#pragma once

#include <string>
#include <unordered_set>
#include <deque>
#include <vector>
#include <unordered_map>
#include <optional>
#include <mutex>
#include <condition_variable>
#include <atomic>

class Crawler {
public:
    Crawler(std::string baseUrl,
            std::string outputDir,
            int maxPages,
            int maxConcurrency,
            int delayMs,
            std::vector<std::string> targetExtensions = {".pdf"});

    void run();

private:
    struct UrlParts {
        std::string scheme;
        std::string host;
        std::string path;
    };

    std::string baseUrl_;
    std::string baseHost_;
    std::string baseScheme_;
    std::string outDir_;
    int maxPages_;
    int maxConcurrency_;
    int delayMs_;
    std::vector<std::string> targetExtensions_;

    // robots rules for User-agent: *
    std::vector<std::string> robotsAllow_;
    std::vector<std::string> robotsDisallow_;

    std::unordered_set<std::string> visitedPages_;
    std::unordered_set<std::string> downloadedPdfs_;
    std::unordered_set<std::string> enqueuedPages_;
    mutable std::mutex visited_mtx_;
    mutable std::mutex downloaded_mtx_;
    mutable std::mutex enqueued_mtx_;

    // manifest items
    struct ManifestItem {
        std::string pdf_url;
        std::string saved_path;
        std::string referer;
        std::string category;
        long status = 0;
        long long content_length = -1;
    };
    std::vector<ManifestItem> manifest_;
    mutable std::mutex manifest_mtx_;

    // Queues and threading
    std::deque<std::string> url_queue_;
    std::mutex queue_mtx_;
    std::condition_variable queue_cv_;
    std::atomic<int> pending_pages_{0};
    std::atomic<int> pages_crawled_{0};

    struct DownloadTask { std::string url; std::string referer; std::string category; };
    std::deque<DownloadTask> download_queue_;
    std::mutex download_mtx_;
    std::condition_variable download_cv_;
    std::atomic<int> pending_downloads_{0};

    // Core helpers
    static std::string to_lower(const std::string& s);
    static std::optional<UrlParts> parse_url(const std::string& url);
    static std::string normalize_url(const std::string& url);
    static bool is_absolute_url(const std::string& url);
    static std::string join_url(const UrlParts& base, const std::string& link);
    static std::string dirname_path(const std::string& path);

    static bool ends_with(const std::string& s, const std::string& suf);
    static bool starts_with(const std::string& s, const std::string& pre);

    bool same_host(const std::string& url) const;
    bool is_pdf_url(const std::string& url) const;
    bool ends_with_any(const std::string& s, const std::vector<std::string>& suffices) const;

    void polite_delay() const;

    void fetch_robots();
    bool robots_allowed(const std::string& path) const;

    std::string get_category_from_url(const std::string& url) const;
    static std::string sanitize_filename(const std::string& name);

    std::vector<std::string> extract_links(const std::string& html, const std::string& base_url) const;
    std::string fetch_text(const std::string& url, long* status = nullptr);

    std::optional<std::pair<long,long long>> download_to_file(const std::string& url,
                                                             const std::string& filepath,
                                                             const std::unordered_map<std::string,std::string>& headers = {});

    void ensure_dir(const std::string& path) const;

    void write_manifest(const std::string& filepath) const;

    // Workers
    void crawl_worker();
    void download_worker();
};
