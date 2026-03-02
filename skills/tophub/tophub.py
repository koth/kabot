#!/usr/bin/env python3
import argparse
import json
import os
import re
from typing import List, Dict

from playwright.sync_api import sync_playwright
from bs4 import BeautifulSoup


def fetch_html(url: str) -> str:
    with sync_playwright() as p:
        browser = p.chromium.launch(headless=True)
        page = browser.new_page()
        page.goto(url)
        html_content = page.content()
        browser.close()
    return html_content


def parse_boards(html: str) -> List[Dict]:
    soup = BeautifulSoup(html, "html.parser")
    boards = []
    for card in soup.select('.cc-cd[id^="node-"]'):
        site = card.select_one(".cc-cd-lb span")
        board = card.select_one(".cc-cd-sb-st")
        if not site or not board:
            continue
        board_name = f"{site.get_text(strip=True)} / {board.get_text(strip=True)}"
        items = []
        for a in card.select(".cc-cd-cb-l a"):
            title_el = a.select_one(".t")
            if not title_el:
                continue
            items.append({
                "title": title_el.get_text(strip=True),
                "url": a.get("href", "")
            })
        boards.append({
            "board": board_name,
            "items": items
        })
    return boards


def load_json(path: str) -> List[Dict]:
    with open(path, "r", encoding="utf-8") as f:
        return json.load(f)


def save_json(path: str, data: List[Dict]) -> None:
    with open(path, "w", encoding="utf-8") as f:
        json.dump(data, f, ensure_ascii=False, indent=2)


def cmd_fetch(args: argparse.Namespace) -> None:
    html = fetch_html(args.url)
    data = parse_boards(html)
    save_json(args.output, data)


def cmd_list(args: argparse.Namespace) -> None:
    data = load_json(args.input)
    for item in data:
        print(item.get("board", ""))


def cmd_show(args: argparse.Namespace) -> None:
    data = load_json(args.input)
    pattern = re.compile(args.board)
    for item in data:
        board_name = item.get("board", "")
        if pattern.search(board_name):
            print(json.dumps(item, ensure_ascii=False, indent=2))


def cmd_dump(args: argparse.Namespace) -> None:
    data = load_json(args.input)
    exclude = {"淘宝", "京东", "天猫", "电影榜","Behance","App Store","少数派","先知社区","即刻圈子","懂球帝","NBA论坛热帖","新浪体育新闻","音乐榜","Apple Music","历史上的今天","今日热卖"}
    lines: List[str] = []
    for item in data:
        board_name = item.get("board", "")
        if any(keyword in board_name for keyword in exclude):
            continue
        items = item.get("items", [])
        if not items:
            continue
        if board_name:
            lines.append(board_name)
        for idx, entry in enumerate(items, 1):
            title = entry.get("title", "")
            if title:
                lines.append(f"{idx}. {title}")
        if lines and lines[-1] != "":
            lines.append("")
    content = "\n".join(lines).rstrip() + "\n"
    with open(args.output, "w", encoding="utf-8") as f:
        f.write(content)


def main() -> None:
    base_dir = os.path.dirname(os.path.abspath(__file__))
    default_json = os.path.join(base_dir, "tophub.json")

    parser = argparse.ArgumentParser(description="Tophub 抓取与解析工具")
    sub = parser.add_subparsers(dest="cmd", required=True)

    p_fetch = sub.add_parser("fetch", help="抓取 tophub.today 并生成 tophub.json")
    p_fetch.add_argument("--url", default="https://tophub.today/", help="抓取 URL")
    p_fetch.add_argument("-o", "--output", default=default_json, help="输出 JSON 路径")
    p_fetch.set_defaults(func=cmd_fetch)

    p_list = sub.add_parser("list", help="列出榜单名")
    p_list.add_argument("-i", "--input", default=default_json, help="tophub.json 路径")
    p_list.set_defaults(func=cmd_list)

    p_show = sub.add_parser("show", help="获取指定榜单条目")
    p_show.add_argument("board", help="榜单名正则")
    p_show.add_argument("-i", "--input", default=default_json, help="tophub.json 路径")
    p_show.set_defaults(func=cmd_show)

    p_dump = sub.add_parser("dump", help="导出热门榜单与条目到文本")
    p_dump.add_argument("-i", "--input", default=default_json, help="tophub.json 路径")
    p_dump.add_argument("-o", "--output", default=os.path.join(base_dir, "hotnews.txt"),
                        help="输出文本路径")
    p_dump.set_defaults(func=cmd_dump)

    args = parser.parse_args()
    args.func(args)


if __name__ == "__main__":
    main()
