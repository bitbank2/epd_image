//
// epd_image - prepare image data for e-paper displays
// and output it as hex data ready to compile into c code
//
// Specifically - do pixel color matching for GRAY/BW/BWR/BWY/BWYR output
// from any input image, split it into the 1 or 2 memory planes
// and prepare that data to be easily compiled into C code
//
// Written by Larry Bank
//
// Copyright 2023 BitBank Software, Inc. All Rights Reserved.
// Licensed under the Apache License, Version 2.0 (the "License");
// you may not use this file except in compliance with the License.
// You may obtain a copy of the License at
//    http://www.apache.org/licenses/LICENSE-2.0
// Unless required by applicable law or agreed to in writing, software
// distributed under the License is distributed on an "AS IS" BASIS,
// WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
// See the License for the specific language governing permissions and
// limitations under the License.
//===========================================================================

#include <stdint.h>
#include <string.h>
#include <stdio.h>
#include <stdlib.h>
#ifdef _WIN32
#include <direct.h>
#else
#include <unistd.h>
#endif

#define __LINUX__
#include "JPEGDEC.h"
#include "/Users/laurencebank/Projects/epd_image/jpeg.inl"

enum
{
  OPTION_BW = 0,
  OPTION_BWR,
  OPTION_BWY,
  OPTION_BWYR,
  OPTION_4GRAY,
  OPTION_COUNT
};
// How many hex bytes are written per line of output
#define BYTES_PER_LINE 16

// Output format options (black & white, black/white/red, black/white/yellow, 2-bit grayscale)
const char *szOptions[] = {"BW", "BWR", "BWY", "BWYR", "4GRAY", NULL};
uint8_t ucBlue[256], ucGreen[256], ucRed[256]; // palette colors
#ifdef _WIN32
#define SLASH_CHAR '\\'
#else
#define SLASH_CHAR '/'
#endif
int iWidth, iHeight, iBpp;
FILE * ihandle;
void GetLeafName(char *fname, char *leaf);
void FixName(char *name);
unsigned char GetGrayPixel(int x, int y, uint8_t *pData, int iPitch, int iBpp);
/* Table to flip the bit direction of a byte */
const uint8_t ucMirror[256]=
     {0, 128, 64, 192, 32, 160, 96, 224, 16, 144, 80, 208, 48, 176, 112, 240,
      8, 136, 72, 200, 40, 168, 104, 232, 24, 152, 88, 216, 56, 184, 120, 248,
      4, 132, 68, 196, 36, 164, 100, 228, 20, 148, 84, 212, 52, 180, 116, 244,
      12, 140, 76, 204, 44, 172, 108, 236, 28, 156, 92, 220, 60, 188, 124, 252,
      2, 130, 66, 194, 34, 162, 98, 226, 18, 146, 82, 210, 50, 178, 114, 242,
      10, 138, 74, 202, 42, 170, 106, 234, 26, 154, 90, 218, 58, 186, 122, 250,
      6, 134, 70, 198, 38, 166, 102, 230, 22, 150, 86, 214, 54, 182, 118, 246,
      14, 142, 78, 206, 46, 174, 110, 238, 30, 158, 94, 222, 62, 190, 126, 254,
      1, 129, 65, 193, 33, 161, 97, 225, 17, 145, 81, 209, 49, 177, 113, 241,
      9, 137, 73, 201, 41, 169, 105, 233, 25, 153, 89, 217, 57, 185, 121, 249,
      5, 133, 69, 197, 37, 165, 101, 229, 21, 149, 85, 213, 53, 181, 117, 245,
      13, 141, 77, 205, 45, 173, 109, 237, 29, 157, 93, 221, 61, 189, 125, 253,
      3, 131, 67, 195, 35, 163, 99, 227, 19, 147, 83, 211, 51, 179, 115, 243,
      11, 139, 75, 203, 43, 171, 107, 235, 27, 155, 91, 219, 59, 187, 123, 251,
      7, 135, 71, 199, 39, 167, 103, 231, 23, 151, 87, 215, 55, 183, 119, 247,
      15, 143, 79, 207, 47, 175, 111, 239, 31, 159, 95, 223, 63, 191, 127, 255};

