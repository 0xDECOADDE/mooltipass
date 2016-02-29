/* CDDL HEADER START
 *
 * The contents of this file are subject to the terms of the
 * Common Development and Distribution License (the "License").
 * You may not use this file except in compliance with the License.
 *
 * You can obtain a copy of the license at src/license_cddl-1.0.txt
 * or http://www.opensolaris.org/os/licensing.
 * See the License for the specific language governing permissions
 * and limitations under the License.
 *
 * When distributing Covered Code, include this CDDL HEADER in each
 * file and include the License file at src/license_cddl-1.0.txt
 * If applicable, add the following below this CDDL HEADER, with the
 * fields enclosed by brackets "[]" replaced with your own identifying
 * information: Portions Copyright [yyyy] [name of copyright owner]
 *
 * CDDL HEADER END
 */
/*! \file   oledmini.c
 *  \brief  Mooltipass SSD1305 128x32x1 OLED display library
 *  Created: 15/2/2016
 *  Copyright [2016] [Mathieu Stephan]
 */
#include <avr/pgmspace.h>
#include <string.h>
#include <string.h>
#include <stdio.h>
#include <ctype.h>
#include "logic_fwflash_storage.h"
#include "bitstreammini.h"
#include "timer_manager.h"
#include "flash_mem.h"
#include "oledmini.h"
#include "defines.h"
#include "fonts.h"
#include "spi.h"
#include "usb.h"
/***********************************************************/
/*  This file is only used for the Mooltipass mini version */
#if defined(MINI_VERSION)

// Frame buffer, first byte is X0 Y7 (MSB) to X0 Y0 (LSB)
uint8_t miniOledFrameBuffer[SSD1305_OLED_WIDTH*SSD1305_OLED_HEIGHT/SSD1305_PAGE_HEIGHT];
// Current y offset in buffer
uint8_t miniOledBufferYOffset;
// Boolean to know if OLED on
uint8_t miniOledIsOn = FALSE;
// Used to know which address to request in the SPI flash
static flashFont_t* miniOledFontp = (flashFont_t *)0;
// Current font used by our display
static fontHeader_t miniOledCurrentFont;
// Address of current font in SPI flash
uint16_t miniOledFontAddr;
// Current font index in SPI flash
uint8_t miniOledFontId = 255;
// Current x for text to write
uint8_t miniOledTextCurX = 0;
// Current y for text to write
uint8_t miniOledTextCurY = 0;

// OLED initialization sequence
static const uint8_t mini_oled_init[] __attribute__((__progmem__)) = 
{
    1,  SSD1305_CMD_DISPLAY_OFF,                                        // Display Off
    2,  SSD1305_CMD_SET_DISPLAY_CLOCK_DIVIDE,   0x10,                   // Display divide ratio of 0, Oscillator frequency of around 300kHz
    2,  SSD1305_CMD_SET_MULTIPLEX_RATIO,        0x1F,                   // Multiplex ratio of 32
    2,  SSD1305_CMD_SET_DISPLAY_OFFSET,         0x00,                   // Display offset 0
    1,  SSD1305_CMD_SET_DISPLAY_START_LINE,                             // Display start line 0
    2,  SSD1305_CMD_SET_MASTER_CONFIGURATION,   0x8E,                   // Select external Vcc supply
    2,  SSD1305_CMD_SET_AREA_COLOR_MODE,        0x05,                   // Set low power display mode
    1,  SSD1305_CMD_SET_SEGMENT_REMAP_COL_131,                          // Column address 131 is mapped to SEG0 
    1,  SSD1305_CMD_COM_OUTPUT_REVERSED,                                // Remapped mode. Scan from COM[N~1] to COM0
    2,  SSD1305_CMD_SET_MEM_ADDRESSING_MODE,    0x00,                   // Horizontal addressing mode
    2,  SSD1305_CMD_SET_COM_PINS_CONF,          0x12,                   // Alternative COM pin configuration
    5,  SSD1305_CMD_SET_LUT,                    0x3F,0x3F,0x3F,0x3F,    // Set Look up Table
    2,  SSD1305_CMD_SET_CONTRAST_CURRENT,       SSD1305_OLED_CONTRAST,  // Set current control (contrast)
    2,  SSD1305_CMD_SET_PRECHARGE_PERIOD,       0xD2,                   // Precharge period
    2,  SSD1305_CMD_SET_VCOMH_VOLTAGE,          0x08,                   // VCOM deselect level (around 0.5Vcc)
    1,  SSD1305_CMD_ENTIRE_DISPLAY_NORMAL,                              // Entire display in normal mode
    1,  SSD1305_CMD_ENTIRE_DISPLAY_NREVERSED,                           // Entire display not reversed
};


