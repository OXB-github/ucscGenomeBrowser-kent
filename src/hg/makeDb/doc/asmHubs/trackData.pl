#!/usr/bin/env perl

use strict;
use warnings;
use File::stat;

my $argc = scalar(@ARGV);
if ($argc != 2) {
  printf STDERR "usage: trackData.pl Name asmHubName > trackData.html\n";
  printf STDERR "e.g.: trackData.pl Mammals mammals > trackData.html\n";
  exit 255;
}
my $Name = shift;
my $asmHubName = shift;

my $home = $ENV{'HOME'};
my $toolsDir = "$home/kent/src/hg/makeDb/doc/asmHubs";

my $commonNameList = "$asmHubName.asmId.commonName.tsv";
my $commonNameOrder = "$asmHubName.commonName.asmId.orderList.tsv";
my @orderList;	# asmId of the assemblies in order from the *.list files
# the order to read the different .list files:
my %betterName;	# key is asmId, value is better common name than found in
		# assembly_report

my $assemblyTotal = 0;	# complete list of assemblies in this group
my $asmCount = 0;	# count of assemblies completed and in the table
my $overallNucleotides = 0;
my $overallSeqCount = 0;
my $overallGapSize = 0;
my $overallGapCount = 0;

##############################################################################
# from Perl Cookbook Recipe 2.17, print out large numbers with comma delimiters:
##############################################################################
sub commify($) {
    my $text = reverse $_[0];
    $text =~ s/(\d\d\d)(?=\d)(?!\d*\.)/$1,/g;
    return scalar reverse $text
}

sub oneTrackData($$) {
  my ($file, $genomeSize) = @_;
# printf STDERR "# %s\n", $file;
  my $itemCount = 0;
  my $percentCover = 0;
  if (! -s "${file}") {
    return("n/a", "n/a");
  }
  if ($file =~ m/.bw$/) {
      my $bigWigInfo = `bigWigInfo "$file" | egrep "basesCovered:|mean:" | awk '{print \$NF}' | xargs echo | sed -e 's/,//g;'`;
      chomp $bigWigInfo;
      my ($bases, $mean) = split('\s+', $bigWigInfo);
      $percentCover = sprintf("%.2f %%", 100.0 * $bases / $genomeSize);
      $itemCount = sprintf ("%.2f", $mean);
# printf STDERR "# bigWigInfo %s %s %s\n", $itemCount, $percentCover, $file;
  } else {
      my $bigBedInfo = `bigBedInfo "$file" | egrep "itemCount:|basesCovered:" | awk '{print \$NF}' | xargs echo | sed -e 's/,//g;'`;
      chomp $bigBedInfo;
      my ($items, $bases) = split('\s', $bigBedInfo);
      $itemCount = commify($items);
      $percentCover = sprintf("%.2f %%", 100.0 * $bases / $genomeSize);
# printf STDERR "# bigBedInfo %s %s %s\n", $itemCount, $percentCover, $file;
  }
  return ($itemCount, $percentCover);
}

##############################################################################
### start the HTML output
##############################################################################
sub startHtml() {

my $timeStamp = `date "+%F"`;
chomp $timeStamp;

my $subSetMessage = "subset of $asmHubName only";
if ($asmHubName eq "vertebrate") {
   $subSetMessage = "subset of other ${asmHubName}s only";
}

print <<"END"
<!DOCTYPE HTML 4.01 Transitional>
<!--#set var="TITLE" value="$Name genomes assembly hubs" -->
<!--#set var="ROOT" value="../.." -->

<!--#include virtual="\$ROOT/inc/gbPageStartHardcoded.html" -->

<h1>$Name Genomes assembly hubs</h1>
<p>
Assemblies from NCBI/Genbank/Refseq sources, $subSetMessage.
</p>

<h3>See also: <a href='index.html'>hub access</a></h3><br>

<h3>Data resource links</h3>
NOTE: <em>Click on the column headers to sort the table by that column</em><br>
The <em>link to genome browser</em> will attach only that single assembly to
the genome browser.<br>
The numbers are: item count (percent coverage)<br>
Except for the gc5Base column which is: overall GC % average (percent coverage)
END
}

##############################################################################
### start the table output
##############################################################################
sub startTable() {
print <<"END"
<table class="sortable" border="1">
<thead><tr><th>count</th>
  <th>common name<br>link&nbsp;to&nbsp;genome&nbsp;browser</th>
  <th class="sorttable_numeric">gc5 base</th>
  <th class="sorttable_numeric">gap</th>
  <th class="sorttable_numeric">assembly</th>
  <th class="sorttable_numeric">rmsk</th>
  <th class="sorttable_numeric">TRF<br>simpleRepeat</th>
  <th class="sorttable_numeric">windowMasker</th>
  <th class="sorttable_numeric">gapOverlap</th>
  <th class="sorttable_numeric">tandemDups</th>
  <th class="sorttable_numeric">cpg<br>unmasked</th>
  <th class="sorttable_numeric">cpg<br>island</th>
  <th class="sorttable_numeric">ncbiGene</th>
  <th class="sorttable_numeric">ncbiRefSeq</th>
  <th class="sorttable_numeric">xenoRefGene</th>
  <th class="sorttable_numeric">augustus</th>
</tr></thead><tbody>
END
}

