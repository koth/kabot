---
name: tophub
description: Tophub 榜单抓取与解析工具
---

# Tophub

使用 `tophub.py` 抓取 tophub.today，并解析榜单为 JSON。

## 安装依赖

```bash
pip install -r requirements.txt
playwright install chromium
```

## 使用方式

使用本工具获取榜单数据，也可以指定榜单名查看

### 更新榜单数据

```bash
python tophub.py fetch
```


### 列出榜单名

```bash
python tophub.py list
```

### 获取指定榜单条目

支持正则匹配：

```bash
python tophub.py show "知乎|微博"
```

## 参数说明

| 命令 | 参数 | 说明 |
|------|------|------|
| show | board | 榜单名正则 |

## 注意事项

- 需要安装 Chromium 浏览器：`playwright install chromium`
- 请遵守目标网站 robots.txt 与使用条款
