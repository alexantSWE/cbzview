# Maintainer: cbzview contributors
pkgname=cbzview
pkgver=1.0.0
pkgrel=1
pkgdesc="Fast, hardware-accelerated CBZ comic reader"
arch=('x86_64')
url="https://local.cbzview"
license=('custom:MIT')
depends=('glfw' 'libzip' 'libjpeg-turbo' 'libwebp' 'libpng')
makedepends=('gcc' 'pkgconf')
source=()

build() {
    cd "$startdir"
    make clean
    make
}

package() {
    cd "$startdir"
    make DESTDIR="$pkgdir" PREFIX="/usr" install
}
