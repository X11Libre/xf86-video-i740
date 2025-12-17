/*
 * Copyright 2001 by Patrick LERDA
 *
 * Permission to use, copy, modify, distribute, and sell this software and its
 * documentation for any purpose is hereby granted without fee, provided that
 * the above copyright notice appear in all copies and that both that
 * copyright notice and this permission notice appear in supporting
 * documentation, and that the name of Patrick LERDA not be used in
 * advertising or publicity pertaining to distribution of the software without
 * specific, written prior permission.  Patrick LERDA makes no representations
 * about the suitability of this software for any purpose.  It is provided
 * "as is" without express or implied warranty.
 *
 * PATRICK LERDA DISCLAIMS ALL WARRANTIES WITH REGARD TO THIS SOFTWARE,
 * INCLUDING ALL IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS, IN NO
 * EVENT SHALL PATRICK LERDA BE LIABLE FOR ANY SPECIAL, INDIRECT OR
 * CONSEQUENTIAL DAMAGES OR ANY DAMAGES WHATSOEVER RESULTING FROM LOSS OF USE,
 * DATA OR PROFITS, WHETHER IN AN ACTION OF CONTRACT, NEGLIGENCE OR OTHER
 * TORTIOUS ACTION, ARISING OUT OF OR IN CONNECTION WITH THE USE OR
 * PERFORMANCE OF THIS SOFTWARE.
 *
 * Authors:  Patrick LERDA
 */
#include "config.h"

#include "xf86.h"
#include "xf86_OSproc.h"
#include "xf86Pci.h"
#include "vgaHW.h"
#include "xf86xv.h"
#include "i740.h"
#include "dgaproc.h"
#include "i740_dga.h"

static Bool I740_OpenFramebuffer(ScrnInfoPtr, char **, unsigned char **, int *,
                                 int *, int *);
static Bool I740_SetMode(ScrnInfoPtr, DGAModePtr);
static int  I740_GetViewport(ScrnInfoPtr);
static void I740_SetViewport(ScrnInfoPtr, int, int, int);

static DGAFunctionRec I740DGAFuncs = {
    I740_OpenFramebuffer,
    NULL,
    I740_SetMode,
    I740_SetViewport,
    I740_GetViewport,
    NULL, NULL, NULL, NULL
};

Bool
I740DGAInit(ScreenPtr pScreen)
{
    ScrnInfoPtr pScrn = xf86ScreenToScrn(pScreen);
    I740Ptr pI740 = I740PTR(pScrn);
    DGAModePtr modes = NULL, currentMode;
    DisplayModePtr pMode, firstMode;
    int Bpp = pScrn->bitsPerPixel >> 3;
    int num = 0;

    pMode = firstMode = pScrn->modes;

    while (pMode) {

        DGAModePtr newmodes = realloc(modes, (num + 1) * sizeof(DGAModeRec));

        if (!newmodes) {
            free(modes);
            return FALSE;
        }
        modes = newmodes;

        currentMode = modes + num;
        num++;

        currentMode->mode = pMode;
        currentMode->flags = DGA_CONCURRENT_ACCESS | DGA_PIXMAP_AVAILABLE;
        if (pMode->Flags & V_DBLSCAN)
            currentMode->flags |= DGA_DOUBLESCAN;
        if (pMode->Flags & V_INTERLACE)
            currentMode->flags |= DGA_INTERLACED;
        currentMode->byteOrder = pScrn->imageByteOrder;
        currentMode->depth = pScrn->depth;
        currentMode->bitsPerPixel = pScrn->bitsPerPixel;
        currentMode->red_mask = pScrn->mask.red;
        currentMode->green_mask = pScrn->mask.green;
        currentMode->blue_mask = pScrn->mask.blue;
        currentMode->visualClass = (Bpp == 1) ? PseudoColor : TrueColor;
        currentMode->viewportWidth = pMode->HDisplay;
        currentMode->viewportHeight = pMode->VDisplay;
        currentMode->xViewportStep = (Bpp == 3) ? 2 : 1;
        currentMode->yViewportStep = 1;
        currentMode->viewportFlags = DGA_FLIP_RETRACE;
        currentMode->offset = 0;
        currentMode->address = pI740->FbBase;

        currentMode->bytesPerScanline = ((pScrn->displayWidth * Bpp) + 3) & ~3L;
        currentMode->imageWidth = pI740->FbMemBox.x2;
        currentMode->imageHeight = pI740->FbMemBox.y2;
        currentMode->pixmapWidth = currentMode->imageWidth;
        currentMode->pixmapHeight = currentMode->imageHeight;
        currentMode->maxViewportX = currentMode->imageWidth -
            currentMode->viewportWidth;
        /* this might need to get clamped to some maximum */
        currentMode->maxViewportY = currentMode->imageHeight -
            currentMode->viewportHeight;

        pMode = pMode->next;
        if (pMode == firstMode)
            break;
    }

    pI740->numDGAModes = num;
    pI740->DGAModes = modes;

    return DGAInit(pScreen, &I740DGAFuncs, modes, num);
}

static DisplayModePtr I740SavedDGAModes[MAXSCREENS];

static Bool
I740_SetMode(ScrnInfoPtr pScrn, DGAModePtr pMode)
{
    int index = pScrn->pScreen->myNum;
    I740Ptr pI740 = I740PTR(pScrn);

    if (!pMode) {               /* restore the original mode */
        if (pI740->DGAactive) {
            pScrn->currentMode = I740SavedDGAModes[index];
            I740SwitchMode(pScrn, pScrn->currentMode);
            I740AdjustFrame(pScrn, 0, 0);
            pI740->DGAactive = FALSE;
        }
    }
    else {
        if (!pI740->DGAactive) {
            I740SavedDGAModes[index] = pScrn->currentMode;
            pI740->DGAactive = TRUE;
        }

        I740SwitchMode(pScrn, pMode->mode);
    }

    return TRUE;
}

static int
I740_GetViewport(ScrnInfoPtr pScrn)
{
    I740Ptr pI740 = I740PTR(pScrn);

    return pI740->DGAViewportStatus;
}

static void
I740_SetViewport(ScrnInfoPtr pScrn, int x, int y, int flags)
{
    I740Ptr pI740 = I740PTR(pScrn);
    vgaHWPtr hwp = VGAHWPTR(pScrn);

    I740AdjustFrame(pScrn, x, y);

    /* wait for retrace */
    while ((hwp->readST01(hwp) & 0x08));
    while (!(hwp->readST01(hwp) & 0x08));

    pI740->DGAViewportStatus = 0;
}

#if 0
static void
I740_BlitTransRect(ScrnInfoPtr pScrn,
                   int srcx, int srcy,
                   int w, int h, int dstx, int dsty, unsigned long color)
{
    /* this one should be separate since the XAA function would
       prohibit usage of ~0 as the key */
}
#endif

static Bool
I740_OpenFramebuffer(ScrnInfoPtr pScrn,
                     char **name,
                     unsigned char **mem, int *size, int *offset, int *flags)
{
    I740Ptr pI740 = I740PTR(pScrn);

    *name = NULL;               /* no special device */
    *mem = (unsigned char *) pI740->LinearAddr;
    *size = pI740->FbMapSize;
    *offset = 0;
    *flags = DGA_NEED_ROOT;

    return TRUE;
}
