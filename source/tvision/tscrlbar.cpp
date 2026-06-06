/*------------------------------------------------------------*/
/* filename -       tscrlbar.cpp                              */
/*                                                            */
/* function(s)                                                */
/*                  TScrollBar member functions               */
/*------------------------------------------------------------*/
/*
 *      Turbo Vision - Version 2.0
 *
 *      Copyright (c) 1994 by Borland International
 *      All Rights Reserved.
 *
 */

#define Uses_TKeys
#define Uses_TScrollBar
#define Uses_TRect
#define Uses_TDrawBuffer
#define Uses_TEvent
#define Uses_TGroup
#define Uses_opstream
#define Uses_ipstream
#include <tvision/tv.h>
#include <tvision/colors.h> // reverseAttribute() for proportional thumb edges

#if !defined( __CTYPE_H )
#include <ctype.h>
#endif  // __CTYPE_H

#if !defined( __STRING_H )
#include <string.h>
#endif  // __STRING_H

#if !defined( __DOS_H )
#include <dos.h>
#endif  // __DOS_H

#define cpScrollBar  "\x04\x05\x05"

TScrollBar::TScrollBar( const TRect& bounds ) noexcept :
    TView( bounds ),
    value( 0 ),
    minVal( 0 ),
    maxVal( 0 ),
    pgStep( 1 ),
    arStep( 1 )
{
    if( size.x == 1 )
        {
        growMode = gfGrowLoX | gfGrowHiX | gfGrowHiY;
        memcpy( chars, vChars, sizeof(vChars) );
        }
    else
        {
        growMode = gfGrowLoY | gfGrowHiX | gfGrowHiY;
        memcpy( chars, hChars, sizeof(hChars) );
        }
    eventMask |= evMouseWheel;
}

void TScrollBar::draw()
{
    drawPos(getPos());
}

// Modern, proportional scroll bar.
//
// The thumb's *length* reflects the visible fraction of the content
// (pgStep / (range + pgStep)), and both its length and position are drawn to
// 1/8-cell precision using the Unicode block-element glyphs, over a light-shade
// trough. The 'pos' argument is ignored: the thumb is recomputed from 'value',
// which keeps live drag feedback correct (handleEvent's drawPos() call runs
// after setValue() has updated 'value').
//
// Glyphs (UTF-8 'E2 96 xx'): vertical fills from the bottom with the lower
// blocks (0x81..0x88 == one..eight eighths), horizontal fills from the left
// with the left blocks (0x8F..0x88). A solid block only fills from one fixed
// end of the cell, so the *opposite* thumb edge is drawn with the complementary
// block in the reversed attribute, letting its background read as the thumb
// colour. Because the thumb is at least one cell tall a single cell never has a
// trough gap on both ends, so the four cases below are exhaustive.
void TScrollBar::drawPos( int ) noexcept
{
    TDrawBuffer b;

    Boolean vert = Boolean( size.x == 1 );
    int total = getSize();      // length incl. the two arrow cells
    int last = total - 1;       // index of the far arrow
    int track = total - 2;      // cells available for thumb + trough
    if( track < 1 )
        track = 1;

    TColorAttr cThumb = getColor(1);                // {wcBarThumb fg, wcBarTrough bg}
    TColorAttr cThumbR = reverseAttribute(cThumb);  // edge complement (bg reads as thumb)
    TColorAttr cArrow = getColor(2);

    // Thumb extent along the track, measured in eighths of a cell.
    int E = track * 8;
    int range = maxVal - minVal;
    int thumbE, topE;
    if( range <= 0 )
        {
        thumbE = E;             // nothing to scroll: thumb fills the whole track
        topE = 0;
        }
    else
        {
        long denom = (long) range + pgStep;
        if( denom < 1 )
            denom = 1;
        thumbE = int( ((long) E * pgStep + denom/2) / denom );
        int minE = min( 8, E ); // never smaller than one cell
        if( thumbE < minE )
            thumbE = minE;
        if( thumbE > E )
            thumbE = E;
        int freeE = E - thumbE;
        topE = int( ((long) freeE * (value - minVal) + range/2) / range );
        if( topE < 0 )
            topE = 0;
        if( topE > freeE )
            topE = freeE;
        }
    int botE = topE + thumbE;

    b.moveChar( 0, chars[0], cArrow, 1 );
    b.moveChar( last, chars[1], cArrow, 1 );

    char glyph[4] = { '\xE2', '\x96', 0, 0 };
    for( int j = 0; j < track; ++j )
        {
        int cellTop = j*8, cellBot = cellTop + 8;
        int a = max( cellTop, topE );
        int z = min( cellBot, botE );
        int idx = 1 + j;
        if( z <= a )                            // empty: light-shade trough
            {
            glyph[2] = '\x91';                  // U+2591
            b.moveStr( idx, glyph, cThumb, 1 );
            continue;
            }
        int topGap = a - cellTop;               // trough eighths on the top/left end
        int botGap = cellBot - z;               // trough eighths on the bottom/right end
        int k;
        TColorAttr attr;
        if( topGap == 0 && botGap == 0 )        // full thumb cell
            {
            k = 8;
            attr = cThumb;
            }
        else if( vert ? (botGap == 0) : (topGap == 0) ) // gap on the far end: fill directly
            {
            k = 8 - (vert ? topGap : botGap);
            attr = cThumb;
            }
        else                                    // gap on the near end: reversed complement
            {
            k = vert ? botGap : topGap;
            attr = cThumbR;
            }
        glyph[2] = char( vert ? (0x80 + k) : (0x90 - k) );
        b.moveStr( idx, glyph, attr, 1 );
        }

    writeBuf( 0, 0, size.x, size.y, b );
}