//
// Parse the BMP header and read the pixel data into memory
// returns 1 for success, 0 for failure
//
int ReadBMP(uint8_t *pBMP, int *offbits, int *width, int *height, int *bpp)
{
    int cx, cy;
    uint8_t ucCompression;

    if (pBMP[0] != 'B' || pBMP[1] != 'M') // must start with 'BM'
        return 0; // not a BMP file
    cx = pBMP[18] | pBMP[19]<<8;
    cy = pBMP[22] | pBMP[23]<<8;
    ucCompression = pBMP[30]; // 0 = uncompressed, 1/2/4 = RLE compressed
    if (ucCompression != 0) { // unsupported feature
        printf("unsupported compression type %d\n", ucCompression);
        return 0;
    }
    *width = cx;
    *height = cy;
    *bpp = pBMP[28] | pBMP[29]<<8;
    *offbits = pBMP[10] | pBMP[11]<<8;
    // Get the palette as RGB565 values (if there is one)
    if (*bpp == 4 || *bpp == 8)
    {
        int iOff, iColors;
        iColors = pBMP[46]; // colors used BMP field
        if (iColors == 0 || iColors > (1<<(*bpp)))
            iColors = (1 << *bpp); // full palette
        iOff = *offbits - (4 * iColors); // start of color palette
        for (int x=0; x<iColors; x++)
        {
            ucBlue[x] = pBMP[iOff++];
            ucGreen[x] = pBMP[iOff++];
            ucRed[x] = pBMP[iOff++];
            iOff++; // skip extra byte
        }
    }
    return 1;
} /* ReadBMP() */
//
// Create 1 memory plane hex output
//
void MakeC_BW(uint8_t *pSrc, int iOffBits, int iWidth, int iHeight, int iBpp, int iSize, FILE *ohandle, char *szLeaf)
{
	int x, y, iSrcPitch, iPitch;
    int iIn, iTotal, i, iLine;
	uint8_t uc, ucPixel, *s = &pSrc[iOffBits];

    iPitch = (iWidth + 7)/8; // output bytes per line
	iSrcPitch = ((iWidth * iBpp) + 7)/8;
	iSrcPitch = (iSrcPitch + 3) & 0xfffc; // Windows BMP lines are dword aligned
	iTotal = iPitch * iHeight; // how many bytes we're creating
	// show pitch
	fprintf(ohandle, "// Image size: width %d, height %d\n", iWidth, iHeight);
	fprintf(ohandle, "// %d bytes per line\n", iPitch);
	fprintf(ohandle, "// %d bytes per plane\n", iTotal);
    fprintf(ohandle, "const uint8_t %s_0[] PROGMEM = {\n", szLeaf); // start of data array (plane 0)
    iLine = i = iIn = 0;
    for (y=0; y<iHeight; y++) {
        uc = 0;
        for (x=0; x<iWidth; x++) {
            ucPixel = GetGrayPixel(x, y, s, iSrcPitch, iBpp); // slower, but easier on the eyes
            uc <<= 1;
            uc |= (ucPixel >> 1); // only need MSB
            if ((x & 7) == 7 || x == iWidth-1) {
                if ((x & 7) != 7) { // adjust last odd byte
                    uc <<= (7-(x&7));
                }
                i++;
                iLine++;
                fprintf(ohandle, "0x%02x", uc);
                if (i != iTotal) {
                    fprintf(ohandle, ",");
                }
                if (iLine == BYTES_PER_LINE) {
                    fprintf(ohandle, "\n");
                    iLine = 0;
                }
                uc = 0;
            } // if a whole byte was formed
        } // for x
    } // for y
    fprintf(ihandle, "};\n"); // final closing brace
} /* MakeC_BW() */
//
// Match the given pixel to black (00), white (01), or yellow (1x)
//
unsigned char GetYellowPixel(int x, int y, uint8_t *pData, int iPitch, int iBpp)
{
    uint8_t uc=0, *s;
    int gr, r=0, g=0, b=0;
    
    switch (iBpp) {
        case 4:
            s = &pData[(y * iPitch) + (x >> 1)];
            uc = s[0];
            if ((x & 1) == 0) uc >>= 4;
            uc &= 0xf;
            b = ucBlue[uc];
            g = ucGreen[uc];
            r = ucRed[uc];
            break;
        case 8:
            s = &pData[(y * iPitch) + x];
            uc = s[0];
            b = ucBlue[uc];
            g = ucGreen[uc];
            r = ucRed[uc];
            break;
        case 24:
        case 32:
            s = &pData[(y * iPitch) + ((x * iBpp)>>3)];
            b = s[0];
            g = s[1];
            r = s[2];
            break;
    } // switch on bpp
    gr = (b + r + g*2)>>2; // gray
    // match the color to closest of black/white/yellow
    if (r > b && g > b) { // yellow is dominant?
        if (gr < 100 && r < 80) {
            // black
        } else {
            if (r - b > 32 && g - b > 32) {
                // is yellow really dominant?
                uc |= 2;
            } else { // yellowish should be white
                // no, use white instead of pink/yellow
                uc |= 1;
            }
        }
    } else { // check for white/black
        if (gr >= 100) {
            uc |= 1;
        } else {
            // black
        }
    }
    return uc;
} /* GetYellowPixel() */
//
// Match the given pixel to black (00), white (01), or red (1x)
//
unsigned char GetRedPixel(int x, int y, uint8_t *pData, int iPitch, int iBpp)
{
    uint8_t ucOut=0, uc, *s;
    int gr=0, r=0, g=0, b=0;
    
    switch (iBpp) {
        case 4:
            s = &pData[(y * iPitch) + (x >> 1)];
            uc = s[0];
            if ((x & 1) == 0) uc >>= 4;
            uc &= 0xf;
            b = ucBlue[uc];
            g = ucGreen[uc];
            r = ucRed[uc];
            break;
        case 8:
            s = &pData[(y * iPitch) + x];
            uc = s[0];
            b = ucBlue[uc];
            g = ucGreen[uc];
            r = ucRed[uc];
            break;
        case 24:
        case 32:
            s = &pData[(y * iPitch) + ((x * iBpp)>>3)];
            b = s[0];
            g = s[1];
            r = s[2];
            break;
    } // switch on bpp
    gr = (b + r + g*2)>>2; // gray
    // match the color to closest of black/white/red
    if (r > g && r > b) { // red is dominant
        if (gr < 100 && r < 80) {
            // black
        } else {
            if (r-b > 32 && r-g > 32) {
                // is red really dominant?
                ucOut |= 2; // red
            } else { // yellowish should be white
                // no, use white instead of pink/yellow
                ucOut |= 1;
            }
        }
    } else { // check for white/black
        if (gr >= 100) {
            ucOut |= 1; // white
        } else {
            // black
        }
    }
    return ucOut;
} /* GetRedPixel() */
//
// Match the given pixel to black (00), white (01), yellow (10), or red (11)
// returns 2 bit value of closest matching color
//
unsigned char GetBWYRPixel(int x, int y, uint8_t *pData, int iPitch, int iBpp)
{
    uint8_t ucOut=0, uc=0, *s;
    int gr=0, r=0, g=0, b=0;
    
    switch (iBpp) {
        case 4:
            s = &pData[(y * iPitch) + (x >> 1)];
            uc = s[0];
            if ((x & 1) == 0) uc >>= 4;
            uc &= 0xf;
            b = ucBlue[uc];
            g = ucGreen[uc];
            r = ucRed[uc];
            break;
        case 8:
            s = &pData[(y * iPitch) + x];
            uc = s[0];
            b = ucBlue[uc];
            g = ucGreen[uc];
            r = ucRed[uc];
            break;
        case 24:
        case 32:
            s = &pData[(y * iPitch) + ((x * iBpp)>>3)];
            r = s[2];
            g = s[1];
            b = s[0];
            break;
    } // switch on bpp
    gr = (b + r + g*2)>>2; // gray
    // match the color to closest of black/white/yellow/red
    if (r > b || g > b) { // red or yellow is dominant
        if (gr < 90 || (r < 80 && g < 80)) {
            // black
        } else {
            if (r-b > 32 && r-g > 70) {
                // is red really dominant?
                ucOut = 3; // red
            } else if (r-b > 32 && g-b > 32) {
                // yes, yellow
                ucOut = 2;
            } else {
                ucOut = 1; // gray/white
            }
        }
    } else { // check for white/black
        if (gr >= 100) {
            ucOut = 1; // white
        } else {
            // black
        }
    }
    return ucOut;
} /* GetBWYRPixel() */
//
// Return the given pixel as a 2-bit grayscale value
//
unsigned char GetGrayPixel(int x, int y, uint8_t *pData, int iPitch, int iBpp)
{
    uint8_t uc=0, *s;
    int r=0, g=0, b=0;
    
    switch (iBpp) {
        case 1:
            s = &pData[(y * iPitch) + (x >> 3)];
            uc = s[0];
            uc >>= (x & 7);
            uc &= 1;
            uc |= (uc << 1); // only black/white (00/11)
            break;
        case 4:
            s = &pData[(y * iPitch) + (x >> 1)];
            uc = s[0];
            if ((x & 1) == 0) uc >>= 4;
            uc &= 0xf;
            b = ucBlue[uc];
            g = ucGreen[uc];
            r = ucRed[uc];
            b = ((b + g + r*2) >> 2); // simple grayscale
            uc = (uint8_t)(b >> 6); // top 2 bits are the gray level
            break;
        case 8:
            s = &pData[(y * iPitch) + x];
            uc = s[0];
            b = ucBlue[uc];
            g = ucGreen[uc];
            r = ucRed[uc];
            b = ((b + g + r*2) >> 2); // simple grayscale
            uc = (uint8_t)(b >> 6); // top 2 bits are the gray level
            break;
        case 24:
        case 32:
            s = &pData[(y * iPitch) + ((x * iBpp)>>3)];
            b = s[0];
            g = s[1];
            r = s[2];
            b = ((b + g + r*2) >> 2); // simple grayscale
            uc = (uint8_t)(b >> 6); // top 2 bits are the gray level
            break;
    } // switch on bpp
    return uc;
} /* GetGrayPixel() */
//
// Return the given pixel as a 8-bit grayscale value
//
unsigned char GetGrayPixel8(int x, int y, uint8_t *pData, int iPitch, int iBpp)
{
    uint8_t uc=0, *s;
    int r=0, g=0, b=0;
    
    switch (iBpp) {
        case 1:
            s = &pData[(y * iPitch) + (x >> 3)];
            uc = s[0];
            uc >>= (x & 7);
            uc |= (uc << 1); // only black/white (00/11)
            uc |= (uc << 2) | (uc << 4) | (uc << 6);
            break;
        case 4:
            s = &pData[(y * iPitch) + (x >> 1)];
            uc = s[0];
            if ((x & 1) == 0) uc >>= 4;
            uc &= 0xf;
            b = ucBlue[uc];
            g = ucGreen[uc];
            r = ucRed[uc];
            uc = (uint8_t)((b + g + r*2) >> 2); // simple grayscale
            break;
        case 8:
            s = &pData[(y * iPitch) + x];
            uc = s[0];
            b = ucBlue[uc];
            g = ucGreen[uc];
            r = ucRed[uc];
            uc = (uint8_t)((b + g + r*2) >> 2); // simple grayscale
            break;
        case 24:
        case 32:
            s = &pData[(y * iPitch) + ((x * iBpp)>>3)];
            b = s[0];
            g = s[1];
            r = s[2];
            uc = (uint8_t)((b + g + r*2) >> 2); // simple grayscale
            break;
    } // switch on bpp
    return uc;
} /* GetGrayPixel8() */
//
// Convert 2-bit grayscale (4GRAY) into hex 2-plane output
//
void MakeC_4GRAY(uint8_t *pSrc, int iOffBits, int iWidth, int iHeight, int iBpp, int iSize, FILE *ohandle, char *szLeaf)
{
    int iSrcPitch, iPitch;
    int i, iPlane, x, y, iIn, iTotal, iLine;
    uint8_t ucPixel, uc, *s = &pSrc[iOffBits];

    iSrcPitch = ((iBpp * iWidth) + 7)/8;
    iSrcPitch = (iSrcPitch + 3) & 0xfffc; // Windows BMP lines are dword aligned
    iPitch = (iWidth + 7)/8; // bytes per line of each 1-bpp plane
    iTotal = iPitch * iHeight; // how many bytes we're creating
    // show pitch
    fprintf(ohandle, "// Image size: width %d, height %d\n", iWidth, iHeight);
    fprintf(ohandle, "// %d bytes per line\n", iPitch);
    fprintf(ohandle, "// %d bytes per plane\n", iTotal);
    for (iPlane=0; iPlane<2; iPlane++) {
        fprintf(ohandle, "// Plane %d data\n", iPlane);
        fprintf(ohandle, "const uint8_t %s_%d[] PROGMEM = {\n", szLeaf, iPlane);
        iLine = i = iIn = 0;
        for (y=0; y<iHeight; y++) {
            uc = 0;
            for (x=0; x<iWidth; x++) {
                ucPixel = GetGrayPixel(x, y, s, iSrcPitch, iBpp); // slower, but easier on the eyes
                uc <<= 1;
                uc |= ((ucPixel >> iPlane) & 1); // add correct plane's bit
                if ((x & 7) == 7 || x == iWidth-1) {
                    if ((x & 7) != 7) { // adjust last odd byte
                        uc <<= (7-(x&7));
                    }
                    i++;
                    iLine++;
                    fprintf(ohandle, "0x%02x", uc);
                    if (i != iTotal) {
                        fprintf(ohandle, ",");
                    }
                    if (iLine == BYTES_PER_LINE) {
                        fprintf(ohandle, "\n");
                        iLine = 0;
                    }
                    uc = 0;
                } // if a whole byte was formed
            } // for x
        } // for y
        fprintf(ihandle, "};\n"); // final closing brace
    } // for each plane
} /* MakeC_4GRAY() */
//
// Convert to Black/White/Yellow/Red packed 1-plane output
//
void MakeC_4CLR(uint8_t *pSrc, int iOffBits, int iWidth, int iHeight, int iBpp, int iSize, FILE *ohandle, char *szLeaf)
{
    int iSrcPitch, iPitch;
    int i, x, y, iIn, iTotal, iLine;
    uint8_t ucPixel, uc, *s = &pSrc[iOffBits];

    iSrcPitch = ((iBpp * iWidth) + 7)/8;
    iSrcPitch = (iSrcPitch + 3) & 0xfffc; // Windows BMP lines are dword aligned
    iPitch = (iWidth + 3)/4; // bytes per line of the 2-bpp plane
    iTotal = iPitch * iHeight; // how many bytes we're creating
    // show pitch
    fprintf(ohandle, "// Image size: width %d, height %d\n", iWidth, iHeight);
    fprintf(ohandle, "// %d bytes per line\n", iPitch);
    fprintf(ohandle, "// %d bytes total\n", iTotal);
    fprintf(ohandle, "const uint8_t %s[] PROGMEM = {\n", szLeaf);
    iLine = i = iIn = 0;
    for (y=0; y<iHeight; y++) {
        uc = 0;
        for (x=0; x<iWidth; x++) {
            ucPixel = GetBWYRPixel(x, y, s, iSrcPitch, iBpp); // slower, but easier on the eyes
            uc <<= 2;
            uc |= ucPixel; // pack 2 bits at a time into each byte
            if ((x & 3) == 3 || x == iWidth-1) { // store new bytes every 4 pixels
                if ((x & 3) != 3) { // adjust last odd byte
                    uc <<= ((3-(x&3))*2);
                }
                i++;
                iLine++;
                fprintf(ohandle, "0x%02x", uc);
                if (i != iTotal) {
                    fprintf(ohandle, ",");
                }
                if (iLine == BYTES_PER_LINE) {
                    fprintf(ohandle, "\n");
                    iLine = 0;
                }
                uc = 0;
            } // if a whole byte was formed
        } // for x
    } // for y
    fprintf(ihandle, "};\n"); // final closing brace
} /* MakeC_4CLR() */
//
// Convert BWR/BWY into 2-plane output
//
void MakeC_3CLR(uint8_t *pSrc, int iOffBits, int iWidth, int iHeight, int iBpp, int iSize, FILE *ohandle, char *szLeaf, int iType)
{
    int iSrcPitch, iPitch;
    int i, iPlane, x, y, iIn, iTotal, iLine;
    uint8_t ucPixel, uc, *s = &pSrc[iOffBits];

    iSrcPitch = ((iBpp * iWidth) + 7)/8;
    iSrcPitch = (iSrcPitch + 3) & 0xfffc; // Windows BMP lines are dword aligned
    iPitch = (iWidth + 7)/8; // bytes per line of each 1-bpp plane
    iTotal = iPitch * iHeight; // how many bytes we're creating
    // show pitch
    fprintf(ohandle, "// Image size: width %d, height %d\n", iWidth, iHeight);
    fprintf(ohandle, "// %d bytes per line\n", iPitch);
    fprintf(ohandle, "// %d bytes per plane\n", iTotal);
    for (iPlane=0; iPlane<2; iPlane++) {
        fprintf(ohandle, "// Plane %d data\n", iPlane);
        fprintf(ohandle, "const uint8_t %s_%d[] PROGMEM = {\n", szLeaf, iPlane);
        iLine = i = iIn = 0;
        for (y=0; y<iHeight; y++) {
            uc = 0;
            for (x=0; x<iWidth; x++) {
                if (iType == OPTION_BWR)
                    ucPixel = GetRedPixel(x, y, s, iSrcPitch, iBpp); // slower, but easier on the eyes
                else
                    ucPixel = GetYellowPixel(x, y, s, iSrcPitch, iBpp);
                uc <<= 1;
                uc |= ((ucPixel >> iPlane) & 1); // add correct plane's bit
                if ((x & 7) == 7 || x == iWidth-1) {
                    if ((x & 7) != 7) { // adjust last odd byte
                        uc <<= (7-(x&7));
                    }
                    i++;
                    iLine++;
                    fprintf(ohandle, "0x%02x", uc);
                    if (i != iTotal) {
                        fprintf(ohandle, ",");
                    }
                    if (iLine == BYTES_PER_LINE) {
                        fprintf(ohandle, "\n");
                        iLine = 0;
                    }
                    uc = 0;
                } // if a whole byte was formed
            } // for x
        } // for y
        fprintf(ihandle, "};\n"); // final closing brace
    } // for each plane
} /* MakeC_3CLR() */
//
// mirror image horizontally
//
void MirrorBMP(uint8_t *pPixels, int iWidth, int iHeight, int iBpp)
{
    int y;
    uint8_t c1,c2,c3, *s, *d;
    uint32_t lTemp, *ld, *ls;
    int x, iPitch;

       iPitch = ((iWidth * iBpp) + 7)/8;
       iPitch = (iPitch + 3) & 0xfffc;
       switch (iBpp)
          {
          case 1:
                  if ((iWidth & 7) == 0) { // multiple of 8 wide
                      for (y=0; y<iHeight; y++)
                      {
                          s = &pPixels[y * iPitch];
                          d = s + (iWidth>>3) - 1;
                          x = iWidth >> 4;
                          while (x)
                          {
                              c1 = ucMirror[*s];
                              c2 = ucMirror[*d];
                              *s++ = c2;
                              *d-- = c1;
                              x--;
                          }
                      }
                  } else {
                      // DEBUG
                  }
             break;
          case 4:
             for (y=0; y<iHeight; y++)
                {
                s = &pPixels[y * iPitch];
                d = s + (iWidth>>1) - 1;
                x = iWidth >> 2;
                while (x)
                   {
                   c1 = *s;
                   c2 = *d;
                   c1 = (c1 << 4) | (c1 >> 4);
                   c2 = (c2 << 4) | (c2 >> 4);
                   *s++ = c2;
                   *d-- = c1;
                   x--;
                   }
                }
             break;
          case 8:
             for (y=0; y<iHeight; y++)
                {
                s = &pPixels[y * iPitch];
                d = s + iWidth - 1;
                    x = iWidth >> 1;
                    while (x)
                       {
                       c1 = *s;
                       *s++ = *d;
                       *d-- = c1;
                       x--;
                       }
                    }
                 break;
              case 24:
                  for (y=0; y<iHeight; y++)
                    {
                    s = &pPixels[y * iPitch];
                    d = s + (iWidth - 1)*3;
                    x = iWidth >> 1;
                    while (x)
                       {
                       c1 = s[0];
                       c2 = s[1];
                       c3 = s[2];
                           s[0] = d[0];
                           s[1] = d[1];
                           s[2] = d[2];
                           s += 3;
                           d[0] = c1;
                           d[1] = c2;
                           d[2] = c3;
                           d -= 3;
                           x--;
                           }
                        }
                     break;
                  case 32:
                  for (y=0; y<iHeight; y++)
                    {
                        ls = (uint32_t *)&pPixels[y * iPitch];
                        ld = ls + iWidth - 1;
                        x = iWidth >> 1;
                        while (x)
                           {
                           lTemp = *ls;
                           *ls++ = *ld;
                           *ld-- = lTemp;
                           x--;
                           }
                        }
                     break;
                  }
} /* MirrorBMP() */
//
// flip image vertically
//
void FlipBMP(uint8_t *p, int iWidth, int iHeight, int iBpp)
{
int iPitch;
uint8_t c, *s, *d;

	iPitch = ((iWidth*iBpp)+7)/8;
	iPitch = (iPitch + 3) & 0xfffc;
	s = p;
	d = &p[iPitch * (iHeight-1)];
	for (int y=0; y<iHeight/2; y++) {
           for (int x=0; x<iPitch; x++) {
              c = s[x];
	      s[x] = d[x];
	      d[x] = c; // swap top/bottom lines
	   }
	   s += iPitch;
	   d -= iPitch;
	}
} /* FlipBMP() */

