/* Details page for interact type tracks */

/* Copyright (C) 2018 The Regents of the University of California 
 * See README in this or parent directory for licensing information. */

#include "common.h"
#include "obscure.h"
#include "hdb.h"
#include "jksql.h"
#include "hgc.h"
#include "trashDir.h"
#include "hex.h"
#include <openssl/sha.h>

#include "interact.h"
#include "interactUi.h"

struct interactPlusRow
    {
    /* Keep field values in string format, for url processing */
    struct interactPlusRow *next;
    struct interact *interact;
    char **row;
    };

static struct interactPlusRow *getInteractsFromTable(struct trackDb *tdb, char *chrom, 
                                    int start, int end, char *name, char *foot)
/* Retrieve interact items at this position from track table */
{
struct sqlConnection *conn = NULL;
struct customTrack *ct = lookupCt(tdb->track);
char *table;
if (ct != NULL)
    {
    conn = hAllocConn(CUSTOM_TRASH);
    table = ct->dbTableName;
    }
else
    {
    conn = hAllocConnTrack(database, tdb);
    table = tdb->table;
    }
if (conn == NULL)
    return NULL;

struct interactPlusRow *iprs = NULL;
char **row;
int offset;
struct sqlResult *sr = hRangeQuery(conn, table, chrom, start, end, NULL, &offset);
int fieldCount = 0;
while ((row = sqlNextRow(sr)) != NULL)
    {
    struct interact *inter = interactLoadAndValidate(row+offset);
    // got one, save object and row representation
    struct interactPlusRow *ipr;
    AllocVar(ipr);
    ipr->interact = inter;
    char **fieldVals;
    if (fieldCount == 0)
        fieldCount = sqlCountColumns(sr);
    AllocArray(fieldVals, fieldCount);
    int i;
    for (i = 0; i < fieldCount; i++)
        fieldVals[i] = cloneString(row[i]);
    ipr->row = fieldVals;
    slAddHead(&iprs, ipr);
    }
sqlFreeResult(&sr);
hFreeConn(&conn);
return iprs;
}

static struct interactPlusRow *getInteractsFromFile(char *file, char *chrom, int start, int end, 
                                                        char *name, char *foot)
/* Retrieve interact items at this position from big file */
{
struct bbiFile *bbi = bigBedFileOpen(file);
struct lm *lm = lmInit(0);
struct bigBedInterval *bb, *bbList = bigBedIntervalQuery(bbi, chrom, start, end, 0, lm);
struct interactPlusRow *iprs = NULL;

for (bb = bbList; bb != NULL; bb = bb->next)
    {
    char startBuf[16], endBuf[16];
    int maxFields = 32;
    char *row[maxFields];      // big enough ?
    int fieldCount = bigBedIntervalToRow(bb, chrom, startBuf, endBuf, row, maxFields);
    struct interact *inter = interactLoadAndValidate(row);
    if (inter == NULL)
        continue;

    // got one, save object and row representation
    struct interactPlusRow *ipr;
    AllocVar(ipr);
    ipr->interact = inter;
    char **fieldVals;
    AllocArray(fieldVals, fieldCount);
    int i;
    for (i = 0; i < fieldCount; i++)
        fieldVals[i] = cloneString(row[i]);
    ipr->row = fieldVals;
    slAddHead(&iprs, ipr);
    }
return iprs;
}

static struct interactPlusRow *getInteractions(struct trackDb *tdb, char *chrom, int start, int end, 
                                                char *name, char *foot, int *retStart, int *retEnd)
/* Retrieve interact items at this position or name. 
 * Also any others with the same endpoint, if endpoint clicked on.
 * Return full extent of included interactions in returned start, end */
