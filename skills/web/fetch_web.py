#!/usr/bin/env python3
"""
网页内容抓取工具
使用 Playwright 抓取网页内容并转换为 Markdown 格式
"""
import sys
import argparse
from playwright.sync_api import sync_playwright
import html2text


def fetch_page_content(url: str) -> str:
    """使用 Playwright 抓取网页内容并转换为 Markdown"""
    with sync_playwright() as p:
        browser = p.chromium.launch(headless=True)
        page = browser.new_page()
        page.goto(url)
        html_content = page.content()
        browser.close()
        
        # 将 HTML 转换为 Markdown
        h = html2text.HTML2Text()
        h.ignore_links = False
        h.ignore_images = False
        h.ignore_tables = False
        h.body_width = 0  # 不限制行宽
        markdown_content = h.handle(html_content)
        
        return markdown_content


def main():
    parser = argparse.ArgumentParser(description="抓取网页内容并转换为 Markdown 格式")
    parser.add_argument("url", help="要抓取的网页 URL")
    parser.add_argument("-o", "--output", help="输出文件路径（可选）")
    args = parser.parse_args()

    print(f"正在抓取: {args.url}", file=sys.stderr)
    
    try:
        content = fetch_page_content(args.url)
        
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