void RotateImage(int iRotation, uint8_t *pPixels, int *iWidth, int *iHeight, int iBpp)
{
    uint8_t *pTemp, *s, *d, x, y;
    int w = *iWidth, h = *iHeight;
    int iSrcPitch, iDstPitch;
    
    if (iRotation == 0) return; // nothing to do
    if (iRotation == 180) {
        FlipBMP(pPixels, w, h, iBpp);
        MirrorBMP(pPixels, w, h, iBpp);
        return;
    }
    iSrcPitch = ((w * iBpp)+7)/8;
    iSrcPitch = (iSrcPitch + 3) & 0xfffc; // dword aligned
    iDstPitch = ((h * iBpp)+7)/8;
    iDstPitch = (iDstPitch + 3) & 0xfffc;
    switch (iBpp) {
        case 1:
            break;
        case 4:
            pTemp = (uint8_t *) malloc((w+2) * iDstPitch);
            s = pPixels;
            for (y = 0; y < h; y+=2)
            {
                uint8_t c1, c2, c3;
                d = pTemp - y + (w - 1)/2; /* Start at right edge */
                for (x=0; x < (h+1)>>1; x++)
                {
                    c1 = *s;
                    c2 = s[iSrcPitch];
                    s++;
                    c3 = (c1 >> 4) | (c2 & 0xf0); /* swap pixels */
                    c2 = (c2 << 4) | (c1 & 0xf);
                    *d = c3;
                    d[iDstPitch] = c2;
                    d += iDstPitch * 2;
                }
                s += (iSrcPitch * 2);
            }
            memcpy(pPixels, pTemp, iDstPitch * w);
            free(pTemp);
            break;
        case 8:
            break;
        case 24:
        case 32:
            break;
    }
    if (iRotation == 270) {
        FlipBMP(pPixels, h, w, iBpp);
        MirrorBMP(pPixels, h, w, iBpp);
    }
    if (iRotation == 90 || iRotation == 270) { // swap width/height
        x = *iWidth;
        *iWidth = *iHeight;
        *iHeight = x;
    }
} /* RotateImage() */
//
// Pick the best color of black/white/red/yellow
// depending on the output format option
//
void MatchBestColor(uint8_t *pR, uint8_t *pG, uint8_t *pB, int iOutFormat)
{
    int gr;
    uint8_t r, g, b;
    
    r = *pR; g = *pG; b = *pB;
    gr = (b + r + g*2)>>2; // gray
    switch (iOutFormat)
    {
        case OPTION_BWR:
            // match the color to closest of black/white/red
            if (r > g && r > b) { // red is dominant
                if (gr < 100 && r < 80) {
                    // black
                    b = g = r = 0;
                } else {
                    if (r-b > 32 && r-g > 32) {
                        // is red really dominant?
                        b = g = 0; r = 0xff;
                    } else { // yellowish should be white
                        // no, use white instead of pink/yellow
                        b = g = r = 0xff;
                    }
                }
            } else { // check for white/black
                if (gr >= 100) {
                    b = g = r = 0xff;
                } else {
                    // black
                    b = g = r = 0;
                }
            }
            break;
        case OPTION_BWY:
            // match the color to closest of black/white/yellow
            if (r > b && g > b) { // yellow is dominant?
                if (gr < 100 && r < 80) {
                    r = g = b = 0;
                } else {
                    if (r - b > 32 && g - b > 32) {
                        // is yellow really dominant?
                        r = g = 0xff; b = 0;
                    } else { // yellowish should be white
                        // no, use white instead of pink/yellow
                        r = g = b = 0xff;
                    }
                }
            } else { // check for white/black
                if (gr >= 100) {
                    r = g = b = 0xff; // white
                } else {
                    r = g = b = 0;
                }
            }
            break;
        case OPTION_BWYR:
            // match the color to closest of black/white/yellow/red
            if (r > b || g > b) { // red or yellow is dominant
                if (gr < 90 || (r < 80 && g < 80)) {
                    r = g = b = 0; // black
                } else {
                    if (r-b > 32 && r-g > 70) {
                        // is red really dominant?
                        r = 0xff; g = b = 0; // red
                    } else if (r-b > 32 && g-b > 32) {
                        // yellow
                        r = g = 0xff; b = 0;
                    } else {
                        r = g = b = 0xff; // gray/white
                    }
                }
            } else { // check for white/black
                if (gr >= 100) {
                    r = g = b = 0xff; // white
                } else {
                    r = g = b = 0; // black
                }
            }
            break;
    } // switch on output display format
    *pR = r; *pG = g; *pB = b; // store
} /* MatchBestColor() */
//
// Dither the image to the destination color scheme
//
uint8_t * DitherBMP(uint8_t *pPixels, int iWidth, int iHeight, int *pBpp, int iOutFormat)
{
    int x, y, xmask, iDestPitch;
    int32_t cNew, lFErr, lFErrR, lFErrG, lFErrB, v=0, h;
    int32_t e1,e2,e3,e4;
    uint8_t cOut; // forward errors for gray
    uint8_t *pDest, *errors, *pErrors=NULL, *d; // destination 8bpp image
    uint8_t pixelmask=0, shift=0;
    uint8_t ucTemp[1024];
    int iSrcPitch, iBpp = *pBpp;
    int32_t iErrors[1024*3];
    errors = ucTemp; // plenty of space here for the bitmaps we'll generate
    memset(ucTemp, 0, sizeof(ucTemp));
    iDestPitch = (iWidth+7)/8;
    iDestPitch = (iDestPitch+3) & 0xfffc; // our other code assumes this is a BMP image src
    iSrcPitch = ((iWidth * iBpp)+7)/8;
    iSrcPitch = (iSrcPitch + 3) & 0xfffc; // Windows BMP has dword aligned pitch
    pixelmask = 0x80;
    shift = 1;
    xmask = 7;
    if (iOutFormat == OPTION_BW) { // Black/white version
        pDest = (uint8_t *)malloc(iDestPitch * iHeight);
        for (y=0; y<iHeight; y++)
        {
            d = &pDest[y * iDestPitch];
            pErrors = &errors[1]; // point to second pixel to avoid boundary check
            lFErr = 0;
            cOut = 0;
            for (x=0; x<iWidth; x++)
            {
                cNew = GetGrayPixel8(x, y, pPixels, iSrcPitch, iBpp); // get grayscale uint8_t pixel
                cNew = (cNew * 2)/3; // make white end of spectrum less "blown out"
                // add forward error
                cNew += lFErr;
                if (cNew > 255) cNew = 255;     // clip to uint8_t
                cOut >>= shift;                 // pack new pixels into a byte
                cOut |= (cNew & 0x80);    // keep top N bits
                if ((x & xmask) == xmask)       // store it when the byte is full
                {
                    *d++ = cOut;
                    cOut = 0;
                }
                // calculate the Floyd-Steinberg error for this pixel
                v = cNew - (cNew & pixelmask); // new error for N-bit gray output (always positive)
                h = v >> 1;
                e1 = (7*h)>>3;  // 7/16
                e2 = h - e1;  // 1/16
                e3 = (5*h) >> 3;   // 5/16
                e4 = h - e3;  // 3/16
                // distribute error to neighbors
                lFErr = e1 + pErrors[1];
                pErrors[1] = (uint8_t)e2;
                pErrors[0] += e3;
                pErrors[-1] += e4;
                pErrors++;
            } // for x
            if (iWidth & 7) {
                cOut <<= (8-(x & 7));
                *d++ = ~cOut; // store partial byte
            }
        } // for y
        *pBpp = 1; // now it's 1-bit per pixel
        return pDest;
    } else { // black/white/red/yellow
        int32_t *pErr, iDelta;
        memset(iErrors, 0, sizeof(iErrors));
        iDelta = (iBpp == 32) ? 4:3; // bytes per pixel
    // Do the dithering in-place
    for (y=0; y<iHeight; y++)
    {
        uint8_t *s, r, g, b, r1, g1, b1;
        s = &pPixels[y * iSrcPitch];
        pErr = &iErrors[3]; // point to second pixel to avoid boundary check
        lFErrR = lFErrG = lFErrB = 0;
        for (x=0; x<iWidth; x++)
        {
            r = s[2]; g = s[1]; b = s[0]; // read a color pixel
            lFErr = r + lFErrR;
            if (lFErr < 0) lFErr = 0;
            else if (lFErr > 255) lFErr = 255;
            r1 = lFErr;
            lFErr = g + lFErrG;
            if (lFErr < 0) lFErr = 0;
            else if (lFErr > 255) lFErr = 255;
            g1 = lFErr;
            lFErr = b + lFErrB;
            if (lFErr < 0) lFErr = 0;
            else if (lFErr > 255) lFErr = 255;
            b1 = lFErr;
            MatchBestColor(&r1, &g1, &b1, iOutFormat);
            // accumulate the R/G/B error of the matched color vs original
            // calculate the Floyd-Steinberg error for this pixel
            v = (int32_t)(r - r1); // new error for red
            h = v >> 1;
            e1 = (7*h)>>3;  // 7/16
            e2 = h - e1;  // 1/16
            e3 = (5*h) >> 3;   // 5/16
            e4 = h - e3;  // 3/16
            // distribute error to neighbors
            lFErrR = e1 + pErr[3];
            pErr[3] = e2;
            pErr[0] += e3;
            pErr[-3] += e4;
            v = (int32_t)(g - g1); // new error for green
            h = v >> 1;
            e1 = (7*h)>>3;  // 7/16
            e2 = h - e1;  // 1/16
            e3 = (5*h) >> 3;   // 5/16
            e4 = h - e3;  // 3/16
            // distribute error to neighbors
            lFErrG = e1 + pErr[4];
            pErr[4] = e2;
            pErr[1] += e3;
            pErr[-2] += e4;
            v = (int32_t)(b - b1); // new error for blue
            h = v >> 1;
            e1 = (7*h)>>3;  // 7/16
            e2 = h - e1;  // 1/16
            e3 = (5*h) >> 3;   // 5/16
            e4 = h - e3;  // 3/16
            // distribute error to neighbors
            lFErrB = e1 + pErr[5];
            pErr[5] = e2;
            pErr[2] += e3;
            pErr[-1] += e4;
            pErr += 3;
            // Store back the dithered pixel (in place)
            s[2] = r1; s[1] = g1; s[0] = b1;
            s += iDelta;
        } // for x
        } // for y
        return NULL; // signals to keep the original bitmap
    } // BWR
     return NULL;
} /* DitherBMP() */

