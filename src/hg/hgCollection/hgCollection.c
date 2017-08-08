/* hgCollection - hub builder */

/* Copyright (C) 2017 The Regents of the University of California 
 * See README in this or parent directory for licensing information. */
#include "common.h"
#include "cartTrackDb.h"
#include "trackHub.h"
#include "trashDir.h"
#include "hubConnect.h"
#include "hui.h"
#include "grp.h"
#include "cheapcgi.h"
#include "jsHelper.h"
#include "web.h"
#include "knetUdc.h"
#include "api.h"
#include "genbank.h"
#include "htmshell.h"
#include "jsonParse.h"
#include "customComposite.h"
#include "stdlib.h"

/* Global Variables */
struct cart *cart;		/* CGI and other variables */
struct hash *oldVars = NULL;	/* The cart before new cgi stuff added. */
char *genome = NULL;		/* Name of genome - mouse, human, etc. */
char *database = NULL;		/* Current genome database - hg17, mm5, etc. */
char *regionType = NULL;	/* genome, ENCODE pilot regions, or specific position range. */
struct grp *fullGroupList = NULL;	/* List of all groups. */
struct trackDb *fullTrackList = NULL;	/* List of all tracks in database. */
 struct pipeline *compressPipeline = (struct pipeline *)NULL;


// Null terminated list of CGI Variables we don't want to save permanently:
char *excludeVars[] = {"Submit", "submit", "hgva_startQuery", NULL,};

struct track
{
struct track *next;
struct track *trackList;
struct trackDb *tdb;
char *name;
char *shortLabel;
char *longLabel;
char *visibility;
unsigned long color;
};

char *getString(char **input)
// grab a quoted string out of text blob
{
char *ptr = *input;

if (*ptr != '"')
    errAbort("string must start with \"");
ptr++;
char *ret = ptr;
for(; *ptr != '"'; ptr++)
    ;
*ptr = 0;
ptr++;

if (*ptr == ',')
    ptr++;

if (startsWith("coll_", ret))
    ret = ret + 5;

*input = ptr;
return ret;
}

char *makeUnique(struct hash *nameHash, char *name)
// Make the name of this track unique.
{
char *skipHub = trackHubSkipHubName(name);
if (hashLookup(nameHash, skipHub) == NULL)
    {
    hashStore(nameHash, name);
    return skipHub;
    }

unsigned count = 0;
char buffer[4096];

for(;; count++)
    {
    safef(buffer, sizeof buffer, "%s%d", skipHub, count);
    if (hashLookup(nameHash, buffer) == NULL)
        {
        hashStore(nameHash, buffer);
        return cloneString(buffer);
        }
    }

return NULL;
}

static boolean trackCanBeAdded(struct trackDb *tdb)
// are we allowing this track into a custom composite
{
return  (tdb->subtracks == NULL) && !startsWith("wigMaf",tdb->type) &&  (startsWith("wig",tdb->type) || startsWith("bigWig",tdb->type)) ;
}

static void printGroup(char *parent, struct trackDb *tdb, boolean folder, boolean user)
// output the table rows for a group
{
char *userString = "";
char *prefix = "";

if (user)
    {
    prefix = "coll_";
    if (tdb->parent && tdb->subtracks) 
        userString = "class='user view'";
    else
        userString = "class='user'";
    }
    
#define IMAKECOLOR_32(r,g,b) ( ((unsigned int)b<<0) | ((unsigned int)g << 8) | ((unsigned int)r << 16))

jsInlineF("<tr color='#%06x' visibility='%s'  data-tt-parent-id='%s%s' data-tt-id='%s%s' %s><td><span class='%s'>%s</span></td>",  IMAKECOLOR_32(tdb->colorR,tdb->colorG,tdb->colorB), hStringFromTv(tdb->visibility), prefix, parent,prefix,  trackHubSkipHubName(tdb->track),   userString, folder ? "folder" : "file", tdb->shortLabel );
jsInlineF("<td>%s</td></tr>", tdb->longLabel);


if (tdb->subtracks)
    {
    struct trackDb *subTdb;

    for(subTdb = tdb->subtracks; subTdb; subTdb = subTdb->next)
        printGroup(trackHubSkipHubName(tdb->track), subTdb, user && (subTdb->subtracks != NULL), user);
    }
}

