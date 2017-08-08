/* gtexEqtlClusterTrack - Track of clustered expression QTL's from GTEx */

/* Copyright (C) 2017 The Regents of the University of California 
 * See README in this or parent directory for licensing information. */

#include "common.h"
#include "dystring.h"
#include "hgTracks.h"
#include "gtexInfo.h"
#include "gtexTissue.h"
#include "gtexUi.h"
#include "gtexEqtlCluster.h"

struct gtexEqtlClusterTrack
/* Track info for filtering (maybe later for draw, map) */
{
    struct gtexTissue *tissues; /*  Tissue names, descriptions */
    struct hash *tissueHash;    /* Tissue info, keyed by UCSC name, filtered by UI */
    double minEffect;           /* Effect size filter (abs value) */
    double minProb;             /* Probability filter */
};

/* Track constants */

#define TISSUE_COLOR_DOT        "*"

/* Utility functions */

static struct gtexEqtlCluster *loadOne(char **row)
/* Load up gtexEqtlCluster from array of strings. */
{
return gtexEqtlClusterLoad(row);
}

static boolean filterTissuesFromCart(struct track *track, struct gtexEqtlClusterTrack *extras)
/* Check cart for tissue selection. Populate track tissues hash */
{
char *version = gtexVersionSuffix(track->table);
extras->tissues = gtexGetTissues(version);
extras->tissueHash = hashNew(0);
struct gtexTissue *tis = NULL;
for (tis = extras->tissues; tis != NULL; tis = tis->next)
    hashAdd(extras->tissueHash, tis->name, tis);

// if all tissues included, return full hash
if (!cartListVarExistsAnyLevel(cart, track->tdb, FALSE, GTEX_TISSUE_SELECT))
    return FALSE;

// create tissue hash with only included tissues
struct hash *tisHash = hashNew(0);
struct slName *selectedValues = cartOptionalSlNameListClosestToHome(cart, track->tdb,
                                                    FALSE, GTEX_TISSUE_SELECT);
if (selectedValues == NULL)
    return FALSE;

struct slName *name;
for (name = selectedValues; name != NULL; name = name->next)
    {
    tis = (struct gtexTissue *)hashFindVal(extras->tissueHash, name->name);
    if (tis != NULL)
        hashAdd(tisHash, name->name, tis);
    }
extras->tissueHash = tisHash;
return TRUE;
}

static void eqtlExcludeTissue(struct gtexEqtlCluster *eqtl, int i)
/* Mark the tissue to exclude from display */
{
eqtl->expScores[i] = 0.0;
}

static boolean eqtlIsExcludedTissue(struct gtexEqtlCluster *eqtl, int i)
/* Check if eQTL is excluded */
{
return (eqtl->expScores[i] == 0.0);
}

static boolean eqtlIncludeFilter(struct track *track, void *item)
/* Apply all filters (except gene) to eQTL item. Invoked by filterTissues method */
{
int i;
int excluded = 0;
double maxEffect = 0.0;
double maxProb = 0.0;
struct gtexEqtlCluster *eqtl = (struct gtexEqtlCluster *)item;
struct gtexEqtlClusterTrack *extras = (struct gtexEqtlClusterTrack *)track->extraUiData;
for (i=0; i<eqtl->expCount; i++)
    {
    if (hashFindVal(extras->tissueHash, eqtl->expNames[i]))
        {
        maxEffect = fmax(maxEffect, fabs(eqtl->expScores[i]));
        maxProb = fmax(maxProb, fabs(eqtl->expProbs[i]));
        }
    else
        {
        eqtlExcludeTissue(eqtl, i);
        excluded++;
        }
    }
// exclude if no tissues match selector
if (excluded == eqtl->expCount || 
    // or if variant has no selected tissue where effect size is above cutoff
    maxEffect < extras->minEffect || 
    // or if variant has no selected tissue where probability is above cutoff
    maxProb < extras->minProb)
        return FALSE;
return TRUE;
}

static int eqtlTissueCount(struct gtexEqtlCluster *eqtl)
/* Return count of non-excluded tissues in the item */
{
int included = 0;
int i;
for (i=0; i<eqtl->expCount; i++)
    if (!eqtlIsExcludedTissue(eqtl, i))
        included++;
return included;
}