/*! \fn     miniOledWriteCommand(uint8_t* data, uint8_t nbBytes)
 *  \brief  Write a command or register address to the display
 *  \param  data    Pointer to the data to be written
 *  \param  nbBytes Number of bytes to be written
 */
void miniOledWriteCommand(uint8_t* data, uint8_t nbBytes)
{
    PORT_OLED_SS &= ~(1 << PORTID_OLED_SS);
    PORT_OLED_DnC &= ~(1 << PORTID_OLED_DnC);
    while(nbBytes--)
    {
        spiUsartTransfer(*data);
        data++;
    }
    PORT_OLED_SS |= (1 << PORTID_OLED_SS);
}

/*! \fn     miniOledWriteSimpleCommand(uint8_t reg)
 *  \brief  Write a command or register address to the display
 *  \param  reg     the command or register to write
 */
void miniOledWriteSimpleCommand(uint8_t reg)
{
    PORT_OLED_SS &= ~(1 << PORTID_OLED_SS);
    PORT_OLED_DnC &= ~(1 << PORTID_OLED_DnC);
    spiUsartTransfer(reg);
    PORT_OLED_SS |= (1 << PORTID_OLED_SS);
}

/*! \fn     miniOledWriteData(uint8_t* data, uint16_t nbBytes)
 *  \brief  Write a command or register address to the display
 *  \param  data    Pointer to the data to be written
 *  \param  nbBytes Number of bytes to be written
 */
void miniOledWriteData(uint8_t* data, uint16_t nbBytes)
{
    spiUsartDummyWrite();
    PORT_OLED_SS &= ~(1 << PORTID_OLED_SS);
    PORT_OLED_DnC |= (1 << PORTID_OLED_DnC);
    while(nbBytes--)
    {
        spiUsartSendTransfer(*data);
        data++;
    }
    spiUsartWaitEndSendTransfer();
    PORT_OLED_SS |= (1 << PORTID_OLED_SS);
}

/*! \fn     miniOledSetColumnAddress(uint8_t columnStart, uint8_t columnEnd)
 *  \brief  Setup column start and end address
 *  \param  columnStart   Start column
 *  \param  columnEnd     End column
 */
void miniOledSetColumnAddress(uint8_t columnStart, uint8_t columnEnd)
{
    uint8_t data[3] = {SSD1305_CMD_SET_COLUMN_ADDR, columnStart + SSD1305_X_OFFSET, columnEnd + SSD1305_X_OFFSET};
    miniOledWriteCommand(data, sizeof(data));
}

/*! \fn     miniOledSetPageAddress(uint8_t pageStart, uint8_t pageEnd)
 *  \brief  Setup page start and end address
 *  \param  pageStart   Start page
 *  \param  pageEnd     End page
 */
void miniOledSetPageAddress(uint8_t pageStart, uint8_t pageEnd)
{
    uint8_t data[3] = {SSD1305_CMD_SET_PAGE_ADDR, pageStart, pageEnd};
    miniOledWriteCommand(data, sizeof(data));
}

/*! \fn     miniOledSetWindow(uint8_t columnStart, uint8_t columnEnd, uint8_t pageStart, uint8_t pageEnd)
 *  \brief  Set the window we want to update in the screen
 *  \param  columnStart Start column
 *  \param  columnEnd   End column
 *  \param  pageStart   Start page
 *  \param  pageEnd     End page
 */
void miniOledSetWindow(uint8_t columnStart, uint8_t columnEnd, uint8_t pageStart, uint8_t pageEnd)
{
    miniOledSetColumnAddress(columnStart, columnEnd);
    miniOledSetPageAddress(pageStart, pageEnd);
}

/*! \fn     miniOledFlushBufferContents(uint8_t xstart, uint8_t xend, uint8_t ystart, uint8_t yend)
 *  \brief  Flush buffer contents to the display
 *  \param  xstart  From which x to start flushing
 *  \param  xend    end x position
 *  \param  ystart  From which y to start flushing
 *  \param  yend    end y position
 */