static void outHubHeader(FILE *f, char *db, char *hubName)
// output a track hub header
{
char *hubFile = strrchr(hubName, '/') + 1;

fprintf(f,"hub hub1\n\
shortLabel User Composite\n\
longLabel User Composite\n\
genomesFile %s\n\
email braney@soe.ucsc.edu\n\
descriptionUrl hub.html\n\n", hubFile);
fprintf(f,"genome %s\n\
trackDb %s\n\n", db, hubFile);  
}


static char *getHubName(char *db)
// get the name of the hub to use for user collections
{
struct tempName hubTn;
char buffer[4096];
safef(buffer, sizeof buffer, "%s-%s", customCompositeCartName, db);
char *hubName = cartOptionalString(cart, buffer);

if (hubName == NULL)
    {
    trashDirDateFile(&hubTn, "hgComposite", "hub", ".txt");
    hubName = cloneString(hubTn.forCgi);
    cartSetString(cart, buffer, hubName);
    FILE *f = mustOpen(hubName, "a");
    outHubHeader(f, db, hubName);
    fclose(f);
    cartSetString(cart, "hubUrl", hubName);
    cartSetString(cart, hgHubConnectRemakeTrackHub, hubName);
    }
return hubName;
}

static bool subtrackEnabledInTdb(struct trackDb *subTdb)
/* Return TRUE unless the subtrack was declared with "subTrack ... off". */
{
bool enabled = TRUE;
char *words[2];
char *setting;
if ((setting = trackDbLocalSetting(subTdb, "parent")) != NULL)
    {
    if (chopLine(cloneString(setting), words) >= 2)
        if (sameString(words[1], "off"))
            enabled = FALSE;
    }
else
    return subTdb->visibility != tvHide;

return enabled;
}

bool isSubtrackVisible(struct trackDb *tdb)
/* Has this subtrack not been deselected in hgTrackUi or declared with
 *  * "subTrack ... off"?  -- assumes composite track is visible. */
{
boolean overrideComposite = (NULL != cartOptionalString(cart, tdb->track));
bool enabledInTdb = subtrackEnabledInTdb(tdb);
char option[1024];
safef(option, sizeof(option), "%s_sel", tdb->track);
boolean enabled = cartUsualBoolean(cart, option, enabledInTdb);
if (overrideComposite)
    enabled = TRUE;
return enabled;
}


bool isParentVisible( struct trackDb *tdb)
// Are this track's parents visible?
{
if (tdb->parent == NULL)
    return TRUE;

if (!isParentVisible(tdb->parent))
    return FALSE;

char *cartVis = cartOptionalString(cart, tdb->parent->track);
boolean vis;
if (cartVis != NULL) 
    vis =  differentString(cartVis, "hide");
else if (tdbIsSuperTrack(tdb->parent))
    vis = tdb->parent->isShow;
else
    vis = tdb->parent->visibility != tvHide;

return vis;
}


void addVisibleTracks()
// add the visible tracks table rows
{
printf("<tr data-tt-id='visible' ><td><span class='file'>All Visible</td><td>All the tracks visible in hgTracks</td></tr>\n");
struct trackDb *tdb;
for(tdb = fullTrackList; tdb; tdb = tdb->next)
    {
    if (isParentVisible(tdb) &&  isSubtrackVisible(tdb))
        {
        printGroup("visible", tdb, FALSE, FALSE);
        }
    }
}

void doTable()
// output the tree table
{
char *hubName = hubNameFromUrl(getHubName(database));
jsInlineF("$('#tracks tr:last').after(\"");
struct grp *curGroup;
for(curGroup = fullGroupList; curGroup;  curGroup = curGroup->next)
    {
    if ((hubName != NULL) && sameString(curGroup->name, hubName))
        break;
    }
if (curGroup != NULL)
    {
    // print out all the tracks in this group
    struct trackDb *tdb;
    for(tdb = fullTrackList; tdb;  tdb = tdb->next)
        {
        if (sameString(tdb->grp, hubName))
            printGroup("collections", tdb, TRUE, TRUE);
        }
    }
//addVisibleTracks();
for(curGroup = fullGroupList; curGroup;  curGroup = curGroup->next)
    {
    if ((hubName != NULL) && sameString(curGroup->name, hubName))
        continue;
    jsInlineF("<tr data-tt-id='%s'><td><span class='file'>%s</span></td><td></td></tr>", curGroup->name, curGroup->label );
    struct trackDb *tdb;
    for(tdb = fullTrackList; tdb;  tdb = tdb->next)
        {
        if ( sameString(tdb->grp, curGroup->name))
            {
            printGroup(curGroup->name, tdb, FALSE, FALSE);
            }
        }
    }
jsInlineF("\");\n");
jsInlineF("collections.init();\n");
}