static int eqtlTissueIndex(struct gtexEqtlCluster *eqtl)
/* Return index of first non-excluded tissue in an eQTL cluster. Used for single-tissue items. */
{
int i;
for (i=0; i<eqtl->expCount; i++)
    if (!eqtlIsExcludedTissue(eqtl, i))
        return i;
return -1;
}

static struct rgbColor eqtlTissueColor(struct track *track, struct gtexEqtlCluster *eqtl)
/* Return tissue color for single-tissue item, or NULL if none found */
{
int i = eqtlTissueIndex(eqtl);
assert(i>=0);
struct gtexEqtlClusterTrack *extras = (struct gtexEqtlClusterTrack *)track->extraUiData;
struct gtexTissue *tis = (struct gtexTissue *)hashFindVal(extras->tissueHash, eqtl->expNames[i]);
assert (tis);
return (struct rgbColor){.r=COLOR_32_BLUE(tis->color), .g=COLOR_32_GREEN(tis->color), 
                .b=COLOR_32_RED(tis->color)};
}

static char *eqtlSourcesLabel(struct gtexEqtlCluster *eqtl)
/* Right label is tissue (or number of tissues if >1) */
{
int ct = eqtlTissueCount(eqtl);
if (ct == 1)
    {
    int i = eqtlTissueIndex(eqtl);
    if (i<0)
        errAbort("GTEx eQTL %s/%s track tissue index is negative", eqtl->name, eqtl->target);
    return eqtl->expNames[i];
    }
struct dyString *ds = dyStringNew(0);
dyStringPrintf(ds, "%d tissues", ct);
return dyStringCannibalize(&ds);
}

/* Track methods */

static void gtexEqtlClusterLoadItems(struct track *track)
/* Load items in window and prune those that don't pass filter */
{
/* initialize track info */
struct gtexEqtlClusterTrack *extras;
AllocVar(extras);
track->extraUiData = extras;

// filter by gene via SQL
char cartVar[64];
safef(cartVar, sizeof cartVar, "%s.%s", track->track, GTEX_EQTL_GENE);
char *gene = cartNonemptyString(cart, cartVar);
char *where = NULL;
if (gene)
    {
    struct dyString *ds = dyStringNew(0);
    sqlDyStringPrintfFrag(ds, "%s = '%s'", GTEX_EQTL_GENE_FIELD, gene); 
    where = dyStringCannibalize(&ds);
    }
bedLoadItemWhere(track, track->table, where, (ItemLoader)loadOne);

safef(cartVar, sizeof cartVar, "%s.%s", track->track, GTEX_EQTL_EFFECT);
extras->minEffect = fabs(cartUsualDouble(cart, cartVar, GTEX_EFFECT_MIN_DEFAULT));
safef(cartVar, sizeof cartVar, "%s.%s", track->track, GTEX_EQTL_PROBABILITY);
extras->minProb = cartUsualDouble(cart, cartVar, 0.0);
boolean hasTissueFilter = filterTissuesFromCart(track, extras);
if (!hasTissueFilter && extras->minEffect == 0.0 && extras->minProb == 0.0)
    return;

// more filtering
filterItems(track, eqtlIncludeFilter, "include");
}

static char *gtexEqtlClusterItemName(struct track *track, void *item)
/* Left label is gene name */
{
struct gtexEqtlCluster *eqtl = (struct gtexEqtlCluster *)item;
return eqtl->target;
}

static Color gtexEqtlClusterItemColor(struct track *track, void *item, struct hvGfx *hvg)
/* Color by highest effect in list (blue -, red +), with brighter for higher effect (teal, fuschia) */
{
struct gtexEqtlCluster *eqtl = (struct gtexEqtlCluster *)item;
double maxEffect = 0.0;
int i;
for (i=0; i<eqtl->expCount; i++)
    {
    if (eqtlIsExcludedTissue(eqtl, i))
        continue;
    double effect = eqtl->expScores[i];
    if (fabs(effect) > fabs(maxEffect))
        maxEffect = effect;
    }
double cutoff = 2.0;
if (maxEffect < 0.0)
    {
    /* down-regulation displayed as blue */
    if (maxEffect < 0.0 - cutoff)
        return MG_CYAN;
    return MG_BLUE;
    }
/* up-regulation displayed as red */
if (maxEffect > cutoff)
    return MG_MAGENTA;
return MG_RED;
}

