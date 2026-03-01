#!/usr/bin/env python3
import argparse
import json
import re
from typing import List, Dict

from bs4 import BeautifulSoup


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


def main() -> None:
    parser = argparse.ArgumentParser(description="解析 output.html 榜单标题与链接")
    parser.add_argument("input", help="HTML 文件路径")
    parser.add_argument("-o", "--output", help="输出 JSON 文件路径")
    parser.add_argument("--filter", help="按榜单名正则过滤(可选)")
    args = parser.parse_args()

    with open(args.input, "r", encoding="utf-8") as f:
        html = f.read()

    data = parse_boards(html)
    if args.filter:
        pattern = re.compile(args.filter)
        data = [b for b in data if pattern.search(b["board"])]

    payload = json.dumps(data, ensure_ascii=False, indent=2)
    if args.output:
        with open(args.output, "w", encoding="utf-8") as f:
            f.write(payload)
    else:
        print(payload)


if __name__ == "__main__":
    main()