int JPEGDraw(JPEGDRAW *pDraw)
{
    // defaults to RGB565 little endian
    int x, y;
    // convert each pixel to RGB888 and store in our image buffer
    uint8_t r, g, b, *s8, *d, *pDst = (uint8_t *)pDraw->pUser;
    uint16_t u16, *s;
    for (y=0; y<pDraw->iHeight; y++) {
        if (pDraw->iBpp == 16) {
            d = &pDst[3 * ((pDraw->y + y) * iWidth) + (pDraw->x * 3)];
            s = &pDraw->pPixels[y * pDraw->iWidth];
            for (x=0; x<pDraw->iWidth; x++) {
                u16 = *s++;
                r = (u16 >> 11); // top 5 bits = R
                r = (r << 3) | (r >> 2);
                g = (u16 >> 5) & 0x3f; // middle 6 bits = G
                g = (g << 2) | (g >> 4);
                b = (u16 & 0x1f); // bottom 5 bits = B
                b = (b << 3) | (b >> 2);
                d[0] = b; d[1] = g; d[2] = r;
                d += 3; // 24-bpp output
            } // for x
        } else { // 8bpp
            s8 = (uint8_t *)pDraw->pPixels;
            s8 += (y * pDraw->iWidth);
            d = &pDst[((pDraw->y + y) * iWidth) + pDraw->x]; // must be 8bpp
            memcpy(d, s8, pDraw->iWidth);
        }
    } // for y
    return 1; // returning true (1) tells JPEGDEC to continue decoding. Returning false (0) would quit decoding immediately.
} /* JPEGDraw() */

