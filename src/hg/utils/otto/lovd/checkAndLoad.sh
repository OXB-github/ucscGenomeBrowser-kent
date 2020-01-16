#!/bin/sh -e
#	Do not modify this script, modify the source tree copy:
#	src/utils/otto/lovd/check.sh
#	This script is used via a cron job 

umask 002
WORKDIR="/hive/data/outside/otto/lovd"
TAWK=/cluster/bin/scripts/tawk
KENTBIN=/cluster/bin/x86_64/

if [ ! -d "${WORKDIR}" ]; then
    echo "ERROR in lovd release, Can not find the directory: ${WORKDIR}" 
    exit 255
fi

cd "${WORKDIR}"
today=`date +%F`
cd $today

# count columns and make sure new files have 6 columns
new19Cc=`cat lovd.hg19.bed | $TAWK '(NF!=6)' | wc -l`
#new18Cc=`cat lovd.hg18.bed | $TAWK '(NF!=6)' | wc -l`
if [ "$new19Cc" -ne "0" ]; then
        echo LVOD hg19 $today: found rows with not six columns, quitting
        exit 255
fi

#if [ "$new18Cc" -ne "0" ]; then
#        echo LVOD hg18 $today: found rows with not six columns, quitting
#        exit 255
#fi

# compare old and new line counts and abort if no increase
$KENTBIN/bedClip lovd.hg19.bed /cluster/data/hg19/chrom.sizes lovd.hg19.clipped.bed
cat lovd.hg19.clipped.bed | awk '(($3-$2)<=100)' > lovd.hg19.short.bed
cat lovd.hg19.clipped.bed | awk '(($3-$2)>100)' > lovd.hg19.long.bed
#old19Lc=`$KENTBIN/hgsql hg19 -e "SELECT COUNT(*) from lovd" -NB`
old19ShortLc=`$KENTBIN/hgsql hg19 -e "SELECT COUNT(*) from lovdShort" -NB`
old19LongLc=`$KENTBIN/hgsql hg19 -e "SELECT COUNT(*) from lovdLong" -NB`
new19ShortLc=`wc -l lovd.hg19.short.bed | cut -d' ' -f1 `
new19LongLc=`wc -l lovd.hg19.long.bed | cut -d' ' -f1 `
#old18Lc=`$KENTBIN/hgsql hg18 -e "SELECT COUNT(*) from lovd" -NB`
#new18Lc=`wc -l lovd.hg18.bed | cut -d' ' -f1 `

echo hg19 short rowcount: old $old19ShortLc new: $new19ShortLc
echo hg19 long rowcount: old $old19LongLc new: $new19LongLc
#echo hg18 rowcount: old $old18Lc new: $new18Lc

if [ "$new19ShortLc" -eq "$old19ShortLc" ]; then
        echo LOVD hg19 short: rowcount for $today is equal to old rowcount in mysql, quitting
        exit 0
fi

if [ "$new19LongLc" -eq "$old19LongLc" ]; then
        echo LOVD hg19 long: rowcount for $today is equal to old rowcount in mysql, quitting
        exit 0
fi

# commenting out, as LOVD count will go down in the future, because they have a 
# db cleanup procedure in place now
if [ "$new19ShortLc" -lt "$old19ShortLc" ]; then
        echo LOVD hg19 short: rowcount for $today is smaller to old rowcount in mysql, quitting
        exit 255
fi

if [ "$new19LongLc" -lt "$old19LongLc" ]; then
        echo LOVD hg19 long: rowcount for $today is smaller to old rowcount in mysql, quitting
        exit 255
fi

shortDiff=`echo $old19ShortLc $new19ShortLc | awk '{if (($2-$1)/$1 > 0.1) printf "validate on hg19 LOVD short failed: old count: %d, new count: %d\n", $1,$2; exit 1;}'`
longDiff=`echo $old19LongLc $new19LongLc | awk '{if (($2-$1)/$1 > 0.1) printf "validate on hg19 LOVD long failed: old count: %d, new count: %d\n", $1,$2; exit 1;}'`

# bedDetail4.sql was generated like this:
# egrep -v 'score|strand|thick|reserved|block|chromStarts' /cluster/home/max/kent/src/hg/lib/bedDetail.sql > bedDetail4.sql 
# need to use bedClip as current files include invalid coords which LOVD won't fix.
#$KENTBIN/bedClip lovd.hg19.bed /cluster/data/hg19/chrom.sizes lovd.hg19.clipped.bed
cat lovd.hg19.clipped.bed | awk '(($3-$2)<=100)' | $KENTBIN/hgLoadBed hg19 lovdShort stdin -tab -sqlTable=../bedDetail4.sql -renameSqlTable -noBin
cat lovd.hg19.clipped.bed | awk '(($3-$2)>100)' | $KENTBIN/hgLoadBed hg19 lovdLong stdin -tab -sqlTable=../bedDetail4.sql -renameSqlTable -noBin
#$KENTBIN/bedClip lovd.hg18.bed /cluster/data/hg18/chrom.sizes stdout | $KENTBIN/hgLoadBed hg18 lovd stdin  -tab -sqlTable=../bedDetail4.sql -renameSqlTable -noBin