void miniOledFlushBufferContents(uint8_t xstart, uint8_t xend, uint8_t ystart, uint8_t yend)
{
    // Compute page start & page end
    uint8_t page_start = ystart >> SSD1305_PAGE_HEIGHT_BIT_SHIFT;
    uint8_t page_end = yend >> SSD1305_PAGE_HEIGHT_BIT_SHIFT;
    
    // Set the correct display window
    miniOledSetWindow(xstart, xend, page_start, page_end);
    
    // Send data, we go low level to have better speed
    spiUsartDummyWrite();
    PORT_OLED_SS &= ~(1 << PORTID_OLED_SS);
    PORT_OLED_DnC |= (1 << PORTID_OLED_DnC);
    for (uint8_t page = page_start; page <= page_end; page++)
    {
        uint16_t buffer_shift = ((uint16_t)page) << SSD1305_WIDTH_BIT_SHIFT;
        for (uint8_t x = xstart; x <= xend; x++)
        {
            //spiUsartTransfer(miniOledFrameBuffer[buffer_shift + x]);
            spiUsartSendTransfer(miniOledFrameBuffer[buffer_shift + x]);
        }
    }
    spiUsartWaitEndSendTransfer();
    PORT_OLED_SS |= (1 << PORTID_OLED_SS); 
}

/*! \fn     miniOledFlushEntireBufferToDisplay(void)
 *  \brief  Flush buffer contents to the display
 *  \notes  timed at 1.6ms!
 */
void miniOledFlushEntireBufferToDisplay(void)
{
    // Set display window
    uint8_t data[3] = {SSD1305_CMD_SET_COLUMN_ADDR, SSD1305_X_OFFSET, SSD1305_OLED_WIDTH + SSD1305_X_OFFSET - 1};
    miniOledWriteCommand(data, sizeof(data));
    uint8_t data2[3] = {SSD1305_CMD_SET_PAGE_ADDR, 0, (SSD1305_OLED_HEIGHT/SSD1305_PAGE_HEIGHT)-1};
    miniOledWriteCommand(data2, sizeof(data2));
    
    // Send data
    miniOledWriteData(miniOledFrameBuffer, sizeof(miniOledFrameBuffer));
}

/*! \fn     miniOledOn(void)
 *  \brief  Turn the display on
 */
void miniOledOn(void)
{
    PORT_OLED_POW &= ~(1 << PORTID_OLED_POW);
    timerBased130MsDelay();
    miniOledWriteSimpleCommand(SSD1305_CMD_DISPLAY_NORMAL_MODE);
    miniOledIsOn = TRUE;
}

/*! \fn     miniOledIsScreenOn(void)
 *  \brief  See if the the OLED display is on
 *  \return The boolean
 */
RET_TYPE miniOledIsScreenOn(void)
{
    return miniOledIsOn;
}

/*! \fn     miniOledInit(void)
 *  \brief  Initialize the OLED hardware and get it ready for use.
 */
void miniOledInit(void)
{
    uint8_t dataBuffer[10];
    uint8_t dataSize, i;
    
    // Parse initialization sequence
    for (uint8_t ind=0; ind<sizeof(mini_oled_init);) 
    {
        i = 0;
        dataSize = pgm_read_byte(&mini_oled_init[ind++]);
        while (dataSize--) 
        {
            dataBuffer[i++] = pgm_read_byte(&mini_oled_init[ind++]);
        }
        miniOledWriteCommand(dataBuffer, i);
    }

    // Switch on an empty screen
    miniOledClear();
    miniOledFlushEntireBufferToDisplay();
    miniOledOn();
}

/*! \fn     miniOledReset(void)
 *  \brief  Reset the OLED display
 */
void miniOledReset(void)
{
    PORT_OLED_nR &= ~(1 << PORTID_OLED_nR);
    timerBased130MsDelay();
    PORT_OLED_nR |= (1 << PORTID_OLED_nR);
    timerBasedDelayMs(10);
}

/*! \fn     miniOledInitIOs(void)
 *  \brief  Initialize OLED controller ports
 */
