#!/usr/bin/perl

my $is_problem_file = 0;
my $save_msg;
my $msg_accum = "";
my $msg;
my $expanded = 0;

sub do_expand {
    $msg =~ s/\@a/extended attribute/g;
    $msg =~ s/\@A/error allocating/g;
    $msg =~ s/\@b/block/g;
    $msg =~ s/\@B/bitmap/g;
    $msg =~ s/\@c/compress/g;
    $msg =~ s/\@C/conflicts with some other fs block/g;
    $msg =~ s/\@i/inode/g;
    $msg =~ s/\@I/illegal/g;
    $msg =~ s/\@j/journal/g;
    $msg =~ s/\@D/deleted/g;
    $msg =~ s/\@d/directory/g;
    $msg =~ s/\@e/entry/g;
    $msg =~ s/\@E/entry '%Dn' in %p (%i)/g;
    $msg =~ s/\@f/filesystem/g;
    $msg =~ s/\@F/for inode %i (%Q) is/g;
    $msg =~ s/\@g/group/g;
    $msg =~ s/\@h/HTREE directory inode/g;
    $msg =~ s/\@l/lost+found/g;
    $msg =~ s/\@L/is a link/g;
    $msg =~ s/\@o/orphaned/g;
    $msg =~ s/\@p/problem in/g;
    $msg =~ s/\@r/root inode/g;
    $msg =~ s/\@s/should be/g;
    $msg =~ s/\@S/superblock/g;
    $msg =~ s/\@u/unattached/g;
    $msg =~ s/\@v/device/g;
    $msg =~ s/\@z/zero-length/g;
    $msg =~ s/\@\@/@/g;
}


while (<>) {
    if (/^#: /)
    {
	$is_problem_file = (/^#: e2fsck\/problem/) ? 1 : 0;
    }
    $msg = "";
    if (/^msgid / && $is_problem_file) {
	($msg) = /^msgid "(.*)"$/;
	$save_msgid = $_;
	if ($msg =~ /\@/) {
	    $expanded++;
	}
	&do_expand();
	if ($msg ne "") {
	    $msg_accum = $msg_accum . "#. \@-expand: $msg\n";
	}
	next;
    }
    if (/^"/ && $is_problem_file) {
	($msg) = /^"(.*)"$/;
	$save_msgid = $save_msgid . $_;
	if ($msg =~ /\@/) {
	    $expanded++;
	}
	&do_expand();
	$msg_accum = $msg_accum . "#. \@-expand: $msg\n";
	next;
    }
    if (/^msgstr / && $is_problem_file) {
	if ($expanded) {
	    print $msg_accum;
	}
	print $save_msgid;
	$msg_accum = "";
	$expanded = 0;
    }
    print $_;
}
