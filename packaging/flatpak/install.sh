#!/usr/bin/env bash
set -e

LOCAL_BUILD=false
if [ "$1" = "--local" ]; then
    LOCAL_BUILD=true
fi

SCRIPT_DIR="$(cd "$(dirname "$0")" && pwd)"
MANIFEST="$SCRIPT_DIR/org.fcitx.Fcitx5.Addon.Hazkey.yml"
BUILD_DIR="$SCRIPT_DIR/build-dir"
FLATPAK_REPO="$SCRIPT_DIR/hazkey-repo"
REMOTE_NAME="hazkey-local"
APP_ID="org.fcitx.Fcitx5.Addon.Hazkey"
DESKTOP_FILE="org.fcitx.Fcitx5.Addon.Hazkey.hazkey-settings.desktop"

# D-Bus 名のオーナーと flatpak ps から fcitx5 の実行状態を判定する
# "flatpak" | "native" | "stopped"
check_fcitx5_runtime() {
    if flatpak ps --columns=application 2>/dev/null | grep -q org.fcitx.Fcitx5; then
        echo "flatpak"
    elif dbus-send --session --dest=org.freedesktop.DBus --type=method_call --print-reply \
            /org/freedesktop/DBus org.freedesktop.DBus.NameHasOwner string:"org.fcitx.Fcitx5" \
            2>/dev/null | grep -q "true"; then
        echo "native"
    else
        echo "stopped"
    fi
}

missing_cmds=()
for cmd in flatpak flatpak-builder appstreamcli; do
    if ! command -v "$cmd" >/dev/null 2>&1; then
        missing_cmds+=("$cmd")
    fi
