#include "render.h"
#include "instance.h"

static RrInstance *definst = NULL;

void RrTrueColorSetup (RrInstance *inst);
void RrPseudoColorSetup (RrInstance *inst);

RrInstance* RrInstanceNew (Display *display, gint screen)
{
    definst = g_new (RrInstance, 1);
    definst->display = display;
    definst->screen = screen;

    definst->depth = DefaultDepth(display, screen);
    definst->visual = DefaultVisual(display, screen);
    definst->colormap = DefaultColormap(display, screen);

    definst->pseudo_colors = NULL;

    switch (definst->visual->class) {
    case TrueColor:
        RrTrueColorSetup(definst);
        break;
    case PseudoColor:
    case StaticColor:
    case GrayScale:
    case StaticGray:
        RrPseudoColorSetup(definst);
        break;
    default:
        g_critical("Unsupported visual class");
        g_free (definst);
        return definst = NULL;
    }
    return definst;
}

void RrTrueColorSetup (RrInstance *inst)
{
  unsigned long red_mask, green_mask, blue_mask;
  XImage *timage = NULL;

  timage = XCreateImage(inst->display, inst->visual, inst->depth,
                        ZPixmap, 0, NULL, 1, 1, 32, 0);
  g_assert(timage != NULL);
  /* find the offsets for each color in the visual's masks */
  inst->red_mask = red_mask = timage->red_mask;
  inst->green_mask = green_mask = timage->green_mask;
  inst->blue_mask = blue_mask = timage->blue_mask;

  inst->red_offset = 0;
  inst->green_offset = 0;
  inst->blue_offset = 0;

  while (! (red_mask & 1))   { inst->red_offset++;   red_mask   >>= 1; }
  while (! (green_mask & 1)) { inst->green_offset++; green_mask >>= 1; }
  while (! (blue_mask & 1))  { inst->blue_offset++;  blue_mask  >>= 1; }

  inst->red_shift = inst->green_shift = inst->blue_shift = 8;
  while (red_mask)   { red_mask   >>= 1; inst->red_shift--;   }
  while (green_mask) { green_mask >>= 1; inst->green_shift--; }
  while (blue_mask)  { blue_mask  >>= 1; inst->blue_shift--;  }
  XFree(timage);
}

#define RrPseudoNcolors(isnt) (1 << (inst->pseudo_bpc * 3))