TPalette& TScrollBar::getPalette() const
{
    static TPalette palette( cpScrollBar, sizeof( cpScrollBar )-1 );
    return palette;
}

int TScrollBar::getPos() noexcept
{
    int r = maxVal - minVal;
    if( r == 0 )
        return 1;
    else
        return  int(( ((long(value - minVal) * (getSize() - 3)) + (r >> 1)) / r) + 1);
}

int TScrollBar::getSize() noexcept
{
    int s;

    if( size.x == 1 )
        s = size.y;
    else
        s = size.x;

    return max( 3, s );
}

static TPoint mouse;
static int p, s;
static TRect extent;

int TScrollBar::getPartCode() noexcept
{
    int part= - 1;
    if( extent.contains(mouse) )
        {
        int mark = (size.x == 1) ? mouse.y : mouse.x;

        if (mark == p)
            part= sbIndicator;
        else
            {
            if( mark < 1 )
                part = sbLeftArrow;
            else if( mark < p )
                part= sbPageLeft;
            else if( mark < s )
                part= sbPageRight;
            else
                part= sbRightArrow;

            if( size.x == 1 )
                part += 4;
            }
        }
    return part;
}

void TScrollBar::handleEvent( TEvent& event )
{
    int i, clickPart, step = 0;

    TView::handleEvent(event);
    switch( event.what )
        {
        case evMouseWheel:
            if (state & sfVisible)
                {
                if( size.x == 1 )
                    switch ( event.mouse.wheel )
                        {
                        case mwUp: step = -arStep; break;
                        case mwDown: step = arStep; break;
                        }
                else
                    switch ( event.mouse.wheel )
                        {
                        case mwLeft: step = -arStep; break;
                        case mwRight: step = arStep; break;
                        }
                }
            if( step )
                {
                // E.g. when the bar is associated to a TListViewer, this message
                // causes it to become selected.
                message(owner, evBroadcast, cmScrollBarClicked, this);
                setValue(value + 3*step);
                clearEvent(event);
                }
            break;
        case evMouseDown:
            message(owner, evBroadcast, cmScrollBarClicked,this); // Clicked()
            mouse = makeLocal( event.mouse.where );
            extent = getExtent();
            extent.grow(1, 1);
            p = getPos();
            s = getSize() - 1;
            clickPart= getPartCode();
            switch( clickPart )
                {
                case sbLeftArrow:   // If an arrow was pressed,
                case sbRightArrow:  // do the appropiate action.
                case sbUpArrow:
                case sbDownArrow:
                    do  {
                        mouse = makeLocal( event.mouse.where );
                        if( getPartCode() == clickPart )
                            setValue(value + scrollStep(clickPart) );
                        } while( mouseEvent(event, evMouseAuto) );
                    break;
                default:            // Otherwise, move the thumb along the mouse cursor.
                    do  {
                        mouse = makeLocal( event.mouse.where );
                        if( size.x == 1 )
                            i = mouse.y;
                        else
                            i = mouse.x;
                        i = max( i, 1 ); // Keep the thumb between the two arrows.
                        i = min( i, s-1 );
                        p = i;
                        if( s > 2 ) // Update the scroll value if there's free scroll space.
                            setValue( int(((long(p - 1) * (maxVal - minVal) + ((s - 2) >> 1)) / (s - 2)) + minVal) );
                        drawPos(p);
                        } while( mouseEvent(event, evMouseMove) );
                    break;
                }
            clearEvent(event);
            break;
        case  evKeyDown:
            if( (state & sfVisible) != 0 )
                {
                clickPart = sbIndicator;
                if( size.y == 1 )
                    switch( ctrlToArrow(event.keyDown.keyCode) )
                        {
                        case kbLeft:
                            clickPart = sbLeftArrow;
                            break;
                        case kbRight:
                            clickPart = sbRightArrow;
                            break;
                        case kbCtrlLeft:
                            clickPart = sbPageLeft;
                            break;
                        case kbCtrlRight:
                            clickPart = sbPageRight;
                            break;
                        case kbCtrlUp:
                            clickPart = sbPageUp;
                            break;
                        case kbCtrlDown:
                            clickPart = sbPageDown;
                            break;
                        case kbHome:
                            i = minVal;
                            break;
                        case kbEnd:
                            i = maxVal;
                            break;
                        default:
                            return;
                        }
                else
                    switch( ctrlToArrow(event.keyDown.keyCode) )
                        {
                        case kbUp:
                            clickPart = sbUpArrow;
                            break;
                        case kbDown:
                            clickPart = sbDownArrow;
                            break;
                        case kbPgUp:
                            clickPart = sbPageUp;
                            break;
                        case kbPgDn:
                            clickPart = sbPageDown;
                            break;
                        case kbCtrlPgUp:
                            i = minVal;
                            break;
                        case kbCtrlPgDn:
                            i = maxVal;
                            break;
                        default:
                            return;
                        }
                message(owner,evBroadcast,cmScrollBarClicked,this); // Clicked
                if( clickPart != sbIndicator )
                    i = value + scrollStep(clickPart);
                setValue(i);
                clearEvent(event);
                }
        }
}