done
if [ ${#missing_cmds[@]} -gt 0 ]; then
    echo "エラー: 以下のコマンドがインストールされていません: ${missing_cmds[*]}" >&2
    exit 1
fi

if [ "$(check_fcitx5_runtime)" = "native" ]; then
    echo "警告: ネイティブ版の fcitx5 が動作しています。" >&2
    echo "この Flatpak 拡張は Flatpak 版の Fcitx5 でのみ使用できます。" >&2
    echo "続行しますか？ [y/N]"
    read -r reply
    if [ "$reply" != "y" ] && [ "$reply" != "Y" ]; then
        echo "中断しました。"
        exit 0
    fi
fi

# flatpak info は --user/system 両方を検索する
if ! flatpak info org.fcitx.Fcitx5//stable >/dev/null 2>&1; then
    echo "org.fcitx.Fcitx5 ランタイムをインストールしています..."
    flatpak install --user -y flathub org.fcitx.Fcitx5//stable
fi

if ! flatpak info org.kde.Sdk//6.9 >/dev/null 2>&1; then
    echo "org.kde.Sdk//6.9 をインストールしています..."
    flatpak install --user -y flathub org.kde.Sdk//6.9
fi

EXTRA_BUILDER_ARGS=()
if [ "$LOCAL_BUILD" = true ]; then
    REPO_ROOT="$(cd "$SCRIPT_DIR/../.." && pwd)"
    BUILD_MANIFEST="$SCRIPT_DIR/org.fcitx.Fcitx5.Addon.Hazkey.local.yml"
    LOCAL_SRC="$SCRIPT_DIR/local-src"

    # type: dir は .gitignore を尊重しないため、.flatpak-builder/ 等のビルド成果物も
    # コピーされてしまう。git ls-files で追跡中のファイルのみをコピーする。
    # rsync の --filter=':- .gitignore' は .gitignore の否定パターン (!) を
    # サポートしないため使用できない (llama.cpp が build* + !build-info.cmake を使う)。
    rm -rf "$LOCAL_SRC"
    mkdir -p "$LOCAL_SRC"
    echo "ローカルソース ($REPO_ROOT) をクリーンコピーしています..."
    git -C "$REPO_ROOT" ls-files -z --recurse-submodules | rsync -a --files-from=- --from0 "$REPO_ROOT/" "$LOCAL_SRC/"
    rsync -a "$REPO_ROOT/.git" "$LOCAL_SRC/"  # llama.cpp の cmake が git コマンドを実行するため
    echo "ローカルソース ($REPO_ROOT) を使用してビルドします"

    # rofiles-fuse が前のモジュールでインストールされたバイナリ (Swift ツールチェーンの
    # clang 等) をハードリンク+読み取り専用にするため、後続モジュールのビルドで
    # "openat: Permission denied" が発生する。ローカルビルドではキャッシュの整合性より
    # ビルド成功を優先し、rofiles-fuse を無効化する。
    EXTRA_BUILDER_ARGS+=(--disable-rofiles-fuse)
else
    BUILD_MANIFEST="$MANIFEST"
fi

echo "ビルドしています... (出力先: $FLATPAK_REPO)"
flatpak-builder --user --force-clean --repo="$FLATPAK_REPO" "${EXTRA_BUILDER_ARGS[@]}" "$BUILD_DIR" "$BUILD_MANIFEST"

# --force: 既存 ref があっても削除 (直後に再インストールするため)
flatpak remote-delete --user --force "$REMOTE_NAME" 2>/dev/null || true
flatpak remote-add --user --no-gpg-verify "$REMOTE_NAME" "$FLATPAK_REPO"

echo "$APP_ID をインストールしています..."
flatpak install --user -y --or-update "$REMOTE_NAME" "$APP_ID"

# Flatpak は拡張のデスクトップエントリ・アイコンをエクスポートしないため
# (flatpak/flatpak#4006, #5888)、手動でホスト側にコピーする。
DESKTOP_SRC="$SCRIPT_DIR/$DESKTOP_FILE"
DESKTOP_DST="$HOME/.local/share/applications/$DESKTOP_FILE"
if [ -f "$DESKTOP_SRC" ]; then
    mkdir -p "$HOME/.local/share/applications"
    cp "$DESKTOP_SRC" "$DESKTOP_DST"
    echo "デスクトップエントリをインストールしました: $DESKTOP_DST"
fi

EXTENSION_LOCATION="$(flatpak info --show-location --user "$APP_ID//stable")"
icons_copied=false
if [ -d "$EXTENSION_LOCATION/files/share/icons" ]; then
    for icon in "$EXTENSION_LOCATION/files/share/icons/hicolor/"*"/apps/"*; do
        if [ -f "$icon" ]; then
            size_dir="$(basename "$(dirname "$(dirname "$icon")")")"
            dest_dir="$HOME/.local/share/icons/hicolor/$size_dir/apps"
            mkdir -p "$dest_dir"
            cp "$icon" "$dest_dir/"
            icons_copied=true
        fi
    done
fi
if [ "$icons_copied" = true ]; then
    echo "アイコンをインストールしました"
else
    echo "警告: アイコンが見つかりませんでした" >&2
fi

"$SCRIPT_DIR/reload_cache.sh"

# Flatpak 拡張のマウントはプロセス起動時に確定するため、設定リロードでは不十分で再起動が必要。
case "$(check_fcitx5_runtime)" in
    flatpak)
        echo "Fcitx5 を再起動しています..."
        flatpak kill org.fcitx.Fcitx5
        sleep 1  # セッションマネージャによる自動再起動を待つ
        if [ "$(check_fcitx5_runtime)" = "stopped" ]; then
            flatpak run org.fcitx.Fcitx5 &
            disown
            sleep 1
        fi
        echo "Fcitx5 を再起動しました"
        ;;
    native)
        echo "注意: Fcitx5 がホスト側で動作しています。"
        echo "この Flatpak 拡張を使用するには、Flatpak 版の Fcitx5 で起動してください。"
        ;;
    stopped)
        echo "注意: Flatpak 版の Fcitx5 が起動していません。Fcitx5 を起動してください。"
        ;;
esac

echo ""
echo "インストール完了!"
echo "タスクバーのキーボードアイコンを右クリックし、Fcitx5 の設定から Hazkey を入力メソッドに追加してください。"
echo ""
echo "ヒント: 変換精度を向上させるには、Hazkey 設定ツールから Zenzai モデルをダウンロードしてください。"
echo "  アプリランチャーから「Hazkey Settings」を起動 → AI タブからモデルをダウンロードできます。"