// NOTE: Consider sortable table of matching interactions
{
struct interactPlusRow *ipr, *iprs = NULL, *next, *filtered = NULL;
char *file = trackDbSetting(tdb, "bigDataUrl");
char *clusterMode = interactUiClusterMode(cart, tdb->track, tdb);
if (file != NULL)
    iprs = getInteractsFromFile(file, chrom, start, end, name, foot);
else
    iprs = getInteractsFromTable(tdb, chrom, start, end, name, foot);

int minStart = 999999999;
int maxEnd = 0;
for (ipr = iprs; ipr; ipr = next)
    {
    struct interact *inter = ipr->interact;
    next = ipr->next;
    if (!name || sameString(name, "."))
        {
        if (inter->chromStart != start || inter->chromEnd != end)
            continue;
        }
    else
        {
        char *match = inter->name;
        if (clusterMode)
            match = sameString(clusterMode, INTERACT_CLUSTER_SOURCE) ?
                                                inter->sourceName : inter->targetName;
        if (differentString(name, match))
            {
            if (clusterMode || !foot)
                continue;
            // if clicked on foot, look at endpoint names
            if (differentString(name, inter->sourceName) && differentString(name, inter->targetName))
                continue;
            }
        }
    minStart = inter->chromStart <  minStart ? inter->chromStart : minStart;
    maxEnd = inter->chromEnd > maxEnd ? inter->chromEnd : maxEnd;
    slAddHead(&filtered, ipr);
    }

*retStart = minStart;
*retEnd = maxEnd;
// consider sorting on score or position
return filtered;
}

static char *makeInteractRegionFile(char *name, struct interact *inters)
/* Create bed file in trash directory with end coordinates for multi-region mode */
{
struct tempName mrTn;
trashDirFile(&mrTn, "hgt", "custRgn_interact", ".bed");
FILE *f = fopen(mrTn.forCgi, "w");
if (f == NULL)
    errAbort("can't create temp file %s", mrTn.forCgi);
char regionInfo[1024];
int padding = 200;
safef(regionInfo, sizeof regionInfo, "#padding %d\n", padding);
mustWrite(f, regionInfo, strlen(regionInfo));
//warn("%s", regionInfo);

safef(regionInfo, sizeof regionInfo, "#shortDesc %s\n", name);
mustWrite(f, regionInfo, strlen(regionInfo));
//warn("%s", regionInfo);

struct interact *inter = NULL;
struct bed *region, *regions = NULL;
struct hash *uniqRegions = hashNew(0);
for (inter = inters; inter != NULL; inter = inter->next)
    {
    char buf[256];
    //safef(buf, sizeof buf, "%s:%d-%d", region1->chrom, region1->chromStart, region1->chromEnd);
    safef(buf, sizeof buf, "%s:%d-%d", inter->sourceChrom, inter->sourceStart, inter->sourceEnd);
    if (!hashLookup(uniqRegions, buf))
        {
        hashAdd(uniqRegions, cloneString(buf), NULL);
        AllocVar(region);
        region->chrom = inter->sourceChrom;
        region->chromStart = inter->sourceStart;
        region->chromEnd = inter->sourceEnd;
        slAddHead(&regions, region);
        }
    //safef(buf, sizeof buf, "%s:%d-%d", region2->chrom, region2->chromStart, region2->chromEnd);
    safef(buf, sizeof buf, "%s:%d-%d", inter->targetChrom, inter->targetStart, inter->targetEnd);
    if (!hashLookup(uniqRegions, buf))
        {
        hashAdd(uniqRegions, cloneString(buf), NULL);
        AllocVar(region);
        region->chrom = inter->chrom;
        region->chromStart = inter->targetStart;
        region->chromEnd = inter->targetEnd;
        slAddHead(&regions, region);
        }
    }
slSort(&regions, bedCmp);
for (region = regions; region != NULL; region = region->next)
    {
    safef(regionInfo, sizeof regionInfo, "%s\t%d\t%d\n",
                    region->chrom, region->chromStart, region->chromEnd);
    mustWrite(f, regionInfo, strlen(regionInfo));
    //warn("%s", regionInfo);
    }
fclose(f);

// create SHA1 file; used to see if file has changed
unsigned char hash[SHA_DIGEST_LENGTH];
SHA1((const unsigned char *)regionInfo, strlen(regionInfo), hash);
char newSha1[(SHA_DIGEST_LENGTH + 1) * 2];
hexBinaryString(hash, SHA_DIGEST_LENGTH, newSha1, (SHA_DIGEST_LENGTH + 1) * 2);
char sha1File[1024];
safef(sha1File, sizeof sha1File, "%s.sha1", mrTn.forCgi);
f = mustOpen(sha1File, "w");
mustWrite(f, newSha1, strlen(newSha1));
carefulClose(&f);
return cloneString(mrTn.forCgi);
}