##############################################################################
### end the table output
##############################################################################
sub endTable() {

my $commaNuc = commify($overallNucleotides);
my $commaSeqCount = commify($overallSeqCount);
my $commaGapSize = commify($overallGapSize);
my $commaGapCount = commify($overallGapCount);

my $percentDone = 100.0 * $asmCount / $assemblyTotal;

if ($assemblyTotal > 1) {
  print <<"END"

</tbody>
<tfoot><tr><th>TOTALS:</th><td align=center colspan=13>total assembly count&nbsp;${assemblyTotal}</td>
  </tr></tfoot>
</table>
END
} else {
  print <<"END"

</tbody>
</table>
END
}
}	#	sub endTable()

##############################################################################
### end the HTML output
##############################################################################
sub endHtml() {

if ($asmHubName ne "viral") {
  printf "<p>\nOther assembly hubs available:<br>\n<table border='1'><thead>\n<tr>";

  printf "<th><a href='../primates/trackData.html'>Primates</a></th>\n"
    if ($asmHubName ne "primates");
  printf "<th><a href='../mammals/trackData.html'>Mammals</a></th>\n"
    if ($asmHubName ne "mammals");
  printf "<th><a href='../birds/trackData.html'>Birds</a></th>\n"
    if ($asmHubName ne "birds");
  printf "<th><a href='../fish/trackData.html'>Fish</a></th>\n"
    if ($asmHubName ne "fish");
  printf "<th><a href='../vertebrate/trackData.html'>other vertebrates</a></th>\n"
    if ($asmHubName ne "vertebrate");

  printf "</tr></thead>\n</table>\n</p>\n";
}

print <<"END"
</div><!-- closing gbsPage from gbPageStartHardcoded.html -->
</div><!-- closing container-fluid from gbPageStartHardcoded.html -->
<!--#include virtual="\$ROOT/inc/gbFooterHardcoded.html"-->
<script type="text/javascript" src="/js/sorttable.js"></script>
</body></html>
END
}

sub asmCounts($) {
  my ($chromSizes) = @_;
  my ($sequenceCount, $totalSize) = split('\s+', `ave -col=2 $chromSizes | egrep "^count|^total" | awk '{printf "%d\\n", \$NF}' | xargs echo`);
  return ($sequenceCount, $totalSize);
}

