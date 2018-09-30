#pragma once

#include <vector>
using namespace::std;

struct MipMap {
	enum Channel {
		R, G, B, A
	};

	struct Pixel {
		BYTE r, g, b, a;

		Pixel(BYTE r, BYTE g, BYTE b, BYTE a) : r(r), g(g), b(b), a(a) {};
		static Pixel avg(Pixel p1, Pixel p2, Pixel p3, Pixel p4);
	};

	MipMap(BYTE* b, int w, int h, int l) : bytes(b), width(w), height(h), level(l) {};
	MipMap next();
	Pixel getPixel(int i, int j);
	void setPixel(int i, int j, Pixel p);
	inline int getByteIndex(int i, int j, Channel c);

	BYTE* bytes;
	int width, height;
	int level;
};

class ImageLoader
{
public:
	ImageLoader(const wchar_t* fname);
	~ImageLoader();
	// Mip level == 0 retrieves the base image
	MipMap getMipMap(int mipLevel);
	
private:
	void generateMipMap(int level);
	const wchar_t* fname;
	vector<MipMap> mipMaps;
};