static void multiRegionLink(char *name, struct interact *inters)
// Print link to multi-region view of ends if appropriate 
// (or provide a link to remove if already in this mode) 
{
if (inters->next == NULL && interactEndsOverlap(inters))
    return;
char *virtShortDesc = cartOptionalString(cart, "virtShortDesc");
boolean isVirtMode = cartUsualBoolean(cart, "virtMode", FALSE);
//warn("virtShortDesc: %s, name: %s", virtShortDesc, name);
if (isVirtMode && virtShortDesc && sameString(virtShortDesc, name))
    {
    printf("<br>Show interaction%s in "
                "<a href='hgTracks?"
                    "virtMode=0&"
                    "virtModeType=default'>"
                " normal browser view</a> (exit multi-region view)",
                        inters->next != NULL ? "s" : "");
    }
else
    {
    char *regionFile = makeInteractRegionFile(name, inters);
    //warn("regionFile: %s", regionFile);
    printf("<br>Show interaction ends in "
            "<a href='hgTracks?"
                "virtMode=1&"
                "virtModeType=customUrl&"
                "virtWinFull=on&"
                "virtShortDesc=%s&"
                "multiRegionsBedUrl=%s'>"
            " multi-region browser view </a>(custom region mode)",
                    name, cgiEncode(regionFile));
    if (isVirtMode)
        printf(" or "
                "<a href='hgTracks?"
                    "virtMode=0&"
                    "virtModeType=default'>"
                " normal browser view</a>");
    }
printf("&nbsp;&nbsp;&nbsp;");
printf("<a href=\"../goldenPath/help/multiRegionHelp.html\" target=_blank>(Help)</a>\n");
}

void doInteractRegionDetails(struct trackDb *tdb, struct interact *inter)
{
    /* print info for both regions */
/* Use different labels:
        1) directional (source/target)
        2) non-directional same chrom (lower/upper)
        3) non-directional other chrom (this/other)
*/
char startBuf[1024], endBuf[1024], sizeBuf[1024];
printf("<b>Interaction region:</b> ");
if (interactOtherChrom(inter))
    printf("across chromosomes<br>");
else
    {
    sprintLongWithCommas(startBuf, inter->chromStart+1);
    sprintLongWithCommas(endBuf, inter->chromEnd);
    sprintLongWithCommas(sizeBuf, inter->chromEnd - inter->chromStart);
    printf("<a href='hgTracks?position=%s:%d-%d' target='_blank'>%s:%s-%s</a>", 
                inter->chrom, inter->chromStart, inter->chromEnd,
                inter->chrom, startBuf, endBuf);
    printf("&nbsp;&nbsp;%s bp<br>\n", sizeBuf);
    }
printf("<br>");
char *region1Label = "Source";
char *region1Chrom = inter->sourceChrom;
int region1Start = inter->sourceStart;
int region1End = inter->sourceEnd;
char *region1Name = inter->sourceName;
if (isEmptyTextField(inter->sourceName))
    region1Name = "";

char *region2Label = "Target";
char *region2Chrom = inter->targetChrom;
int region2Start = inter->targetStart;
int region2End = inter->targetEnd;
char *region2Name = inter->targetName;
if (isEmptyTextField(inter->targetName))
    region2Name = "";

if (!interactUiDirectional(tdb))
    {
    if (interactOtherChrom(inter))
        {
        region1Label = "This";
        region2Label = "Other";
        }
    else
        {
        region1Label = "Lower";
        region2Label = "Upper";
        }
    }
// format and print
sprintLongWithCommas(startBuf, region1Start + 1);
sprintLongWithCommas(endBuf, region1End);
sprintLongWithCommas(sizeBuf, region1End - region1Start);
printf("<b>%s region:</b> %s&nbsp;&nbsp;"
                "<a href='hgTracks?position=%s:%d-%d' target='_blank'>%s:%s-%s</a> %s",
                region1Label, region1Name, region1Chrom, region1Start+1, region1End,
                region1Chrom, startBuf, endBuf, 
                inter->sourceStrand[0] == '.' ? "" : inter->sourceStrand);
printf("&nbsp;&nbsp;%s bp<br>\n", sizeBuf);

sprintLongWithCommas(startBuf, region2Start+1);
sprintLongWithCommas(endBuf, region2End);
sprintLongWithCommas(sizeBuf, region2End - region2Start);
printf("<b>%s region:</b> %s&nbsp;&nbsp;"
                "<a href='hgTracks?position=%s:%d-%d' target='_blank'>%s:%s-%s</a> %s",
                region2Label, region2Name, region2Chrom, region2Start+1, region2End,
                region2Chrom, startBuf, endBuf, 
                inter->targetStrand[0] == '.' ? "" : inter->targetStrand);
printf("&nbsp;&nbsp;%s bp<br>\n", sizeBuf);
int distance = interactRegionDistance(inter);
if (distance > 0)
    {
    // same chrom
    sprintLongWithCommas(sizeBuf, distance);
    printf("<b>Distance between midpoints:</b> %s bp<br>\n", sizeBuf); 
    }
}