static void onclickJumpToTop(char *id)
/* CSP-safe click handler arrows that cause scroll to top */
{
jsOnEventById("click", id, "$('html,body').scrollTop(0);");
}

static void printHelp()
// print out the help page
{
puts(
"<a name='INFO_SECTION'></a>\n"
"    <div class='row gbSectionBanner'>\n"
"        <div class='col-md-11'>Help</div>\n"
"        <div class='col-md-1'>\n"
);
#define DATA_INFO_JUMP_ARROW_ID    "hgGtexDataInfo_jumpArrow"
printf(
"            <i id='%s' title='Jump to top of page' \n"
"               class='gbIconArrow fa fa-lg fa-arrow-circle-up'></i>\n",
DATA_INFO_JUMP_ARROW_ID
);
onclickJumpToTop(DATA_INFO_JUMP_ARROW_ID);
puts(
"       </div>\n"
"    </div>\n"
);
puts(
"    <div class='row gbTrackDescriptionPanel'>\n"
"       <div class='gbTrackDescription'>\n");
puts("<div class='dataInfo'>");
puts("</div>");
webIncludeHelpFileSubst("hgCompositeHelp", NULL, FALSE);

puts("<div class='dataInfo'>");
puts("</div>");

puts(
"     </div>\n"
"   </div>\n");
}

void doMainPage()
/* Print out initial HTML of control page. */
{
webStartGbNoBanner(cart, database, "Collections");
webIncludeResourceFile("jquery.treetable.css");
webIncludeResourceFile("jquery.treetable.theme.default.css");
webIncludeResourceFile("gb.css");
//webIncludeResourceFile("jWest.css");
webIncludeResourceFile("spectrum.min.css");
webIncludeResourceFile("hgGtexTrackSettings.css");

webIncludeFile("inc/hgCollection.html");

printHelp();
doTable();

puts("<script src=\"//code.jquery.com/jquery-1.9.1.min.js\"></script>");
puts("<script src=\"//code.jquery.com/ui/1.10.3/jquery-ui.min.js\"></script>");
jsIncludeFile("jquery.treetable.js", NULL);
jsIncludeFile("utils.js", NULL);
jsIncludeFile("ajax.js", NULL);
jsIncludeFile("spectrum.min.js", NULL);
jsIncludeFile("hgCollection.js", NULL);
webEndGb();
}

static char *getSqlBigWig(struct sqlConnection *conn, char *db, struct trackDb *tdb)
// figure out the bigWig for native tables
{
char buffer[4096];

safef(buffer, sizeof buffer, "NOSQLINJ select fileName from %s", tdb->table);
return sqlQuickString(conn, buffer);
}

char *getUrl(struct sqlConnection *conn, char *db,  struct track *track, struct hash *nameHash)
// get the bigDataUrl for a track
{
struct trackDb *tdb = hashMustFindVal(nameHash, track->name);

if (tdb == NULL)
    errAbort("cannot find trackDb for %s\n", track->name);

char *bigDataUrl = trackDbSetting(tdb, "bigDataUrl");
if (bigDataUrl == NULL)
    {
    if (startsWith("bigWig", tdb->type))
        bigDataUrl = getSqlBigWig(conn, db, tdb);
    }
return bigDataUrl;
}

void outTdb(struct sqlConnection *conn, char *db, FILE *f, char *name,  struct trackDb *tdb, char *parent, char *visibility, unsigned int color, struct track *track, struct hash *nameHash, struct hash *collectionNameHash, int numTabs)
// out the trackDb for one track
{
char *dataUrl = NULL;
char *bigDataUrl = trackDbSetting(tdb, "bigDataUrl");
char *tabs = "\t";
if (numTabs == 2)
    tabs = "\t\t";

if (bigDataUrl == NULL)
    {
    if (startsWith("bigWig", tdb->type))
        dataUrl = getSqlBigWig(conn, db, tdb);
    }
struct hashCookie cookie = hashFirst(tdb->settingsHash);
struct hashEl *hel;
fprintf(f, "%strack %s\n",tabs, makeUnique(collectionNameHash, name));
fprintf(f, "%sshortLabel %s\n",tabs, track->shortLabel);
fprintf(f, "%slongLabel %s\n",tabs, track->longLabel);
while ((hel = hashNext(&cookie)) != NULL)
    {
    if (differentString(hel->name, "parent") && differentString(hel->name, "polished")&& differentString(hel->name, "shortLabel")&& differentString(hel->name, "longLabel")&& differentString(hel->name, "color")&& differentString(hel->name, "visibility")&& differentString(hel->name, "track")&& differentString(hel->name, "trackNames")&& differentString(hel->name, "superTrack"))
        fprintf(f, "%s%s %s\n", tabs,hel->name, (char *)hel->val);
    }
if (bigDataUrl == NULL)
    {
    if (dataUrl != NULL)
        fprintf(f, "%sbigDataUrl %s\n", tabs,dataUrl);
    }
fprintf(f, "%sparent %s\n",tabs,parent);
fprintf(f, "%scolor %d,%d,%d\n", tabs,(color >> 16) & 0xff,(color >> 8) & 0xff,color & 0xff);
fprintf(f, "%svisibility %s\n",tabs,visibility);
fprintf(f, "\n");
}