void miniOledInitIOs(void)
{
    /* Setup OLED slave select as output, high */
    DDR_OLED_SS |= (1 << PORTID_OLED_SS);
    PORT_OLED_SS |= (1 << PORTID_OLED_SS);
    
    /*  Setup OLED Data/nCommand as output */
    DDR_OLED_DnC |= (1 << PORTID_OLED_DnC);
    
    /* Setup OLED Reset */
    DDR_OLED_nR |= (1 << PORTID_OLED_nR);
    miniOledReset();
    
    /* Setup Oled power, high */
    DDR_OLED_POW |= (1 << PORTID_OLED_POW);
    PORT_OLED_POW |= (1 << PORTID_OLED_POW);
}

/*! \fn     stockOledBegin(uint8_t font)
 *  \brief  Initialize the OLED controller and prep the display
 *  \param  font    UID of the font to use
 */
void miniOledBegin(uint8_t font)
{
    miniOledBufferYOffset = 0;
    miniOledSetFont(font);
    miniOledInit();
}

/*! \fn     miniOledWriteInactiveBuffer(void)
 *  \brief  Set write buffer to be the inactive (offscreen) buffer
 */
void miniOledWriteInactiveBuffer(void)
{
    //oled_writeBuffer = !oled_displayBuffer;
    //oled_writeOffset = OLED_HEIGHT;
}

/*! \fn     miniOledWriteActiveBuffer(void)
 *  \brief  Set write buffer to the be the active (onscreen) buffer
 */
void miniOledWriteActiveBuffer(void)
{
    //oled_writeBuffer = oled_displayBuffer;
    //oled_writeOffset = 0;
}

/*! \fn     miniOledDisplayOtherBuffer(void)
 *  \brief  Display the other buffer on the screen
 */
void miniOledDisplayOtherBuffer(void)
{
    //oled_writeBuffer = oled_displayBuffer;
    //oled_writeOffset = 0;
}

/*! \fn     miniOledDrawRectangle(uint8_t x, uint8_t y, uint8_t width, uint8_t height)
 *  \brief  Draw a rectangle on the screen
 *  \param  x       X position
 *  \param  y       Y position
 *  \param  width   width
 *  \param  height  height
 *  \param  full    boolean for color or not
 */
void miniOledDrawRectangle(uint8_t x, uint8_t y, uint8_t width, uint8_t height, uint8_t full)
{
    // Compute page start & page end
    uint8_t page_start = y >> SSD1305_PAGE_HEIGHT_BIT_SHIFT;
    uint8_t page_end = (y + height - 1) >> SSD1305_PAGE_HEIGHT_BIT_SHIFT;
    
    // Compute mask settings
    uint8_t f_bitshift_mask[] = {0x01, 0x03, 0x07, 0x0F, 0x1F, 0x3F, 0x7F, 0xFF};
    uint8_t f_bitshift = (y + height - 1) & 0x07;
    uint8_t l_bitshift_mask[] = {0xFF, 0xFE, 0xFC, 0xF8, 0xF0, 0xE0, 0xC0, 0x80};
    uint8_t l_bitshift = y & 0x07;
    
    for(uint8_t page = page_start; page <= page_end; page++)
    {
        uint16_t buffer_shift = ((uint16_t)page) << SSD1305_WIDTH_BIT_SHIFT;
        for(uint8_t xpos = x; xpos < x + width; xpos++)
        {
            uint8_t or_mask = 0xFF;
            uint8_t and_mask = 0x00;
            if(page == page_start)
            {
                if(full == TRUE)
                {
                    or_mask = l_bitshift_mask[l_bitshift];
                }
                else
                {
                    and_mask = ~l_bitshift_mask[l_bitshift];
                }                
            }
            if(page == page_end)
            {
                if(full == TRUE)
                {
                    or_mask = or_mask & f_bitshift_mask[f_bitshift];
                }
                else
                {
                    and_mask = and_mask | ~f_bitshift_mask[f_bitshift];
                }
            }
            if(page != page_start && page != page_end)
            {
                if(full == TRUE)
                {
                    or_mask = 0xFF;
                }
                else
                {
                    and_mask = 0x00;
                }
            }
            if (full == TRUE)
            {
                miniOledFrameBuffer[buffer_shift+xpos] |= or_mask;
            }
            else
            {
                miniOledFrameBuffer[buffer_shift+xpos] &= and_mask;
            }
        }
    }
}

/*! \fn     miniOledClear(void)
 *  \brief  Clear the display by setting every pixel to the background color
 */
