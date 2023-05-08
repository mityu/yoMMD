all: yoMMD.icns yoMMD.ico statusicon.png;

yoMMD.icns: yoMMD.png
	@which iconutil > /dev/null || (echo "iconutil command not found" && exit 1)
	@which convert > /dev/null || (echo "imagemagick not found" && exit 1)
	@[ -d "yoMMD.iconset" ] || mkdir yoMMD.iconset
	convert -resize 512x512 yoMMD.png yoMMD.iconset/icon_512x512.png
	convert -resize 256x256 yoMMD.png yoMMD.iconset/icon_256x256.png
	convert -resize 128x128 yoMMD.png yoMMD.iconset/icon_128x128.png
	convert -resize 32x32   yoMMD.png yoMMD.iconset/icon_32x32.png
	convert -resize 64x64   yoMMD.png yoMMD.iconset/icon_64x64.png
	convert -resize 16x16   yoMMD.png yoMMD.iconset/icon_16x16.png
	cp yoMMD.png yoMMD.iconset/icon_512x512@2x.png
	cp yoMMD.iconset/icon_512x512.png yoMMD.iconset/icon_256x256@2x.png
	cp yoMMD.iconset/icon_256x256.png yoMMD.iconset/icon_128x128@2x.png
	cp yoMMD.iconset/icon_128x128.png yoMMD.iconset/icon_64x64@2x.png
	cp yoMMD.iconset/icon_64x64.png   yoMMD.iconset/icon_32x32@2x.png
	cp yoMMD.iconset/icon_32x32.png   yoMMD.iconset/icon_16x16@2x.png
	iconutil -c icns yoMMD.iconset

statusicon.png: statusicon-base.png
	convert -resize -filter point 28x28 statusicon-base.png statusicon.png

yoMMD.ico: yoMMD.png
	convert -define icon:auto-resize $< $@

statusicon.ico: statusicon-base.png
	convert -filter point -define icon:auto-resize $< $@

.PHONY: all
