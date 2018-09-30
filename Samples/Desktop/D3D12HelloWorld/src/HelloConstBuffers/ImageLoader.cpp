#include "stdafx.h"
#include "ImageLoader.h"
#include <gdiplus.h>
using Gdiplus::Bitmap;

ImageLoader::ImageLoader(const wchar_t* f) : fname(f)
{
	mipMaps = vector<MipMap>();
	Bitmap *tp = Bitmap::FromFile(fname, false);
	Bitmap &t = *tp;
	const int h = t.GetHeight();
	const int w = t.GetWidth();

	BYTE* textureBytes = new BYTE[h*w * 4];
	for (int i = 0; i < h; i++)
	{
		for (int j = 0; j < w; j++)
		{
			Gdiplus::Color c;
			t.GetPixel(j, i, &c);
			textureBytes[i*w * 4 + j * 4] = c.GetR();
			textureBytes[i*w * 4 + j * 4 + 1] = c.GetG();
			textureBytes[i*w * 4 + j * 4 + 2] = c.GetB();
			textureBytes[i*w * 4 + j * 4 + 3] = c.GetA();
		}
	}

	MipMap m(textureBytes, w, h, 0);
	mipMaps.push_back(m);
}

MipMap ImageLoader::getMipMap(int level) {
	if (level >= mipMaps.size()) {
		generateMipMap(level);
	}

	return mipMaps[level];
}


void ImageLoader::generateMipMap(int level) {
	if (level < mipMaps.size()) {
		// Mip map has already been generated
		return;
	}
	if (mipMaps.size() < level) {
		// Previous level hasn't been generated yet, so do that
		generateMipMap(level - 1);
	}

	MipMap lastLevel(mipMaps[level - 1]);
	MipMap nextLevel(lastLevel.next());

	for (int i = 0; i < nextLevel.height; ++i) {
		for (int j = 0; j < nextLevel.width; ++j) {
			MipMap::Pixel p1 = lastLevel.getPixel(i, j);
			MipMap::Pixel p2 = lastLevel.getPixel(i + 1, j);
			MipMap::Pixel p3 = lastLevel.getPixel(i, j + 1);
			MipMap::Pixel p4 = lastLevel.getPixel(i + 1, j + 1);

			MipMap::Pixel result = MipMap::Pixel::avg(p1, p2, p3, p4);

			nextLevel.setPixel(i, j, result);
		}
	}

	assert(mipMaps.size() == level);

	mipMaps.push_back(nextLevel);
}

ImageLoader::~ImageLoader()
{
}

MipMap MipMap::next() {
	int nextWidth = width / 2;
	int nextHeight = height / 2;
	BYTE* nextBytes = new BYTE[nextWidth * nextHeight * 4];
	int nextLevel = level + 1;
	return MipMap(nextBytes, nextWidth, nextHeight, nextLevel);
}

MipMap::Pixel MipMap::getPixel(int i, int j) {
	return MipMap::Pixel(
		bytes[getByteIndex(i, j, Channel::R)],
		bytes[getByteIndex(i, j, Channel::G)],
		bytes[getByteIndex(i, j, Channel::B)],
		bytes[getByteIndex(i, j, Channel::A)]
	);
}

void MipMap::setPixel(int i, int j, Pixel p) {
	bytes[getByteIndex(i, j, Channel::R)] = p.r;
	bytes[getByteIndex(i, j, Channel::G)] = p.g;
	bytes[getByteIndex(i, j, Channel::B)] = p.b;
	bytes[getByteIndex(i, j, Channel::A)] = p.a;
}

inline int MipMap::getByteIndex(int i, int j, MipMap::Channel c) {
	return i * width * 4 + j * 4 + c;
}

MipMap::Pixel MipMap::Pixel::avg(Pixel p1, Pixel p2, Pixel p3, Pixel p4) {
	return MipMap::Pixel(
		(BYTE)((p1.r + p2.r + p3.r + p4.r) / 4),
		(BYTE)((p1.g + p2.g + p3.g + p4.g) / 4),
		(BYTE)((p1.b + p2.b + p3.b + p4.b) / 4),
		(BYTE)((p1.a + p2.a + p3.a + p4.a) / 4)
	 );
}
