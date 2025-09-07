# BookScraper (C++)

一个简洁的多线程站内爬虫：从起始页面开始遍历同域页面，解析页面中的超链接（仅 a href），抽取指定后缀的文件链接（默认 .pdf，允许跨域下载），按“首个路径段”归类保存，并输出下载清单 `manifest.json`。

要点与限制：
- 遵守 robots.txt（仅遍历同域页面；文件链接可跨域下载）。
- 速率限制（可配置），请合理设置并避免过载访问。
- 仅用于学习和个人备份，请遵守目标站点条款与版权。

## 特性
- 多线程：页面抓取与文件下载分别使用线程池并行执行。
- 仅解析 a href，减少无效抓取；支持相对路径与协议相对链接（//host/path）。
- 文件类型可配：通过命令行指定多个后缀（如 .pdf,.epub）。
- 站点友好：读取 robots.txt，按请求间隔限速。
- 结果可追踪：输出 JSON 清单（包含状态码、Referer 等）。

## 构建

依赖：CMake ≥ 3.16，C++17，联网（通过 FetchContent 拉取依赖：CPR 与 nlohmann/json）。

```bash
# 生成构建目录
cmake -S . -B build -DCMAKE_BUILD_TYPE=Release
# 编译
cmake --build build -j
```

可执行文件：`build/book_scraper`

## 用法

```bash
./build/book_scraper <起始URL> <输出目录> [并发数] [后缀列表] [请求间隔ms] [最大页面数]
```

- 并发数：页面线程数与下载线程数（默认 4）。
- 后缀列表：逗号分隔，大小写不敏感。可写 `.pdf,.epub` 或 `pdf,epub`。
- 请求间隔ms：每个请求之间的休眠时间（默认 800）。
- 最大页面数：遍历页面上限（默认 2000）；设置为 `0` 表示不限制。

示例：

```bash
# 仅抓取 PDF，4 并发，默认节流
./build/book_scraper https://freecomputerbooks.com ./downloads 4 .pdf

# 抓取 PDF+EPUB，6 并发，请求间隔 1500ms，最多 400 页
./build/book_scraper https://freecomputerbooks.com ./downloads 6 .pdf,.epub 1500 400

# 替换为其他站点，抓取 PDF+DJVU，不限页数
./build/book_scraper https://example.com ./out 8 .pdf,.djvu 1000 0
```

## 输出
- 目录结构：`<输出目录>/<分类>/<文件名>`，分类为“URL 主机后的首个路径段”（根路径记为 `root`，例如 `https://site.com/top-books.html/...` -> `top-books.html`）。
- 清单文件：`<输出目录>/manifest.json`
	- 字段：`pdf_url` / `saved_path` / `referer` / `category` / `status` / `content_length`

## 工作原理（简述）
- 遍历范围：仅同域 URL 会入队继续抓（更换起始 URL 可爬取不同站点）；文件链接允许跨域下载。
- 链接解析：仅从 `<a href="...">` 提取，支持相对与协议相对链接，统一归一化。
- robots.txt：读取 `User-agent: *` 段的 Allow/Disallow 前缀规则并应用。

## 性能与礼貌建议
- 并发与节流是双刃剑：请根据目标站点能力设置 `并发数` 与 `请求间隔ms`。
- 建议先小范围验证（较小 `最大页面数`），确认行为与站点规则相符后再扩大范围。

## 故障排查
- 首次构建较慢：可能在拉取/构建 cURL/CPR 依赖。
- macOS SSL 报错：CPR 默认使用系统 Secure Transport；如仍有问题，可安装 OpenSSL 并配置 CMake。
- 未下载到文件：检查页面是否存在直链到目标后缀的链接；有些站点需进入详情页才能出现直链。

## 开发
- 默认参数在 `src/main.cpp` 中设定，可按需修改。
- 关键实现：`src/crawler.hpp` / `src/crawler.cpp`（多线程队列、robots、链接解析、下载与清单）。

---

本项目仅供学习与研究使用。使用者须自行确保其抓取与下载行为符合目标网站条款与适用法律法规。
