pkgname=impulse
pkgver=1.0.0
pkgrel=1
pkgdesc="Desktop music player using Electron, React, and mpv IPC"
arch=('x86_64')
url="https://github.com/kylxbn/impulse"
license=('unknown')
depends=('electron' 'mpv' 'ffmpeg')
makedepends=('nodejs')
source=(
  'impulse.desktop'
  'impulse-launcher.sh'
)
sha256sums=(
  'SKIP'
  'SKIP'
)

prepare() {
  local _builddir="$srcdir/../$pkgname-build"
  rm -rf "$_builddir"
  mkdir -p "$_builddir"

  shopt -s dotglob nullglob
  for item in "$startdir"/*; do
    local name="${item##*/}"
    case "$name" in
      .|..|pkg|$pkgname-build|.pnpm-store|*.pkg.tar*|*.src.tar*)
        continue
        ;;
    esac
    cp -a "$item" "$_builddir"/
  done
  shopt -u dotglob nullglob

  cd "$_builddir"
  rm -rf .git .pnpm-store node_modules dist dist-renderer release pkg
}

build() {
  local _builddir="$srcdir/../$pkgname-build"
  cd "$_builddir"

  npm install
  npm run build
  npm prune --omit=dev
}

package() {
  local _builddir="$srcdir/../$pkgname-build"
  cd "$_builddir"

  local appdir="$pkgdir/usr/lib/$pkgname"
  install -d "$appdir"
  cp -a dist dist-renderer node_modules package.json impulse-512.png "$appdir/"

  install -Dm755 "$srcdir/impulse-launcher.sh" "$pkgdir/usr/bin/$pkgname"
  install -Dm644 "$srcdir/impulse.desktop" "$pkgdir/usr/share/applications/$pkgname.desktop"
  install -Dm644 "$_builddir/impulse-512.png" "$pkgdir/usr/share/pixmaps/$pkgname.png"
  install -Dm644 "$_builddir/impulse-512.png" "$pkgdir/usr/share/icons/hicolor/512x512/apps/$pkgname.png"
  install -Dm644 README.md "$pkgdir/usr/share/doc/$pkgname/README.md"
}