void miniOledClear(void)
{
    memset(miniOledFrameBuffer, 0x00, sizeof(miniOledFrameBuffer));    
}

/*! \fn     miniOledGetFileAddr(uint8_t fileId, uint16_t* addr)
 *  \brief  Return the address and type of the media file for the specified file id
 *  \param  fileId  index of the media file to read
 *  \param  addr    pointer to address to fill
 *  \return -1 on error, else the type of the media file.
 *  \note   Media files start after string files, first is BITMAP_ID_OFFSET
 */
int16_t miniOledGetFileAddr(uint8_t fileId, uint16_t* addr)
{
    uint16_t type;

    if (getStoredFileAddr((uint16_t)fileId+BITMAP_ID_OFFSET, addr) == RETURN_NOK)
    {
        return -1;
    }
    
    flashRawRead((uint8_t *)&type, *addr, sizeof(type));
    *addr += sizeof(type);
    #ifdef OLED_DEBUG_OUTPUT_USB
        usbPrintf_P(PSTR("oledGetFileAddr file %d type 0x%x addr 0x%04x\n"), fileId, type, *addr);
    #endif

    return type;
}

/*! \fn     miniOledSetFont(uint8_t fontIndex)
 *  \brief  Set the font to use
 *  \param  font    New font to use
 */
void miniOledSetFont(uint8_t fontIndex)
{
    if (miniOledGetFileAddr(fontIndex, &miniOledFontAddr) != MEDIA_FONT)
    {        
        #ifdef OLED_DEBUG_OUTPUT_USB
            usbPrintf_P(PSTR("oled failed to set font %d\n"),fontIndex);
        #endif
        return;
    }
    
    // Read the font header
    miniOledFontId = fontIndex;
    flashRawRead((uint8_t *)&miniOledCurrentFont, miniOledFontAddr, sizeof(miniOledCurrentFont));
    
    // Check the bit depth
    if (miniOledCurrentFont.depth != 1)
    {
        miniOledFontId = FONT_NONE;
    }

    #ifdef OLED_DEBUG_OUTPUT_USB
        usbPrintf_P(PSTR("found font at file index %d\n"),fontIndex);
        usbPrintf_P(PSTR("oled set font %d\n"),fontIndex);
        //oledDumpFont();
    #endif
}

/*! \fn     miniOledBitmapDrawRaw(uint8_t x, uint8_t y, bitstream_mini_t* bs, uint8_t options)
 *  \brief  Draw a rectangular bitmap on the screen at x,y
 *  \param  x       x position for the bitmap
 *  \param  y       y position for the bitmap (0=top, 63=bottom)
 *  \param  bs      pointer to the bitstream object
 *  \param  options display options:
 *                  OLED_SCROLL_UP - scroll bitmap up
 *                  OLED_SCROLL_DOWN - scroll bitmap down
 *                  0 - don't make bitmap active (unless already drawing to active buffer)
 */
void miniOledBitmapDrawRaw(uint8_t x, uint8_t y, bitstream_mini_t* bs, uint8_t options)
{
    // Computing bitshifts, start/end pages...
    (void)options;
    uint8_t end_page = (y + bs->height - 1) >> SSD1305_PAGE_HEIGHT_BIT_SHIFT;
    uint8_t start_page = y >> SSD1305_PAGE_HEIGHT_BIT_SHIFT;
    uint8_t data_rbitshift = 7 - ((y + bs->height - 1) & 0x07);
    uint8_t data_lbitshift = (8 - data_rbitshift) & 0x07;
    uint8_t cur_pixels = 0, prev_pixels = 0;
    uint8_t end_x = x + bs->width - 1;
    uint8_t start_x = x;
    
    // Bitmasks
    uint8_t rbitmask[] = {0x00, 0x80, 0xC0, 0xE0, 0xF0, 0xF8, 0xFC, 0xFE};
    uint8_t lbitmask[] = {0xFF, 0x01, 0x03, 0x07, 0x0F, 0x1F, 0x3F, 0x7F};


    for (uint8_t x = start_x; x <= end_x; x++)
    {
        uint16_t buffer_shift = ((uint16_t)end_page) << SSD1305_WIDTH_BIT_SHIFT;
        for (int8_t page = end_page; page >= start_page; page--)
        {                     
            if (page == end_page)
            {
                cur_pixels = miniBistreamGetNextByte(bs);
                miniOledFrameBuffer[buffer_shift+x] &= rbitmask[data_rbitshift];
                miniOledFrameBuffer[buffer_shift+x] |= cur_pixels >> data_rbitshift;
            }
            if (page == start_page)
            {
                if(data_rbitshift == 0)
                {
                    // Data is aligned with our data storage system :D
                    cur_pixels = miniBistreamGetNextByte(bs);
                    miniOledFrameBuffer[buffer_shift+x] = cur_pixels;
                }
                else
                {
                    miniOledFrameBuffer[buffer_shift+x] &= lbitmask[data_lbitshift];
                    miniOledFrameBuffer[buffer_shift+x] |= prev_pixels << data_lbitshift;
                }
            }
            if (page != end_page && page != start_page)
            {
                cur_pixels = miniBistreamGetNextByte(bs);
                if(data_rbitshift == 0)
                {
                    // Data is aligned with our data storage system :D
                    miniOledFrameBuffer[buffer_shift+x] = cur_pixels;
                }
                else
                {
                    miniOledFrameBuffer[buffer_shift+x] = prev_pixels << data_lbitshift;
                    miniOledFrameBuffer[buffer_shift+x] |= cur_pixels >> data_rbitshift;                    
                }
            }
            prev_pixels = cur_pixels;
            buffer_shift -= ((uint16_t)1 << SSD1305_WIDTH_BIT_SHIFT);
        }
    }  
}