void RrPseudoColorSetup (RrInstance *inst)
{
    XColor icolors[256];
    int tr, tg, tb, n, r, g, b, i, incolors, ii;
    unsigned long dev;
    int cpc, _ncolors;
    g_message("Initializing PseudoColor RenderControl\n");

    /* determine the number of colors and the bits-per-color */
    inst->pseudo_bpc = 2; /* XXX THIS SHOULD BE A USER OPTION */
    g_assert(inst->pseudo_bpc >= 1);
    _ncolors = RrPseudoNcolors(inst);

    if (_ncolors > 1 << inst->depth) {
        g_warning("PseudoRenderControl: Invalid colormap size. Resizing.\n");
        inst->pseudo_bpc = 1 << (inst->depth/3) >> 3;
        _ncolors = 1 << (inst->pseudo_bpc * 3);
    }

    /* build a color cube */
    inst->pseudo_colors = g_new(XColor, _ncolors);
    cpc = 1 << inst->pseudo_bpc; /* colors per channel */

    for (n = 0, r = 0; r < cpc; r++)
        for (g = 0; g < cpc; g++)
            for (b = 0; b < cpc; b++, n++) {
                tr = (int)(((float)(r)/(float)(cpc-1)) * 0xFF);
                tg = (int)(((float)(g)/(float)(cpc-1)) * 0xFF);
                tb = (int)(((float)(b)/(float)(cpc-1)) * 0xFF);
                inst->pseudo_colors[n].red = tr | tr << 8;
                inst->pseudo_colors[n].green = tg | tg << 8;
                inst->pseudo_colors[n].blue = tb | tb << 8;
                /* used to track allocation */
                inst->pseudo_colors[n].flags = DoRed|DoGreen|DoBlue;
            }

    /* allocate the colors */
    for (i = 0; i < _ncolors; i++)
        if (!XAllocColor(inst->display, inst->colormap,
                         &inst->pseudo_colors[i]))
            inst->pseudo_colors[i].flags = 0; /* mark it as unallocated */

    /* try allocate any colors that failed allocation above */

    /* get the allocated values from the X server
       (only the first 256 XXX why!?)
     */
    incolors = (((1 << inst->depth) > 256) ? 256 : (1 << inst->depth));
    for (i = 0; i < incolors; i++)
        icolors[i].pixel = i;
    XQueryColors(inst->display, inst->colormap, icolors, incolors);

    /* try match unallocated ones */
    for (i = 0; i < _ncolors; i++) {
        if (!inst->pseudo_colors[i].flags) { /* if it wasn't allocated... */
            unsigned long closest = 0xffffffff, close = 0;
            for (ii = 0; ii < incolors; ii++) {
                /* find deviations */
                r = (inst->pseudo_colors[i].red - icolors[ii].red) & 0xff;
                g = (inst->pseudo_colors[i].green - icolors[ii].green) & 0xff;
                b = (inst->pseudo_colors[i].blue - icolors[ii].blue) & 0xff;
                /* find a weighted absolute deviation */
                dev = (r * r) + (g * g) + (b * b);

                if (dev < closest) {
                    closest = dev;
                    close = ii;
                }
            }

            inst->pseudo_colors[i].red = icolors[close].red;
            inst->pseudo_colors[i].green = icolors[close].green;
            inst->pseudo_colors[i].blue = icolors[close].blue;
            inst->pseudo_colors[i].pixel = icolors[close].pixel;

            /* try alloc this closest color, it had better succeed! */
            if (XAllocColor(inst->display, inst->colormap,
                            &inst->pseudo_colors[i]))
                /* mark as alloced */
                inst->pseudo_colors[i].flags = DoRed|DoGreen|DoBlue;
            else
                /* wtf has gone wrong, its already alloced for chissake! */
                g_assert_not_reached();
        }
    }
}

void RrInstanceFree (RrInstance *inst)
{
    if (inst) {
        if (inst == definst) definst = NULL;
        g_free(inst->pseudo_colors);
    }
}

Display* RrDisplay (const RrInstance *inst)
{
    return (inst ? inst : definst)->display;
}

gint RrScreen (const RrInstance *inst)
{
    return (inst ? inst : definst)->screen;
}

Window RrRootWindow (const RrInstance *inst)
{
    return RootWindow (RrDisplay (inst), RrScreen (inst));
}

Visual *RrVisual (const RrInstance *inst)
{
    return (inst ? inst : definst)->visual;
}

gint RrDepth (const RrInstance *inst)
{
    return (inst ? inst : definst)->depth;
}

Colormap RrColormap (const RrInstance *inst)
{
    return (inst ? inst : definst)->colormap;
}

gint RrRedOffset (const RrInstance *inst)
{
    return (inst ? inst : definst)->red_offset;
}

gint RrGreenOffset (const RrInstance *inst)
{
    return (inst ? inst : definst)->green_offset;
}

gint RrBlueOffset (const RrInstance *inst)
{
    return (inst ? inst : definst)->blue_offset;
}

gint RrRedShift (const RrInstance *inst)
{
    return (inst ? inst : definst)->red_shift;
}

gint RrGreenShift (const RrInstance *inst)
{
    return (inst ? inst : definst)->green_shift;
}

gint RrBlueShift (const RrInstance *inst)
{
    return (inst ? inst : definst)->blue_shift;
}

gint RrRedMask (const RrInstance *inst)
{
    return (inst ? inst : definst)->red_mask;
}

gint RrGreenMask (const RrInstance *inst)
{
    return (inst ? inst : definst)->green_mask;
}

gint RrBlueMask (const RrInstance *inst)
{
    return (inst ? inst : definst)->blue_mask;
}

guint RrPseudoBPC (const RrInstance *inst)
{
    return (inst ? inst : definst)->pseudo_bpc;
}

XColor *RrPseudoColors (const RrInstance *inst)
{
    return (inst ? inst : definst)->pseudo_colors;
}