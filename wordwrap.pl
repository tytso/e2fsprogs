#!/usr/bin/perl
#
# wordwrap.pl --- does word wrap
#
while (<>) {
    if (/^#/) {		# don't word wrap comments
	print;
	next;
    }
    next if (/^$/);	# skip blank lines
    $linelen = 0;
    split;
    while (defined($word = shift @_)) {
	if ($linelen > 0) {
	    printf(" ");
	}
	$len = length($word) + 1;
	$linelen += $len;
	if ($linelen > 78) {
	    printf("\\\n ");
	    $linelen = 1+$len;
	}
	printf("%s", $word);
    }
    printf("\n");
}
