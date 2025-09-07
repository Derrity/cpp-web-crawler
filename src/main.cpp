#include "crawler.hpp"
#include <iostream>
#include <string>

int main(int argc, char** argv) {
    std::string base = "https://freecomputerbooks.com";
    std::string outDir = "downloads";
    int maxPages = 2000;          // safety cap
    int maxConcurrency = 4;       // simple limit
    int delayMs = 800;            // polite delay
    std::vector<std::string> exts = {".pdf"};

    if (argc > 1) base = argv[1];
    if (argc > 2) outDir = argv[2];
    if (argc > 3) maxConcurrency = std::max(1, atoi(argv[3]));
    if (argc > 4) {
        // comma-separated extensions, e.g. .pdf,.epub,.djvu
        exts.clear();
        std::string list = argv[4];
        size_t pos = 0;
        while (pos != std::string::npos) {
            size_t comma = list.find(',', pos);
            std::string token = (comma == std::string::npos) ? list.substr(pos) : list.substr(pos, comma - pos);
            if (!token.empty()) exts.push_back(token);
            pos = (comma == std::string::npos) ? std::string::npos : comma + 1;
        }
        if (exts.empty()) exts.push_back(".pdf");
    }
    if (argc > 5) delayMs = std::max(0, atoi(argv[5]));
    if (argc > 6) {
        int v = atoi(argv[6]);
        maxPages = (v <= 0 ? 0 : v);
    }

    try {
    Crawler crawler(base, outDir, maxPages, maxConcurrency, delayMs, exts);
        crawler.run();
    } catch (const std::exception& ex) {
        std::cerr << "Error: " << ex.what() << std::endl;
        return 1;
    }
    return 0;
}