/*! \fn     miniOledBitmapDrawFlash(uint8_t x, uint8_t y, uint8_t fileId, uint8_t options)
 *  \brief  Draw a bitmap from a Flash storage slot
 *  \param  x       x position for the bitmap
 *  \param  y       y position for the bitmap (0=top, 63=bottom)
 *  \param  addr    address of the bitmap in flash
 *  \param  options display options:
 *                  OLED_SCROLL_UP - scroll bitmap up
 *                  OLED_SCROLL_DOWN - scroll bitmap down
 *                  0 - don't make bitmap active (unless already drawing to active buffer)
 */
void miniOledBitmapDrawFlash(uint8_t x, uint8_t y, uint8_t fileId, uint8_t options)
{
    bitstream_mini_t bs;
    bitmap_t bitmap;
    uint16_t addr;

    // Get address of our bitmap in the external flash
    if (miniOledGetFileAddr(fileId, &addr) != MEDIA_BITMAP)
    {
        return;
    }

    // Read bitmap header
    flashRawRead((uint8_t*)&bitmap, addr, sizeof(bitmap));
    
    // Initialize bitstream (pixel data starts right after the header)
    miniBistreamInit(&bs, bitmap.height, bitmap.width, addr+sizeof(bitmap));
    
    // Draw the bitmap
    miniOledBitmapDrawRaw(x, y, &bs, options);
}

/*! \fn     miniOledGlyphWidth(char ch)
 *  \brief  Return the width of the specified character in the current font
 *  \param  ch      return the width of this character
 *  \return width of the glyph
 */
uint8_t miniOledGlyphWidth(char ch)
{
    glyph_t glyph;
    uint8_t gind;
    
    // Check that a font was actually chosen
    if (miniOledFontId != FONT_NONE) 
    {        
        // We only support characters after ' '
        if (ch < ' ')
        {
            ch = '?';
        }     
        
        // Convert character to glyph index
        flashRawRead(&gind, miniOledFontAddr + (uint16_t)&miniOledFontp->map[ch - ' '], sizeof(gind));
        
        // Check that we know this glyph
        if(gind == 0xFF)
        {
            // If we don't know this character, try again with '?'
            ch = '?';
            flashRawRead(&gind, miniOledFontAddr + (uint16_t)&miniOledFontp->map[ch - ' '], sizeof(gind));
            
            // If we still don't know it, return 0
            if (gind == 0xFF)
            {
                return 0;
            }            
        }

        // Read the beginning of the glyph
        flashRawRead((uint8_t*)&glyph, miniOledFontAddr + (uint16_t)&miniOledFontp->glyph[gind], sizeof(glyph_t));

        if ((uint16_t)glyph.glyph == 0xFFFF)
        {
            // If there's no glyph data, it is the space!
            return glyph.width + glyph.xoffset + 1;
        }
        else
        {
            return glyph.xrect + glyph.xoffset + 1;
        }
    }
    else 
    {
        return 0;
    }
}

