#!/usr/bin/env python3
import sys
import argparse
import time
from playwright.sync_api import sync_playwright
from playwright.sync_api import Error as PlaywrightError
import html2text


def truncate(content: str, max_bytes: int) -> str:
    encoded = content.encode("utf-8")
    if len(encoded) <= max_bytes:
        return content
    truncated = encoded[:max_bytes].decode("utf-8", errors="ignore")
    return truncated + "\n...(truncated)..."


def wait_for_stable_dom(page, attempts: int = 8, interval_ms: int = 500, stable_rounds: int = 2) -> str:
    previous_html = None
    stable_count = 0
    last_error = None

    for _ in range(attempts):
        try:
            current_html = page.evaluate("() => document.documentElement.outerHTML")
            if current_html == previous_html and current_html:
                stable_count += 1
            else:
                stable_count = 0
            previous_html = current_html
            if stable_count >= stable_rounds:
                return current_html
        except PlaywrightError as exc:
            last_error = exc
            stable_count = 0

        page.wait_for_timeout(interval_ms)

    if previous_html:
        return previous_html
    if last_error is not None:
        raise last_error
    raise RuntimeError("无法获取稳定的 DOM")


def fetch_page_content(url: str, text_only: bool) -> str:
    with sync_playwright() as p:
        browser = p.chromium.launch(headless=True)
        try:
            page = browser.new_page()
            page.goto(url, wait_until="domcontentloaded", timeout=60000)

            last_error = None
            html_content = ""
            for _ in range(3):
                try:
                    page.wait_for_load_state("domcontentloaded", timeout=15000)
                except PlaywrightError:
                    pass

                try:
                    page.wait_for_load_state("networkidle", timeout=5000)
                except PlaywrightError:
                    pass

                try:
                    html_content = wait_for_stable_dom(page)
                    break
                except PlaywrightError as exc:
                    last_error = exc
                    time.sleep(1)

            if not html_content:
                if last_error is not None:
                    raise last_error
                raise RuntimeError("无法获取页面内容")

            h = html2text.HTML2Text()
            h.ignore_links = text_only
            h.ignore_images = False
            h.ignore_tables = False
            h.ignore_emphasis = False
            h.body_width = 0
            markdown_content = h.handle(html_content)

            return markdown_content
        finally:
            browser.close()


def main():
    parser = argparse.ArgumentParser(description="抓取网页内容并转换为 Markdown 格式")
    parser.add_argument("url", help="要抓取的网页 URL")
    parser.add_argument("-o", "--output", help="输出文件路径（可选）")
    parser.add_argument("--max-bytes", type=int, default=8000, help="最大输出字节数")
    parser.add_argument("--text-only", action="store_true", help="输出纯文本模式")
    args = parser.parse_args()

    print(f"正在抓取: {args.url}", file=sys.stderr)

    try:
        content = fetch_page_content(args.url, args.text_only)
        content = truncate(content, max(1024, min(args.max_bytes, 20000)))

        if args.output:
            with open(args.output, "w", encoding="utf-8") as f:
                f.write(content)
            print(f"内容已保存到: {args.output}", file=sys.stderr)
        else:
            print(content)

    except Exception as e:
        print(f"错误: {e}", file=sys.stderr)
        sys.exit(1)


if __name__ == "__main__":
    main()