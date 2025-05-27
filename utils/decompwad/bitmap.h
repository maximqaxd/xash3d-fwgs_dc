/*
================================================
https://en.wikipedia.org/wiki/BMP_file_format
================================================
*/

#ifdef _WIN32
#include <windows.h>
#define _BITMAP_H
#endif

#ifndef _BITMAP_H
#define _BITMAP_H

enum {
	BI_RGB,
	BI_RLE8,
	BI_RLE4,
	BI_BITFIELDS,
	BI_JPEG,
	BI_PNG,
	BI_ALPHABITFILDS,
	BI_CMYK,
	BI_CMYKRLE8,
	BI_CMYKRLE4,
};

typedef struct {
    unsigned short bfType;
	unsigned int bfSize;
	unsigned short bfReserved1;
	unsigned short bfReserved2;
	unsigned int bfOffBits;
} __attribute__((packed)) BITMAPFILEHEADER;

typedef struct {
	unsigned int biSize;
	int biWidth;
	int biHeight;
	unsigned short biPlanes;
	unsigned short biBitCount;
	unsigned int biCompression;
	unsigned int biSizeImage;
	int biXPelsPerMeter;
	int biYPelsPerMeter;
	unsigned int biClrUsed;
	unsigned int biClrImportant;
} __attribute__((packed)) BITMAPINFOHEADER;

typedef struct {
	unsigned char rgbBlue;
	unsigned char rgbGreen;
	unsigned char rgbRed;
	unsigned char rgbReserved;
} RGBQUAD;

#endif /* _BITMAP_H */
