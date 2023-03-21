//
// image_to_c - convert binary image files into c-compatible data tables
//
// Written by Larry Bank
// Copyright (c) 2020 BitBank Software, Inc.
// Change history
// 12/2/20 - Started the project
//
#include <stdint.h>
#include <string.h>
#include <stdio.h>
#include <stdlib.h>

enum
{
  OPTION_BW = 0,
  OPTION_BWR,
  OPTION_BWY,
  OPTION_4GRAY,
  OPTION_COUNT
};

const char *szOptions[] = {"BW", "BWR", "BWY", "4GRAY", NULL};
uint8_t ucBlue[256], ucGreen[256], ucRed[256]; // palette colors
#ifdef _WIN32
#define PILIO_SLASH_CHAR '\\'
#else
#define PILIO_SLASH_CHAR '/'
#endif

typedef unsigned char BOOL;

FILE * ihandle;
void MakeC(unsigned char *, int, int);
void GetLeafName(char *fname, char *leaf);
void FixName(char *name);

int ParseNumber(unsigned char *buf, int *iOff, int iLength)
{
    int i, iOffset;
    
    i = 0;
    iOffset = *iOff;
    
    while (iOffset < iLength && buf[iOffset] >= '0' && buf[iOffset] <= '9')
    {
        i *= 10;
        i += (int)(buf[iOffset++] - '0');
    }
    *iOff = iOffset+1; /* Skip ending char */
    return i;
    
} /* ParseNumber() */
//
// Parse the BMP file info and prepare for processing the pixels
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
    if (ucCompression != 0) // unsupported feature
        return 0;
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
// Main program entry point
//
int main(int argc, char *argv[])
{
    int iSize;
    int iOffBits, iWidth, iHeight, iBpp;
    int iNameParam = 1;
    int iOption = OPTION_BW; // default
    unsigned char *p;
    char szLeaf[256];
    
    if (argc < 2 || argc > 3)
    {
        printf("epd_image Copyright (c) 2023 BitBank Software, Inc.\n");
        printf("Written by Larry Bank\n\n");
        printf("Usage: epd_image <option> <filename>\n");
        printf("output is written to stdout\n");
        printf("example:\n\n");
        printf("epd_image --BW ./test.bmp > test.h\n");
	printf("valid options (defaults to BW):\n");
	printf("BW = create output for black/white displays\n");
	printf("BWR = create output for black/white/red displays\n");
	printf("BWY = create output for black/white/yellow displays\n");
	printf("4GRAY = create output for 2-bit grayscale displays\n");
        return 0; // no filename passed
    }
    if (argv[1][0] == '-') { // check option

       while (iOption < OPTION_COUNT && strcmp(&argv[1][2], szOptions[iOption]) != 0) {
          iOption++;
       }
       if (iOption == OPTION_COUNT) { // unrecognized option
	       printf("Invalid option: %s\n", argv[1]);
	       return -1;
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
    if (ReadBMP(p, &iOffBits, &iWidth, &iHeight, &iBpp) == 0) {
	    printf("Invalid BMP file, exiting...\n");
	    return -1;
    } else {
	    printf("BMP loaded: width %d, height %d, bpp %d\n", iWidth, iHeight, iBpp);
    }
    GetLeafName(argv[1], szLeaf);
    printf("// Created with epd_image\n// https://github.com/bitbank2/epd_image\n");
    printf("//\n// %s\n//\n", szLeaf); // comment header with filename
    FixName(szLeaf); // remove unusable characters
    printf("// for non-Arduino builds...\n");
    printf("#ifndef PROGMEM\n#define PROGMEM\n#endif\n");
    printf("const uint8_t %s[] PROGMEM = {\n", szLeaf); // start of data array
    //MakeC(p, iData, iSize == iData); // create the output data
    free(p);
    printf("};\n"); // final closing brace
    return 0;
} /* main() */
//
// Generate C hex characters from each byte of file data
//
void MakeC(unsigned char *p, int iLen, int bLast)
{
    int i, j, iCount;
    char szTemp[256], szOut[256];
    
    iCount = 0;
    for (i=0; i<iLen>>4; i++) // do lines of 16 bytes
    {
        strcpy(szOut, "\t");
        for (j=0; j<16; j++)
        {
            if (iCount == iLen-1 && bLast) // last one, skip the comma
                sprintf(szTemp, "0x%02x", p[(i*16)+j]);
            else
                sprintf(szTemp, "0x%02x,", p[(i*16)+j]);
            strcat(szOut, szTemp);
            iCount++;
        }
        if (!bLast || iCount != iLen)
            strcat(szOut, "\n");
        printf("%s",szOut);
    }
    p += (iLen & 0xfff0); // point to last section
    if (iLen & 0xf) // any remaining characters?
    {
        strcpy(szOut, "\t");
        for (j=0; j<(iLen & 0xf); j++)
        {
            if (iCount == iLen-1 && bLast)
                sprintf(szTemp, "0x%02x", p[j]);
            else
                sprintf(szTemp, "0x%02x,", p[j]);
            strcat(szOut, szTemp);
            iCount++;
        }
        if (!bLast)
            strcat(szOut, "\n");
        printf("%s",szOut);
    }
} /* MakeC() */
//
// Make sure the name can be used in C/C++ as a variable
// replace invalid characters and make sure it starts with a letter
//
void FixName(char *name)
{
    char c, *d, *s, szTemp[256];
    int i, iLen;
    
    iLen = strlen(name);
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
    
    iLen = strlen(fname);
    for (i=iLen-1; i>=0; i--)
    {
        if (fname[i] == '\\' || fname[i] == '/') // Windows or Linux
            break;
    }
    strcpy(leaf, &fname[i+1]);
    // remove the filename extension
    iLen = strlen(leaf);
    for (i=iLen-1; i>=0; i--)
    {
        if (leaf[i] == '.')
        {
            leaf[i] = 0;
            break;
        }
    }
} /* GetLeafName() */

