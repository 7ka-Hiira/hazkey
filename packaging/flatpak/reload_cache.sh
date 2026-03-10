#!/usr/bin/env bash
# デスクトップエントリ・アイコンのキャッシュを再構築する。
# install.sh / uninstall.sh から呼び出される。

# KDE
if command -v kbuildsycoca6 >/dev/null 2>&1; then
    kbuildsycoca6 2>/dev/null || echo "警告: kbuildsycoca6 の実行に失敗しました (キャッシュは次回ログイン時に更新されます)" >&2
fi

# GNOME / Cinnamon / GTK ベースの DE
if command -v update-desktop-database >/dev/null 2>&1; then
    update-desktop-database -q "$HOME/.local/share/applications" 2>/dev/null || echo "警告: update-desktop-database の実行に失敗しました" >&2
fi

if command -v gtk-update-icon-cache >/dev/null 2>&1; then
    gtk-update-icon-cache -q "$HOME/.local/share/icons/hicolor" 2>/dev/null || echo "警告: gtk-update-icon-cache の実行に失敗しました" >&2
fi
