/* cdwFindSymlinkable - Find files under wrangle dir that could be symlinked to the file under cdw/ to save space. */
#include "common.h"
#include "linefile.h"
#include "filePath.h"
#include "md5.h"
#include "hash.h"
#include "options.h"
#include "cdw.h"
#include "cdwLib.h"

boolean fix = FALSE;
char *idList = NULL;
char *submitDirFilter = NULL;

void usage()
/* Explain usage and exit. */
{
errAbort(
  "cdwFindSymlinkable - Find files under wrangle dir that could be symlinked to the file under cdw/ to save space.\n"
  "usage:\n"
  "   cdwFindSymlinkable find \n"
  "       does a dry run looking for symlinkable files \n"
  "   cdwFindSymlinkable fix \n"
  "       replaces files under wrangle or submit with symlinks to files under /data/cirm/cdw/.\n"
  "options:\n"
  "   -idList=290,291 limits operation to a comma-separated list of submitId's.\n"
  "   -submitDir=/my/submitDir limits operation to submits belonging to submitDir.\n"
  );
}

/* Command line validation table. */
static struct optionSpec options[] = {
   {"idList", OPTION_STRING},
   {"submitDir", OPTION_STRING},
   {NULL, 0},
};


void cdwFindSymlinkable()
/* Find files that have not been symlinked to cdw/ yet to save space. */
{
struct sqlConnection *conn = cdwConnectReadWrite();
char query[512];

sqlSafef(query, sizeof(query), "select id from cdwSubmit");

// optional filtering
if (idList && submitDirFilter)
    errAbort("You can use -idList or -submitDir, but not both at the same time.");

if (idList)
    {
    sqlSafefAppend(query, sizeof(query), " where id in (");
    unsigned *idArray = NULL; 
    int idCount = 0;
    sqlUnsignedDynamicArray(idList, &idArray, &idCount);
    int i;
    for (i=0; i < idCount; ++i)
	{
	if (i > 0)
	    sqlSafefAppend(query, sizeof(query), ",");
	sqlSafefAppend(query, sizeof(query), "%d", idArray[i]);
	}
    sqlSafefAppend(query, sizeof(query), ")");
    freeMem(idArray);
    }

if (submitDirFilter)
    {
    char query2[512];
    sqlSafef(query2, sizeof(query2), "select id from cdwSubmitDir where url='%s'", submitDirFilter);
    int submitDirId = sqlQuickNum(conn, query2);
    if (submitDirId <= 0)
	errAbort("%s not found", submitDirFilter);
    sqlSafefAppend(query, sizeof(query), " where submitDirId = %d", submitDirId);
    }

struct slInt *submitIdList = sqlQuickNumList(conn, query);
verbose(1, "%d submits\n", slCount(submitIdList));
char *newPath = NULL;
int filesSymlinked = 0;
off_t fileSpaceSaved = 0;
struct stat sb;
struct slInt *sel;
for (sel = submitIdList; sel != NULL; sel = sel->next)
    {
    verbose(1, "-----------------------------------------\n"); 
    verbose(1, "%d\n", sel->val); 

    struct cdwSubmit *es = cdwSubmitFromId(conn, sel->val);
    verbose(1, "url=%s submitDirId=%d manifestFileId=%d metaFileId=%d \n", 
	es->url, es->submitDirId, es->manifestFileId, es->metaFileId);
    
    struct cdwSubmitDir *ed = cdwSubmitDirFromId(conn, es->submitDirId);
    if (!ed)
	{
	verbose(1, "submitDir record missing id=%d\n", es->submitDirId);
	continue;
	}
    verbose(1, "submitDir url=%s\n", ed->url);

    verbose(1, "files:\n");
    sqlSafef(query, sizeof(query), "select id from cdwFile where submitId=%d", sel->val);
    struct slInt *fileIdList = sqlQuickNumList(conn, query);
    verbose(1, "%d files in submission\n", slCount(fileIdList));
    struct slInt *el;
    for (el = fileIdList; el != NULL; el = el->next)
	{
	verbose(1, "fileId %d\n", el->val); 
	struct cdwFile *ef = cdwFileFromId(conn, el->val);
	if (!ef->cdwFileName)
	    {
	    verbose(1, "ef->cdwFileName is null for file id=%d\n", el->val);
	    continue;
	    }
	char *path = cdwPathForFileId(conn, el->val);
	verbose(1, "id=%u, submitFileName=%s path=%s cdwFileName=%s\n", 
	    ef->id, ef->submitFileName, path, ef->cdwFileName);

	// skip if meta or manifest which do not get symlink
	if ((el->val == es->manifestFileId) || (el->val == es->metaFileId))
	    continue;

        // test for already-symlinked case
	char *lastPath = NULL;
	lastPath = findSubmitSymlink(ef->submitFileName, ed->url, path);
	if (lastPath)
	    {
	    verbose(1, "Already symlinked to file under cdw/ lastPath=%s path=%s\n", lastPath, path);
	    freeMem(lastPath);
	    continue;
	    }

	// apply submitFileName to submitDir, giving an absolute path
	freeMem(newPath);
        newPath = mustExpandRelativePath(ed->url, ef->submitFileName);

	if (!fileExists(newPath))
	    {
	    verbose(1, "Original submit path %s does not exist or is a broken symlink.\n", newPath);
	    continue;
	    }

	// Check that the target is not already a symlink.
        // If so, report it and skip it. This is an unexpected case.
        // Cannot be certain that it does not through some chain of links point back to the original file.
	if (lstat(path, &sb) == -1) 
	    errnoAbort("stat failure on %s", path);
	if ((sb.st_mode & S_IFMT) != S_IFREG) // regular file?
	    {
	    char *ftype = "";
	    if ((sb.st_mode & S_IFMT) == S_IFLNK) ftype = "symlink";
	    if ((sb.st_mode & S_IFMT) == S_IFDIR) ftype = "directory";
	    if ((sb.st_mode & S_IFMT) == S_IFDIR) ftype = "special file";  // some other special file type
	    verbose(1, "skipping since target %s is not a regular file, but a %s.\n", path, ftype);
	    continue;
	    }

	// check if file sizes match since it is quick
	off_t subFSize = fileSize(newPath);
	off_t cdwFSize = fileSize(path);
	if (subFSize != cdwFSize)
	    {
	    verbose(1, "skipping since file sizes do not match between %s (%llu) and %s (%llu)\n", 
		newPath, (long long unsigned)subFSize, path, (long long unsigned)cdwFSize);
	    continue;
	    }

	verbose(1, "calculating md5 for %s\n", newPath);
	char *md5 = md5HexForFile(newPath);
	if (!sameString(md5, ef->md5))
	    {
	    verbose(1, "skipping since md5 does not match between %s and %s\n", newPath, path);
	    continue;
	    }

	// save space by finding the last real file in symlinks chain
	// and replace IT with a symlink to the submitted file under cdwFileName. 

	if (fix)
	    replaceOriginalWithSymlink(ef->submitFileName, ed->url, path);
	else
	    verbose(1, "fix command not specified,  skipping symlink between %s and %s\n", newPath, path);
	    

	filesSymlinked++;
	fileSpaceSaved += fileSize(newPath);

	}
    }

verbose(1, "\n\n%d files %ssymlinked.\n", filesSymlinked, fix ? "":"would have been ");
verbose(1, "\n%llu space %ssaved.\n", (unsigned long long) fileSpaceSaved, fix ? "":"would have been ");

if (!fix)
    verbose(1, "\nfix command was not specified. Dry run only.\n\n");

}

int main(int argc, char *argv[])
/* Process command line. */
{
optionInit(&argc, argv, options);
if (argc != 2)
    usage();
char *command = argv[1];
verbose(1, "command=%s\n", command);
if (sameString(command, "fix"))
    fix = TRUE;
else if (sameString(command, "find"))
    fix = FALSE;
else
   usage(); 

idList = optionVal("idList", NULL);
submitDirFilter = optionVal("submitDir", NULL);
cdwFindSymlinkable();
return 0;
}