//
// Decode a JPEG image
//
uint8_t * ReadJPEG(uint8_t *pData, int iSize, int *pWidth, int *pHeight, int *pBpp)
{
    int rc;
    uint8_t *pImage = NULL;
    JPEGIMAGE jpg;
    
    rc = JPEG_openRAM(&jpg, pData, iSize, JPEGDraw);
    if (rc) {
        *pBpp = jpg.ucBpp;
        *pWidth = jpg.iWidth;
        *pHeight = jpg.iHeight;
        if (jpg.ucBpp == 8) {
            // create a fake grayscale palette
            for (int i=0; i<256; i++) {
                ucRed[i] = i;
                ucGreen[i] = i;
                ucBlue[i] = i;
            }
            jpg.ucPixelType = EIGHT_BIT_GRAYSCALE;
            jpg.pUser = malloc(jpg.iWidth * jpg.iHeight);
        } else {
            jpg.pUser = malloc(jpg.iWidth * jpg.iHeight * 3);
        }
        rc = JPEG_decode(&jpg, 0, 0, 0); // full size decode
    }
    return jpg.pUser;
} /* ReadJPEG() */
//
// Main program entry point
//
int main(int argc, char *argv[])
{
    int iSize;
    int iOffBits;
    int iNameParam = 1;
    int iRotation = 0;
    int iOption = OPTION_BW; // default
    int bMirror = 0, bFlipv = 0, bInvert = 0;
    int bDither = 0;
    unsigned char *p;
    char szLeaf[256];
    char szOutName[256];
    
    if (argc < 3 || argc > 5)
    {
        printf("epd_image Copyright (c) 2023 BitBank Software, Inc.\n");
        printf("Written by Larry Bank\n\n");
        printf("Usage: epd_image <options> <infile> <outfile>\n");
        printf("example:\n\n");
        printf("epd_image --BW ./test.bmp test.h\n");
        printf("valid options (defaults to BW, no rotation):\n");
        printf("BW = create output for black/white displays\n");
        printf("BWR = create output for black/white/red displays\n");
        printf("BWY = create output for black/white/yellow displays\n");
        printf("BWYR = create output for black/white/yellow/red displays\n");
        printf("4GRAY = create output for 2-bit grayscale displays\n");
        printf("DITHER = use Floyd Steinberg dithering\n");
        printf("ROTATE <degrees> = rotate the image clockwise by N degrees\n");
        printf("MIRROR = mirror the image horizontally\n");
        printf("FLIPV = flip the image vertically\n");
        printf("INVERT = invert the colors\n");

        return 0; // no filename passed
    }
    while (argv[iNameParam][0] == '-') { // check options
        if (strcmp(argv[iNameParam], "ROTATE") == 0) {
            iRotation = atoi(&argv[iNameParam][2]);
            if (iRotation % 90 != 0) {
                printf("Rotation angle must be 0, 90, 180 or 270\n");
                return -1;
            }
        } else if (strcmp(argv[iNameParam], "--MIRROR") == 0) {
            bMirror = 1;
        } else if (strcmp(argv[iNameParam], "--FLIPV") == 0) {
            bFlipv = 1;
        } else if (strcmp(argv[iNameParam], "--INVERT") == 0) {
            bInvert = 1;
        } else if (strcmp(argv[iNameParam], "--DITHER") == 0) {
            bDither = 1;
        } else {
            while (iOption < OPTION_COUNT && strcmp(&argv[iNameParam][2], szOptions[iOption]) != 0) {
                iOption++;
            }
            if (iOption == OPTION_COUNT) { // unrecognized option
                printf("Invalid option: %s\n", argv[iNameParam]);
                return -1;
            }
        }
       iNameParam++;
    }
    ihandle = fopen(argv[iNameParam],"rb"); // open input file
    if (ihandle == NULL)
    {
        fprintf(stderr, "Unable to open file: %s\n", argv[iNameParam]);
        return -1; // bad filename passed
    }
    
    fseek(ihandle, 0L, SEEK_END); // get the file size
    iSize = (int)ftell(ihandle);
    fseek(ihandle, 0, SEEK_SET);
    p = (unsigned char *)malloc(iSize); // read it into RAM
    fread(p, 1, iSize, ihandle);
    fclose(ihandle);
    if (p[0] == 'B' && p[1] == 'M') {
        if (ReadBMP(p, &iOffBits, &iWidth, &iHeight, &iBpp) == 0) {
            printf("Invalid BMP file, exiting...\n");
            return -1;
        }
    } else if (p[0] == 0xff && p[1] == 0xd8) {
        uint8_t *pOut = ReadJPEG(p, iSize, &iWidth, &iHeight, &iBpp);
        if (!pOut) {
            printf("Invalid JPEG file, exiting...\n");
            return -1;
        }
        free(p);
        p = pOut;
        iOffBits = 0;
    } else {
        printf("Unrecognized file format. For now, only BMP and JPEG are supported\n");
        return -1;
    }
    if (iHeight > 0) FlipBMP(&p[iOffBits], iWidth, iHeight, iBpp); // positive means bottom-up
    else iHeight = -iHeight; // negative means top-down
    if (bMirror) {
        MirrorBMP(&p[iOffBits], iWidth, iHeight, iBpp);
    }
    if (bFlipv) {
        FlipBMP(&p[iOffBits], iWidth, iHeight, iBpp);
    }
    if (bInvert) {
        for (int i=0; i<(iSize - iOffBits); i++) {
            p[iOffBits + i] = ~p[iOffBits + i];
        }
    }
    if (bDither) {
        if (iBpp < 24 && (iOption == OPTION_BWR || iOption == OPTION_BWY || iOption == OPTION_BWYR))
        {
            printf("Color dithering requires a full color (24/32-bit) source image\n");
            return -1;
        }
        uint8_t *pNew = DitherBMP(&p[iOffBits], iWidth, iHeight, &iBpp, iOption);
        if (pNew) { // the bitmap image was replaced
            free(p);
            p = pNew; // bitmap has been replaced
            iOffBits = 0; // image starts at beginning of buffer
        }
    }
    RotateImage(iRotation, &p[iOffBits], &iWidth, &iHeight, iBpp);
    GetLeafName(argv[iNameParam], szLeaf);
    if (argv[iNameParam+1][0] != SLASH_CHAR) { // need to form full name
       if (getcwd(szOutName, sizeof(szOutName))) {
	  int i;
	  i = (int)strlen(szOutName);
          szOutName[i] = SLASH_CHAR;
	  szOutName[i+1] = 0;
          strcat(szOutName, argv[iNameParam+1]);
       }
    } else {
       strcpy(szOutName, argv[iNameParam+1]);
    }
    ihandle = fopen(szOutName, "wb");
    if (ihandle == NULL) {
       printf("Error creating output file: %s\n", szOutName);
       return -1;
    }
    fprintf(ihandle, "//\n// Created with epd_image\n// https://github.com/bitbank2/epd_image\n");
    fprintf(ihandle, "//\n// %s\n//\n", szLeaf); // comment header with filename
    FixName(szLeaf); // remove unusable characters
    fprintf(ihandle, "// for non-Arduino builds...\n");
    fprintf(ihandle, "#ifndef PROGMEM\n#define PROGMEM\n#endif\n");
    switch (iOption) {
	    case OPTION_BW:
               MakeC_BW(p, iOffBits, iWidth, iHeight, iBpp, iSize, ihandle, szLeaf); // create the output data
	    break;
	    case OPTION_BWR:
	    case OPTION_BWY:
            MakeC_3CLR(p, iOffBits, iWidth, iHeight, iBpp, iSize, ihandle, szLeaf, iOption);
	    break;
        case OPTION_BWYR:
            MakeC_4CLR(p, iOffBits, iWidth, iHeight, iBpp, iSize, ihandle, szLeaf);
            break;
	    case OPTION_4GRAY:
	       MakeC_4GRAY(p, iOffBits, iWidth, iHeight, iBpp, iSize, ihandle, szLeaf);
	    break;
    } // switch
    fflush(ihandle);
    fclose(ihandle);
    free(p);
    return 0;
} /* main() */
//
// Make sure the name can be used in C/C++ as a variable
// replace invalid characters and make sure it starts with a letter
//
void FixName(char *name)
{
    char c, *d, *s, szTemp[256];
    int i, iLen;
    
    iLen = (int)strlen(name);
    d = szTemp;
    s = name;
    if (s[0] >= '0' && s[0] <= '9') // starts with a digit
        *d++ = '_'; // Insert an underscore
    for (i=0; i<iLen; i++)
    {
        c = *s++;
        // these characters can't be in a variable name
        if (c < ' ' || (c >= '!' && c < '0') || (c > 'Z' && c < 'a'))
            c = '_'; // convert all to an underscore
        *d++ = c;
    }
    *d++ = 0;
    strcpy(name, szTemp);
} /* FixName() */
//
// Trim off the leaf name from a fully
// formed file pathname
//
void GetLeafName(char *fname, char *leaf)
{
    int i, iLen;
    
    iLen = (int)strlen(fname);
    for (i=iLen-1; i>=0; i--)
    {
        if (fname[i] == '\\' || fname[i] == '/') // Windows or Linux
            break;
    }
    strcpy(leaf, &fname[i+1]);
    // remove the filename extension
    iLen = (int)strlen(leaf);
    for (i=iLen-1; i>=0; i--)
    {
        if (leaf[i] == '.')
        {
            leaf[i] = 0;
            break;
        }
    }
} /* GetLeafName() */