static void outComposite(FILE *f, struct track *collection)
// output a composite header for user composite
{
char *parent = collection->name;
char *shortLabel = collection->shortLabel;
char *longLabel = collection->longLabel;
fprintf(f,"track %s\n\
shortLabel %s\n\
compositeTrack on\n\
aggregate none\n\
longLabel %s\n\
%s on\n\
type wig \n\
visibility full\n\n", parent, &shortLabel[2], longLabel, CUSTOM_COMPOSITE_SETTING);
}

int snakePalette2[] =
{
0x1f77b4, 0xaec7e8, 0xff7f0e, 0xffbb78, 0x2ca02c, 0x98df8a, 0xd62728, 0xff9896, 0x9467bd, 0xc5b0d5, 0x8c564b, 0xc49c94, 0xe377c2, 0xf7b6d2, 0x7f7f7f, 0xc7c7c7, 0xbcbd22, 0xdbdb8d, 0x17becf, 0x9edae5
};

static char *skipColl(char *str)
{
if (startsWith("coll_", str))
    return &str[5];
return str;
}

static void outView(FILE *f, struct sqlConnection *conn, char *db, struct track *view, char *parent, struct hash *nameHash, struct hash *collectionNameHash)
// output a view to a trackhub
{
fprintf(f,"\ttrack %s\n\
\tshortLabel %s\n\
\tlongLabel %s\n\
\tview %s \n\
\tparent %s \n\
\tcolor %ld,%ld,%ld \n\
\tvisibility %s\n", view->name, &view->shortLabel[2], view->longLabel, view->name, parent, 0xff& (view->color >> 16),0xff& (view->color >> 8),0xff& (view->color), view->visibility);
//fprintf(f,"\tequation +\n");
fprintf(f, "\n");

//int useColor = 0;
struct track *track = view->trackList;
for(; track; track = track->next)
    {
    struct trackDb *tdb = hashMustFindVal(nameHash, skipColl(track->name));

    outTdb(conn, db, f, skipColl(track->name),tdb, view->name, track->visibility, track->color, track,  nameHash, collectionNameHash, 2);
    //useColor++;
    }

}

void updateHub(char *db, struct track *collectionList, struct hash *nameHash)
// save our state to the track hub
{
char *hubName = getHubName(db);

chmod(hubName, 0666);
FILE *f = mustOpen(hubName, "w");
struct hash *collectionNameHash = newHash(6);

outHubHeader(f, db, hubName);
//int useColor = 0;
struct track *collection;
struct sqlConnection *conn = hAllocConn(db);
for(collection = collectionList; collection; collection = collection->next)
    {
    outComposite(f, collection);
    struct trackDb *tdb;
    struct track *track;
    for (track = collection->trackList; track; track = track->next)
        {
        if (track->trackList != NULL)
            {
            outView(f, conn, db, track, collection->name,  nameHash, collectionNameHash);
            }
        else
            {
            tdb = hashMustFindVal(nameHash, track->name);

            outTdb(conn, db, f, track->name,tdb, collection->name, track->visibility, track->color, track,  nameHash, collectionNameHash, 1);
            /*
            useColor++;
            if (useColor == (sizeof snakePalette2 / sizeof(int)))
                useColor = 0;
                */
            }
        }
    }
fclose(f);
hFreeConn(&conn);
}

static unsigned long hexStringToLong(char *str)
{
/*
char buffer[1024];

strcpy(buffer, "0x");
strcat(buffer, &str[1]);
*/

return strtol(&str[1], NULL, 16);
}