#    my ($gapSize) = maskStats($faSizeTxt);
sub maskStats($) {
  my ($faSizeFile) = @_;
  my $gapSize = `grep 'sequences in 1 file' $faSizeFile | awk '{print \$3}'`;
  chomp $gapSize;
  $gapSize =~ s/\(//;
  my $totalBases = `grep 'sequences in 1 file' $faSizeFile | awk '{print \$1}'`;
  chomp $totalBases;
  my $maskedBases = `grep 'sequences in 1 file' $faSizeFile | awk '{print \$9}'`;
  chomp $maskedBases;
  my $maskPerCent = 100.0 * $maskedBases / $totalBases;
  return ($gapSize, $maskPerCent);
}

# grep "sequences in 1 file" GCA_900324465.2_fAnaTes1.2.faSize.txt
# 555641398 bases (3606496 N's 552034902 real 433510637 upper 118524265 lower) in 50 sequences in 1 files

sub gapStats($$) {
  my ($buildDir, $asmId) = @_;
  my $gapBed = "$buildDir/trackData/allGaps/$asmId.allGaps.bed.gz";
  my $gapCount = 0;
  if ( -s "$gapBed" ) {
    $gapCount = `zcat $gapBed | awk '{print \$3-\$2}' | ave stdin | grep '^count' | awk '{print \$2}'`;
  }
  chomp $gapCount;
  return ($gapCount);
}

##############################################################################
### tableContents()
##############################################################################
sub tableContents() {

  my @trackList = qw(gc5Base gap assembly rmsk simpleRepeat windowMasker gapOverlap tandemDups cpgIslandExtUnmasked cpgIslandExt ncbiGene ncbiRefSeq xenoRefGene augustus);


  foreach my $asmId (reverse(@orderList)) {
    my ($gcPrefix, $asmAcc, $asmName) = split('_', $asmId, 3);
    my $accessionId = sprintf("%s_%s", $gcPrefix, $asmAcc);
    my $accessionDir = substr($asmId, 0 ,3);
    $accessionDir .= "/" . substr($asmId, 4 ,3);
    $accessionDir .= "/" . substr($asmId, 7 ,3);
    $accessionDir .= "/" . substr($asmId, 10 ,3);
    my $buildDir = "/hive/data/genomes/asmHubs/refseqBuild/$accessionDir/$asmId";
#    next if ($asmId ne "GCF_000001405.39_GRCh38.p13");
    my $asmReport="$buildDir/download/${asmId}_assembly_report.txt";
    if (! -s "$asmReport") {
      printf STDERR "# no assembly report:\n# %s\n", $asmReport;
      next;
    }
    my $chromSizes = "${buildDir}/${asmId}.chrom.sizes";
    my $twoBit = "${buildDir}/trackData/addMask/${asmId}.masked.2bit";
    if (! -s "$twoBit") {
      printf STDERR "# no 2bit file:\n# %s\n", $twoBit;
      printf "<tr><td align=right>%d</td>\n", ++$asmCount;
      printf "<td align=center>%s</td>\n", $accessionId;
      printf "<th colspan=14 align=center>missing masked 2bit file</th>\n";
      printf "</tr>\n";
      next;
    }
    my $faSizeTxt = "${buildDir}/${asmId}.faSize.txt";
    if ( ! -s "$faSizeTxt" ) {
       printf STDERR "twoBitToFa $twoBit stdout | faSize stdin > $faSizeTxt\n";
       print `twoBitToFa $twoBit stdout | faSize stdin > $faSizeTxt`;
    }
    my ($gapSize, $maskPerCent) = maskStats($faSizeTxt);
    $overallGapSize += $gapSize;
    my ($seqCount, $totalSize) = asmCounts($chromSizes);
    $overallSeqCount += $seqCount;
#    my $totalSize=`ave -col=2 $chromSizes | grep "^total" | awk '{printf "%d", \$NF}'`;
    $overallNucleotides += $totalSize;
    my $gapCount = gapStats($buildDir, $asmId);
    $overallGapCount += $gapCount;
    my $sciName = "notFound";
    my $commonName = "notFound";
    my $asmDate = "notFound";
    my $itemsFound = 0;
    open (FH, "<$asmReport") or die "can not read $asmReport";
    while (my $line = <FH>) {
      last if ($itemsFound > 5);
      chomp $line;
      $line =~ s///g;;
      $line =~ s/\s+$//g;;
      if ($line =~ m/Date:/) {
        if ($asmDate =~ m/notFound/) {
           ++$itemsFound;
           $asmDate = $line;
           $asmDate =~ s/.*:\s+//;
        }
      } elsif ($line =~ m/Organism name:/) {
        if ($sciName =~ m/notFound/) {
           ++$itemsFound;
           $commonName = $line;
           $sciName = $line;
           $commonName =~ s/.*\(//;
           $commonName =~ s/\)//;
           $commonName = $betterName{$asmId} if (exists($betterName{$asmId}));
           $sciName =~ s/.*:\s+//;
           $sciName =~ s/\s+\(.*//;
        }
      }
    }
    close (FH);
    my $hubUrl = "https://hgdownload.soe.ucsc.edu/hubs/$accessionDir/$accessionId";
    printf "<tr><td align=right>%d</td>\n", ++$asmCount;
    printf "<td align=center><a href='https://genome-test.gi.ucsc.edu/h/%s' target=_blank>%s<br>%s</a></td>\n", $accessionId, $commonName, $accessionId;
    foreach my $track (@trackList) {
      my $trackFile = "$buildDir/bbi/$asmId.$track";
      my ($itemCount, $percentCover);
      if ( "$track" eq "gc5Base" ) {
         $trackFile .= ".bw";
      } else {
         $trackFile .= ".bb";
      }
      if ( "$track" eq "rmsk") {
        my $rmskStats = "$buildDir/trackData/repeatMasker/$asmId.rmsk.stats";
        if (! -s "${rmskStats}") {
         my $faOut = "$buildDir/trackData/repeatMasker/$asmId.sorted.fa.out.gz";
          if ( -s "$faOut") {
            my $items = `zcat "$faOut" | wc -l`;
            chomp $items;
            $itemCount = commify($items);
            my $masked = `grep masked "$buildDir/trackData/repeatMasker/faSize.rmsk.txt" | awk '{print \$4}' | sed -e 's/%//;'`;
            chomp $masked;
            $percentCover = sprintf("%.2f %%", $masked);
            open (RS, ">$rmskStats") or die "can now write to $rmskStats";
            printf RS "%s\t%s\n", $itemCount, $percentCover;
            close (RS);
          } else {
            $itemCount = "n/a";
            $percentCover = "n/a";
          }
        } else {
          ($itemCount, $percentCover) = split('\s+', `cat $rmskStats`);
          chomp $percentCover;
        }
      } else {
        ($itemCount, $percentCover) = oneTrackData($trackFile, $totalSize);
      }
      printf "    <td align=right>%s<br>(%s)</td>\n", $itemCount, $percentCover;
    }
    printf "</tr>\n";
  }
}

##############################################################################
### main()
##############################################################################

open (FH, "<$toolsDir/${commonNameOrder}") or die "can not read ${commonNameOrder}";
while (my $line = <FH>) {
  next if ($line =~ m/^#/);
  chomp $line;
  my ($commonName, $asmId) = split('\t', $line);
  push @orderList, $asmId;
  $betterName{$asmId} = $commonName;
  ++$assemblyTotal;
}
close (FH);

startHtml();
startTable();
tableContents();
endTable();
endHtml();