void doInteractItemDetails(struct trackDb *tdb, struct interactPlusRow *ipr, char *item, 
                                boolean isMultiple, boolean doMultiRegion)
/* Details of interaction item */
{
struct interact *inter = ipr->interact;
struct slPair *fields = getFields(tdb, ipr->row);
printCustomUrlWithFields(tdb, inter->name, inter->name, TRUE, fields);
if (!isEmptyTextField(inter->name))
    printf("<b>Interaction:</b> %s<br>\n", inter->name);
printf("<b>Score:</b> %d<br>\n", inter->score);
printf("<b>Value:</b> %0.3f<br>\n", inter->value);
if (!isEmptyTextField(inter->exp))
    printf("<b>Experiment:</b> %s<br>\n", inter->exp);
puts("<p>");
if (!isMultiple)
    {
    doInteractRegionDetails(tdb, inter);
    if (doMultiRegion)
        multiRegionLink(inter->name, inter);
    }
}

static struct interact *iprsToInters(struct interactPlusRow *iprs)
/* Create list of interacts from interactPlusRows */
{
struct interactPlusRow *ipr;
struct interact *inters = NULL;
for (ipr = iprs; ipr != NULL; ipr = ipr->next)
    slAddHead(&inters, ipr->interact);
return inters;
}

void doInteractDetails(struct trackDb *tdb, char *item)
/* Details of interaction items */
{
char *chrom = cartString(cart, "c");
int start = cartInt(cart, "o");
int end = cartInt(cart, "t");
char *foot = cgiOptionalString("foot");
boolean doMultiRegion = trackDbSettingOn(tdb, "interactMultiRegion");
int minStart, maxEnd;
struct interactPlusRow *iprs = getInteractions(tdb, chrom, start, end, item, foot, &minStart, &maxEnd);
start = minStart;
end = maxEnd;
if (iprs == NULL)
    errAbort("Can't find interaction %s", item ? item : "");
int count = slCount(iprs);
char *clusterMode = interactUiClusterMode(cart, tdb->track, tdb);
if (count > 1 || clusterMode)
    {
    printf("<b>Interactions:</b> %d<p>", count);
    struct interact *inters = iprsToInters(iprs);
    if (clusterMode || foot)
        {
        char startBuf[1024], endBuf[1024], sizeBuf[1024];
        sprintLongWithCommas(startBuf, start + 1);
        sprintLongWithCommas(endBuf, end);
        sprintLongWithCommas(sizeBuf, end - start + 1);
        printf("<b>%s interactions region:</b> &nbsp;&nbsp;"
                        "<a href='hgTracks?position=%s:%d-%d' target='_blank'>%s:%s-%s</a> ",
                        item, chrom, start+1, end, chrom, startBuf, endBuf);
        printf("&nbsp;&nbsp;%s bp<br>\n", sizeBuf);
        }
    else
        {
        // overlapping items, same start/end/name
        doInteractRegionDetails(tdb, inters);
        }
    if (doMultiRegion)
        multiRegionLink(item, inters);
    printf("</p>");
    }

//genericHeader(tdb, item);
static struct interactPlusRow *ipr = NULL;
for (ipr = iprs; ipr != NULL; ipr = ipr->next)
    {
    if (count > 1)
        printf("<hr>\n");
    doInteractItemDetails(tdb, ipr, item, count > 1, doMultiRegion);
    if (foot || (clusterMode && count > 1))
        {
        struct interact *inter = ipr->interact;
        // just one interact (we have these in list for handling clusters earlier)
        inter->next = NULL;
        doInteractRegionDetails(tdb, inter);
        if (doMultiRegion)
            multiRegionLink(inter->name, inter);
        }
    if (count > 1 && !isEmptyTextField(ipr->interact->name) && sameString(ipr->interact->name, item))
        printf("<hr>\n");
    }
}


