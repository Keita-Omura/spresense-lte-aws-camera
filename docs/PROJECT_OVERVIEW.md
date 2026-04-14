# Spresense LTE Camera to AWS Pipeline - Project Overview

**Version**: 4.0  
**Last Updated**: 2026-04-15  

---

## 目次

1. [プロジェクト概要](#プロジェクト概要)
2. [システムアーキテクチャ](#システムアーキテクチャ)
3. [AWS構成詳細](#aws構成詳細)
4. [Spresense構成](#spresense構成)
5. [開発の変遷・ハマりポイント](#開発の変遷ハマりポイント)

---

## プロジェクト概要

### 何ができるか

Sony Spresenseのボタンを1回押すだけで、以下が自動で実行されます：

1. カメラで写真を撮影
2. SDカードに保存（連番・永久保存）
3. LTE経由でAWSにアップロード
4. 撮影した画像がメールで届く

Wi-Fi不要・モバイルバッテリーで完全自律動作します。

### なぜ作ったか

- LTE → AWS パイプラインの実証
- エッジAI分野へのキャリア転換のためのポートフォリオ
- 将来的なエッジAIシステムとの統合基盤

### パフォーマンス

```
起動時間        : 約30秒（LTE接続含む）
撮影時間        : 約1秒
アップロード時間 : 約5-15秒
メール受信まで  : ボタン押下から約10-20秒

画像サイズ      : 約60-100 KB/枚（QUADVGA JPEG）
月間データ使用量: 約10 MB（100枚/月）

AWSコスト（100枚/月）: $0.00（無料枠内）
```

---

## システムアーキテクチャ

```
┌─────────────────────────────────────────┐
│              Spresense デバイス           │
│                                          │
│  [ボタン] → [カメラ撮影] → [SD保存]      │
│                  ↓                       │
│           [LTE拡張ボード]                 │
└──────────────────┬──────────────────────┘
                   │ HTTPS POST
                   │ Content-Type: image/jpeg
                   ↓
┌─────────────────────────────────────────┐
│                 AWS Cloud                │
│                                         │
│  [API Gateway]                          │
│  POST /image-upload                     │
│       ↓                                 │
│  [Lambda #1]                            │
│  spresense-image-uploader               │
│  画像を受信してS3に保存                   │
│       ↓                                 │
│  [S3 Bucket]                            │
│  images/*.jpg として保存                  │
│       ↓ ObjectCreated イベント           │
│  [Lambda #2]                            │
│  spresense-image-email-sender           │
│  S3から画像を取得してメール送信            │
│       ↓                                 │
│  [Amazon SES]                           │
└──────────────────┬──────────────────────┘
                   ↓
            📧 メール受信（画像添付）
```

### データフロー詳細

```
[1] ボタン押下
[2] QUADVGA JPEG撮影（約60-100 KB）
[3] SDカードに2ファイル保存
    - /images/PICT0001.JPG（連番・永久保存）
    - /images/temp_upload.jpg（アップロード用・上書き）
[4] LTE接続確認
[5] HTTPS POST → API Gateway
    Headers:
      Content-Type: image/jpeg
      Content-Length: [画像サイズ]
      Connection: close
    Body: [JPEGバイナリデータ]
[6] Lambda #1 が画像を受信
    - isBase64Encoded を確認してデコード
    - タイムスタンプ + UUID でファイル名生成
    - S3に put_object
[7] S3 ObjectCreated イベントが Lambda #2 を起動
[8] Lambda #2 が実行
    - S3から画像を get_object
    - MIMEMultipart でメール作成
    - SES で送信
[9] メール受信 ✅
```

---

## AWS構成詳細

### 全体設定

```
リージョン: ap-northeast-1（東京）
```

---

### API Gateway

```
名前          : SpresenseImageAPI
タイプ        : HTTP API（REST APIではない）
エンドポイント : Regional

ルート:
  メソッド : ANY
  パス     : /image-upload

統合:
  タイプ   : Lambda関数
  関数名   : spresense-image-uploader

自動デプロイ  : 有効
ステージ      : $default

エンドポイントURL:
  https://your-api-id.execute-api.ap-northeast-1.amazonaws.com/image-upload
```

**重要な設定ポイント:**

- HTTP APIを使用しているためバイナリデータを自動処理（REST APIの場合はバイナリメディアタイプの設定が別途必要）
- 自動デプロイが有効なため、ルート変更は即時反映される

---

### Lambda #1: spresense-image-uploader

**役割**: API Gatewayから画像を受信してS3に保存

```
ランタイム    : Python 3.12
アーキテクチャ: x86_64
メモリ        : 128 MB
タイムアウト  : 30秒（デフォルト）
```

**環境変数:**

```
キー         : BUCKET_NAME
値           : your-bucket-name
```

**IAM権限（実行ロールにアタッチ）:**

```
- AWSLambdaBasicExecutionRole（自動付与）
- AmazonS3FullAccess（手動追加）
```

**リソースベースのポリシー（重要）:**

API GatewayがこのLambdaを呼び出すための権限。
ルート名を変更した場合は必ずこのARNも更新が必要。

```json
{
  "ArnLike": {
    "AWS:SourceArn": "arn:aws:execute-api:ap-northeast-1:your-account-id:your-api-id/*/*/image-upload"
  }
}
```

**⚠️ ハマりポイント:**
API Gatewayのルート名を変更した際、リソースベースのポリシーのARNを更新しないと
HTTP 500エラーが発生する。ルート名変更時は必ずセットで更新すること。

**コードの動作:**

```python
# API Gatewayはバイナリデータをbase64エンコードして渡す
if event.get('isBase64Encoded', False):
    image_data = base64.b64decode(event['body'])

# タイムスタンプ + UUIDでユニークなファイル名を生成
key = f"images/{timestamp}_{uuid4()}.jpg"

# S3に保存
s3.put_object(Bucket=BUCKET_NAME, Key=key, Body=image_data)
```

---

### Lambda #2: spresense-image-email-sender

**役割**: S3に画像が保存されたらメールに添付して送信

```
ランタイム    : Python 3.12
アーキテクチャ: x86_64
メモリ        : 128 MB
タイムアウト  : 30秒（デフォルト）
```

**トリガー（S3イベント）:**

```
バケット      : your-bucket-name
イベントタイプ : s3:ObjectCreated:*
サフィックス  : .jpg
```

サフィックスを`.jpg`に限定することで、.jpgファイル保存時のみ発火する。

**IAM権限（実行ロールにアタッチ）:**

```
- AWSLambdaBasicExecutionRole（自動付与）
- AmazonS3ReadOnlyAccess（手動追加）← 読み取りのみでOK
- AmazonSESFullAccess（手動追加）
```

**最小権限の原則:**
Lambda #2はS3から読み取るだけなので`AmazonS3ReadOnlyAccess`で十分。
Lambda #1はS3に書き込むため`AmazonS3FullAccess`が必要。

**コードの動作:**

```python
# S3イベントからバケット・キーを取得
bucket = event['Records'][0]['s3']['bucket']['name']
key = event['Records'][0]['s3']['object']['key']

# S3から画像を取得
image_data = s3.get_object(Bucket=bucket, Key=key)['Body'].read()

# MIMEマルチパートでメール作成・添付・SES送信
msg = MIMEMultipart('mixed')
ses.send_raw_email(...)
```

---

### S3 バケット

```
バケット名  : your-bucket-name
リージョン  : ap-northeast-1（東京）
パブリックアクセス: ブロック
バージョニング   : 無効
暗号化           : SSE-S3（デフォルト）
```

**フォルダ構成（自動生成）:**

```
your-bucket-name/
└── images/
    ├── 2026-04-14_22-45-30_xxxxxxxx-xxxx-xxxx-xxxx-xxxxxxxxxxxx.jpg
    ├── 2026-04-14_22-50-10_xxxxxxxx-xxxx-xxxx-xxxx-xxxxxxxxxxxx.jpg
    └── ...
```

ファイル名はタイムスタンプ（UTC）+ UUIDで生成されるため重複しない。

---

### Amazon SES

```
リージョン  : ap-northeast-1（東京）
モード      : サンドボックス

検証済みアドレス:
  送信元 : your-email@example.com
  宛先   : your-email@example.com
```

**サンドボックスモードの制限:**

```
- 検証済みアドレスとの間でのみ送受信可能
- 最大送信数: 200通/日
- 送信レート: 1通/秒
```

本番運用で不特定多数へ送信する場合はサンドボックス解除が必要。

---

## Spresense構成

### ハードウェア

```
メインボード  : Sony Spresense（CXD5602 ARM Cortex-M4F）
カメラボード  : Spresense Camera Board（5MP CMOS）
LTE拡張ボード : Spresense LTE Extension Board
SDカード      : FAT32フォーマット、32GB以下
SIMカード     : データSIM（APN設定が必要）
ボタン        : タクトスイッチ（D01ピンとGND間）
電源          : USBまたはモバイルバッテリー（5V/1A以上）
```

### Arduino IDE設定

```
ボード         : Spresense
Memory         : 1536 KB  ← 必須！（最小動作は1024 KB）
Upload Speed   : 115200
```

**メモリ設定の詳細:**

| 設定値 | 動作結果 |
|--------|---------|
| 896 KB | LTEモデム初期化失敗（`lte_set_report_restart error: -12`） |
| 1024 KB | 最小動作（推奨しない） |
| 1536 KB | 推奨設定（余裕あり） |

カメラ・LTEモデム・TLS通信が同時に動作するため1536 KBを推奨。

### SDカード構成

```
/
├── certs/
│   └── AmazonRootCA1.pem  （1188 bytes）← API GatewayへのTLS認証用
└── images/
    ├── PICT0001.JPG  ← 撮影画像（連番・永久保存）
    ├── PICT0002.JPG
    └── temp_upload.jpg  ← アップロード用一時ファイル（上書き）
```

**AmazonRootCA1.pemの取得:**

```
URL: https://www.amazontrust.com/repository/AmazonRootCA1.pem
有効期限: 2038年（長期間有効）
```

### 主要な設定値

```cpp
// LTE（使用するSIMのAPN設定に変更）
#define APP_LTE_APN      "your.apn.here"
#define APP_LTE_USER_NAME "your_username"
#define APP_LTE_PASSWORD  "your_password"

// AWS API Gateway
#define API_HOST "your-api-id.execute-api.ap-northeast-1.amazonaws.com"
#define API_PORT 443
#define API_PATH "/image-upload"

// ボタン
#define BUTTON_PIN 1  // D01ピン
```

---

## 開発の変遷・ハマりポイント

### v1 → v2: モバイルバッテリー問題

**問題:** PC接続では動作するがモバイルバッテリーでは動作しない

**原因:** `Serial.print()`がシリアル未接続時にバッファが詰まりブロッキング

**解決策:** シリアル接続を自動検出して条件付き出力

```cpp
// 3秒待機してシリアル接続を確認
bool serialEnabled = false;
unsigned long t = millis();
while (!Serial && (millis() - t < 3000)) { ; }
serialEnabled = (bool)Serial;

// マクロで条件付き出力
#define DEBUG_PRINTLN(x) if (serialEnabled) Serial.println(x)
```

### v2 → v3: LED視覚フィードバック

**追加:** アップロード中に全LED消灯

```
待機中   : 🔴🔴🔴🔴
送信中   : ⚫⚫⚫⚫  ← 一目で「送信中」と分かる
完了後   : 🔴🔴🔴🔴
```

### v3 → v4: HTTPステータスコード確認バグ修正

**問題:** HTTP 500エラーでも「Upload successful!」と表示されてしまう

**原因:** `uploadImage()`関数がHTTPレスポンスが来たら無条件に`true`を返していた

**解決策:** レスポンスの1行目からステータスコードを取得して判定

```cpp
int statusCode = readResponse();  // 200 or 500 などを返す
if (statusCode == 200) {
    return true;
} else {
    return false;
}
```

### 関数名変更時のハマりポイント

Lambda関数名を変更した際に発生しやすい問題：

```
症状: HTTP 500 Internal Server Error
原因: LambdaのリソースベースポリシーのARNが古いルート名のまま

対処: Lambda → 設定 → アクセス権限
      → リソースベースのポリシーステートメント
      → ARN末尾のルート名を新しい名前に更新
```

**API Gatewayのルート名を変更する際のチェックリスト:**

```
□ API Gatewayのルート名を変更
□ LambdaのリソースベースポリシーのARNを更新
□ .inoのAPI_PATHを更新
□ Spresenseに再書き込み
□ 動作確認
```

### Presigned URL方式を断念した経緯

当初はS3 Presigned URLを使った2段階アップロードを試みたが断念。

```
断念理由:
- Spresenseのメモリ制約
- 2回のHTTP通信が必要で複雑
- Lambda関数名のみ当時の名残が残った（spresense_presigned_URL）

現在の実装:
- API Gateway → Lambda → S3への直接アップロード
- 1回のHTTP通信でシンプル
```

---

## ファイル構成

```
spresense-lte-aws-camera/
├── README.md
├── LICENSE
├── .gitignore
├── spresense_camera_aws_upload_v4.ino  ← Spresenseコード
├── lambda_uploader.py                  ← Lambda #1
├── lambda_email_sender.py              ← Lambda #2
└── docs/
    ├── SETUP.md
    ├── TROUBLESHOOTING.md
    ├── LED_GUIDE.md
    └── DEV_HISTORY.md
```

---

*このドキュメントはGitHub公開前のプロジェクト全体構成のまとめです。*
*各詳細は docs/ 配下のドキュメントを参照してください。*
