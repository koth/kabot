---
name: web
description: 网页抓取和内容获取工具
---

# Web

使用 `fetch_web.py` 脚本抓取网页内容并转换为 Markdown 格式。

## 安装依赖

```bash
pip install -r requirements.txt
playwright install chromium
```

## 使用方式

脚本会自动将网页 HTML 转换为 Markdown 格式输出。

### 基本用法

输出到控制台：
```bash
python fetch_web.py https://example.com
```

保存到文件：
```bash
python fetch_web.py https://example.com -o output.md
```

## 功能特性

- 使用 Playwright 渲染 JavaScript 动态内容
- 自动将 HTML 转换为 Markdown 格式
- 保留链接、图片和表格
- 支持输出到文件或控制台

## 参数说明

| 参数 | 说明 | 必填 |
|------|------|------|
| `url` | 要抓取的网页 URL | 是 |
| `-o, --output` | 输出文件路径 | 否 |

## 示例

抓取博客文章：
```bash
python fetch_web.py https://blog.example.com/article -o article.md
```

查看网页内容：
```bash
python fetch_web.py https://example.com | less
```

## 注意事项

- 需要安装 Chromium 浏览器：`playwright install chromium`
- 确保目标网站允许爬虫访问
- 遵守网站的 robots.txt 和 terms of service