/*! \fn     miniOledStrWidth(const char* str)
 *  \brief  Return the pixel width of the string.
 *  \param  str     string to get width of
 *  \return width of string in pixels based on current font
 */
uint16_t miniOledStrWidth(const char* str)
{
    uint16_t width=0;
    for (uint8_t ind=0; str[ind] != 0; ind++) 
    {
        width += miniOledGlyphWidth(str[ind]);
    }
    return width;
}

/*! \fn     miniOledGlyphDraw(uint8_t x, uint8_t y, char ch)
 *  \brief  Draw a glyph at X & Y
 *  \param  x   X position
 *  \param  y   Y position
 *  \param  ch  Character to display
 *  \return width of the glyph
 */
uint8_t miniOledGlyphDraw(uint8_t x, uint8_t y, char ch)
{
    bitstream_mini_t bs;                // Character bitstream
    uint8_t glyph_height;               // Glyph height
    uint8_t glyph_width;                // Glyph width
    glyph_t glyph;                      // Glyph header
    uint8_t gind;                       // Glyph index
    
    // Check that a font is set
    if (miniOledFontId == FONT_NONE)
    {
        return 0;
    }

    // We only support characters after ' '
    if (ch < ' ')
    {
        ch = '?';
    }
    
    // Convert character to glyph index
    flashRawRead(&gind, miniOledFontAddr + (uint16_t)&miniOledFontp->map[ch - ' '], sizeof(gind));
    
    // Check that we know this glyph
    if(gind == 0xFF)
    {
        // If we don't know this character, try again with '?'
        ch = '?';
        flashRawRead(&gind, miniOledFontAddr + (uint16_t)&miniOledFontp->map[ch - ' '], sizeof(gind));
        
        // If we still don't know it, return 0
        if (gind == 0xFF)
        {
            return 0;
        }
    }
    
    // Get the glyph header data
    flashRawRead((uint8_t*)&glyph, miniOledFontAddr + (uint16_t)&miniOledFontp->glyph[gind], sizeof(glyph_t));
    
    #ifdef OLED_DEBUG_OUTPUT_USB
        usbPrintf_P(PSTR("    glyph_t addr 0x%04x\n"), miniOledFontAddr + (uint16_t)&miniOledFontp->glyph[gind]);
    #endif
    
    if ((uint16_t)glyph.glyph == 0xFFFF)
    {
        // space character, just fill in the gddram buffer and output background pixels
        glyph_width = glyph.width;
        glyph_height = miniOledCurrentFont.height;
        #ifdef OLED_DEBUG_OUTPUT_USB
            usbPrintf_P(PSTR("    space character width %u height %u\n"), glyph_width, glyph_height);
        #endif
    }
    else
    {
        // Store glyph height and width, increment with offset
        glyph_width = glyph.xrect;
        glyph_height = glyph.yrect;
        x += glyph.xoffset;
        y += glyph.yoffset;
        
        // Compute glyph data address
        uint16_t gaddr = miniOledFontAddr + (uint16_t)&miniOledFontp->glyph[miniOledCurrentFont.count] + (uint16_t)glyph.glyph;
        
        #ifdef OLED_DEBUG_OUTPUT_USB
            // glyph data offsets are from the end of the glyph header array
            usbPrintf_P(PSTR("    glyph '%c' width %d height %d addr 0x%04x\n"), ch, glyph_width, glyph_height, gaddr);
        #endif
        
        // Initialize bitstream & draw the character
        miniBistreamInit(&bs, glyph_height, glyph_width, gaddr);   
        miniOledBitmapDrawRaw(x, y, &bs, 0);
    }
    
    return (uint8_t)(glyph_width + glyph.xoffset) + 1;
}    

/*! \fn     miniOledPutch(char ch)
 *  \brief  Print an character on the screen at the current X and Y position. X and Y position is updated after the print operation, with X wrapping if necessary
 *  \param  ch  the character to print
 *  \note '\n' is will increment the row position based on the current font height, and also reset x to 0. '\r' will reset x to 0.
 */
