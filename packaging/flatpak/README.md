# Flatpak

Hazkey を Flatpak の Fcitx5 の拡張アドオンとしてパッケージします。

プラグイン (fcitx5-hazkey)、変換エンジン (hazkey-server)、設定GUI (hazkey-settings) の
3コンポーネントすべてが 1 つの Fcitx5 拡張に含まれます。

## インストール

### 前提条件

- `flatpak`、`flatpak-builder`、`appstreamcli` がインストールされていること
- Flathub リポジトリが追加されていること

```sh
flatpak remote-add --user --if-not-exists flathub https://flathub.org/repo/flathub.flatpakrepo
```

### 実行

`install.sh` がランタイムの取得、ビルド、ローカルリポジトリの作成、インストールを
すべて自動で行います。

```sh
packaging/flatpak/install.sh
```

ローカルにチェックアウトしたソースからビルドする場合は `--local` を指定します。

```sh
packaging/flatpak/install.sh --local
```

インストール後、Fcitx5 を再起動すると Hazkey が利用可能になります。

## アンインストール

`uninstall.sh` で Flatpak 拡張、ローカルリモート、ビルド成果物、
デスクトップエントリをすべて削除します。

```sh
packaging/flatpak/uninstall.sh
```

## 手動でのビルド・インストール

スクリプトを使わずに手動で行う場合：

```sh
# ランタイムのインストール
flatpak install --user flathub org.fcitx.Fcitx5//stable
flatpak install --user flathub org.kde.Sdk//6.9

# ビルドし、結果を packaging/flatpak/hazkey-repo/ にエクスポート
flatpak-builder --user --force-clean --repo=packaging/flatpak/hazkey-repo packaging/flatpak/build-dir packaging/flatpak/org.fcitx.Fcitx5.Addon.Hazkey.yml

# packaging/flatpak/hazkey-repo/ を "hazkey-local" という名前で Flatpak リモートとして登録
flatpak remote-add --user --no-gpg-verify hazkey-local packaging/flatpak/hazkey-repo

# 登録したリモートからインストール
flatpak install --user hazkey-local org.fcitx.Fcitx5.Addon.Hazkey
```

## メンテナー向けノート

### Flathub への公開について

現時点では Flathub への公開はできません。以下の課題を解決する必要があります。

#### 1. ビルド時のネットワークアクセス (`--share=network`)

Flathub ではビルド中のネットワークアクセスが禁止されています
([要件](https://docs.flathub.org/docs/for-app-authors/requirements),
[flathub/flathub#3392](https://github.com/flathub/flathub/issues/3392))。
現在のマニフェストでは SwiftPM の依存関係取得のために `--share=network` を使用しています。

Flathub に公開するには、SwiftPM の依存関係を事前にダウンロードし、
オフラインでビルドできるようにする必要があります。
解決策:
[flatpak-spm-generator](https://github.com/flatpak/flatpak-builder-tools/tree/master/spm) を使うことで SwiftPM の依存関係を
flatpak-builder の sources として生成できます。ただし SwiftPM の内部フォーマット
(`workspace-state.json`) に依存しているため、Swift バージョン更新時に動作確認が必要です。

- [ ] ネットワークアクセスの解消
  - flatpak-spm-generator の検証
  - `--share=network` の除去
  - (任意) CI での差分チェック

#### 2. KDE SDK のバージョン固定

Swift の C++ interop が GCC 15 のヘッダーと互換性がないため
([swiftlang/swift#81774](https://github.com/swiftlang/swift/issues/81774))、
KDE SDK 6.9 (freedesktop SDK 24.08, GCC 14) に固定しています。
Flathub は通常最新の SDK を要求するため、Swift 側の修正が入るまでブロッカーになります。

> [!NOTE]
> 修正 PR ([#87620](https://github.com/swiftlang/swift/pull/87620)) は 2026-03-03 に main にマージ済みですが、
> リリース版 (6.2.4 時点) にはまだ含まれていません。cherry-pick を待つ必要があります。

#### 3. 拡張のデスクトップエントリ・アイコンのエクスポート

Flatpak は拡張のデスクトップエントリとアイコンをホスト側にエクスポートしません
(アイコン: [flatpak/flatpak#4006](https://github.com/flatpak/flatpak/issues/4006),
デスクトップエントリ: [flatpak/flatpak#5888](https://github.com/flatpak/flatpak/issues/5888))。
ローカルビルドでは `install.sh` が手動コピーで対処しています。

hazkey-settings はランチャーから起動できず、アイコンもトレイに表示されません。
Fcitx5 の設定UIからは起動できるため、デスクトップエントリはブロッカーではなく利便性の問題です。

解決策案:

- Flatpak 本体の upstream 対応を待つ (上記 issue、いずれも進展なし)
- hazkey-settings を独立した Flatpak アプリとしてパッケージする
- Fcitx5 本体のマニフェストにエントリ・アイコンを含める
  (Mozc アイコンと同様。[fcitx/flatpak-fcitx5](https://github.com/fcitx/flatpak-fcitx5) への変更が必要)

### その他の TODO

- [ ] Flatpak リポジトリをセルフホストする
  - OSTree リポジトリをビルドし、`flatpak remote-add` で追加できる公開リポジトリを提供する
  - GitHub Pages + GitHub Actions など
  - 上記問題 3 の解決が必要
- [ ] [fcitx/flatpak-fcitx5](https://github.com/fcitx/flatpak-fcitx5) にこのマニフェストを追加する
- [ ] 上記問題 1 ~ 3 の課題がすべて解決したら Flathub で公開する
  - <https://docs.flathub.org/docs/for-app-authors/requirements>

### マニフェストの tag / commit ハッシュ

`org.fcitx.Fcitx5.Addon.Hazkey.yml` 内の `tag:` と `commit:` フィールドは、
ビルド対象の fcitx5-hazkey のバージョンを指定しています。
リリース時には `tag:` をリリースタグに、`commit:` をそのタグのフルコミットハッシュに
更新してください (`git rev-list -n1 <tag>` で取得できます)。
