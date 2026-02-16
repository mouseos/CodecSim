# CodecSim

リアルタイム オーディオコーデック シミュレーター (VST3 プラグイン / スタンドアロン)

---

## 概要

CodecSim は、さまざまなオーディオコーデックの音質をリアルタイムでシミュレーションできる VST3 プラグイン / スタンドアロンアプリケーションです。バウンスすることなく、MP3、AAC、Opus をはじめとする 30 種類以上のコーデックを通した音を即座に確認できます。

内部では ffmpeg をパイプ経由で使用し、エンコード・デコードを行っています。

---

## 主な特徴

- 30 種類以上のオーディオコーデックをリアルタイムでシミュレーション
- ビットレート、サンプルレート、チャンネルモード (ステレオ / モノラル) を自由に設定
- コーデック固有のオプションにも対応
- DAW 上で VST3 エフェクトとして使用可能
- スタンドアロンアプリとしても動作

---

## 対応コーデック一覧

### Lossy (非可逆圧縮)

MP3, AAC, HE-AAC, Opus, Vorbis, MP2, WMA v1/v2, AC-3, E-AC-3, DTS, SBC, Speex, Nellymoser

### Lossless (可逆圧縮) / ADPCM

FLAC, WavPack, ADPCM (IMA / MS / Yamaha), aptX, aptX HD

### Telephony (電話音声)

AMR-NB, AMR-WB, GSM 06.10, G.711 (A-law / mu-law), G.722, G.723.1, G.726, iLBC

### Retro

RealAudio 1.0, DFPWM

---

## インストール方法 (エンドユーザー向け)

### VST3 プラグイン

1. ダウンロードした `CodecSim.vst3` フォルダを以下のディレクトリにコピーしてください。

   ```
   C:\Program Files\Common Files\VST3\
   ```

2. DAW を起動 (または再スキャン) すれば、CodecSim が使用可能になります。

ffmpeg は VST3 バンドル内に同梱されています (`CodecSim.vst3/Contents/x86_64-win/ffmpeg.exe`)。別途インストールや PATH の設定は不要です。

### スタンドアロンアプリ

`CodecSim.exe` と同じディレクトリに ffmpeg.exe が同梱されています。そのまま実行してください。

### ffmpeg が見つからない場合 (通常は不要)

万が一、同梱の ffmpeg が見つからない場合は、システムの PATH から ffmpeg を検索するフォールバック動作になります。その場合は以下の手順で ffmpeg を導入してください。

1. [FFmpeg Builds](https://github.com/BtbN/FFmpeg-Builds/releases) から Windows 向け GPL または LGPL ビルドをダウンロード
2. 展開し、`bin` フォルダをシステムの PATH 環境変数に追加

---

## 使い方

1. DAW で任意のトラックに VST3 エフェクトとして CodecSim を読み込む
2. コーデックを選択する
3. ビットレート、サンプルレート、チャンネルモードを設定する
4. 「Apply」ボタンをクリックしてコーデックシミュレーションを開始する
5. オーディオがリアルタイムでエンコード → デコードのパイプラインを通過する
6. 設定を変更した場合は再度「Apply」をクリックする

---

## ビルド方法 (開発者向け)

### 必要な環境

- CMake
- Visual Studio 2022
- iPlug2 フレームワーク

### ffmpeg のセットアップ

プロジェクトルートに `ffmpeg/` ディレクトリを作成し、ffmpeg のファイルを配置してください。

```
codecsim/
├─ ffmpeg/
│  ├─ bin/
│  │  ├─ ffmpeg.exe
│  │  ├─ avcodec-62.dll
│  │  └─ ... (その他の DLL)
│  ├─ include/   (オプション、開発用)
│  └─ LICENSE.txt
├─ CodecSim/
│  ├─ CodecSim.cpp
│  └─ ...
└─ CMakeLists.txt
```

### ビルドコマンド

通常版 (フルバージョン):

```bash
cmake -B build
cmake --build build --target CodecSim-vst3 --config Release
```

体験版 (MP3 のみ):

```bash
cmake -DCODECSIM_TRIAL=ON -B build-trial
cmake --build build-trial --target CodecSim-vst3 --config Release
```

### 出力先

ビルド成果物は `build/out/CodecSim.vst3/` に生成されます (VST3 フォルダへ自動デプロイ)。

---

## 体験版について

体験版では MP3 コーデックのみ使用可能です。その他のコーデックをご利用いただくには、製品版をご購入ください。

製品版の購入: [https://mousesoft.booth.pm/](https://mousesoft.booth.pm/)

---

## ライセンス

Copyright 2025 MouseSoft

ffmpeg は LGPL ライセンスに基づいて使用しており、別途同梱しています。