void TScrollBar::scrollDraw()
{
    message(owner, evBroadcast, cmScrollBarChanged,this);
}

int TScrollBar::scrollStep( int part )
{
    int  step;

    if( !(part & 2) )
        step = arStep;
    else
        step = pgStep;
    if( !(part & 1) )
        return -step;
    else
        return step;
}

void TScrollBar::setParams( int aValue,
                            int aMin,
                            int aMax,
                            int aPgStep,
                            int aArStep
                          ) noexcept
{
    int  sValue;

    aMax = max( aMax, aMin );
    aValue = max( aMin, aValue );
    aValue = min( aMax, aValue );
    sValue = value;
    if( sValue != aValue || minVal != aMin || maxVal != aMax )
        {
        value = aValue;
        minVal = aMin;
        maxVal = aMax;
        drawView();
        if( sValue != aValue )
            scrollDraw();
        }
    pgStep = aPgStep;
    arStep = aArStep;
}

void TScrollBar::setRange( int aMin, int aMax ) noexcept
{
    setParams( value, aMin, aMax, pgStep, arStep );
}

void TScrollBar::setStep( int aPgStep, int aArStep ) noexcept
{
    setParams( value, minVal, maxVal, aPgStep, aArStep );
}

void TScrollBar::setValue( int aValue ) noexcept
{
    setParams( aValue, minVal, maxVal, pgStep, arStep );
}

#if !defined(NO_STREAMABLE)

void TScrollBar::write( opstream& os )
{
    TView::write( os );
    os << value << minVal << maxVal << pgStep << arStep;
    os.writeBytes(chars, sizeof(chars));
}

void *TScrollBar::read( ipstream& is )
{
    TView::read( is );
    is >> value >> minVal >> maxVal >> pgStep >> arStep;
    is.readBytes(chars, sizeof(TScrollChars));
    return this;
}

TStreamable *TScrollBar::build()
{
    return new TScrollBar( streamableInit );
}

TScrollBar::TScrollBar( StreamableInit ) noexcept : TView( streamableInit )
{
}

#endif