static struct track *parseJson(char *jsonText)
// parse the JSON of the treetable from the Javascript
{
struct hash *trackHash = newHash(5);
struct track *collectionList = NULL;
struct track *track;
char *ptr = jsonText;
if (*ptr != '[')
    errAbort("element didn't start with [");
ptr++;

do
    {
    if (*ptr != '[')
        errAbort("element didn't start with [");
    ptr++;

    AllocVar(track);
    char *parentName = getString(&ptr);
    if (sameString(parentName, "collections"))
        slAddHead(&collectionList, track);
    else
        {
        struct track *parent = hashMustFindVal(trackHash, parentName);
        slAddTail(&parent->trackList, track);
        }

    track->shortLabel = getString(&ptr);
    track->longLabel = getString(&ptr);
    track->name = getString(&ptr);
    track->visibility = getString(&ptr);
    char *colorString = getString(&ptr);

    track->color = hexStringToLong(colorString);
    hashAdd(trackHash, track->name, track);
    if (*ptr != ']')
        errAbort("element didn't end with ]");
    ptr++;
    if (*ptr == ',')
        ptr++;
    } while (*ptr != ']');

slReverse(&collectionList);
return collectionList;
}

void doAjax(char *db, char *jsonText, struct hash *nameHash)
// Save our state
{
struct track *collectionList = parseJson(jsonText);

updateHub(db, collectionList, nameHash);
}

static void buildNameHash(struct hash *nameHash, struct trackDb *list)
{
if (list == NULL)
    return;

struct trackDb *tdb;
for(tdb = list; tdb;  tdb = tdb->next)
    {
    hashAdd(nameHash, trackHubSkipHubName(tdb->track), tdb);
    buildNameHash(nameHash, tdb->subtracks);
    }
}

static struct trackDb *traverseTree(struct trackDb *oldList, struct hash *groupHash)
// add acceptable tracks to our tree
{
struct trackDb *newList = NULL, *tdb, *tdbNext;

for(tdb = oldList;  tdb ; tdb = tdbNext)
    {
    tdbNext = tdb->next;
    if (tdb->subtracks)
        {
        tdb->subtracks = traverseTree(tdb->subtracks, groupHash);

        if (tdb->subtracks)
            {
            hashStore(groupHash, tdb->grp);
            slAddHead(&newList, tdb);
            }
        }
    else
        {
        if (trackCanBeAdded(tdb))
            {
            hashStore(groupHash, tdb->grp);
            slAddHead(&newList, tdb);
            }
        }
    }
slReverse(&newList);

return newList;
}

static void pruneTrackList(struct trackDb **fullTrackList, struct grp **fullGroupList)
// drop track types we don't grok yet
{
struct hash *groupHash = newHash(5);

*fullTrackList = traverseTree(*fullTrackList, groupHash);
struct grp *newGroupList = NULL, *grp, *nextGrp;

for (grp = *fullGroupList; grp; grp = nextGrp)
    {
    nextGrp = grp->next;

    if (hashLookup(groupHash, grp->name))
        slAddHead(&newGroupList, grp);
    }
slReverse(&newGroupList);
*fullGroupList = newGroupList;
}

void doMiddle(struct cart *theCart)
/* Set up globals and make web page */
{
cart = theCart;
getDbAndGenome(cart, &database, &genome, oldVars);
int timeout = cartUsualInt(cart, "udcTimeout", 300);
if (udcCacheTimeout() < timeout)
    udcSetCacheTimeout(timeout);
knetUdcInstall();
cartTrackDbInit(cart, &fullTrackList, &fullGroupList, TRUE);
pruneTrackList(&fullTrackList, &fullGroupList);
struct hash *nameHash = newHash(5);
buildNameHash(nameHash, fullTrackList);

char *jsonIn = cgiUsualString("jsonp", NULL);
fprintf(stderr, "BRANEY %s\n", jsonIn);
if (jsonIn != NULL)
    {
    doAjax(database, jsonIn, nameHash);
    apiOut("{\"serverSays\": \"bit me\"}", NULL);
    }
else
    {
    doMainPage();
    }
}

int main(int argc, char *argv[])
/* Process command line. */
{
long enteredMainTime = clock1000();

initGenbankTableNames(database);

cgiSpoof(&argc, argv);

boolean isCommandLine = (cgiOptionalString("cgiSpoof") != NULL);
if (!isCommandLine)
    htmlPushEarlyHandlers(); /* Make errors legible during initialization. */
oldVars = hashNew(10);

cartEmptyShellNoContent(doMiddle, hUserCookie(), excludeVars, oldVars);

if (! isCommandLine)
    cgiExitTime("hgCollection", enteredMainTime);
return 0;
}