static int gtexEqtlClusterItemRightPixels(struct track *track, void *item)
/* Return number of pixels we need to the right of an item (for sources label). */
{
struct gtexEqtlCluster *eqtl = (struct gtexEqtlCluster *)item;
int ret = mgFontStringWidth(tl.font, eqtlSourcesLabel(eqtl));
if (eqtlTissueCount(eqtl) == 1)
    ret += mgFontStringWidth(tl.font, TISSUE_COLOR_DOT);
return ret;
}

static void gtexEqtlClusterDrawItemAt(struct track *track, void *item, 
	struct hvGfx *hvg, int xOff, int y, 
	double scale, MgFont *font, Color color, enum trackVisibility vis)
/* Draw GTEx eQTL cluster with right label indicating source(s) */
{
bedDrawSimpleAt(track, item, hvg, xOff, y, scale, font, color, vis);
if (vis != tvFull && vis != tvPack)
    return;

/* Draw text to the right */
struct gtexEqtlCluster *eqtl = (struct gtexEqtlCluster *)item;
int x2 = round((double)((int)eqtl->chromEnd-winStart)*scale) + xOff;
int x = x2 + tl.mWidth/2;
char *label = eqtlSourcesLabel(eqtl);
int w = mgFontStringWidth(font, label);
hvGfxTextCentered(hvg, x, y, w, track->heightPer, MG_BLACK, font, label);
if (eqtlTissueCount(eqtl) == 1)
    {
    // append asterisk in tissue color
    struct rgbColor tisColor = eqtlTissueColor(track, eqtl);
    x += w;
    w = mgFontStringWidth(font, TISSUE_COLOR_DOT);
    int ix = hvGfxFindColorIx(hvg, tisColor.r, tisColor.g, tisColor.b);
    font = mgFontForSizeAndStyle(tl.textSize, "bold");
    hvGfxTextCentered(hvg, x, y, w, track->heightPer, ix, font, TISSUE_COLOR_DOT);
    }
}

static void gtexEqtlClusterMapItem(struct track *track, struct hvGfx *hvg, void *item, 
                                char *itemName, char *mapItemName, int start, int end, 
                                int x, int y, int width, int height)
/* Create a map box on item and label with list of tissues with colors and effect size */
{
char *title = itemName;
if (track->limitedVis != tvDense)
    {
    struct gtexEqtlCluster *eqtl = (struct gtexEqtlCluster *)item;
    // Experiment: construct list of tissues with colors and effect sizes for mouseover
    //struct gtexEqtlClusterTrack *extras = (struct gtexEqtlClusterTrack *)track->extraUiData;
    //struct hash *tissueHash = extras->tissueHash;
    struct dyString *ds = dyStringNew(0);
    dyStringPrintf(ds, "%s/%s: ", eqtl->name, eqtl->target);
    int i;
    for (i=0; i<eqtl->expCount; i++)
        {
        if (eqtlIsExcludedTissue(eqtl, i))
            continue;
        double effect= eqtl->expScores[i];
        dyStringPrintf(ds, "%s(%s%0.2f)%s", eqtl->expNames[i], effect < 0 ? "" : "+", effect, 
                        i < eqtl->expCount - 1 ? ", " : "");
        //struct gtexTissue *tis = (struct gtexTissue *)hashFindVal(tissueHash, eqtl->expNames[i]);
        //unsigned color = tis ? tis->color : 0;       // BLACK
        //char *name = tis ? tis->name : "unknown";
        //#dyStringPrintf(ds,"<tr><td style='color: #%06X;'>*</td><td>%s</td><td>%s%0.2f</td></tr>\n", 
                                //color, name, effect < 0 ? "" : "+", effect); 
        }
    title = dyStringCannibalize(&ds);
    }
int w = width + gtexEqtlClusterItemRightPixels(track, item);
genericMapItem(track, hvg, item, title, itemName, start, end, x, y, w, height);
}

void gtexEqtlClusterMethods(struct track *track)
/* BED5+5 with target, expNames,expScores, expProbs */
{
track->loadItems = gtexEqtlClusterLoadItems;
track->itemName  = gtexEqtlClusterItemName;
track->itemColor = gtexEqtlClusterItemColor;
track->itemRightPixels = gtexEqtlClusterItemRightPixels;
track->drawItemAt = gtexEqtlClusterDrawItemAt;
track->mapItem = gtexEqtlClusterMapItem;
}
