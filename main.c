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
#include <unistd.h>

enum
{
  OPTION_BW = 0,
  OPTION_BWR,
  OPTION_BWY,
  OPTION_4GRAY,
  OPTION_COUNT
};
// How many hex bytes are written per line of output
#define BYTES_PER_LINE 16
const char *szOptions[] = {"BW", "BWR", "BWY", "4GRAY", NULL};
uint8_t ucBlue[256], ucGreen[256], ucRed[256]; // palette colors
#ifdef _WIN32
#define SLASH_CHAR '\\'
#else
#define SLASH_CHAR '/'
#endif

typedef unsigned char BOOL;

FILE * ihandle;
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

void MakeC_BW(uint8_t *pSrc, int iOffBits, int iWidth, int iHeight, int iSize, FILE *ohandle, char *szLeaf)
{
	int iSrcPitch, iPitch, iDelta;
        int iIn, iTotal, iLine, iOut;
	uint8_t *s = &pSrc[iOffBits];

	iSrcPitch = iPitch = ((iBpp * iWidth) + 7)/8;
	iSrcPitch = (iSrcPitch + 3) & 0xfffc; // Windows BMP lines are dword aligned
	iDelta = iSrcPitch - iPitch;
	iTotal = iPitch * iHeight; // how many bytes we're creating
	// show pitch
	fprintf(ohandle, "// Image size: width %d, height %d\n", iWidth, iHeight);
	fprintf(ohandle, "// %d bytes per line\n", iPitch);
	fprintf(ohandle, "// %d bytes per plane\n", iTotal);
        fprintf(ohandle, "const uint8_t %s_0[] PROGMEM = {\n", szLeaf); // start of data array (plane 0)
                iOut = iLine = iIn = 0;
                for (int i=0; i<iTotal; i++) {
                    fprintf(ohandle, "0x%02x", (~s[iIn++]) & 0xff); // first plane is inverted
		    if (iLine == BYTES_PER_LINE-1 || i == iTotal-1) {
			    fprintf(ohandle, "\n");
                            iLine = -1;
	            } else {
			    fprintf(ohandle, ",");
		    }
		    iLine++;
		    iOut++;
		    if (iOut == iPitch) {
			    iIn += iDelta;
			    iOut = 0;
		    }
		}
} /* MakeC_BW() */
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
    char szOutName[256];
    
    if (argc < 3 || argc > 4)
    {
        printf("epd_image Copyright (c) 2023 BitBank Software, Inc.\n");
        printf("Written by Larry Bank\n\n");
        printf("Usage: epd_image <option> <infile> <outfile>\n");
        printf("example:\n\n");
        printf("epd_image --BW ./test.bmp test.h\n");
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
    }
    if (iOption == OPTION_BW && iBpp != 1) {
         printf("Input image for OPTION_BW must be 1-bit per pixel\n");
	 printf("aborting...\n");
	 return -1;
    }
    if (iHeight > 0) FlipBMP(&p[iOffBits], iWidth, iHeight, iBpp); // positive means bottom-up
    else iHeight = -iHeight; // negative means top-down
    GetLeafName(argv[iNameParam], szLeaf);
    if (argv[iNameParam+1][0] != SLASH_CHAR) { // need to form full name
       if (getcwd(szOutName, sizeof(szOutName))) {
	  int i;
	  i = strlen(szOutName);
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
	    break;
	    case OPTION_BWY:
	    break;
	    case OPTION_4GRAY:
	       MakeC_4GRAY(p, iOffBits, iWidth, iHeight, iBpp, iSize, ihandle, szLeaf);
	    break;
    } // switch	   
    fprintf(ihandle, "};\n"); // final closing brace
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

