#! /bin/bash

set -e

echo "run with buildah unshare $0"

I=dirage2build
LINUXDEPLOY="$HOME/bin/linuxdeploy-x86_64.AppImage"
APPIMAGETOOL="$HOME/bin/appimagetool-x86_64.AppImage"
IMAGENAME="DirAge2-1.1-x86_64.AppImage"

if ! buildah images | grep -q $I; then
  C=$(buildah bud -q .)
  buildah tag $C localhost/$I
fi

echo "Running build."

C=$(buildah from localhost/$I)
echo $C
buildah copy $C . /b
buildah copy $C "$LINUXDEPLOY" /b
buildah copy $C "$APPIMAGETOOL" /b
buildah --workingdir /b run $C cmake . -DCMAKE_BUILD_TYPE=Release
buildah --workingdir /b run $C make
buildah --workingdir /b run $C "./$(basename $LINUXDEPLOY)" --appimage-extract-and-run --appdir appdir --executable=dirage2 --desktop-file=dirage2.desktop --icon-file=dirage2.png
buildah --workingdir /b run $C "./$(basename $APPIMAGETOOL)" --appimage-extract-and-run --comp xz appdir $IMAGENAME
M=$(buildah mount $C)
cp -v $M/b/dirage2 .
cp -v $M/b/$IMAGENAME .

buildah images
buildah containers