void miniOledPutch(char ch)
{    
    #ifdef OLED_DEBUG_OUTPUT_USB
        if (isprint(ch))
        {
            usbPrintf_P(PSTR("oledPutch('%c') x=%d, y=%d, oled_offset=%d, buf=%d, height=%u\n"), ch, miniOledTextCurX, miniOledTextCurY, 0, 0, glyphHeight);
        }
        else 
        {
            usbPrintf_P(PSTR("oledPutch('0x%02x') x=%d, y=%d, oled_offset=%d, buf=%d\n"), ch, miniOledTextCurX, miniOledTextCurY, 0, 0);
        }
    #endif
    
    miniOledTextCurX += miniOledGlyphDraw(miniOledTextCurX, miniOledTextCurY, ch);
/*
    // see if the current offset is offscreen
    if ((oled_cur_y[oled_writeBuffer] + glyphHeight) > OLED_HEIGHT) 
    {
        if (oled_wrap || oled_writeBuffer != oled_displayBuffer)
        {
            // Wrap back to the top of the display
            oled_cur_y[oled_writeBuffer] = 0;
        }
        else
        {
#ifdef OLED_DEBUG
            usbPrintf_P(PSTR("oledPutch('0x%02x') scroll x=%d, y=%d, oled_offset=%d, buf=%d\n"),
                    ch, oled_cur_x[oled_writeBuffer], oled_cur_y[oled_writeBuffer], oled_offset, oled_writeBuffer);
#endif            
            oledScrollUp(0, true);
            oled_cur_y[oled_writeBuffer] -= glyphHeight;
#ifdef OLED_DEBUG
            usbPrintf_P(PSTR("oledPutch('0x%02x') scroll completed y=%d, oled_offset=%d, gHeight=%d\n"),
                    ch, oled_cur_y[oled_writeBuffer], oled_offset, glyphHeight);
#endif            
        } 
    }*/

/*
    switch (ch) 
    {
        case '\n':
            oled_cur_y[oled_writeBuffer] += glyphHeight;
            // Fall through
        case '\r':
            oled_cur_x[oled_writeBuffer] = 0;
            break;
        default: {
            uint8_t width = oledGlyphWidth(ch, NULL, NULL);
    #ifdef OLED_DEBUG
            usbPrintf_P(PSTR("oled_putch('%c')\n"), ch);
    #endif
            if ((oled_cur_x[oled_writeBuffer] + width) > OLED_WIDTH) 
            {
    #ifdef OLED_DEBUG
                usbPrintf_P(PSTR("wrap at y=%d x=%d, '%c' width %d\n"),oled_cur_y,oled_cur_x[oled_writeBuffer],ch,width);
    #endif
                oled_cur_y[oled_writeBuffer] += glyphHeight;
                oled_cur_x[oled_writeBuffer] = 0;
            }
            oled_cur_x[oled_writeBuffer] += oledGlyphDraw(oled_cur_x[oled_writeBuffer], oled_cur_y[oled_writeBuffer], ch, oled_foreground, oled_background);
            break;
            }
    }*/
}

/*! \fn     miniOledPutstr(const char* str)
 *  \brief  Print the string on the OLED starting at the X,Y location
 *  \param  x       x position
 *  \param  y       y position
 *  \param  str     pointer to the string in ram
 */
void miniOledPutstr(uint8_t x, uint8_t y, const char* str)
{
    miniOledTextCurX = x;
    miniOledTextCurY = y;
    
    // Write chars until we find final 0
    while (*str)
    {
        miniOledPutch(*str++);
    }
}

/*! \fn     miniOledPutstrXY(int16_t x, uint8_t y, uint8_t justify, const char* str)
 *  \brief  Print a string at the specified pixel line, justified left, center, or right of x.
 *  \param  x       x position
 *  \param  y       y position
 *  \param  justify OLED_LEFT, OLED_CENTRE, OLED_RIGHT
 *  \param  str     pointer to the string in ram
 */
void miniOledPutstrXY(uint8_t x, uint8_t y, uint8_t justify, const char* str)
{
    int16_t width = (int16_t)miniOledStrWidth(str);

    // Compute where to start displaying the string
    if (justify == OLED_CENTRE)
    {
        x = SSD1305_OLED_WIDTH/2 - width/2 + x;
    } 
    else if (justify == OLED_RIGHT)
    {
        if (x >= width) 
        {
            x -= width;
        } 
        else 
        {
            x = SSD1305_OLED_WIDTH - width;
        }
    }

    // Display string
    miniOledPutstr(x, y, str);
}

#endif
/***************************************************************/