---
name: tts
description: Text-to-speech synthesis using Edge TTS.
metadata: {"kabot":{"always":true}}
---

# TTS

Use the `tts` tool to synthesize speech and save it to an audio file, with optional auto-play.

## When to use

- 用户要求"合成语音 / 朗读 / 生成音频 / 语音播报"。
- 需要返回音频文件路径以供播放或发送。
- 需要自动播放生成的语音（设置 auto_play=true）。

## Parameters

- `text` (required): 要合成的文本。
- `voice`: 语音角色，例如 `zh-CN-XiaoyiNeural`。
- `lang`: 语言，例如 `zh-CN`。
- `output_format`: 输出格式，例如 `audio-24khz-48kbitrate-mono-mp3`。
- `rate` / `pitch` / `volume`: 语速/音调/音量，默认 `default`。
- `save_subtitles`: 是否保存字幕，默认 `false`。
- `audio_path`: 输出路径（可选，默认自动生成）。
- `auto_play`: 是否自动播放，默认 `false`。如为 `true`，生成后将使用 `ffplay` 播放。

## Examples

合成语音并保存：
```
tts(text="你好，欢迎使用语音合成", voice="zh-CN-XiaoyiNeural")
```

合成/path/to/article.txt的文件内容：
```
tts(file="/path/to/article.txt")
```

合成并自动播放：
```
tts(text="今天天气很好", auto_play=true)
```

指定输出路径：
```
tts(text="播报一下", audio_path="weather.opus")
```

调速与音调：
```
tts(text="播报一下", rate="+10%", pitch="+5%")
```
