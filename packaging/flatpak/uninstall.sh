#!/usr/bin/env bash
set -e

SCRIPT_DIR="$(cd "$(dirname "$0")" && pwd)"
BUILD_DIR="$SCRIPT_DIR/build-dir"
FLATPAK_REPO="$SCRIPT_DIR/hazkey-repo"
REMOTE_NAME="hazkey-local"
APP_ID="org.fcitx.Fcitx5.Addon.Hazkey"
DESKTOP_FILE="org.fcitx.Fcitx5.Addon.Hazkey.hazkey-settings.desktop"

# 拡張がマウント中だとアンインストールできないため、先に停止する。
if flatpak ps --columns=application 2>/dev/null | grep -q org.fcitx.Fcitx5; then
    echo "Fcitx5 を停止しています..."
    flatpak kill org.fcitx.Fcitx5
    sleep 1
fi

if flatpak info --user "$APP_ID" >/dev/null 2>&1; then
    echo "$APP_ID をアンインストールしています..."
    flatpak uninstall --user -y "$APP_ID"
else
    echo "$APP_ID はインストールされていません。"
fi

if flatpak remotes --user | grep -q "$REMOTE_NAME"; then
    echo "リモート $REMOTE_NAME を削除しています..."
    flatpak remote-delete --user "$REMOTE_NAME"
fi

if [ -d "$FLATPAK_REPO" ]; then
    echo "ローカルリポジトリを削除しています: $FLATPAK_REPO"
    rm -rf "$FLATPAK_REPO"
fi

if [ -d "$BUILD_DIR" ]; then
    echo "ビルドディレクトリを削除しています: $BUILD_DIR"
    rm -rf "$BUILD_DIR"
fi

# install.sh が手動コピーしたデスクトップエントリ・アイコンを掃除 (flatpak/flatpak#4006, #5888)
DESKTOP_DST="$HOME/.local/share/applications/$DESKTOP_FILE"
if [ -f "$DESKTOP_DST" ]; then
    rm "$DESKTOP_DST"
    echo "デスクトップエントリを削除しました: $DESKTOP_DST"
fi

ICON_NAME="org.fcitx.Fcitx5.Addon.Hazkey"
icons_removed=false
for icon in "$HOME/.local/share/icons/hicolor/"*"/apps/$ICON_NAME."*; do
    if [ -f "$icon" ]; then
        rm "$icon"
        icons_removed=true
    fi
done
if [ "$icons_removed" = true ]; then
    echo "アイコンを削除しました: $ICON_NAME"
fi

"$SCRIPT_DIR/reload_cache.sh"

if [ -d "$SCRIPT_DIR/.flatpak-builder" ]; then
    echo ""
    echo "注意: flatpak-builder のキャッシュ ($SCRIPT_DIR/.flatpak-builder) が残っています。"
    echo "不要であれば手動で削除してください: rm -rf $SCRIPT_DIR/.flatpak-builder"
fi

if flatpak info org.fcitx.Fcitx5//stable >/dev/null 2>&1; then
    if ! flatpak ps --columns=application 2>/dev/null | grep -q org.fcitx.Fcitx5; then
        echo "Fcitx5 を再起動しています..."
        flatpak run org.fcitx.Fcitx5 &
        disown
        sleep 1
    fi
fi

echo "完了しました。"
